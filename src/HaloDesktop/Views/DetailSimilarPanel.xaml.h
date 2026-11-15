#pragma once

#include "DetailSimilarPanel.g.h"

#include "Views/DetailScrollRow.h"

#include <memory>
#include <winrt/Microsoft.UI.Xaml.Data.h>

namespace winrt::HaloDesktop::implementation
{
    struct DetailSimilarPanel : DetailSimilarPanelT<DetailSimilarPanel>
    {
        DetailSimilarPanel() = default;
        void InitializeComponent();

        [[nodiscard]] winrt::HaloDesktop::DetailViewModel ViewModel() const { return m_viewModel; }
        // Set once by the page before the control loads.
        void ViewModel(winrt::HaloDesktop::DetailViewModel const& value);

        void OnSimilarClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        void OnViewModelChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Data::PropertyChangedEventArgs const& args);

        winrt::HaloDesktop::DetailViewModel m_viewModel{ nullptr };
        std::unique_ptr<::HaloDesktop::Views::DetailScrollRow> m_row;
        winrt::Microsoft::UI::Xaml::Data::INotifyPropertyChanged::PropertyChanged_revoker m_viewModelChanged;
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct DetailSimilarPanel : DetailSimilarPanelT<DetailSimilarPanel, implementation::DetailSimilarPanel>
    {
    };
}
