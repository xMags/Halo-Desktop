#include "pch.h"
#include "Views/DetailEpisodeRow.xaml.h"
#if __has_include("DetailEpisodeRow.g.cpp")
#include "DetailEpisodeRow.g.cpp"
#endif

namespace winrt::HaloDesktop::implementation
{
    void DetailEpisodeRow::Episode(winrt::HaloDesktop::DetailEpisodeViewModel const& value)
    {
        if (m_episode == value) return;
        m_episode = value;
        // The row is handed its episode by its list's template, which can be
        // after its own bindings started; they follow the new one from here.
        if (Bindings) Bindings->Update();
    }

    void DetailEpisodeRow::OnClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        m_click(*this, args);
    }

    void DetailEpisodeRow::OnHoverStarted(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
    {
        SetHovered(true);
    }

    void DetailEpisodeRow::OnHoverEnded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
    {
        SetHovered(false);
    }

    void DetailEpisodeRow::SetHovered(bool hovered)
    {
        auto const opacity = hovered ? 1.0 : 0.0;
        if (auto const fill = HoverFill()) fill.Opacity(opacity);
        if (auto const circle = PlayCircle()) circle.Opacity(opacity);
    }
}
