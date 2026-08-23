#include "pch.h"
#include "ViewModels/SearchViewModel.h"
#if __has_include("RecentSearchViewModel.g.cpp")
#include "RecentSearchViewModel.g.cpp"
#endif
#if __has_include("SearchViewModel.g.cpp")
#include "SearchViewModel.g.cpp"
#endif
#include "Models/Models.h"
#include "Services/NavigationService.h"
#include "Services/SampleData.h"
#include "ViewModels/ObservableHelper.h"
#include <algorithm>
#include <cwctype>
#include <string>
#include <utility>
#include <vector>

namespace winrt::HaloDesktop::implementation
{
    RecentSearchViewModel::RecentSearchViewModel(winrt::hstring term, winrt::hstring age) : m_term(std::move(term)), m_age(std::move(age)) {}
    winrt::hstring RecentSearchViewModel::Term() const { return m_term; }
    winrt::hstring RecentSearchViewModel::Age() const { return m_age; }

    SearchViewModel::SearchViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_catalog(services.Catalog), m_navigation(services.Navigation),
          m_results(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_recentItems(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>())
    {
        auto const terms = m_catalog->RecentTerms();
        auto const ages = ::HaloDesktop::Services::SampleData::RecentSearchAges();
        for (std::uint32_t index = 0; index < terms.Size(); ++index)
        {
            m_recentItems.Append(winrt::make<RecentSearchViewModel>(terms.GetAt(index), ages.GetAt(index)));
        }
    }
    winrt::hstring SearchViewModel::Query() const { return m_query; }
    void SearchViewModel::Query(winrt::hstring const& value)
    {
        if (m_query == value) return;
        m_query = value;
        Rebuild();
    }
    winrt::hstring SearchViewModel::Crumb() const
    {
        std::wstring crumb(m_query);
        std::transform(crumb.begin(), crumb.end(), crumb.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(std::towupper(character));
        });
        return winrt::hstring(crumb);
    }
    winrt::Windows::Foundation::IInspectable SearchViewModel::Results() const { return m_results; }
    winrt::Windows::Foundation::IInspectable SearchViewModel::RecentItems() const { return m_recentItems; }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> SearchViewModel::ResultsView() const { return m_results; }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> SearchViewModel::RecentItemsView() const { return m_recentItems; }
    std::int32_t SearchViewModel::AllCount() const noexcept { return m_movieCount + m_seriesCount; }
    std::int32_t SearchViewModel::MovieCount() const noexcept { return m_movieCount; }
    std::int32_t SearchViewModel::SeriesCount() const noexcept { return m_seriesCount; }
    winrt::hstring SearchViewModel::TopMatchTitle() const { return m_topMatch ? m_topMatch.Title() : L""; }
    winrt::hstring SearchViewModel::TopMatchMeta() const
    {
        if (!m_topMatch) return L"";
        if (m_topMatch.Id() == L"northwind-divide") return ::HaloDesktop::Services::SampleData::Copy::SearchTopMatchMeta;
        return winrt::hstring(std::wstring(m_topMatch.KindLabel()) + L" · " + std::wstring(m_topMatch.Meta()));
    }
    winrt::hstring SearchViewModel::TopMatchSynopsis() const
    {
        return m_topMatch && m_topMatch.Id() == L"northwind-divide"
            ? winrt::hstring{ ::HaloDesktop::Services::SampleData::Copy::HeroSynopsis }
            : winrt::hstring{};
    }
    Microsoft::UI::Xaml::Visibility SearchViewModel::TopMatchVisibility() const noexcept
    {
        return m_topMatch && m_filterIndex != 3
            ? Microsoft::UI::Xaml::Visibility::Visible
            : Microsoft::UI::Xaml::Visibility::Collapsed;
    }
    Microsoft::UI::Xaml::Visibility SearchViewModel::ResultsVisibility() const noexcept { return m_query.empty() ? Microsoft::UI::Xaml::Visibility::Collapsed : Microsoft::UI::Xaml::Visibility::Visible; }
    Microsoft::UI::Xaml::Visibility SearchViewModel::RecentVisibility() const noexcept { return Microsoft::UI::Xaml::Visibility::Visible; }
    void SearchViewModel::SetFilter(std::int32_t index) { if (index < 0 || index > 3 || index == m_filterIndex) return; m_filterIndex = index; Rebuild(); }
    void SearchViewModel::Submit(winrt::hstring const& query)
    {
        Query(query);
        if (query.empty()) return;
        for (std::uint32_t index = 0; index < m_recentItems.Size(); ++index)
        {
            if (m_recentItems.GetAt(index).as<winrt::HaloDesktop::RecentSearchViewModel>().Term() == query)
            {
                m_recentItems.RemoveAt(index);
                break;
            }
        }
        m_recentItems.InsertAt(0, winrt::make<RecentSearchViewModel>(query, L"NOW"));
    }
    void SearchViewModel::Clear() { Query(L""); }
    void SearchViewModel::OpenDetail() { m_navigation->GoTo(::HaloDesktop::Services::Page::Detail); }
    winrt::event_token SearchViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler) { return m_propertyChanged.add(handler); }
    void SearchViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }
    void SearchViewModel::Rebuild()
    {
        m_results.Clear();
        m_movieCount = 0;
        m_seriesCount = 0;
        m_topMatch = nullptr;
        if (m_query.empty())
        {
            RaiseState();
            return;
        }
        auto const source = m_catalog->Search(m_query);
        for (auto const& group : source)
        {
            std::vector<winrt::HaloDesktop::MediaSummary> filtered;
            for (auto const& item : group.Items())
            {
                if (item.Kind() == winrt::HaloDesktop::MediaKind::Movie) ++m_movieCount; else ++m_seriesCount;
                if (!m_topMatch) m_topMatch = item;
                auto const include = m_filterIndex == 0 || (m_filterIndex == 1 && item.Kind() == winrt::HaloDesktop::MediaKind::Movie) || (m_filterIndex == 2 && item.Kind() == winrt::HaloDesktop::MediaKind::Series);
                if (include && m_filterIndex != 3) filtered.push_back(item);
            }
            if (!filtered.empty())
            {
                auto items = winrt::single_threaded_vector<winrt::HaloDesktop::MediaSummary>(std::move(filtered)).GetView();
                m_results.Append(winrt::make<winrt::HaloDesktop::implementation::SearchGroup>(group.Title(), group.SourceLabel(), items));
            }
        }
        RaiseState();
    }
    void SearchViewModel::RaiseState()
    {
        for (auto const property : { L"Query", L"Crumb", L"Results", L"RecentItems", L"AllCount", L"MovieCount", L"SeriesCount", L"TopMatchTitle", L"TopMatchMeta", L"TopMatchSynopsis", L"TopMatchVisibility", L"ResultsVisibility", L"RecentVisibility" })
            ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, property);
    }
}
