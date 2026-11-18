#include "pch.h"
#include "Views/SignInSpinner.xaml.h"
#if __has_include("SignInSpinner.g.cpp")
#include "SignInSpinner.g.cpp"
#endif

#include "Views/SignInMotion.h"

#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>

namespace winrt::HaloDesktop::implementation
{
    bool SignInSpinner::IsActive() const noexcept
    {
        return m_active;
    }

    void SignInSpinner::IsActive(bool value)
    {
        if (m_active == value)
        {
            return;
        }
        m_active = value;
        Update();
    }

    void SignInSpinner::OnLoaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_loaded = true;
        Update();
    }

    void SignInSpinner::OnUnloaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_loaded = false;
        Update();
    }

    void SignInSpinner::Update()
    {
        auto const run = m_active && m_loaded && ::HaloDesktop::Views::SignInAnimationsEnabled();
        if (run == m_running)
        {
            return;
        }
        try
        {
            auto const storyboard = Resources()
                .Lookup(winrt::box_value(winrt::hstring{ L"SpinStoryboard" }))
                .as<Microsoft::UI::Xaml::Media::Animation::Storyboard>();
            if (run)
            {
                storyboard.Begin();
            }
            else
            {
                storyboard.Stop();
            }
            m_running = run;
        }
        catch (winrt::hresult_error const&)
        {
            // Decorative: a spinner that cannot turn stands still.
            m_running = false;
        }
    }
}
