#include "pch.h"
#include "Views/DetailSimilarPanel.xaml.h"
#if __has_include("DetailSimilarPanel.g.cpp")
#include "DetailSimilarPanel.g.cpp"
#endif

#include "Controls/SidewaysWheelGuard.h"

namespace winrt::HaloDesktop::implementation
{
    void DetailSimilarPanel::InitializeComponent()
    {
        DetailSimilarPanelT::InitializeComponent();
        m_row = std::make_unique<::HaloDesktop::Views::DetailScrollRow>(::HaloDesktop::Views::DetailScrollRowParts{
            SimilarStrip(), SimilarItems(), SimilarPager(), SimilarPrevious(), SimilarNext() });
    }

    void DetailSimilarPanel::ViewModel(winrt::HaloDesktop::DetailViewModel const& value)
    {
        m_viewModel = value;
        m_viewModelChanged.revoke();
        if (m_viewModel)
        {
            m_viewModelChanged = m_viewModel.PropertyChanged(winrt::auto_revoke, { get_weak(), &DetailSimilarPanel::OnViewModelChanged });
        }
    }

    void DetailSimilarPanel::OnSimilarClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (!m_viewModel) return;
        m_viewModel.OpenSimilar(sender.as<winrt::HaloDesktop::DetailSimilarCard>().Media());
    }

    void DetailSimilarPanel::OnStripPointerWheelChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        ::HaloDesktop::Controls::GuardSidewaysWheel(args);
    }

    void DetailSimilarPanel::OnViewModelChanged(
        winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::Data::PropertyChangedEventArgs const& args)
    {
        if (args.PropertyName() == L"SimilarVisibility" && m_row) m_row->Refresh();
    }
}
