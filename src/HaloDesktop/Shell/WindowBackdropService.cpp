#include "pch.h"
#include "Shell/WindowBackdropService.h"

#include "Controls/ThemeColorLookup.h"
#include "Services/DevicePreferencesStore.h"

#include <stdexcept>
#include <utility>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Windows.UI.h>

namespace HaloDesktop::Shell
{
    namespace SystemBackdrops = winrt::Microsoft::UI::Composition::SystemBackdrops;

    WindowBackdropService::WindowBackdropService(std::shared_ptr<Services::DevicePreferencesStore> preferences)
        : m_preferences(std::move(preferences))
    {
        if (!m_preferences)
        {
            throw std::invalid_argument{ "The window backdrop needs the device preferences." };
        }
        m_enabled = m_preferences->TranslucentWindow();
    }

    WindowBackdropService::~WindowBackdropService()
    {
        Detach();
    }

    bool WindowBackdropService::Enabled() const noexcept
    {
        return m_enabled;
    }

    void WindowBackdropService::SetEnabled(bool enabled)
    {
        if (m_enabled == enabled)
        {
            return;
        }
        m_enabled = enabled;
        try
        {
            m_preferences->TranslucentWindow(enabled);
        }
        catch (...)
        {
            OutputDebugStringW(L"Halo could not store the translucent window setting; it applies until Halo closes.\n");
        }
        Apply();
    }

    bool WindowBackdropService::IsSupported() noexcept
    {
        try
        {
            return SystemBackdrops::DesktopAcrylicController::IsSupported();
        }
        catch (...)
        {
            return false;
        }
    }

    void WindowBackdropService::Attach(
        winrt::Microsoft::UI::Xaml::Window const& window,
        winrt::Microsoft::UI::Xaml::FrameworkElement const& themeScope,
        winrt::Microsoft::UI::Xaml::UIElement const& ground)
    {
        Detach();
        m_window = window;
        m_themeScope = themeScope;
        m_ground = ground;
        Apply();
    }

    void WindowBackdropService::Detach() noexcept
    {
        ReleaseController();
        m_window = nullptr;
        m_themeScope = nullptr;
        m_ground = nullptr;
    }

    void WindowBackdropService::SetWindowActive(bool active)
    {
        if (m_windowActive == active)
        {
            return;
        }
        m_windowActive = active;
        Apply();
    }

    void WindowBackdropService::SetPlayerShowing(bool showing)
    {
        if (m_playerShowing == showing)
        {
            return;
        }
        m_playerShowing = showing;
        Apply();
    }

    void WindowBackdropService::RefreshTheme()
    {
        if (!m_controller)
        {
            return;
        }
        try
        {
            ApplyTint();
        }
        catch (...)
        {
            // Runs from a theme change event: a backdrop that cannot be re-tinted
            // gives way to the opaque ground rather than ending the app.
            OutputDebugStringW(L"Halo could not re-tint the translucent window backdrop; the window stays opaque.\n");
            m_failed = true;
            ShowGround(true);
            ReleaseController();
        }
    }

    void WindowBackdropService::Apply()
    {
        if (!m_window)
        {
            return;
        }
        auto const plan = ResolveWindowBackdrop({
            .PreferenceOn = m_enabled,
            .AcrylicSupported = !m_failed && IsSupported(),
            .WindowActive = m_windowActive,
            .PlayerShowing = m_playerShowing,
        });

        if (!plan.UseAcrylic)
        {
            // The ground goes back first, so no frame shows the bare window.
            ShowGround(true);
            ReleaseController();
            return;
        }

        try
        {
            if (!m_controller)
            {
                m_configuration = SystemBackdrops::SystemBackdropConfiguration{};
                m_controller = SystemBackdrops::DesktopAcrylicController{};
                ApplyTint();
                m_configuration.IsInputActive(plan.BlurLive);
                m_controller.SetSystemBackdropConfiguration(m_configuration);
                if (!m_controller.AddSystemBackdropTarget(
                        m_window.as<winrt::Microsoft::UI::Composition::ICompositionSupportsSystemBackdrop>()))
                {
                    throw winrt::hresult_error{ E_FAIL };
                }
            }
            else
            {
                m_configuration.IsInputActive(plan.BlurLive);
            }
            // Only once the backdrop is there, so no frame shows the bare window.
            ShowGround(false);
        }
        catch (...)
        {
            OutputDebugStringW(L"Halo could not attach the translucent window backdrop; the window stays opaque.\n");
            m_failed = true;
            ShowGround(true);
            ReleaseController();
        }
    }

    void WindowBackdropService::ApplyTint()
    {
        auto const light = m_themeScope
            && m_themeScope.ActualTheme() == winrt::Microsoft::UI::Xaml::ElementTheme::Light;
        auto const theme = light ? winrt::Microsoft::UI::Xaml::ElementTheme::Light : winrt::Microsoft::UI::Xaml::ElementTheme::Dark;
        auto const ground = ::HaloDesktop::Controls::ThemeColor(theme, L"HaloMicaColor");
        if (!ground)
        {
            throw std::runtime_error{ "The palette has no window ground colour." };
        }
        auto const strength = AcrylicStrengthFor(light);
        m_configuration.Theme(light ? SystemBackdrops::SystemBackdropTheme::Light : SystemBackdrops::SystemBackdropTheme::Dark);
        m_controller.TintColor(*ground);
        m_controller.FallbackColor(*ground);
        m_controller.TintOpacity(strength.TintOpacity);
        m_controller.LuminosityOpacity(strength.LuminosityOpacity);
    }

    void WindowBackdropService::ShowGround(bool visible) noexcept
    {
        if (!m_ground)
        {
            return;
        }
        try
        {
            m_ground.Visibility(visible
                ? winrt::Microsoft::UI::Xaml::Visibility::Visible
                : winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        }
        catch (...)
        {
        }
    }

    void WindowBackdropService::ReleaseController() noexcept
    {
        if (!m_controller)
        {
            return;
        }
        try
        {
            m_controller.RemoveAllSystemBackdropTargets();
            m_controller.Close();
        }
        catch (...)
        {
        }
        m_controller = nullptr;
        m_configuration = nullptr;
    }
}
