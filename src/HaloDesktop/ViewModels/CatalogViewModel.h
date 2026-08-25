#pragma once

#include "CatalogViewModel.g.h"
#include "Services/AppServices.h"

#include <memory>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace winrt::HaloDesktop::implementation
{
    struct CatalogViewModel : CatalogViewModelT<CatalogViewModel>
    {
        explicit CatalogViewModel(::HaloDesktop::Services::AppServices const& services);

        [[nodiscard]] winrt::hstring Title() const;
        [[nodiscard]] winrt::hstring SourceLabel() const;
        [[nodiscard]] winrt::hstring CountLabel() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable Items() const;
        [[nodiscard]] auto ItemsView() const { return m_items; }
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SourceVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ContentVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility EmptyVisibility() const noexcept;

        void Load(winrt::Windows::Foundation::IInspectable const& parameter);
        void OpenDetail(winrt::Windows::Foundation::IInspectable const& item);

        winrt::event_token PropertyChanged(
            Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        void RaiseState();

        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        winrt::hstring m_title{ L"Catalog" };
        winrt::hstring m_sourceLabel;
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary> m_items{ nullptr };
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
