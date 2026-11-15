#pragma once

#include "SourceSaveButton.g.h"

namespace winrt::HaloDesktop::implementation
{
    // Save for offline for one source. View behaviour only: the size and the click
    // handed to its host, which owns the start and its dialogs.
    struct SourceSaveButton : SourceSaveButtonT<SourceSaveButton>
    {
        SourceSaveButton() = default;

        [[nodiscard]] winrt::HaloDesktop::SourceSaveViewModel Save() const { return m_save; }
        void Save(winrt::HaloDesktop::SourceSaveViewModel const& value);
        [[nodiscard]] bool Prominent() const noexcept { return m_prominent; }
        void Prominent(bool value);

        winrt::event_token Click(Microsoft::UI::Xaml::RoutedEventHandler const& handler) { return m_click.add(handler); }
        void Click(winrt::event_token const& token) noexcept { m_click.remove(token); }

        void OnClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        winrt::HaloDesktop::SourceSaveViewModel m_save{ nullptr };
        bool m_prominent{};
        winrt::event<Microsoft::UI::Xaml::RoutedEventHandler> m_click;
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct SourceSaveButton : SourceSaveButtonT<SourceSaveButton, implementation::SourceSaveButton>
    {
    };
}
