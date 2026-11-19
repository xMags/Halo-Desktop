#pragma once

#include "Services/Downloads/DownloadAttentionPolicy.h"
#include "Services/ServiceInterfaces.h"

#include <deque>
#include <memory>
#include <set>
#include <string>
#include <windows.h>
#include <ShObjIdl_core.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Windows.UI.Notifications.h>
#include <wrl/client.h>

namespace HaloDesktop::Services
{
    class NavigationService;
}

namespace HaloDesktop::Shell
{
    // The downloads, seen from outside the app: the taskbar button shows their
    // progress (SummarizeTaskbarProgress), and a Windows notification says when
    // one has finished, with a Play button, while Halo is not the window in
    // front. The rules are DownloadAttentionPolicy.
    //
    // Notifications go through Windows' own toast API under Halo's app id,
    // which this registers for the user (its name and icon) the first time.
    // A click is answered while Halo runs; one on a notification left behind
    // after Halo closed only dismisses it.
    //
    // Created by the main window once its handle exists, on the UI thread, and
    // Detach()ed before the window closes. A click is answered on a background
    // thread; the answer is handed to the UI thread and dropped if this has
    // gone by then. Every failure (no taskbar, notifications turned off)
    // leaves the downloads themselves untouched.
    class DownloadAttentionPresenter final : public std::enable_shared_from_this<DownloadAttentionPresenter>
    {
    public:
        DownloadAttentionPresenter(
            HWND window,
            std::shared_ptr<Services::IDownloadService> downloads,
            std::shared_ptr<Services::NavigationService> navigation,
            winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher);
        ~DownloadAttentionPresenter();

        DownloadAttentionPresenter(DownloadAttentionPresenter const&) = delete;
        DownloadAttentionPresenter& operator=(DownloadAttentionPresenter const&) = delete;

        // Subscribes and draws the current state. Call once, after construction.
        void Attach();
        // Clears the taskbar button and stops listening. Safe to call more than once.
        void Detach() noexcept;

    private:
        void OnDownloadsChanged();
        void ApplyTaskbar(Services::Downloads::TaskbarProgress const& progress) noexcept;
        void Announce(std::wstring const& jobId) noexcept;
        void OnToastActivated(std::wstring const& arguments);
        void BringToFront() noexcept;

        HWND m_window{};
        std::shared_ptr<Services::IDownloadService> m_downloads;
        std::shared_ptr<Services::NavigationService> m_navigation;
        winrt::Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        Microsoft::WRL::ComPtr<ITaskbarList3> m_taskbar;
        Services::Downloads::TaskbarProgress m_shownProgress;
        bool m_taskbarShown{};
        std::set<std::wstring, std::less<>> m_unfinished;
        Services::DownloadChangedToken m_changedToken{};
        winrt::Windows::UI::Notifications::ToastNotifier m_notifier{ nullptr };
        // The notifications still on screen or in the action centre, kept so
        // their click is still heard; the oldest go first.
        std::deque<winrt::Windows::UI::Notifications::ToastNotification> m_shownToasts;
        bool m_attached{};
    };
}
