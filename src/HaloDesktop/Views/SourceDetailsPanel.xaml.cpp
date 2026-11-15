#include "pch.h"
#include "Views/SourceDetailsPanel.xaml.h"
#if __has_include("SourceDetailsPanel.g.cpp")
#include "SourceDetailsPanel.g.cpp"
#endif

namespace winrt::HaloDesktop::implementation
{
    void SourceDetailsPanel::Details(winrt::HaloDesktop::SourceDetailsViewModel const& value)
    {
        if (m_details == value) return;
        m_details = value;
        // Handed its details by a template, possibly after its bindings started.
        if (Bindings) Bindings->Update();
    }

    void SourceDetailsPanel::OnSaveClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        m_saveClick(*this, args);
    }

    void SourceDetailsPanel::OnCopyClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        m_copyClick(*this, args);
    }
}
