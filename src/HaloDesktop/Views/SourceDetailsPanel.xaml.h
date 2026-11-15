#pragma once

#include "SourceDetailsPanel.g.h"

namespace winrt::HaloDesktop::implementation
{
    // A source's expanded read-out. View behaviour only: Save and Copy are handed
    // to the sheet, which owns the download start, its dialogs and the clipboard.
    struct SourceDetailsPanel : SourceDetailsPanelT<SourceDetailsPanel>
    {
        SourceDetailsPanel() = default;

        [[nodiscard]] winrt::HaloDesktop::SourceDetailsViewModel Details() const { return m_details; }
        void Details(winrt::HaloDesktop::SourceDetailsViewModel const& value);

        winrt::event_token SaveClick(Microsoft::UI::Xaml::RoutedEventHandler const& handler) { return m_saveClick.add(handler); }
        void SaveClick(winrt::event_token const& token) noexcept { m_saveClick.remove(token); }
        winrt::event_token CopyClick(Microsoft::UI::Xaml::RoutedEventHandler const& handler) { return m_copyClick.add(handler); }
        void CopyClick(winrt::event_token const& token) noexcept { m_copyClick.remove(token); }

        void OnSaveClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnCopyClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        winrt::HaloDesktop::SourceDetailsViewModel m_details{ nullptr };
        winrt::event<Microsoft::UI::Xaml::RoutedEventHandler> m_saveClick;
        winrt::event<Microsoft::UI::Xaml::RoutedEventHandler> m_copyClick;
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct SourceDetailsPanel : SourceDetailsPanelT<SourceDetailsPanel, implementation::SourceDetailsPanel>
    {
    };
}
