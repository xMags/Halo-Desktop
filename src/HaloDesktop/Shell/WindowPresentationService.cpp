#include "pch.h"
#include "Shell/WindowPresentationService.h"

#include <stdexcept>

namespace HaloDesktop::Shell
{
    void WindowPresentationService::Attach(std::uintptr_t windowHandle,
                                           winrt::Microsoft::UI::Windowing::AppWindow const& appWindow,
                                           winrt::Microsoft::UI::Xaml::Controls::RowDefinition const& titleBarRow)
    {
        if (windowHandle == 0 || !appWindow || !titleBarRow)
        {
            throw std::invalid_argument("Window presentation requires a window handle, AppWindow, and title row");
        }
        m_windowHandle = windowHandle;
        m_appWindow = appWindow;
        m_titleBarRow = titleBarRow;
    }
    void WindowPresentationService::Detach() noexcept
    {
        m_windowHandle = 0;
        m_appWindow = nullptr;
        m_titleBarRow = nullptr;
        m_fullscreen = false;
        m_wasMaximized = false;
    }
    void WindowPresentationService::SetFullscreen(bool fullscreen)
    {
        if (!m_appWindow || !m_titleBarRow)
        {
            throw std::logic_error("Window presentation is not attached");
        }
        if (m_fullscreen == fullscreen)
        {
            return;
        }
        if (fullscreen)
        {
            auto const presenter =
                m_appWindow.Presenter().try_as<winrt::Microsoft::UI::Windowing::OverlappedPresenter>();
            m_wasMaximized =
                presenter && presenter.State() == winrt::Microsoft::UI::Windowing::OverlappedPresenterState::Maximized;
            m_appWindow.SetPresenter(winrt::Microsoft::UI::Windowing::AppWindowPresenterKind::FullScreen);
            m_titleBarRow.Height({ 0.0, winrt::Microsoft::UI::Xaml::GridUnitType::Pixel });
            m_fullscreen = true;
            return;
        }
        m_appWindow.SetPresenter(winrt::Microsoft::UI::Windowing::AppWindowPresenterKind::Overlapped);
        m_titleBarRow.Height({ 32.0, winrt::Microsoft::UI::Xaml::GridUnitType::Pixel });
        if (m_wasMaximized)
        {
            m_appWindow.Presenter().as<winrt::Microsoft::UI::Windowing::OverlappedPresenter>().Maximize();
        }
        m_fullscreen = false;
        m_wasMaximized = false;
    }
    bool WindowPresentationService::IsFullscreen() const noexcept
    {
        return m_fullscreen;
    }

    std::uintptr_t WindowPresentationService::WindowHandle() const
    {
        if (m_windowHandle == 0)
        {
            throw std::logic_error("Window presentation is not attached");
        }
        return m_windowHandle;
    }
} // namespace HaloDesktop::Shell
