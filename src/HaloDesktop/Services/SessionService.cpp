#include "pch.h"
#include "Services/SessionService.h"

#include "Api/Addons/AddonFetcher.h"
#include "Api/ApiClient.h"
#include "Api/ApiError.h"
#include "Api/HaloBackend.h"
#include "Services/Auth/BrowserSignInStep.h"
#include "Services/Auth/OidcSignInFlow.h"
#include "Services/Auth/ServerAddressPolicy.h"
#include "Services/Auth/SessionController.h"
#include "Services/Auth/SessionStore.h"
#include "Services/Auth/SignInAddonRegistry.h"
#include "Services/Device/DeviceBackend.h"
#include "Services/Device/DeviceModePolicy.h"
#include "Services/Device/DeviceStore.h"
#include "Services/DevicePreferencesStore.h"
#include "Services/NavigationService.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Windows.System.Threading.h>

namespace
{
    constexpr std::size_t MaximumUsernameLength = 512;
    constexpr std::size_t MaximumPasswordLength = 4096;

    winrt::hstring Trimmed(winrt::hstring const& input)
    {
        std::wstring value{ input };
        auto const first = std::find_if_not(value.begin(), value.end(), [](wchar_t character)
        {
            return std::iswspace(character) != 0;
        });
        auto const last = std::find_if_not(value.rbegin(), value.rend(), [](wchar_t character)
        {
            return std::iswspace(character) != 0;
        }).base();
        return first < last ? winrt::hstring{ std::wstring(first, last) } : winrt::hstring{};
    }

    ::HaloDesktop::Services::SignInAttempt Failed(::HaloDesktop::Services::Auth::SignInErrorInfo error = {})
    {
        return ::HaloDesktop::Services::SignInAttempt{
            .Outcome = ::HaloDesktop::Services::SignInAttemptOutcome::Failed,
            .Error = std::move(error),
            .HttpStatus = std::nullopt,
        };
    }

    ::HaloDesktop::Services::SignInAttempt FailedWithStatus(std::optional<std::uint16_t> status)
    {
        return ::HaloDesktop::Services::SignInAttempt{
            .Outcome = ::HaloDesktop::Services::SignInAttemptOutcome::Failed,
            .Error = {},
            .HttpStatus = status,
        };
    }

    ::HaloDesktop::Services::SignInAttempt Superseded()
    {
        return ::HaloDesktop::Services::SignInAttempt{ .Outcome = ::HaloDesktop::Services::SignInAttemptOutcome::Superseded };
    }

    ::HaloDesktop::Services::SignInAttempt Succeeded()
    {
        return ::HaloDesktop::Services::SignInAttempt{ .Outcome = ::HaloDesktop::Services::SignInAttemptOutcome::Succeeded };
    }

    std::int64_t NowMilliseconds()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // Best effort, as the reference's topUpDeviceAddons: the next start tries again.
    concurrency::task<bool> TopUpAddonsAsync(std::shared_ptr<::HaloDesktop::Api::IHaloBackend> backend)
    {
        try
        {
            auto const payload = co_await backend->GetAddonsAsync();
            std::vector<std::wstring> installed;
            for (auto const& addon : payload.User)
            {
                if (addon.TransportUrl) installed.emplace_back(*addon.TransportUrl);
            }
            auto const next = ::HaloDesktop::Services::Device::TopUpAddonList(installed);
            if (!next) co_return false;
            std::vector<winrt::hstring> urls;
            urls.reserve(next->size());
            for (auto const& url : *next) urls.emplace_back(url);
            co_await backend->PutAddonsAsync(std::move(urls));
            co_return true;
        }
        catch (...)
        {
        }
        co_return false;
    }

    std::wstring DescribeProfileFailure(std::exception_ptr const& failure)
    {
        try
        {
            std::rethrow_exception(failure);
        }
        catch (winrt::hresult_error const& error)
        {
            return std::wstring{ error.message() };
        }
        catch (std::exception const& error)
        {
            return std::wstring{ winrt::to_hstring(error.what()) };
        }
        catch (...)
        {
        }
        return L"Unknown error";
    }
}

namespace HaloDesktop::Services
{
    SessionService::SessionService(
        std::shared_ptr<::HaloDesktop::Api::ApiClient> apiClient,
        std::shared_ptr<Auth::SessionController> controller,
        std::shared_ptr<Auth::SessionStore> store,
        std::shared_ptr<Auth::OidcSignInFlow> oidcSignInFlow,
        std::shared_ptr<Auth::SignInAddonRegistry const> addons,
        std::shared_ptr<DevicePreferencesStore> preferences,
        std::shared_ptr<NavigationService> navigation,
        std::shared_ptr<Device::DeviceStore> deviceStore,
        std::shared_ptr<::HaloDesktop::Api::Addons::IAddonFetcher> addonFetcher)
        : m_apiClient(std::move(apiClient)),
          m_controller(std::move(controller)),
          m_store(std::move(store)),
          m_oidcSignInFlow(std::move(oidcSignInFlow)),
          m_addons(std::move(addons)),
          m_preferences(std::move(preferences)),
          m_navigation(std::move(navigation)),
          m_deviceStore(std::move(deviceStore)),
          m_addonFetcher(std::move(addonFetcher))
    {
        if (!m_apiClient || !m_controller || !m_store || !m_oidcSignInFlow || !m_addons
            || !m_preferences || !m_navigation || !m_deviceStore || !m_addonFetcher)
        {
            throw std::invalid_argument{ "SessionService requires all dependencies." };
        }
    }

    SessionMode SessionService::Mode() const noexcept
    {
        if (m_deviceBackend)
        {
            return SessionMode::Device;
        }
        return m_controller->IsSignedIn() ? SessionMode::SignedIn : SessionMode::SignedOut;
    }

    std::optional<LocalDataOwner> SessionService::DataOwner() const
    {
        if (m_deviceBackend && m_deviceProfile)
        {
            return LocalDataOwner{ L"device", m_deviceProfile->Id };
        }
        if (m_controller->IsSignedIn() && !m_userId.empty())
        {
            return LocalDataOwner{ std::wstring{ ServerUrl() }, std::wstring{ m_userId } };
        }
        return std::nullopt;
    }

    bool SessionService::HasDeviceDataToMove() const noexcept
    {
        return !m_deviceBackend && m_deviceProfile.has_value() && m_controller->IsSignedIn();
    }

    std::wstring SessionService::StartupDeviceError() const
    {
        return m_startupDeviceError;
    }

    std::shared_ptr<::HaloDesktop::Api::IHaloBackend> SessionService::ActiveDeviceBackend() const
    {
        return m_deviceBackend;
    }

    std::optional<Device::DeviceProfile> SessionService::PendingDeviceProfile() const
    {
        if (m_deviceBackend)
        {
            return std::nullopt;
        }
        return m_deviceProfile;
    }

    void SessionService::ForgetDeviceProfile(std::wstring const& profileId)
    {
        if (!m_deviceBackend && m_deviceProfile && m_deviceProfile->Id == profileId)
        {
            m_deviceProfile.reset();
        }
    }

    std::shared_ptr<Device::DeviceBackend> SessionService::MakeDeviceBackend() const
    {
        return std::make_shared<Device::DeviceBackend>(Device::DeviceBackendOptions{
            .Store = m_deviceStore,
            .Fetcher = m_addonFetcher,
        });
    }

    void SessionService::EnterDevice(Device::DeviceProfile profile, std::shared_ptr<Device::DeviceBackend> backend)
    {
        m_deviceProfile = std::move(profile);
        m_deviceBackend = std::move(backend);
        m_resumeDeviceOnReject = false;
        m_startupDeviceError.clear();
        ++m_modeSwitches;
        NotifyIdentityChanged();
        NotifyProfileChanged();
    }

    concurrency::task<void> SessionService::ContinueWithoutAccountAsync()
    {
        auto const uiContext = winrt::apartment_context{};
        if (Mode() != SessionMode::SignedOut)
        {
            throw std::logic_error{ "Halo can be used without an account only while signed out." };
        }
        // An earlier profile is resumed: its data may still be waiting to move
        // into an account, or the person signed out after a move that failed.
        auto profile = m_deviceProfile;
        auto const store = m_deviceStore;
        if (!profile)
        {
            profile = co_await store->ReadProfileAsync();
        }
        if (!profile)
        {
            profile = co_await store->CreateProfileAsync(NowMilliseconds());
        }
        auto backend = MakeDeviceBackend();
        static_cast<void>(co_await TopUpAddonsAsync(backend));
        co_await uiContext;
        if (Mode() != SessionMode::SignedOut)
        {
            throw std::logic_error{ "Another session started while Halo was getting ready on this PC." };
        }
        EnterDevice(std::move(*profile), std::move(backend));
    }

    concurrency::task<bool> SessionService::TopUpDeviceAddonsAsync()
    {
        auto backend = m_deviceBackend;
        if (!backend)
        {
            co_return false;
        }
        co_return co_await TopUpAddonsAsync(std::move(backend));
    }

    void SessionService::ReloadAccountData()
    {
        NotifyIdentityChanged();
    }

    winrt::hstring SessionService::ServerUrl() const
    {
        auto const target = m_controller->CurrentTarget();
        return target.ServerUrl && !target.ServerUrl->empty() ? *target.ServerUrl : m_apiClient->BaseUrl();
    }

    winrt::hstring SessionService::UserName() const
    {
        return m_userName;
    }

    winrt::hstring SessionService::UserId() const
    {
        return m_userId;
    }

    bool SessionService::IsSignedIn() const noexcept
    {
        return m_controller->IsSignedIn();
    }

    bool SessionService::IsAdmin() const noexcept
    {
        return m_isAdmin;
    }

    std::optional<winrt::hstring> SessionService::Plan() const
    {
        return m_plan;
    }

    std::optional<winrt::hstring> SessionService::AvatarVersion() const
    {
        return m_avatarVersion;
    }

    std::uint64_t SessionService::Generation() const noexcept
    {
        // Both counts only grow, so their sum moves whenever either does.
        return m_controller->SessionGeneration() + m_modeSwitches;
    }

    concurrency::task<void> SessionService::RestoreAsync()
    {
        auto const uiContext = winrt::apartment_context{};
        auto const builtIn = std::wstring{ m_apiClient->BaseUrl() };
        co_await winrt::resume_background();

        // The reference's forgetSessionsFromAnotherServer: sessions saved by a
        // build for another server must never reach this one.
        std::wstring recorded;
        try
        {
            recorded = m_preferences->SessionServerUrl();
        }
        catch (...)
        {
            recorded.clear();
        }
        auto const check = Auth::CheckSessionServer(
            recorded.empty() ? std::nullopt : std::optional<std::wstring_view>{ recorded },
            builtIn);
        co_await m_controller->RestoreAsync(check == Auth::SessionServerCheck::Different);
        if (check != Auth::SessionServerCheck::Matches)
        {
            try
            {
                m_preferences->SessionServerUrl(builtIn);
            }
            catch (...)
            {
                // Checked again next start.
            }
        }

        auto identity = m_controller->IsSignedIn()
            ? co_await m_store->LoadIdentityAsync()
            : std::optional<Auth::StoredIdentity>{};

        // The profile decides the first screen (the reference reads it before
        // its first render). A file that cannot be read is left as it is.
        auto profileRead = Device::ProfileRead::None;
        std::optional<Device::DeviceProfile> profile;
        std::exception_ptr profileFailure;
        try
        {
            profile = co_await m_deviceStore->ReadProfileAsync();
            profileRead = profile ? Device::ProfileRead::Found : Device::ProfileRead::None;
        }
        catch (...)
        {
            profileRead = Device::ProfileRead::Unreadable;
            profileFailure = std::current_exception();
        }
        auto const startup = Device::DecideStartup(m_controller->IsSignedIn(), profileRead);
        auto const deviceBackend = startup.Screen == Device::StartupScreen::Device
            ? MakeDeviceBackend()
            : std::shared_ptr<Device::DeviceBackend>{};

        co_await uiContext;
        ClearIdentity();
        if (startup.ShowDeviceError && profileFailure)
        {
            m_startupDeviceError = Device::DeviceSetupError(DescribeProfileFailure(profileFailure));
        }
        if (startup.RememberProfile)
        {
            m_deviceProfile = profile;
        }
        if (deviceBackend && profile)
        {
            EnterDevice(std::move(*profile), deviceBackend);
            co_return;
        }
        if (identity)
        {
            m_userId = identity->UserId;
            m_userName = identity->Username;
            // What the last /auth/me said, until the live answer replaces it.
            m_isAdmin = identity->IsAdmin.value_or(false);
            m_plan = identity->Plan;
            m_avatarVersion = identity->AvatarVersion;
            NotifyIdentityChanged();
            NotifyProfileChanged();
        }
    }

    concurrency::task<SignInDiscovery> SessionService::DiscoverAuthenticationAsync()
    {
        auto const uiContext = winrt::apartment_context{};
        auto const version = ++m_discoveryVersion;
        auto config = co_await m_apiClient->GetAuthConfigAsync();
        co_await uiContext;

        SignInDiscovery discovery{ .Mode = SignInMode::Local, .ModeName = std::wstring{ config.ModeName }, .ButtonLabel = {} };
        std::optional<::HaloDesktop::Api::Dto::AuthConfig> browserConfig;
        std::shared_ptr<Auth::ISignInAddon> browserAddon;
        switch (config.Mode)
        {
        case ::HaloDesktop::Api::Dto::AuthMode::Local:
            discovery.Mode = SignInMode::Local;
            break;
        case ::HaloDesktop::Api::Dto::AuthMode::Oidc:
            discovery.Mode = SignInMode::Oidc;
            browserConfig = std::move(config);
            break;
        case ::HaloDesktop::Api::Dto::AuthMode::Other:
            if (auto addon = m_addons->Find(std::wstring_view{ config.ModeName }))
            {
                discovery.Mode = SignInMode::Addon;
                discovery.ButtonLabel = addon->ButtonLabel();
                browserAddon = std::move(addon);
                browserConfig = std::move(config);
            }
            else
            {
                discovery.Mode = SignInMode::Unsupported;
            }
            break;
        }
        if (version == m_discoveryVersion)
        {
            m_browserConfig = std::move(browserConfig);
            m_browserAddon = std::move(browserAddon);
        }
        co_return discovery;
    }

    concurrency::task<std::wstring> SessionService::CheckServerModeAsync(winrt::hstring serverUrl)
    {
        auto const parsed = Auth::ParseServerAddress(std::wstring_view{ serverUrl });
        if (!parsed.Address || *parsed.Address != std::wstring_view{ serverUrl })
        {
            throw std::invalid_argument{ "The server address is not usable." };
        }
        auto const config = co_await m_apiClient->GetAuthConfigAtAsync(serverUrl);
        co_return std::wstring{ config.ModeName };
    }

    std::stop_token SessionService::BeginSignIn()
    {
        std::stop_source previous;
        std::stop_token current;
        {
            std::scoped_lock const lock{ m_signInCancellationMutex };
            previous = m_signInCancellation;
            m_signInCancellation = std::stop_source{};
            current = m_signInCancellation.get_token();
        }
        static_cast<void>(previous.request_stop());
        ++m_signInVersion;
        return current;
    }

    concurrency::task<SignInAttempt> SessionService::SignInWithPasswordAsync(
        std::optional<winrt::hstring> serverUrl,
        winrt::hstring username,
        winrt::hstring password)
    {
        auto const uiContext = winrt::apartment_context{};
        static_cast<void>(BeginSignIn());
        auto const version = m_signInVersion.load();
        auto const normalizedUserName = Trimmed(username);
        if (normalizedUserName.empty() || normalizedUserName.size() > MaximumUsernameLength
            || password.empty() || password.size() > MaximumPasswordLength)
        {
            co_return FailedWithStatus(std::uint16_t{ 401 });
        }

        // The password goes to the exact address the form accepted, or to the
        // built-in server; nothing else is ever a target.
        auto server = m_apiClient->BaseUrl();
        if (serverUrl)
        {
            auto const parsed = Auth::ParseServerAddress(std::wstring_view{ *serverUrl });
            if (!parsed.Address || *parsed.Address != std::wstring_view{ *serverUrl })
            {
                co_return Failed();
            }
            server = *serverUrl;
        }

        std::uint64_t establishedGeneration{};
        try
        {
            ClearIdentity();
            co_await m_store->ClearIdentityAsync(m_controller->SessionGeneration() + 1);
            establishedGeneration = co_await m_controller->SignInLocalAsync(
                server,
                normalizedUserName,
                std::move(password));
            m_pendingSessionGeneration.store(establishedGeneration);
        }
        catch (winrt::hresult_error const& error)
        {
            co_return FailedWithStatus(::HaloDesktop::Api::ApiError::HttpStatus(error.code()));
        }
        catch (...)
        {
            co_return Failed();
        }

        co_await uiContext;
        if (version != m_signInVersion.load())
        {
            co_await DiscardAsync(establishedGeneration);
            co_return Superseded();
        }
        auto const identity = co_await RefreshIdentityDetailedAsync();
        co_await uiContext;
        if (version != m_signInVersion.load())
        {
            co_await DiscardAsync(establishedGeneration);
            co_return Superseded();
        }
        if (!identity.Loaded)
        {
            co_await DiscardAsync(establishedGeneration);
            co_return Failed();
        }

        if (serverUrl)
        {
            auto const preferences = m_preferences;
            auto const remembered = std::wstring{ server };
            co_await winrt::resume_background();
            try
            {
                preferences->LastServerAddress(remembered);
            }
            catch (...)
            {
                // Only a convenience for next time.
            }
            co_await uiContext;
        }
        co_return Succeeded();
    }

    concurrency::task<SignInAttempt> SessionService::RequestBrowserSignInAsync(
        std::function<void(winrt::hstring const& authUrl)> onOpened)
    {
        auto const uiContext = winrt::apartment_context{};
        auto const cancellation = BeginSignIn();
        auto const version = m_signInVersion.load();
        if (!m_browserConfig)
        {
            co_return Failed(Auth::SignInErrorInfo{ .Message = L"The server's sign-in method is not known yet." });
        }

        // The page's address is reported from a background thread; the screen
        // hears it on its own.
        auto const dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        Auth::SignInOpenedHandler opened = [dispatcher, onOpened](winrt::hstring const& authUrl)
        {
            if (!dispatcher || !onOpened)
            {
                return;
            }
            static_cast<void>(dispatcher.TryEnqueue([onOpened, authUrl]()
            {
                try
                {
                    onOpened(authUrl);
                }
                catch (...)
                {
                }
            }));
        };

        std::uint64_t establishedGeneration{};
        std::exception_ptr failure;
        if (m_browserAddon)
        {
            auto const addon = m_browserAddon;
            Auth::AddonAuthConfig config{
                .Mode = std::wstring{ m_browserConfig->ModeName },
                .Raw = m_browserConfig->Raw,
            };
            try
            {
                ClearIdentity();
                co_await m_store->ClearIdentityAsync(m_controller->SessionGeneration() + 1);
                establishedGeneration = co_await m_controller->SignInAddonAsync(
                    addon,
                    m_apiClient->BaseUrl(),
                    std::move(config),
                    std::move(opened),
                    cancellation);
                m_pendingSessionGeneration.store(establishedGeneration);
            }
            catch (...)
            {
                failure = std::current_exception();
            }
        }
        else
        {
            auto result = co_await m_oidcSignInFlow->SignInAsync(*m_browserConfig, std::move(opened));
            co_await uiContext;
            if (version != m_signInVersion.load())
            {
                co_return Superseded();
            }
            if (!result.Session)
            {
                co_return Failed(std::move(result.Error));
            }
            try
            {
                ClearIdentity();
                co_await m_store->ClearIdentityAsync(m_controller->SessionGeneration() + 1);
                co_await uiContext;
                if (version != m_signInVersion.load())
                {
                    co_return Superseded();
                }
                establishedGeneration = co_await m_controller->SignInOidcAsync(std::move(*result.Session));
                m_pendingSessionGeneration.store(establishedGeneration);
            }
            catch (...)
            {
                failure = std::current_exception();
            }
        }

        co_await uiContext;
        if (failure)
        {
            if (version != m_signInVersion.load())
            {
                co_return Superseded();
            }
            co_return Failed(Auth::DescribeSignInFailure(failure));
        }
        if (version != m_signInVersion.load())
        {
            co_await DiscardAsync(establishedGeneration);
            co_return Superseded();
        }
        auto const identity = co_await RefreshIdentityDetailedAsync();
        co_await uiContext;
        if (version != m_signInVersion.load())
        {
            co_await DiscardAsync(establishedGeneration);
            co_return Superseded();
        }
        if (!identity.Loaded)
        {
            co_await DiscardAsync(establishedGeneration);
            co_return Failed(Auth::SignInErrorInfo{ .Message = identity.Error });
        }
        co_return Succeeded();
    }

    concurrency::task<void> SessionService::DiscardAsync(std::uint64_t establishedGeneration)
    {
        auto expectedGeneration = establishedGeneration;
        static_cast<void>(m_pendingSessionGeneration.compare_exchange_strong(expectedGeneration, 0));
        try
        {
            co_await m_controller->RejectSessionAsync(establishedGeneration);
        }
        catch (...)
        {
        }
    }

    void SessionService::AcknowledgeSignIn() noexcept
    {
        m_pendingSessionGeneration.store(0);
        m_resumeDeviceOnReject = false;
    }

    void SessionService::CancelSignIn() noexcept
    {
        std::stop_source cancellation;
        {
            std::scoped_lock const lock{ m_signInCancellationMutex };
            cancellation = m_signInCancellation;
        }
        ++m_signInVersion;
        static_cast<void>(cancellation.request_stop());
        m_oidcSignInFlow->Cancel();
        auto const establishedGeneration = m_pendingSessionGeneration.exchange(0);
        if (establishedGeneration != 0)
        {
            m_controller->RejectSessionAsync(establishedGeneration).then([](concurrency::task<void> rejected)
            {
                try { rejected.get(); } catch (...) {}
            });
        }
    }

    concurrency::task<winrt::hstring> SessionService::LastServerAddressAsync()
    {
        auto const preferences = m_preferences;
        co_await winrt::resume_background();
        try
        {
            co_return winrt::hstring{ preferences->LastServerAddress() };
        }
        catch (...)
        {
        }
        co_return winrt::hstring{};
    }

    concurrency::task<std::optional<std::chrono::milliseconds>> SessionService::ProbeHealthAsync()
    {
        // There is no server to ask without an account.
        if (m_deviceBackend)
        {
            co_return std::nullopt;
        }
        concurrency::task_completion_event<std::optional<std::chrono::milliseconds>> completion;
        m_apiClient->GetHealthAsync().then([completion](concurrency::task<::HaloDesktop::Api::Dto::HealthStatus> task) mutable
        {
            try
            {
                auto const health = task.get();
                completion.set(health.Ok
                    ? std::optional<std::chrono::milliseconds>{ health.RoundTrip }
                    : std::nullopt);
            }
            catch (...)
            {
                completion.set(std::nullopt);
            }
        });
        auto const timeout = winrt::Windows::System::Threading::ThreadPoolTimer::CreateTimer(
            [completion](auto const&) mutable { completion.set(std::nullopt); },
            std::chrono::seconds{ 6 });
        auto result = co_await concurrency::create_task(completion);
        timeout.Cancel();
        co_return result;
    }

    concurrency::task<void> SessionService::SignOutAsync()
    {
        auto const uiContext = winrt::apartment_context{};
        // Nothing is signed in without an account.
        if (m_deviceBackend)
        {
            co_return;
        }
        co_await m_controller->SignOutAsync();
        try { co_await m_store->ClearIdentityAsync(m_controller->SessionGeneration()); } catch (...) {}
        co_await uiContext;
        ClearIdentity();
        m_navigation->ShowOverlay(Page::Login);
    }

    concurrency::task<bool> SessionService::RefreshIdentityAsync()
    {
        auto const refresh = co_await RefreshIdentityDetailedAsync();
        co_return refresh.Loaded;
    }

    concurrency::task<SessionService::IdentityRefresh> SessionService::RefreshIdentityDetailedAsync()
    {
        auto const uiContext = winrt::apartment_context{};
        auto const generation = m_controller->SessionGeneration();
        ::HaloDesktop::Api::Dto::Me me;
        std::wstring error;
        try
        {
            me = co_await m_apiClient->GetMeAsync();
        }
        catch (winrt::hresult_error const& failure)
        {
            error = Auth::SanitizedSignInText(std::wstring_view{ failure.message() });
        }
        catch (std::exception const& failure)
        {
            error = Auth::SanitizedSignInText(std::wstring{ winrt::to_hstring(failure.what()) });
        }
        catch (...)
        {
            error = L"Unknown error";
        }
        if (!error.empty())
        {
            co_return IdentityRefresh{ .Loaded = false, .Error = std::move(error) };
        }
        co_await uiContext;
        if (generation != m_controller->SessionGeneration() || !m_controller->IsSignedIn())
        {
            co_return IdentityRefresh{ .Loaded = false, .Error = L"The session changed while the account was loading." };
        }
        // A sign-in started from Halo without an account takes over here, once
        // the account is known: the identity change below moves every service
        // off this PC's data. The profile stays, for the move into the account.
        auto const leavingDevice = m_deviceBackend != nullptr;
        if (leavingDevice)
        {
            m_deviceBackend.reset();
            m_resumeDeviceOnReject = true;
            ++m_modeSwitches;
        }
        auto const accountChanged = leavingDevice || m_userId != me.Id;
        auto const profileChanged = accountChanged
            || m_userName != me.Username
            || m_isAdmin != me.IsAdmin
            || m_plan != me.Plan
            || m_avatarVersion != me.AvatarVersion;
        m_userName = me.Username;
        m_userId = me.Id;
        m_isAdmin = me.IsAdmin;
        m_plan = me.Plan;
        m_avatarVersion = me.AvatarVersion;
        if (accountChanged)
        {
            NotifyIdentityChanged();
        }
        if (profileChanged)
        {
            NotifyProfileChanged();
        }
        try
        {
            co_await m_store->SaveIdentityAsync(Auth::StoredIdentity{
                .UserId = me.Id,
                .Username = me.Username,
                .IsAdmin = me.IsAdmin,
                .Plan = me.Plan,
                .AvatarVersion = me.AvatarVersion,
            }, generation);
        }
        catch (...)
        {
        }
        co_await uiContext;
        auto const loaded = generation == m_controller->SessionGeneration()
            && m_controller->IsSignedIn()
            && !m_userId.empty();
        co_return IdentityRefresh{
            .Loaded = loaded,
            .Error = loaded ? std::wstring{} : std::wstring{ L"The session changed while the account was loading." },
        };
    }

    void SessionService::HandleSessionRejected()
    {
        m_store->ClearIdentityAsync(m_controller->SessionGeneration()).then([](concurrency::task<void> task)
        {
            try { task.get(); } catch (...) {}
        });
        // A sign-in started from Halo without an account was discarded before
        // it took over: this PC's data is still in use, and stays.
        if (m_deviceBackend)
        {
            ClearIdentity();
            return;
        }
        // Discarded after it took over but before the sign-in screen took it:
        // back to this PC's data, as if it had never happened.
        if (std::exchange(m_resumeDeviceOnReject, false) && m_deviceProfile)
        {
            m_deviceBackend = MakeDeviceBackend();
            ++m_modeSwitches;
            // ClearIdentity raises both changes when an account was known.
            auto const raised = !m_userId.empty();
            ClearIdentity();
            if (!raised)
            {
                NotifyIdentityChanged();
                NotifyProfileChanged();
            }
            return;
        }
        ClearIdentity();
        m_navigation->ShowOverlay(Page::Login);
    }

    void SessionService::SetIdentityChangedHandler(std::function<void()> handler)
    {
        m_identityChanged = std::move(handler);
        NotifyIdentityChanged();
    }

    void SessionService::ClearIdentity()
    {
        auto const accountChanged = !m_userId.empty();
        auto const profileChanged = accountChanged
            || !m_userName.empty()
            || m_isAdmin
            || m_plan.has_value()
            || m_avatarVersion.has_value();
        m_userName.clear();
        m_userId.clear();
        m_isAdmin = false;
        m_plan.reset();
        m_avatarVersion.reset();
        if (accountChanged)
        {
            NotifyIdentityChanged();
        }
        if (profileChanged)
        {
            NotifyProfileChanged();
        }
    }

    void SessionService::NotifyIdentityChanged()
    {
        if (m_identityChanged)
        {
            m_identityChanged();
        }
        // Copied first: a listener is free to unsubscribe from inside itself.
        std::vector<IdentityChangedHandler> listeners;
        listeners.reserve(m_identityListeners.size());
        for (auto const& entry : m_identityListeners)
        {
            listeners.push_back(entry.second);
        }
        for (auto const& listener : listeners)
        {
            try
            {
                listener();
            }
            catch (...)
            {
            }
        }
    }

    IdentityChangedToken SessionService::AddIdentityChangedHandler(IdentityChangedHandler handler)
    {
        if (!handler)
        {
            return 0;
        }
        auto const token = m_nextIdentityToken++;
        m_identityListeners.emplace(token, std::move(handler));
        return token;
    }

    void SessionService::RemoveIdentityChangedHandler(IdentityChangedToken token) noexcept
    {
        m_identityListeners.erase(token);
    }

    IdentityChangedToken SessionService::AddProfileChangedHandler(IdentityChangedHandler handler)
    {
        if (!handler)
        {
            return 0;
        }
        auto const token = m_nextProfileToken++;
        m_profileListeners.emplace(token, std::move(handler));
        return token;
    }

    void SessionService::RemoveProfileChangedHandler(IdentityChangedToken token) noexcept
    {
        m_profileListeners.erase(token);
    }

    void SessionService::NotifyProfileChanged()
    {
        // Copied first: a listener is free to unsubscribe from inside itself.
        std::vector<IdentityChangedHandler> listeners;
        listeners.reserve(m_profileListeners.size());
        for (auto const& entry : m_profileListeners)
        {
            listeners.push_back(entry.second);
        }
        for (auto const& listener : listeners)
        {
            try
            {
                listener();
            }
            catch (...)
            {
            }
        }
    }
}
