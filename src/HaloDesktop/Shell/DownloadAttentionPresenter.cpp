#include "pch.h"
#include "Shell/DownloadAttentionPresenter.h"

#include "Services/NavigationService.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <stdexcept>
#include <utility>
#include <winrt/Windows.Data.Xml.Dom.h>

namespace
{
    namespace Downloads = HaloDesktop::Services::Downloads;
    namespace Notifications = winrt::Windows::UI::Notifications;

    // Halo's identity for notifications, and how Windows shows it.
    constexpr wchar_t AppUserModelId[] = L"HaloDesktop.App";
    constexpr wchar_t DisplayName[] = L"Halo Desktop";
    // Enough for a long session's finished downloads to stay clickable.
    constexpr std::size_t KeptToasts = 16;
    constexpr wchar_t KeySeparator = static_cast<wchar_t>(0x5C);

    TBPFLAG TaskbarFlag(Downloads::TaskbarProgressState state) noexcept
    {
        switch (state)
        {
        case Downloads::TaskbarProgressState::Indeterminate: return TBPF_INDETERMINATE;
        case Downloads::TaskbarProgressState::Normal: return TBPF_NORMAL;
        case Downloads::TaskbarProgressState::Paused: return TBPF_PAUSED;
        case Downloads::TaskbarProgressState::Error: return TBPF_ERROR;
        case Downloads::TaskbarProgressState::None: break;
        }
        return TBPF_NOPROGRESS;
    }

    std::filesystem::path AppDirectory()
    {
        std::array<wchar_t, 32768> buffer{};
        auto const length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size()) throw std::runtime_error{ "The app's own path is unknown." };
        return std::filesystem::path{ std::wstring{ buffer.data(), length } }.parent_path();
    }

    bool SetString(HKEY key, wchar_t const* name, std::wstring const& value) noexcept
    {
        auto const bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
        return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<BYTE const*>(value.c_str()), bytes) == ERROR_SUCCESS;
    }

    // An unpackaged app is known to the notification system by an app id with
    // a name and an icon under the user's classes; this writes Halo's, as the
    // Windows Community Toolkit does for desktop apps. A plain file path, so
    // a space in the install folder is no trouble.
    bool RegisterNotificationIdentity() noexcept
    {
        try
        {
            std::wstring const subkey = std::wstring{ L"Software" } + KeySeparator + L"Classes" + KeySeparator
                + L"AppUserModelId" + KeySeparator + AppUserModelId;
            HKEY key{};
            if (RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
            {
                return false;
            }
            auto const icon = (AppDirectory() / L"Assets" / L"Square44x44Logo.scale-200.png").wstring();
            auto const named = SetString(key, L"DisplayName", DisplayName) && SetString(key, L"IconUri", icon);
            RegCloseKey(key);
            return named;
        }
        catch (...)
        {
            return false;
        }
    }

    std::wstring DisplayTitle(Downloads::DownloadRecord const& record)
    {
        auto const& media = record.Media;
        auto const show = media.ShowName.value_or(std::wstring{});
        if (!show.empty() && !media.Title.empty() && show != media.Title)
        {
            return show + L" - " + media.Title;
        }
        return media.Title.empty() ? show : media.Title;
    }
}

namespace HaloDesktop::Shell
{
    DownloadAttentionPresenter::DownloadAttentionPresenter(
        HWND window,
        std::shared_ptr<Services::IDownloadService> downloads,
        std::shared_ptr<Services::NavigationService> navigation,
        winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher)
        : m_window(window),
          m_downloads(std::move(downloads)),
          m_navigation(std::move(navigation)),
          m_dispatcher(std::move(dispatcher))
    {
        if (!m_window || !m_downloads || !m_navigation || !m_dispatcher)
        {
            throw std::invalid_argument{ "The download attention presenter needs its window and services." };
        }
    }

    DownloadAttentionPresenter::~DownloadAttentionPresenter()
    {
        Detach();
    }

    void DownloadAttentionPresenter::Attach()
    {
        if (m_attached) return;
        m_attached = true;

        if (FAILED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_taskbar)))
            || FAILED(m_taskbar->HrInit()))
        {
            m_taskbar.Reset();
        }

        if (RegisterNotificationIdentity())
        {
            try
            {
                m_notifier = Notifications::ToastNotificationManager::CreateToastNotifier(AppUserModelId);
            }
            catch (...)
            {
                // Notifications are a courtesy: without them the downloads still finish.
                m_notifier = nullptr;
            }
        }

        // What is unfinished now is what may announce itself later.
        static_cast<void>(Downloads::TakeNewlyFinished(m_downloads->Records(), m_unfinished));
        m_changedToken = m_downloads->AddChangedHandler([weak = weak_from_this()]()
        {
            if (auto const self = weak.lock()) self->OnDownloadsChanged();
        });
        ApplyTaskbar(Downloads::SummarizeTaskbarProgress(m_downloads->Records()));
    }

    void DownloadAttentionPresenter::Detach() noexcept
    {
        if (!m_attached) return;
        m_attached = false;
        try
        {
            if (m_changedToken != 0) m_downloads->RemoveChangedHandler(m_changedToken);
        }
        catch (...)
        {
        }
        m_changedToken = {};
        if (m_taskbar)
        {
            m_taskbar->SetProgressState(m_window, TBPF_NOPROGRESS);
            m_taskbar.Reset();
        }
        m_shownToasts.clear();
        m_notifier = nullptr;
    }

    void DownloadAttentionPresenter::OnDownloadsChanged()
    {
        if (!m_attached) return;
        auto const& records = m_downloads->Records();
        ApplyTaskbar(Downloads::SummarizeTaskbarProgress(records));
        for (auto const& jobId : Downloads::TakeNewlyFinished(records, m_unfinished))
        {
            Announce(jobId);
        }
    }

    void DownloadAttentionPresenter::ApplyTaskbar(Downloads::TaskbarProgress const& progress) noexcept
    {
        if (!m_taskbar) return;
        if (m_taskbarShown && progress == m_shownProgress) return;
        m_shownProgress = progress;
        m_taskbarShown = true;
        m_taskbar->SetProgressState(m_window, TaskbarFlag(progress.State));
        if (progress.Total > 0
            && progress.State != Downloads::TaskbarProgressState::None
            && progress.State != Downloads::TaskbarProgressState::Indeterminate)
        {
            m_taskbar->SetProgressValue(m_window, progress.Completed, progress.Total);
        }
    }

    // Only while Halo is not the window in front: there, the Downloads page
    // and the side menu's dot already say so.
    void DownloadAttentionPresenter::Announce(std::wstring const& jobId) noexcept
    {
        if (!m_notifier || GetForegroundWindow() == m_window) return;
        try
        {
            auto const& records = m_downloads->Records();
            auto const record = std::find_if(records.begin(), records.end(), [&jobId](auto const& candidate)
            {
                return candidate.JobId == jobId;
            });
            if (record == records.end()) return;

            winrt::Windows::Data::Xml::Dom::XmlDocument content;
            content.LoadXml(Downloads::ReadyToWatchToastXml(DisplayTitle(*record), jobId));
            Notifications::ToastNotification toast{ content };
            toast.Activated([weak = weak_from_this(), dispatcher = m_dispatcher](
                Notifications::ToastNotification const&,
                winrt::Windows::Foundation::IInspectable const& args)
            {
                auto const activated = args.try_as<Notifications::ToastActivatedEventArgs>();
                if (!activated) return;
                std::wstring arguments{ activated.Arguments() };
                dispatcher.TryEnqueue([weak, arguments = std::move(arguments)]()
                {
                    if (auto const self = weak.lock()) self->OnToastActivated(arguments);
                });
            });
            m_notifier.Show(toast);
            m_shownToasts.push_back(toast);
            while (m_shownToasts.size() > KeptToasts) m_shownToasts.pop_front();
        }
        catch (...)
        {
            // Notifications turned off, or the platform refused: nothing to undo.
        }
    }

    void DownloadAttentionPresenter::OnToastActivated(std::wstring const& arguments)
    {
        if (!m_attached) return;
        auto const answer = Downloads::ParseToastArguments(arguments);
        if (!answer) return;
        try
        {
            BringToFront();
            if (answer->Play)
            {
                if (auto const request = m_downloads->BuildPlaybackRequest(winrt::hstring{ answer->JobId }))
                {
                    m_navigation->ShowOverlay(Services::Page::Player, request);
                    return;
                }
            }
            m_navigation->GoTo(Services::Page::Downloads);
        }
        catch (...)
        {
            // A download removed since its notification: the app is in front, which is enough.
        }
    }

    void DownloadAttentionPresenter::BringToFront() noexcept
    {
        if (IsIconic(m_window)) ShowWindow(m_window, SW_RESTORE);
        SetForegroundWindow(m_window);
    }
}
