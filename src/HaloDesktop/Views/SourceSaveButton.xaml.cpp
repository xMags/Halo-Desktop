#include "pch.h"
#include "Views/SourceSaveButton.xaml.h"
#if __has_include("SourceSaveButton.g.cpp")
#include "SourceSaveButton.g.cpp"
#endif

#include <winrt/Windows.UI.Text.h>

namespace winrt::HaloDesktop::implementation
{
    void SourceSaveButton::Save(winrt::HaloDesktop::SourceSaveViewModel const& value)
    {
        if (m_save == value) return;
        m_save = value;
        // Handed its state by a template, possibly after its bindings started.
        if (Bindings) Bindings->Update();
    }

    void SourceSaveButton::Prominent(bool value)
    {
        m_prominent = value;
        auto const button = SaveButtonControl();
        if (!button) return;
        // `.sx-h44` beside the pick's Play, `.sx-h34` in a row's details.
        button.Height(value ? 44.0 : 34.0);
        button.FontSize(value ? 15.0 : 14.0);
        button.FontWeight(value ? winrt::Windows::UI::Text::FontWeight{ 600 } : winrt::Windows::UI::Text::FontWeight{ 400 });
        button.CornerRadius(Microsoft::UI::Xaml::CornerRadius{ value ? 6.0 : 4.0, value ? 6.0 : 4.0, value ? 6.0 : 4.0, value ? 6.0 : 4.0 });
    }

    void SourceSaveButton::OnClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        // A disabled button raises nothing, but a state that changed between the
        // press and the click must still not start anything.
        if (!m_save || !m_save.IsReady()) return;
        m_click(*this, args);
    }
}
