#pragma once

#include "DetailCreditsPanel.g.h"

#include "Views/DetailScrollRow.h"

#include <memory>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>

namespace winrt::HaloDesktop::implementation
{
    struct DetailCreditsPanel : DetailCreditsPanelT<DetailCreditsPanel>
    {
        DetailCreditsPanel() = default;
        void InitializeComponent();

        [[nodiscard]] winrt::HaloDesktop::DetailViewModel ViewModel() const { return m_viewModel; }
        // Set once by the page before the control loads.
        void ViewModel(winrt::HaloDesktop::DetailViewModel const& value);

        // Side by side above 1040px, else one column. Not projected.
        void ApplyLayout(bool sideBySide);

        void OnCreditClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnCastSizeChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&);
        void OnStripPointerWheelChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);

    private:
        void OnViewModelChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Data::PropertyChangedEventArgs const& args);
        void Arrange();
        void MatchAboutToCast();

        winrt::HaloDesktop::DetailViewModel m_viewModel{ nullptr };
        std::unique_ptr<::HaloDesktop::Views::DetailScrollRow> m_castRow;
        bool m_sideBySide{ true };
        bool m_matchHeights{};
        winrt::Microsoft::UI::Xaml::Data::INotifyPropertyChanged::PropertyChanged_revoker m_viewModelChanged;
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct DetailCreditsPanel : DetailCreditsPanelT<DetailCreditsPanel, implementation::DetailCreditsPanel>
    {
    };
}
