#include "pch.h"
#include "Services/ThemeService.h"

#include "Services/DevicePreferencesStore.h"

#include <stdexcept>
#include <utility>

namespace
{
    [[nodiscard]] HaloDesktop::Services::ThemePreference FromStoredValue(std::int32_t value) noexcept
    {
        using HaloDesktop::Services::ThemePreference;

        switch (value)
        {
        case static_cast<std::int32_t>(ThemePreference::Light):
            return ThemePreference::Light;
        case static_cast<std::int32_t>(ThemePreference::Dark):
            return ThemePreference::Dark;
        default:
            return ThemePreference::System;
        }
    }
}

namespace HaloDesktop::Services
{
    ThemeService::ThemeService(std::shared_ptr<DevicePreferencesStore> preferences)
        : m_preferences(std::move(preferences))
    {
        if (!m_preferences)
        {
            throw std::invalid_argument{ "ThemeService requires a device preference store." };
        }
        m_preference = FromStoredValue(m_preferences->Theme());
    }

    ThemePreference ThemeService::Preference() const noexcept
    {
        return m_preference;
    }

    void ThemeService::SetPreference(ThemePreference preference)
    {
        if (m_preference == preference)
        {
            return;
        }

        m_preference = preference;
        m_preferences->Theme(static_cast<std::int32_t>(preference));
        Apply();
    }

    void ThemeService::Attach(winrt::Microsoft::UI::Xaml::FrameworkElement const& root)
    {
        m_root = root;
        Apply();
    }

    void ThemeService::Detach() noexcept
    {
        m_root = nullptr;
    }

    void ThemeService::Apply() const
    {
        if (!m_root)
        {
            return;
        }

        using winrt::Microsoft::UI::Xaml::ElementTheme;
        switch (m_preference)
        {
        case ThemePreference::Light:
            m_root.RequestedTheme(ElementTheme::Light);
            return;
        case ThemePreference::Dark:
            m_root.RequestedTheme(ElementTheme::Dark);
            return;
        case ThemePreference::System:
            // Default defers to the OS setting and keeps following it as it changes.
            m_root.RequestedTheme(ElementTheme::Default);
            return;
        }
    }
}
