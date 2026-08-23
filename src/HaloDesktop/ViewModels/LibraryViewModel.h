#pragma once

#include "LibraryViewModel.g.h"
#include "Services/AppServices.h"
#include "Services/ServiceInterfaces.h"
#include <cstdint>
#include <memory>
#include <winrt/Microsoft.UI.Xaml.Data.h>

namespace winrt::HaloDesktop::implementation
{
    struct LibraryViewModel : LibraryViewModelT<LibraryViewModel>
    {
        explicit LibraryViewModel(::HaloDesktop::Services::AppServices const& services);
        [[nodiscard]] winrt::Windows::Foundation::IInspectable Items() const;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> ItemsView() const;
        [[nodiscard]] std::int32_t FilterIndex() const noexcept;
        [[nodiscard]] std::int32_t SortIndex() const noexcept;
        void SetFilter(std::int32_t index);
        void SetSort(std::int32_t index);
        void OpenDetail();
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        void Rebuild();
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary> m_sourceItems{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_items{ nullptr };
        std::int32_t m_filterIndex{};
        std::int32_t m_sortIndex{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
