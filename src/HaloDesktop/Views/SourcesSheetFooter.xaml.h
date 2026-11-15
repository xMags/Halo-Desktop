#pragma once

#include "SourcesSheetFooter.g.h"

namespace winrt::HaloDesktop::implementation
{
    // The sources sheet's footer. View behaviour only: its two buttons forward to
    // the sheet's view model.
    struct SourcesSheetFooter : SourcesSheetFooterT<SourcesSheetFooter>
    {
        SourcesSheetFooter() = default;

        [[nodiscard]] winrt::HaloDesktop::SourcesViewModel ViewModel() const { return m_viewModel; }
        void ViewModel(winrt::HaloDesktop::SourcesViewModel const& value);

        void OnToggleClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnEditPlaybackClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        winrt::HaloDesktop::SourcesViewModel m_viewModel{ nullptr };
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct SourcesSheetFooter : SourcesSheetFooterT<SourcesSheetFooter, implementation::SourcesSheetFooter>
    {
    };
}
