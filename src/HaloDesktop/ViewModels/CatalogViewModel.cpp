#include "pch.h"
#include "ViewModels/CatalogViewModel.h"
#if __has_include("CatalogViewModel.g.cpp")
#include "CatalogViewModel.g.cpp"
#endif

#include "Models/Models.h"
#include "Services/NavigationService.h"
#include "Services/ServiceInterfaces.h"
#include "ViewModels/ObservableHelper.h"

#include <vector>

namespace
{
    auto EmptyItems()
    {
        return winrt::single_threaded_vector<winrt::HaloDesktop::MediaSummary>().GetView();
    }

    winrt::Microsoft::UI::Xaml::Visibility VisibleWhen(bool condition) noexcept
    {
        return condition ? winrt::Microsoft::UI::Xaml::Visibility::Visible : winrt::Microsoft::UI::Xaml::Visibility::Collapsed;
    }
}

namespace winrt::HaloDesktop::implementation
{
    CatalogViewModel::CatalogViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_navigation(services.Navigation),
          m_catalog(services.Catalog),
          m_snapshotItems(EmptyItems()),
          m_items(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>())
    {
    }

    winrt::hstring CatalogViewModel::Title() const
    {
        return m_title;
    }

    winrt::hstring CatalogViewModel::SourceLabel() const
    {
        return m_sourceLabel;
    }

    winrt::hstring CatalogViewModel::CountLabel() const
    {
        auto const count = m_items ? m_items.Size() : 0;
        return winrt::to_hstring(count) + (count == 1 ? L" ITEM" : L" ITEMS");
    }

    winrt::Windows::Foundation::IInspectable CatalogViewModel::Items() const
    {
        return m_items;
    }

    Microsoft::UI::Xaml::Visibility CatalogViewModel::SourceVisibility() const noexcept
    {
        return VisibleWhen(!m_sourceLabel.empty());
    }

    Microsoft::UI::Xaml::Visibility CatalogViewModel::ContentVisibility() const noexcept
    {
        return VisibleWhen(m_items && m_items.Size() > 0);
    }

    Microsoft::UI::Xaml::Visibility CatalogViewModel::EmptyVisibility() const noexcept
    {
        return ContentVisibility() == Microsoft::UI::Xaml::Visibility::Visible
            ? Microsoft::UI::Xaml::Visibility::Collapsed
            : Microsoft::UI::Xaml::Visibility::Visible;
    }

    Microsoft::UI::Xaml::Visibility CatalogViewModel::MoreVisibility() const noexcept
    {
        return VisibleWhen(m_pager.has_value());
    }

    bool CatalogViewModel::LoadingMore() const noexcept { return m_loadingMore; }

    Microsoft::UI::Xaml::Visibility CatalogViewModel::LoadingMoreVisibility() const noexcept
    {
        return VisibleWhen(m_loadingMore);
    }

    Microsoft::UI::Xaml::Visibility CatalogViewModel::MoreFailedVisibility() const noexcept
    {
        return VisibleWhen(m_moreFailed && !m_loadingMore);
    }

    void CatalogViewModel::Load(winrt::Windows::Foundation::IInspectable const& parameter)
    {
        ++m_pageGeneration;
        m_paging.reset();
        m_pager.reset();
        m_loadingMore = false;
        m_moreFailed = false;
        m_title = L"Catalog";
        m_sourceLabel = {};
        m_snapshotItems = EmptyItems();
        m_items.Clear();

        if (auto const shelf = parameter.try_as<winrt::HaloDesktop::Shelf>())
        {
            if (!shelf.Title().empty())
            {
                m_title = shelf.Title();
            }
            m_sourceLabel = shelf.SourceLabel();
            if (auto const items = shelf.Items())
            {
                m_snapshotItems = items;
                std::vector<winrt::Windows::Foundation::IInspectable> shown;
                shown.reserve(m_snapshotItems.Size());
                for (auto const& item : m_snapshotItems)
                {
                    if (item)
                    {
                        shown.push_back(item);
                    }
                }
                m_items.ReplaceAll(shown);
            }
            // A catalog that pages keeps going past the shelf's titles.
            if (auto paging = winrt::get_self<winrt::HaloDesktop::implementation::Shelf>(shelf)->Paging())
            {
                m_pager.emplace(paging->FirstPage, paging->Filter);
                m_paging = std::move(paging);
            }
        }

        RaiseState();
        RaiseMore();
        // The second page loads with the grid; each after it once the end nears.
        RequestMore();
    }

    void CatalogViewModel::RequestMore()
    {
        if (!m_pager || !m_paging || !m_catalog || !m_pager->HasMore() || m_loadingMore || m_moreFailed)
        {
            return;
        }
        static_cast<void>(LoadMoreAsync());
    }

    void CatalogViewModel::RetryMore()
    {
        if (!m_moreFailed)
        {
            return;
        }
        m_moreFailed = false;
        RaiseMore();
        RequestMore();
    }

    winrt::Windows::Foundation::IAsyncAction CatalogViewModel::LoadMoreAsync()
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        auto const generation = m_pageGeneration;
        auto const paging = m_paging;
        auto const skip = m_pager->NextSkip();
        m_loadingMore = true;
        RaiseMore();

        std::vector<winrt::HaloDesktop::MediaSummary> page;
        bool failed{};
        try
        {
            page = co_await m_catalog->FetchCatalogPageAsync(paging, skip);
        }
        catch (...)
        {
            failed = true;
        }
        co_await uiContext;
        // A page from an earlier grid is never appended to this one.
        if (generation != m_pageGeneration || !m_pager)
        {
            co_return;
        }
        m_loadingMore = false;
        if (failed)
        {
            m_moreFailed = true;
            RaiseMore();
            co_return;
        }

        std::vector<::HaloDesktop::Services::PagedTitle> titles;
        titles.reserve(page.size());
        for (auto const& item : page)
        {
            titles.push_back({ std::wstring{ item.Type() }, std::wstring{ item.Id() } });
        }
        // De-duplicated and filtered; a page of nothing new ends the paging.
        for (auto const index : m_pager->Accept(titles))
        {
            m_items.Append(page[index]);
        }
        RaiseState();
        RaiseMore();
    }

    // The catalog is only ever reached from another shell page, so there is
    // always something behind it; the guard is for a cold navigation in testing.
    void CatalogViewModel::GoBack()
    {
        if (m_navigation->CanGoBack()) m_navigation->GoBack();
        else m_navigation->GoTo(::HaloDesktop::Services::Page::Home);
    }

    void CatalogViewModel::OpenDetail(winrt::Windows::Foundation::IInspectable const& item)
    {
        auto const media = item.try_as<winrt::HaloDesktop::MediaSummary>();
        if (!media || media.Type().empty() || media.Id().empty())
        {
            return;
        }

        m_navigation->GoTo(
            ::HaloDesktop::Services::Page::Detail,
            winrt::make<winrt::HaloDesktop::implementation::DetailNavParams>(
                media.Type(),
                media.Id(),
                media.Title(),
                media.Poster()));
    }

    winrt::event_token CatalogViewModel::PropertyChanged(
        Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }

    void CatalogViewModel::PropertyChanged(winrt::event_token const& token) noexcept
    {
        m_propertyChanged.remove(token);
    }

    void CatalogViewModel::RaiseState()
    {
        for (auto const property :
             { L"Title", L"SourceLabel", L"CountLabel", L"Items", L"SourceVisibility", L"ContentVisibility", L"EmptyVisibility" })
        {
            ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, property);
        }
    }

    void CatalogViewModel::RaiseMore()
    {
        for (auto const property : { L"MoreVisibility", L"LoadingMore", L"LoadingMoreVisibility", L"MoreFailedVisibility" })
        {
            ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, property);
        }
    }
}
