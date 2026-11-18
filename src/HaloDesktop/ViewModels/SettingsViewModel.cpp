#include "pch.h"
#include "ViewModels/SettingsViewModel.h"
#include "Services/DiscordPresence.h"
#include "Playback/IPlaybackEngine.h"
#if __has_include("AddonRowViewModel.g.cpp")
#include "AddonRowViewModel.g.cpp"
#endif
#if __has_include("SettingsViewModel.g.cpp")
#include "SettingsViewModel.g.cpp"
#endif

#include "Services/AccountLabelPolicy.h"
#include "Services/BuildInfoPolicy.h"
#include "Services/CatalogLanguagePolicy.h"
#include "Services/PlaybackPreferences.h"
#include "Services/SettingsSyncService.h"
#include "ViewModels/ObservableHelper.h"
#include "ViewModels/SubtitlePreviewMetrics.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <sstream>
#include <string>
#include <utility>
#include <winrt/Windows.Foundation.h>

namespace
{
    auto const Visible = winrt::Microsoft::UI::Xaml::Visibility::Visible;
    auto const Collapsed = winrt::Microsoft::UI::Xaml::Visibility::Collapsed;

    winrt::Microsoft::UI::Xaml::Visibility VisibleWhen(bool condition) noexcept
    {
        return condition ? Visible : Collapsed;
    }

    std::vector<::HaloDesktop::Services::CatalogExtraDeclaration> ExtraDeclarations(
        ::HaloDesktop::Api::Dto::AddonRecord::Catalog const& catalog)
    {
        std::vector<::HaloDesktop::Services::CatalogExtraDeclaration> declarations;
        declarations.reserve(catalog.Extras.size());
        for (auto const& extra : catalog.Extras)
        {
            ::HaloDesktop::Services::CatalogExtraDeclaration declaration{ std::wstring{ extra.Name }, extra.IsRequired, {} };
            declaration.Options.reserve(extra.Options.size());
            for (auto const& option : extra.Options)
            {
                declaration.Options.emplace_back(option);
            }
            declarations.push_back(std::move(declaration));
        }
        return declarations;
    }

    // The add field shows the server's own sentence when it gave one.
    std::wstring AddFailureText(std::exception_ptr const& failure)
    {
        try
        {
            std::rethrow_exception(failure);
        }
        catch (winrt::hresult_error const& error)
        {
            std::wstring message{ error.message() };
            if (!message.empty())
            {
                return message;
            }
        }
        catch (std::exception const& error)
        {
            std::string const message{ error.what() };
            if (!message.empty())
            {
                return std::wstring{ winrt::to_hstring(message) };
            }
        }
        catch (...)
        {
        }
        return std::wstring{ ::HaloDesktop::Services::InvalidManifestLine };
    }

    std::vector<winrt::hstring> RowIds(
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> const& rows)
    {
        std::vector<winrt::hstring> ids;
        ids.reserve(rows.Size());
        for (auto const& item : rows)
        {
            if (auto const row = item.try_as<winrt::HaloDesktop::AddonRowViewModel>())
            {
                ids.push_back(row.Id());
            }
        }
        return ids;
    }
}

namespace winrt::HaloDesktop::implementation
{
    AddonRowViewModel::AddonRowViewModel(Content content)
        : m_content(std::move(content)),
          m_initials(::HaloDesktop::Services::AddonInitials(std::wstring_view{ m_content.Name }))
    {
    }
    winrt::hstring AddonRowViewModel::Id() const { return m_content.Id; }
    winrt::hstring AddonRowViewModel::Initials() const { return m_initials; }
    winrt::hstring AddonRowViewModel::Name() const { return m_content.Name; }
    winrt::hstring AddonRowViewModel::Version() const { return m_content.Version; }
    winrt::hstring AddonRowViewModel::Provides() const { return m_content.Provides; }
    bool AddonRowViewModel::IsGlobal() const noexcept { return m_content.IsGlobal; }
    bool AddonRowViewModel::IsShadowed() const noexcept { return m_content.Actions.Shadowed; }
    bool AddonRowViewModel::CatalogsShown() const noexcept { return m_content.CatalogsShown; }
    bool AddonRowViewModel::CatalogSwitchEnabled() const noexcept { return m_content.Actions.CatalogSwitchEnabled; }
    winrt::hstring AddonRowViewModel::CatalogSwitchLabel() const
    {
        return winrt::hstring{ ::HaloDesktop::Services::AddonCatalogSwitchLabel(std::wstring_view{ m_content.Name }) };
    }
    winrt::hstring AddonRowViewModel::GripToolTip() const
    {
        return winrt::hstring{ ::HaloDesktop::Services::AddonGripToolTip(m_content.Actions.Grip) };
    }
    // A personal copy the global entry answers for is present but not in use.
    double AddonRowViewModel::IdentityOpacity() const noexcept { return m_content.Actions.Shadowed ? 0.5 : 1.0; }
    Microsoft::UI::Xaml::Visibility AddonRowViewModel::GlobalVisibility() const noexcept { return VisibleWhen(m_content.IsGlobal); }
    Microsoft::UI::Xaml::Visibility AddonRowViewModel::YoursVisibility() const noexcept { return VisibleWhen(!m_content.IsGlobal); }
    Microsoft::UI::Xaml::Visibility AddonRowViewModel::CatalogSwitchVisibility() const noexcept { return VisibleWhen(m_content.Actions.ShowCatalogSwitch); }
    Microsoft::UI::Xaml::Visibility AddonRowViewModel::InstallForEveryoneVisibility() const noexcept { return VisibleWhen(m_content.Actions.ShowInstallForEveryone); }
    Microsoft::UI::Xaml::Visibility AddonRowViewModel::RemoveVisibility() const noexcept { return VisibleWhen(m_content.Actions.ShowRemove); }
    bool AddonRowViewModel::ListActionsEnabled() const noexcept { return m_content.Actions.ListActionsEnabled; }
    Microsoft::UI::Xaml::Thickness AddonRowViewModel::DividerThickness() const noexcept
    {
        return Microsoft::UI::Xaml::Thickness{ 0, m_firstInCard ? 0.0 : 1.0, 0, 0 };
    }
    winrt::event_token AddonRowViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler) { return m_propertyChanged.add(handler); }
    void AddonRowViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }
    bool AddonRowViewModel::IsMovable() const noexcept { return m_content.Actions.Movable; }
    void AddonRowViewModel::SetListBusy(bool busy)
    {
        if (m_content.Actions.ListActionsEnabled == !busy)
        {
            return;
        }
        m_content.Actions.ListActionsEnabled = !busy;
        Raise(L"ListActionsEnabled");
    }
    void AddonRowViewModel::SetFirstInCard(bool first)
    {
        if (m_firstInCard == first)
        {
            return;
        }
        m_firstInCard = first;
        Raise(L"DividerThickness");
    }
    void AddonRowViewModel::Raise(wchar_t const* propertyName) { ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, propertyName); }

    SettingsViewModel::SettingsViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_session(services.Session),
          m_theme(services.Theme),
          m_addonService(services.Addons),
          m_catalog(services.Catalog),
          m_settings(services.SettingsSync),
          m_playbackPreferences(services.PlaybackPreferences),
          m_discordPresenceService(services.DiscordPresence),
          m_playback(services.Playback),
          m_globalAddons(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_ownAddons(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_catalogLanguageOptions(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>())
    {
        if (!m_playbackPreferences || !m_discordPresenceService || !m_playback)
        {
            throw std::invalid_argument{ "SettingsViewModel requires local integration preferences." };
        }
        m_resumePlayback=m_playbackPreferences->ResumeEnabled();
        m_hardwareDecoding=m_playbackPreferences->HardwareDecodingEnabled();
        m_discordPresence=m_playbackPreferences->DiscordPresenceEnabled();
        Refresh();
    }
    SettingsViewModel::~SettingsViewModel()
    {
        if (m_settingsRevertedToken != 0) m_settings->RemoveRevertedHandler(m_settingsRevertedToken);
        if (m_profileChangedToken != 0) m_session->RemoveProfileChangedHandler(m_profileChangedToken);
    }
    winrt::hstring SettingsViewModel::UserName() const { return m_userName; }
    winrt::hstring SettingsViewModel::DisplayName() const
    {
        return winrt::hstring{ ::HaloDesktop::Services::AccountDisplayName(std::wstring_view{ m_userName }) };
    }
    winrt::hstring SettingsViewModel::SignedInLine() const { return m_userName; }
    winrt::hstring SettingsViewModel::AccountRoleLine() const
    {
        // The same caption the shell's account button shows.
        auto const plan = m_session->Plan();
        return winrt::hstring{ ::HaloDesktop::Services::AccountLabel(::HaloDesktop::Services::AccountLabelInput{
            .IsAdmin = m_session->IsAdmin(),
            .Plan = plan ? std::wstring_view{ *plan } : std::wstring_view{},
        }) };
    }
    winrt::hstring SettingsViewModel::VersionLine() const
    {
        ::HaloDesktop::Services::BuildInfoLineInput input;
        input.Loaded = m_buildInfo.has_value();
        if (m_buildInfo)
        {
            input.App = m_buildInfo->App;
            input.Mpv = m_buildInfo->Mpv;
        }
        return winrt::hstring{ ::HaloDesktop::Services::BuildInfoLine(input) };
    }
    winrt::hstring SettingsViewModel::AccountPictureVersion() const
    {
        return m_session->IsSignedIn() ? m_session->AvatarVersion().value_or(winrt::hstring{}) : winrt::hstring{};
    }
    winrt::hstring SettingsViewModel::AccountUserId() const { return m_session->UserId(); }
    winrt::hstring SettingsViewModel::ServerStatusLine() const { return m_serverStatusLine; }
    Microsoft::UI::Xaml::Visibility SettingsViewModel::ServerCheckingVisibility() const noexcept { return VisibleWhen(m_healthState == HealthState::Checking); }
    Microsoft::UI::Xaml::Visibility SettingsViewModel::ServerConnectedVisibility() const noexcept { return VisibleWhen(m_healthState == HealthState::Connected); }
    Microsoft::UI::Xaml::Visibility SettingsViewModel::ServerUnavailableVisibility() const noexcept { return VisibleWhen(m_healthState == HealthState::Unavailable); }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> SettingsViewModel::GlobalAddons() const { return m_globalAddons; }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> SettingsViewModel::OwnAddons() const { return m_ownAddons; }
    Microsoft::UI::Xaml::Visibility SettingsViewModel::GlobalAddonsVisibility() const noexcept { return VisibleWhen(m_globalAddons.Size() > 0); }
    Microsoft::UI::Xaml::Visibility SettingsViewModel::OwnAddonsVisibility() const noexcept { return VisibleWhen(m_ownAddons.Size() > 0); }
    bool SettingsViewModel::CanReorderGlobal() const noexcept
    {
        return ListShown() && !m_listBusy && m_addonService->CanEditScope(true);
    }
    bool SettingsViewModel::CanReorderOwn() const noexcept
    {
        return ListShown() && !m_listBusy && m_addonService->CanEditScope(false);
    }
    winrt::hstring SettingsViewModel::AddonUrl() const { return m_addonUrl; }
    void SettingsViewModel::AddonUrl(winrt::hstring const& value)
    {
        if (value == m_addonUrl)
        {
            return;
        }
        m_addonUrl = value;
        Raise(L"AddonUrl");
        Raise(L"CanAddAddon");
        // The message was about the URL that was there; editing it moves on.
        SetAddError({});
    }
    bool SettingsViewModel::CanAddAddon() const
    {
        return ::HaloDesktop::Services::CanSubmitAddon(::HaloDesktop::Services::AddFieldState{
            .Adding = m_adding,
            .ListBusy = m_listBusy,
            .ListLoaded = ListShown() && m_addonService->CanEditScope(false),
            .Url = std::wstring_view{ m_addonUrl },
        });
    }
    winrt::hstring SettingsViewModel::AddButtonText() const { return winrt::hstring{ ::HaloDesktop::Services::AddButtonLabel(m_adding) }; }
    winrt::hstring SettingsViewModel::AddErrorText() const { return winrt::hstring{ m_addError }; }
    Microsoft::UI::Xaml::Visibility SettingsViewModel::AddErrorVisibility() const noexcept { return VisibleWhen(!m_addError.empty()); }
    winrt::hstring SettingsViewModel::ListErrorText() const { return winrt::hstring{ m_listError }; }
    Microsoft::UI::Xaml::Visibility SettingsViewModel::ListErrorVisibility() const noexcept { return VisibleWhen(!m_listError.empty()); }
    winrt::hstring SettingsViewModel::AddonNoticeText() const { return m_addonNoticeText; }
    winrt::hstring SettingsViewModel::AccountNoticeText() const { return m_accountNoticeText; }
    Microsoft::UI::Xaml::Visibility SettingsViewModel::AddonNoticeVisibility() const noexcept { return VisibleWhen(m_addonNoticeVisible); }
    Microsoft::UI::Xaml::Visibility SettingsViewModel::AccountNoticeVisibility() const noexcept { return VisibleWhen(m_accountNoticeVisible); }
    // A refresh keeps showing the list it already had; only a first load with
    // nothing to show is a spinner or an error.
    Microsoft::UI::Xaml::Visibility SettingsViewModel::AddonLoadingVisibility() const noexcept { return VisibleWhen(m_addonsLoading && !ListShown()); }
    Microsoft::UI::Xaml::Visibility SettingsViewModel::AddonErrorVisibility() const noexcept { return VisibleWhen(!m_addonsLoading && m_addonsError && !ListShown()); }
    Microsoft::UI::Xaml::Visibility SettingsViewModel::AddonEmptyVisibility() const noexcept
    {
        return VisibleWhen(ListShown() && m_globalAddons.Size() + m_ownAddons.Size() == 0);
    }
    Microsoft::UI::Xaml::Visibility SettingsViewModel::AddonContentVisibility() const noexcept
    {
        return VisibleWhen(ListShown() && m_globalAddons.Size() + m_ownAddons.Size() > 0);
    }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> SettingsViewModel::CatalogLanguageOptions() const { return m_catalogLanguageOptions; }
    std::int32_t SettingsViewModel::CatalogLanguageIndex() const noexcept { return m_catalogLanguageIndex; }
    void SettingsViewModel::CatalogLanguageIndex(std::int32_t value)
    {
        // The list being rebuilt reports no selection; that is not a choice.
        if (value < 0 || value == m_catalogLanguageIndex
            || static_cast<std::size_t>(value) > m_catalogLanguageChoices.size())
        {
            return;
        }
        m_catalogLanguageIndex = value;
        auto const language = ::HaloDesktop::Services::CatalogLanguageForIndex(m_catalogLanguageChoices, value);
        m_settings->CatalogLanguage(language
            ? std::optional<winrt::hstring>{ winrt::hstring{ *language } }
            : std::nullopt);
        Raise(L"CatalogLanguageIndex");
    }
    Microsoft::UI::Xaml::Visibility SettingsViewModel::CatalogLanguageVisibility() const noexcept { return VisibleWhen(!m_catalogLanguageChoices.empty()); }
    double SettingsViewModel::SubtitleSize() const noexcept { return m_subtitleSize; }
    void SettingsViewModel::SubtitleSize(double value)
    {
        auto const next = std::clamp(value, 50.0, 200.0);
        if (std::abs(next - m_subtitleSize) < 0.01) return;
        m_subtitleSize = next;
        m_settings->SubtitleScalePercent(static_cast<std::int32_t>(std::lround(next)));
        Raise(L"SubtitleSize");
        Raise(L"SubtitleSizeLabel");
        Raise(L"PreviewFontSize");
        RaisePreviewMetrics();
    }
    winrt::hstring SettingsViewModel::SubtitleSizeLabel() const
    {
        std::wostringstream label;
        label << static_cast<std::int32_t>(std::lround(m_subtitleSize)) << L"%";
        return winrt::hstring(label.str());
    }
    winrt::hstring SettingsViewModel::PreviewText() const { return L"Subtitle preview text"; }
    double SettingsViewModel::PreviewFontSize() const noexcept { return ::HaloDesktop::ViewModels::SubtitlePreviewFontSize(m_subtitleSize); }
    Microsoft::UI::Xaml::Media::FontFamily SettingsViewModel::PreviewFontFamily() const
    {
        if (m_fontIndex == 2) return Microsoft::UI::Xaml::Media::FontFamily{ L"Georgia" };
        if (m_fontIndex == 3) return Microsoft::UI::Xaml::Media::FontFamily{ L"ms-appx:///Assets/Fonts/JetBrainsMono-Regular.ttf#JetBrains Mono" };
        // Default hands the face to the engine, which lands on a system sans. The preview
        // shows that face rather than pretending it knows what a styled track will pick.
        return Microsoft::UI::Xaml::Media::FontFamily{ L"Segoe UI Variable Text" };
    }
    std::int32_t SettingsViewModel::FontIndex() const noexcept { return m_fontIndex; }
    std::int32_t SettingsViewModel::OutlineIndex() const noexcept { return m_outlineIndex; }
    bool SettingsViewModel::IsDefaultFont() const noexcept { return m_fontIndex == 0; }
    bool SettingsViewModel::IsSystemFont() const noexcept { return m_fontIndex == 1; }
    bool SettingsViewModel::IsSerifFont() const noexcept { return m_fontIndex == 2; }
    bool SettingsViewModel::IsMonoFont() const noexcept { return m_fontIndex == 3; }
    bool SettingsViewModel::IsNoOutline() const noexcept { return m_outlineIndex == 0; }
    bool SettingsViewModel::IsThinOutline() const noexcept { return m_outlineIndex == 1; }
    bool SettingsViewModel::IsNormalOutline() const noexcept { return m_outlineIndex == 2; }
    bool SettingsViewModel::IsThickOutline() const noexcept { return m_outlineIndex == 3; }
    std::int32_t SettingsViewModel::AudioLanguageIndex() const noexcept { return m_audioLanguageIndex; }
    void SettingsViewModel::AudioLanguageIndex(std::int32_t value)
    {
        if (value < 0 || value > 4 || value == m_audioLanguageIndex) return;
        m_audioLanguageIndex = value;
        static constexpr wchar_t const* values[] = { L"eng", L"jpn", L"ger", L"spa", nullptr };
        m_settings->PreferredAudioLanguage(values[value] ? std::optional<winrt::hstring>{ values[value] } : std::nullopt);
        Raise(L"AudioLanguageIndex");
    }
    std::int32_t SettingsViewModel::SubtitleLanguageIndex() const noexcept { return m_subtitleLanguageIndex; }
    void SettingsViewModel::SubtitleLanguageIndex(std::int32_t value)
    {
        if (value < 0 || value > 4 || value == m_subtitleLanguageIndex) return;
        m_subtitleLanguageIndex = value;
        static constexpr wchar_t const* values[] = { nullptr, L"eng", L"jpn", L"ger", L"spa" };
        m_settings->PreferredSubtitleLanguage(values[value] ? std::optional<winrt::hstring>{ values[value] } : std::nullopt);
        Raise(L"SubtitleLanguageIndex");
    }
    Microsoft::UI::Xaml::Visibility SettingsViewModel::PreviewOutlineVisibility() const noexcept { return VisibleWhen(m_outlineIndex > 0); }
    double SettingsViewModel::PreviewOutlineOffset() const noexcept
    {
        // Border width follows the caption size the way libass scales its outline with
        // the font, so the preview keeps telling the truth as the size slider moves.
        return ::HaloDesktop::ViewModels::SubtitlePreviewOutlineOffset(
            static_cast<std::size_t>(std::clamp(m_outlineIndex, 0, 3)), m_subtitleSize);
    }
    double SettingsViewModel::PreviewOutlineNegativeOffset() const noexcept { return -PreviewOutlineOffset(); }
    Microsoft::UI::Xaml::Visibility SettingsViewModel::PreviewShadowVisibility() const noexcept { return VisibleWhen(m_subtitleShadow); }
    double SettingsViewModel::PreviewShadowOffset() const noexcept { return ::HaloDesktop::ViewModels::SubtitlePreviewShadowOffset(m_subtitleSize); }
    bool SettingsViewModel::AutoplayNext() const noexcept { return m_autoplayNext; }
    void SettingsViewModel::AutoplayNext(bool value) { if (m_autoplayNext != value) { m_autoplayNext = value; m_settings->AutoplayNextEpisode(value); Raise(L"AutoplayNext"); } }
    bool SettingsViewModel::SubtitleShadow() const noexcept { return m_subtitleShadow; }
    void SettingsViewModel::SubtitleShadow(bool value) { if (m_subtitleShadow != value) { m_subtitleShadow = value; m_settings->SubtitleShadow(value); Raise(L"SubtitleShadow"); Raise(L"PreviewShadowVisibility"); } }
    bool SettingsViewModel::SubtitleTrackStyling() const noexcept { return m_subtitleTrackStyling; }
    void SettingsViewModel::SubtitleTrackStyling(bool value) { if (m_subtitleTrackStyling != value) { m_subtitleTrackStyling = value; m_settings->SubtitleTrackStyling(value); Raise(L"SubtitleTrackStyling"); } }
    bool SettingsViewModel::ResumePlayback() const noexcept { return m_resumePlayback; }
    void SettingsViewModel::ResumePlayback(bool value) { if (m_resumePlayback != value) { m_resumePlayback = value; m_playbackPreferences->ResumeEnabled(value); Raise(L"ResumePlayback"); } }
    bool SettingsViewModel::HardwareDecoding() const noexcept { return m_hardwareDecoding; }
    void SettingsViewModel::HardwareDecoding(bool value) { if (m_hardwareDecoding != value) { m_hardwareDecoding = value; m_playbackPreferences->HardwareDecodingEnabled(value); Raise(L"HardwareDecoding"); } }
    bool SettingsViewModel::DiscordPresence() const noexcept { return m_discordPresence; }
    void SettingsViewModel::DiscordPresence(bool value)
    {
        if (m_discordPresence == value) return;
        m_discordPresence = value;
        m_playbackPreferences->DiscordPresenceEnabled(value);
        m_discordPresenceService->SetEnabled(value);
        if (value) m_discordPresenceService->Update(m_playback->State());
        Raise(L"DiscordPresence");
    }
    void SettingsViewModel::Refresh()
    {
        m_accountNoticeVisible = false;
        m_accountNoticeText.clear();
        m_discordPresence = m_playbackPreferences->DiscordPresenceEnabled();
        m_discordPresenceService->SetEnabled(m_discordPresence);
        m_userName = m_session->UserName();
        RaiseAccountIdentity();
        Raise(L"DiscordPresence");
        Raise(L"AccountNoticeText");
        Raise(L"AccountNoticeVisibility");
        // What the service already holds for this account is shown at once, as
        // a cached list is; another account's rows never stay up.
        if (m_addonService->HasLoaded())
        {
            SynchronizeAddons();
        }
        else
        {
            ClearAddonRows();
        }
        RaiseAddonState();
        if (!m_buildInfoRequested)
        {
            m_buildInfoRequested = true;
            static_cast<void>(ReadBuildInfoAsync());
        }
        static_cast<void>(LoadAsync());
        ProbeHealth();
    }
    void SettingsViewModel::ProbeHealth()
    {
        auto const version = ++m_healthRequestVersion;
        if (m_healthState != HealthState::Connected)
        {
            m_healthState = HealthState::Checking;
            m_serverStatusLine = L"CHECKING…";
            Raise(L"ServerStatusLine");
            Raise(L"ServerCheckingVisibility");
            Raise(L"ServerConnectedVisibility");
            Raise(L"ServerUnavailableVisibility");
        }
        static_cast<void>(ProbeHealthAsync(version));
    }
    void SettingsViewModel::CancelHealthProbe()
    {
        ++m_healthRequestVersion;
    }

    std::optional<::HaloDesktop::Api::Dto::AddonRecord> SettingsViewModel::FindRecord(winrt::hstring const& id) const
    {
        for (auto const& record : m_addonService->Records())
        {
            if (record.Id == id)
            {
                return record;
            }
        }
        return std::nullopt;
    }

    bool SettingsViewModel::ListShown() const noexcept { return m_listShown; }

    void SettingsViewModel::AddAddon()
    {
        if (!CanAddAddon())
        {
            return;
        }
        auto const url = ::HaloDesktop::Services::TrimAddonUrl(std::wstring_view{ m_addonUrl });
        std::vector<std::optional<std::wstring>> installed;
        for (auto const& record : m_addonService->Records())
        {
            installed.push_back(record.TransportUrl
                ? std::optional<std::wstring>{ std::wstring{ *record.TransportUrl } }
                : std::nullopt);
        }
        if (::HaloDesktop::Services::IsAddonInstalled(installed, url))
        {
            SetAddError(std::wstring{ ::HaloDesktop::Services::AlreadyInstalledLine });
            return;
        }
        static_cast<void>(AddAddonAsync(winrt::hstring{ url }));
    }

    void SettingsViewModel::ToggleAddon(winrt::hstring const& id, bool enabled)
    {
        auto const record = FindRecord(id);
        // Realising a row sets its switch, which reports the value it already
        // had; only a real change is sent.
        if (!record || !record->HideCatalogs == enabled)
        {
            return;
        }
        if (record->IsGlobal && !m_session->IsAdmin())
        {
            SynchronizeAddons();
            return;
        }
        SetListError({});
        static_cast<void>(ToggleAddonAsync(id, record->Name, enabled));
    }

    std::optional<::HaloDesktop::Services::ConfirmationCopy> SettingsViewModel::RemoveConfirmation(winrt::hstring const& id) const
    {
        auto const record = FindRecord(id);
        if (!record || m_listBusy)
        {
            return std::nullopt;
        }
        auto const scope = record->IsGlobal ? ::HaloDesktop::Services::AddonScope::Global : ::HaloDesktop::Services::AddonScope::Yours;
        auto const actions = ::HaloDesktop::Services::EvaluateAddonRow(
            ::HaloDesktop::Services::AddonRowFacts{
                .Scope = scope,
                .TransportUrl = record->TransportUrl ? std::optional<std::wstring_view>{ *record->TransportUrl } : std::nullopt,
                .ProvidedGlobally = record->ProvidedGlobally,
            },
            ::HaloDesktop::Services::AddonRowContext{ .IsAdmin = m_session->IsAdmin() });
        if (!actions.ShowRemove)
        {
            return std::nullopt;
        }
        return ::HaloDesktop::Services::RemoveAddonConfirmation(scope, std::wstring_view{ record->Name });
    }

    void SettingsViewModel::RemoveAddon(winrt::hstring const& id)
    {
        if (m_listBusy || !RemoveConfirmation(id))
        {
            return;
        }
        auto const record = FindRecord(id);
        SetListError({});
        SetListBusy(true);
        static_cast<void>(RemoveAddonAsync(id, record ? record->Name : winrt::hstring{}));
    }

    std::optional<::HaloDesktop::Services::ConfirmationCopy> SettingsViewModel::BeginInstallForEveryone(winrt::hstring const& id)
    {
        auto const record = FindRecord(id);
        if (!record || record->IsGlobal || m_listBusy)
        {
            return std::nullopt;
        }
        switch (m_addonService->CheckInstallForEveryone(id))
        {
        case ::HaloDesktop::Services::InstallForEveryoneCheck::Proceed:
            return ::HaloDesktop::Services::InstallForEveryoneConfirmation(std::wstring_view{ record->Name });
        case ::HaloDesktop::Services::InstallForEveryoneCheck::GlobalListIncomplete:
            SetListError(::HaloDesktop::Services::AddonListErrorLine(
                ::HaloDesktop::Services::AddonListFailure::GlobalListIncomplete, {}));
            return std::nullopt;
        default:
            return std::nullopt;
        }
    }

    void SettingsViewModel::InstallForEveryone(winrt::hstring const& id)
    {
        auto const record = FindRecord(id);
        if (!record || record->IsGlobal || m_listBusy)
        {
            return;
        }
        SetListError({});
        SetListBusy(true);
        static_cast<void>(InstallForEveryoneAsync(id, record->Name));
    }

    void SettingsViewModel::CommitReorder(bool global)
    {
        auto const rows = global ? m_globalAddons : m_ownAddons;
        auto ids = RowIds(rows);
        if (m_listBusy || !m_addonService->CanEditScope(global))
        {
            // Put the rows back where the account has them.
            SynchronizeAddons();
            return;
        }
        std::vector<winrt::hstring> current;
        for (auto const& record : m_addonService->Records())
        {
            if (record.IsGlobal == global)
            {
                current.push_back(record.Id);
            }
        }
        RefreshRowDividers();
        if (ids == current)
        {
            return;
        }
        SetListError({});
        SetListBusy(true);
        static_cast<void>(SaveOrderAsync(global, std::move(ids)));
    }

    SettingsViewModel::RowMoves SettingsViewModel::MovesFor(winrt::hstring const& id) const
    {
        RowMoves moves;
        for (auto const& rows : { m_globalAddons, m_ownAddons })
        {
            auto const ids = RowIds(rows);
            auto const found = std::find(ids.begin(), ids.end(), id);
            if (found == ids.end())
            {
                continue;
            }
            auto const row = rows.GetAt(static_cast<std::uint32_t>(found - ids.begin())).as<winrt::HaloDesktop::AddonRowViewModel>();
            auto const global = rows == m_globalAddons;
            auto const canReorder = global ? CanReorderGlobal() : CanReorderOwn();
            if (!canReorder || !winrt::get_self<AddonRowViewModel>(row)->IsMovable())
            {
                return moves;
            }
            moves.Offered = true;
            moves.CanMoveUp = found != ids.begin();
            moves.CanMoveDown = found + 1 != ids.end();
            return moves;
        }
        return moves;
    }

    void SettingsViewModel::MoveAddon(winrt::hstring const& id, std::int32_t offset)
    {
        auto const moves = MovesFor(id);
        if (!moves.Offered || (offset < 0 && !moves.CanMoveUp) || (offset > 0 && !moves.CanMoveDown) || offset == 0)
        {
            return;
        }
        for (auto const& rows : { m_globalAddons, m_ownAddons })
        {
            auto const ids = RowIds(rows);
            std::vector<std::wstring> plain(ids.begin(), ids.end());
            auto const moved = ::HaloDesktop::Services::MoveAddonId(plain, std::wstring_view{ id }, offset);
            if (!moved)
            {
                continue;
            }
            auto const from = static_cast<std::uint32_t>(std::find(ids.begin(), ids.end(), id) - ids.begin());
            auto const to = static_cast<std::uint32_t>(std::find(moved->begin(), moved->end(), std::wstring{ id }) - moved->begin());
            auto const item = rows.GetAt(from);
            rows.RemoveAt(from);
            rows.InsertAt(to, item);
            CommitReorder(rows == m_globalAddons);
            return;
        }
    }

    bool SettingsViewModel::CanDragRows(std::vector<winrt::hstring> const& ids) const
    {
        if (m_listBusy || ids.empty())
        {
            return false;
        }
        for (auto const& id : ids)
        {
            auto const moves = MovesFor(id);
            if (!moves.Offered)
            {
                return false;
            }
        }
        return true;
    }

    bool SettingsViewModel::IsLightTheme() const noexcept
    {
        return m_theme->Preference() == ::HaloDesktop::Services::ThemePreference::Light;
    }
    bool SettingsViewModel::IsDarkTheme() const noexcept
    {
        return m_theme->Preference() == ::HaloDesktop::Services::ThemePreference::Dark;
    }
    bool SettingsViewModel::IsSystemTheme() const noexcept
    {
        return m_theme->Preference() == ::HaloDesktop::Services::ThemePreference::System;
    }
    void SettingsViewModel::SetTheme(std::int32_t index)
    {
        using ::HaloDesktop::Services::ThemePreference;
        if (index < 0 || index > 2) return;
        m_theme->SetPreference(static_cast<ThemePreference>(index));
        Raise(L"IsLightTheme");
        Raise(L"IsDarkTheme");
        Raise(L"IsSystemTheme");
    }
    void SettingsViewModel::SetFont(std::int32_t index)
    {
        if (index < 0 || index > 3 || index == m_fontIndex) return;
        m_fontIndex = index;
        m_settings->SubtitleFontFamily(winrt::hstring{
            ::HaloDesktop::ViewModels::kSubtitleFontFamilies[static_cast<std::size_t>(index)] });
        Raise(L"FontIndex");
        Raise(L"IsDefaultFont");
        Raise(L"IsSystemFont");
        Raise(L"IsSerifFont");
        Raise(L"IsMonoFont");
        Raise(L"PreviewFontFamily");
    }
    void SettingsViewModel::SetOutline(std::int32_t index)
    {
        if (index < 0 || index > 3 || index == m_outlineIndex) return;
        m_outlineIndex = index;
        m_settings->SubtitleOutline(winrt::hstring{
            ::HaloDesktop::ViewModels::kSubtitleOutlines[static_cast<std::size_t>(index)] });
        Raise(L"OutlineIndex");
        Raise(L"IsNoOutline");
        Raise(L"IsThinOutline");
        Raise(L"IsNormalOutline");
        Raise(L"IsThickOutline");
        Raise(L"PreviewOutlineVisibility");
        RaisePreviewMetrics();
    }
    void SettingsViewModel::SignOut()
    {
        CancelHealthProbe();
        static_cast<void>(RunSignOutAsync());
    }
    winrt::Windows::Foundation::IAsyncAction SettingsViewModel::RunSignOutAsync()
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        bool failed{};
        try
        {
            co_await m_session->SignOutAsync();
        }
        catch (...)
        {
            failed = true;
        }
        co_await uiContext;
        if (failed)
        {
            m_accountNoticeText = L"Halo could not remove the saved session. You are still signed in. Try again.";
            m_accountNoticeVisible = true;
            Raise(L"AccountNoticeText");
            Raise(L"AccountNoticeVisibility");
            co_return;
        }
        m_userName.clear();
        RaiseAccountIdentity();
    }
    winrt::event_token SettingsViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler) { return m_propertyChanged.add(handler); }
    void SettingsViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }

    void SettingsViewModel::SynchronizeAddons()
    {
        if (!m_addonService->HasLoaded())
        {
            ClearAddonRows();
            return;
        }
        auto const isAdmin = m_session->IsAdmin();
        auto const globalComplete = m_addonService->CanEditScope(true);
        auto const ownComplete = m_addonService->CanEditScope(false);
        m_globalAddons.Clear();
        m_ownAddons.Clear();
        for (auto const& record : m_addonService->Records())
        {
            auto const scope = record.IsGlobal ? ::HaloDesktop::Services::AddonScope::Global : ::HaloDesktop::Services::AddonScope::Yours;
            // Built without the busy flag, which each row then follows on its own.
            auto const actions = ::HaloDesktop::Services::EvaluateAddonRow(
                ::HaloDesktop::Services::AddonRowFacts{
                    .Scope = scope,
                    .TransportUrl = record.TransportUrl ? std::optional<std::wstring_view>{ *record.TransportUrl } : std::nullopt,
                    .ProvidedGlobally = record.ProvidedGlobally,
                    .HideCatalogs = record.HideCatalogs,
                    .CatalogCount = record.Catalogs.size(),
                },
                ::HaloDesktop::Services::AddonRowContext{
                    .IsAdmin = isAdmin,
                    .ListBusy = false,
                    .ScopeListComplete = record.IsGlobal ? globalComplete : ownComplete,
                });
            std::vector<std::wstring> resources(record.Resources.begin(), record.Resources.end());
            auto const provides = actions.Shadowed
                ? std::wstring{ ::HaloDesktop::Services::ShadowedAddonLine }
                : ::HaloDesktop::Services::AddonProvidesLine(record.Catalogs.size(), resources, std::wstring_view{ record.Description });
            auto row = winrt::make_self<AddonRowViewModel>(AddonRowViewModel::Content{
                .Id = record.Id,
                .Name = record.Name,
                .Version = record.Version,
                .Provides = winrt::hstring{ provides },
                .IsGlobal = record.IsGlobal,
                .CatalogsShown = !record.HideCatalogs,
                .Actions = actions,
            });
            row->SetListBusy(m_listBusy);
            (record.IsGlobal ? m_globalAddons : m_ownAddons).Append(*row);
        }
        m_listShown = true;
        RefreshRowDividers();
        RebuildCatalogLanguage();
        RaiseAddonState();
    }

    void SettingsViewModel::ClearAddonRows()
    {
        m_globalAddons.Clear();
        m_ownAddons.Clear();
        m_listShown = false;
        RebuildCatalogLanguage();
    }

    void SettingsViewModel::RefreshRowDividers()
    {
        // One card holds both lists; only its very first row has no hairline above it.
        auto first = true;
        for (auto const& rows : { m_globalAddons, m_ownAddons })
        {
            for (auto const& item : rows)
            {
                if (auto const row = item.try_as<winrt::HaloDesktop::AddonRowViewModel>())
                {
                    winrt::get_self<AddonRowViewModel>(row)->SetFirstInCard(first);
                    first = false;
                }
            }
        }
    }

    void SettingsViewModel::SetListBusy(bool busy)
    {
        if (m_listBusy == busy)
        {
            return;
        }
        m_listBusy = busy;
        for (auto const& rows : { m_globalAddons, m_ownAddons })
        {
            for (auto const& item : rows)
            {
                if (auto const row = item.try_as<winrt::HaloDesktop::AddonRowViewModel>())
                {
                    winrt::get_self<AddonRowViewModel>(row)->SetListBusy(busy);
                }
            }
        }
        RaiseAddonState();
    }

    void SettingsViewModel::SetAdding(bool adding)
    {
        if (m_adding == adding)
        {
            return;
        }
        m_adding = adding;
        Raise(L"AddButtonText");
        Raise(L"CanAddAddon");
    }

    void SettingsViewModel::SetAddError(std::wstring text)
    {
        if (m_addError == text)
        {
            return;
        }
        m_addError = std::move(text);
        Raise(L"AddErrorText");
        Raise(L"AddErrorVisibility");
    }

    void SettingsViewModel::SetListError(std::wstring text)
    {
        if (m_listError == text)
        {
            return;
        }
        m_listError = std::move(text);
        Raise(L"ListErrorText");
        Raise(L"ListErrorVisibility");
    }

    void SettingsViewModel::RebuildCatalogLanguage()
    {
        std::vector<std::vector<::HaloDesktop::Services::CatalogExtraDeclaration>> catalogs;
        if (m_listShown)
        {
            for (auto const& record : m_addonService->Records())
            {
                for (auto const& catalog : record.Catalogs)
                {
                    catalogs.push_back(ExtraDeclarations(catalog));
                }
            }
        }
        auto choices = ::HaloDesktop::Services::FirstLanguageChoices(catalogs).value_or(std::vector<std::wstring>{});
        if (choices != m_catalogLanguageChoices)
        {
            m_catalogLanguageChoices = std::move(choices);
            m_catalogLanguageOptions.Clear();
            if (!m_catalogLanguageChoices.empty())
            {
                m_catalogLanguageOptions.Append(winrt::box_value(winrt::hstring{ ::HaloDesktop::Services::AddonDefaultLanguageLabel }));
                for (auto const& choice : m_catalogLanguageChoices)
                {
                    m_catalogLanguageOptions.Append(winrt::box_value(winrt::hstring{ choice }));
                }
            }
            Raise(L"CatalogLanguageVisibility");
        }
        auto const current = m_settings->CatalogLanguage();
        m_catalogLanguageIndex = ::HaloDesktop::Services::CatalogLanguageIndex(
            m_catalogLanguageChoices,
            current ? std::optional<std::wstring_view>{ std::wstring_view{ *current } } : std::nullopt);
        Raise(L"CatalogLanguageIndex");
    }

    void SettingsViewModel::RaiseAddonState()
    {
        Raise(L"CanAddAddon");
        Raise(L"AddButtonText");
        Raise(L"CanReorderGlobal");
        Raise(L"CanReorderOwn");
        Raise(L"GlobalAddonsVisibility");
        Raise(L"OwnAddonsVisibility");
        Raise(L"AddonLoadingVisibility");
        Raise(L"AddonErrorVisibility");
        Raise(L"AddonEmptyVisibility");
        Raise(L"AddonContentVisibility");
    }

    void SettingsViewModel::RaiseAccountIdentity()
    {
        Raise(L"UserName");
        Raise(L"DisplayName");
        Raise(L"SignedInLine");
        Raise(L"AccountRoleLine");
        // Not a bound property: the page redraws the picture when it hears it.
        Raise(L"AccountPicture");
    }

    void SettingsViewModel::RaisePreviewMetrics()
    {
        Raise(L"PreviewOutlineOffset");
        Raise(L"PreviewOutlineNegativeOffset");
        Raise(L"PreviewShadowOffset");
    }

    winrt::Windows::Foundation::IAsyncAction SettingsViewModel::ProbeHealthAsync(std::uint64_t version)
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        auto const latency = co_await m_session->ProbeHealthAsync();
        co_await uiContext;
        if (version != m_healthRequestVersion)
        {
            co_return;
        }
        if (latency)
        {
            m_healthState = HealthState::Connected;
            m_serverStatusLine = L"CONNECTED · " + winrt::to_hstring(latency->count()) + L" MS";
        }
        else
        {
            m_healthState = HealthState::Unavailable;
            m_serverStatusLine = L"UNREACHABLE";
        }
        Raise(L"ServerStatusLine");
        Raise(L"ServerCheckingVisibility");
        Raise(L"ServerConnectedVisibility");
        Raise(L"ServerUnavailableVisibility");
    }

    winrt::Windows::Foundation::IAsyncAction SettingsViewModel::ReadBuildInfoAsync()
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        co_await winrt::resume_background();
        auto info = ::HaloDesktop::Services::ReadBuildInfo();
        co_await uiContext;
        m_buildInfo = std::move(info);
        Raise(L"VersionLine");
    }

    winrt::Windows::Foundation::IAsyncAction SettingsViewModel::LoadAsync()
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        auto const version = ++m_loadVersion;
        m_addonsLoading = true;
        m_addonsError = false;
        m_addonNoticeVisible = false;
        Raise(L"AddonNoticeVisibility");
        RaiseAddonState();
        bool settingsFailed{};
        try
        {
            co_await m_settings->LoadAsync();
        }
        catch (...)
        {
            settingsFailed = true;
        }
        co_await uiContext;
        bool addonsFailed{};
        try
        {
            co_await m_addonService->LoadAsync();
        }
        catch (...)
        {
            addonsFailed = true;
        }
        co_await uiContext;
        SubscribeToServiceChanges();
        if (version != m_loadVersion)
        {
            co_return;
        }
        m_addonsLoading = false;
        m_addonsError = addonsFailed;
        // A failed refresh leaves the service's last list in place, which
        // stays on screen; this account having none yet clears the rows.
        SynchronizeAddons();
        RaiseAddonState();

        if (!settingsFailed)
        {
            ReadSyncedSettings();
        }
        else
        {
            m_addonNoticeVisible = true;
            m_addonNoticeText = L"Synced settings could not be refreshed. Device-local choices remain available.";
            Raise(L"AddonNoticeText");
            Raise(L"AddonNoticeVisibility");
        }
        if (!addonsFailed && m_addonService->HasLoaded() && !m_addonService->CanEditScope(false))
        {
            m_addonNoticeVisible = true;
            m_addonNoticeText = L"This addon list cannot be edited safely because one or more transport URLs are hidden.";
            Raise(L"AddonNoticeText");
            Raise(L"AddonNoticeVisibility");
        }
    }

    // Subscribed here rather than in the constructor: by the time a load has
    // resumed this object is fully built, so a weak reference to it can be had.
    // Both are removed in the destructor.
    void SettingsViewModel::SubscribeToServiceChanges()
    {
        if (m_settingsRevertedToken == 0)
        {
            // An edit made before the account's settings were read is dropped
            // when none can be had; the page then shows the stored values again.
            m_settingsRevertedToken = m_settings->AddRevertedHandler([weak = get_weak()]()
            {
                if (auto const self = weak.get()) self->ReadSyncedSettings();
            });
        }
        if (m_profileChangedToken == 0)
        {
            // The admin flag can arrive after the addons did, and decides which
            // rows can be edited and what the account line says.
            m_profileChangedToken = m_session->AddProfileChangedHandler([weak = get_weak()]()
            {
                if (auto const self = weak.get()) self->OnProfileChanged();
            });
        }
    }

    void SettingsViewModel::OnProfileChanged()
    {
        // Idempotent, so whichever of this and the service's own handler runs
        // first does the work.
        m_addonService->RefreshEditability();
        if (m_listShown)
        {
            SynchronizeAddons();
        }
        RaiseAddonState();
        m_userName = m_session->UserName();
        RaiseAccountIdentity();
    }

    void SettingsViewModel::ReadSyncedSettings()
    {
        m_subtitleSize = m_settings->SubtitleScalePercent();
        auto const family = m_settings->SubtitleFontFamily();
        m_fontIndex = static_cast<std::int32_t>(::HaloDesktop::ViewModels::SubtitleFontIndex(family));
        auto const outline = m_settings->SubtitleOutline();
        m_outlineIndex = static_cast<std::int32_t>(::HaloDesktop::ViewModels::SubtitleOutlineIndex(outline));
        m_subtitleShadow = m_settings->SubtitleShadow();
        m_subtitleTrackStyling = m_settings->SubtitleTrackStyling();
        m_autoplayNext = m_settings->AutoplayNextEpisode();
        auto const audio = m_settings->PreferredAudioLanguage();
        m_audioLanguageIndex = !audio ? 4 : *audio == L"jpn" ? 1 : *audio == L"ger" ? 2 : *audio == L"spa" ? 3 : 0;
        auto const subtitles = m_settings->PreferredSubtitleLanguage();
        m_subtitleLanguageIndex = !subtitles ? 0 : *subtitles == L"jpn" ? 2 : *subtitles == L"ger" ? 3 : *subtitles == L"spa" ? 4 : 1;
        Raise(L"SubtitleSize");
        Raise(L"SubtitleSizeLabel");
        Raise(L"PreviewFontFamily");
        Raise(L"PreviewFontSize");
        Raise(L"FontIndex");
        Raise(L"OutlineIndex");
        Raise(L"IsDefaultFont");
        Raise(L"IsSystemFont");
        Raise(L"IsSerifFont");
        Raise(L"IsMonoFont");
        Raise(L"IsNoOutline");
        Raise(L"IsThinOutline");
        Raise(L"IsNormalOutline");
        Raise(L"IsThickOutline");
        Raise(L"PreviewOutlineVisibility");
        Raise(L"PreviewShadowVisibility");
        RaisePreviewMetrics();
        Raise(L"SubtitleShadow");
        Raise(L"SubtitleTrackStyling");
        Raise(L"AutoplayNext");
        Raise(L"AudioLanguageIndex");
        Raise(L"SubtitleLanguageIndex");
        RebuildCatalogLanguage();
    }

    winrt::Windows::Foundation::IAsyncAction SettingsViewModel::AddAddonAsync(winrt::hstring url)
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        SetAddError({});
        SetAdding(true);
        SetListBusy(true);
        std::exception_ptr failure;
        try
        {
            co_await m_addonService->AddAsync(url);
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        co_await uiContext;
        SetAdding(false);
        SetListBusy(false);
        if (failure)
        {
            SetAddError(AddFailureText(failure));
        }
        else
        {
            m_catalog->InvalidateCatalogs();
            m_addonUrl = winrt::hstring{};
            Raise(L"AddonUrl");
        }
        SynchronizeAddons();
    }

    winrt::Windows::Foundation::IAsyncAction SettingsViewModel::ToggleAddonAsync(winrt::hstring id, winrt::hstring name, bool enabled)
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        bool failed{};
        try
        {
            co_await m_addonService->SetCatalogsVisibleAsync(std::move(id), enabled);
        }
        catch (...)
        {
            failed = true;
        }
        co_await uiContext;
        if (failed)
        {
            SetListError(::HaloDesktop::Services::AddonListErrorLine(
                ::HaloDesktop::Services::AddonListFailure::Catalogs, std::wstring_view{ name }));
        }
        else
        {
            m_catalog->InvalidateCatalogs();
        }
        // A refused change puts the switch back where the account has it.
        SynchronizeAddons();
    }

    winrt::Windows::Foundation::IAsyncAction SettingsViewModel::RemoveAddonAsync(winrt::hstring id, winrt::hstring name)
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        bool failed{};
        try
        {
            co_await m_addonService->RemoveAsync(std::move(id));
        }
        catch (...)
        {
            failed = true;
        }
        co_await uiContext;
        SetListBusy(false);
        if (failed)
        {
            SetListError(::HaloDesktop::Services::AddonListErrorLine(
                ::HaloDesktop::Services::AddonListFailure::Remove, std::wstring_view{ name }));
        }
        else
        {
            m_catalog->InvalidateCatalogs();
        }
        SynchronizeAddons();
    }

    winrt::Windows::Foundation::IAsyncAction SettingsViewModel::InstallForEveryoneAsync(winrt::hstring id, winrt::hstring name)
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        std::optional<::HaloDesktop::Services::InstallForEveryoneOutcome> outcome;
        try
        {
            outcome = co_await m_addonService->InstallForEveryoneAsync(std::move(id));
        }
        catch (...)
        {
        }
        co_await uiContext;
        SetListBusy(false);
        if (!outcome)
        {
            SetListError(::HaloDesktop::Services::AddonListErrorLine(
                ::HaloDesktop::Services::AddonListFailure::InstallForEveryone, std::wstring_view{ name }));
        }
        else
        {
            m_catalog->InvalidateCatalogs();
            if (*outcome == ::HaloDesktop::Services::InstallForEveryoneOutcome::OwnCopyKept)
            {
                SetListError(::HaloDesktop::Services::AddonListErrorLine(
                    ::HaloDesktop::Services::AddonListFailure::OwnCopyKept, std::wstring_view{ name }));
            }
        }
        SynchronizeAddons();
    }

    winrt::Windows::Foundation::IAsyncAction SettingsViewModel::SaveOrderAsync(bool global, std::vector<winrt::hstring> ids)
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        bool failed{};
        try
        {
            co_await m_addonService->SaveOrderAsync(global, std::move(ids));
        }
        catch (...)
        {
            failed = true;
        }
        co_await uiContext;
        SetListBusy(false);
        if (failed)
        {
            SetListError(::HaloDesktop::Services::AddonListErrorLine(
                ::HaloDesktop::Services::AddonListFailure::Reorder, {}));
        }
        else
        {
            // Order is priority, and Home lists catalogs in it.
            m_catalog->InvalidateCatalogs();
        }
        // The rows follow the account's list: the new order once saved, the
        // old one when the save was refused.
        SynchronizeAddons();
    }

    void SettingsViewModel::Raise(wchar_t const* propertyName) { ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, propertyName); }
}
