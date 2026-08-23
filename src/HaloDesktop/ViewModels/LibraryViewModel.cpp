#include "pch.h"
#include "ViewModels/LibraryViewModel.h"
#if __has_include("LibraryViewModel.g.cpp")
#include "LibraryViewModel.g.cpp"
#endif
#include "Services/NavigationService.h"
#include "ViewModels/ObservableHelper.h"
#include <algorithm>
#include <string>
#include <vector>

namespace
{
    int ReleaseYear(winrt::HaloDesktop::MediaSummary const& item)
    {
        auto const meta = std::wstring(item.Meta());
        return meta.size() >= 4 ? std::stoi(meta.substr(0, 4)) : 0;
    }
}

namespace winrt::HaloDesktop::implementation
{
    LibraryViewModel::LibraryViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_navigation(services.Navigation), m_sourceItems(services.Catalog->LibraryItems()),
          m_items(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>())
    {
        Rebuild();
    }
    winrt::Windows::Foundation::IInspectable LibraryViewModel::Items() const { return m_items; }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> LibraryViewModel::ItemsView() const { return m_items; }
    std::int32_t LibraryViewModel::FilterIndex() const noexcept { return m_filterIndex; }
    std::int32_t LibraryViewModel::SortIndex() const noexcept { return m_sortIndex; }
    void LibraryViewModel::SetFilter(std::int32_t index)
    {
        if (index < 0 || index > 2 || index == m_filterIndex) return;
        m_filterIndex = index; Rebuild();
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, L"FilterIndex");
    }
    void LibraryViewModel::SetSort(std::int32_t index)
    {
        if (index < 0 || index > 3 || index == m_sortIndex) return;
        m_sortIndex = index; Rebuild();
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, L"SortIndex");
    }
    void LibraryViewModel::OpenDetail() { m_navigation->GoTo(::HaloDesktop::Services::Page::Detail); }
    winrt::event_token LibraryViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler) { return m_propertyChanged.add(handler); }
    void LibraryViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }
    void LibraryViewModel::Rebuild()
    {
        std::vector<winrt::HaloDesktop::MediaSummary> items;
        for (auto const& item : m_sourceItems)
        {
            auto const include = m_filterIndex == 0
                || (m_filterIndex == 1 && item.Kind() == winrt::HaloDesktop::MediaKind::Movie)
                || (m_filterIndex == 2 && item.Kind() == winrt::HaloDesktop::MediaKind::Series);
            if (include) items.push_back(item);
        }
        if (m_sortIndex == 1)
        {
            std::sort(items.begin(), items.end(), [](auto const& left, auto const& right) { return left.Title() < right.Title(); });
        }
        else if (m_sortIndex == 2)
        {
            std::sort(items.begin(), items.end(), [](auto const& left, auto const& right)
            {
                auto const leftYear = ReleaseYear(left);
                auto const rightYear = ReleaseYear(right);
                return leftYear == rightYear ? left.Title() < right.Title() : leftYear > rightYear;
            });
        }
        else if (m_sortIndex == 3 && items.size() > 5)
        {
            std::rotate(items.begin(), items.begin() + 5, items.end());
        }
        m_items.Clear();
        for (auto const& item : items) m_items.Append(item);
    }
}
