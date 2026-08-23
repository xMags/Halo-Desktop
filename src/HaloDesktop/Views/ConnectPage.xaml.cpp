#include "pch.h"
#include "Views/ConnectPage.xaml.h"
#if __has_include("ConnectPage.g.cpp")
#include "ConnectPage.g.cpp"
#endif

#include "App.xaml.h"
#include "ViewModels/ConnectViewModel.h"

namespace winrt::HaloDesktop::implementation
{
    ConnectPage::ConnectPage()
        : m_viewModel(winrt::make<ConnectViewModel>(App::Services()))
    {
    }

    winrt::HaloDesktop::ConnectViewModel ConnectPage::ViewModel() const
    {
        return m_viewModel;
    }

    void ConnectPage::OnTestServerClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        static_cast<void>(m_viewModel.TestServerAsync());
    }

    void ConnectPage::OnContinueClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        m_viewModel.Continue();
    }
}
