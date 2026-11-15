#include "pch.h"
#include "Views/SourcesSheetFooter.xaml.h"
#if __has_include("SourcesSheetFooter.g.cpp")
#include "SourcesSheetFooter.g.cpp"
#endif

namespace winrt::HaloDesktop::implementation
{
    void SourcesSheetFooter::ViewModel(winrt::HaloDesktop::SourcesViewModel const& value)
    {
        if (m_viewModel == value) return;
        m_viewModel = value;
        if (Bindings) Bindings->Update();
    }

    void SourcesSheetFooter::OnToggleClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (m_viewModel) m_viewModel.ToggleInfo();
    }

    void SourcesSheetFooter::OnEditPlaybackClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (m_viewModel) m_viewModel.OpenSettings();
    }
}
