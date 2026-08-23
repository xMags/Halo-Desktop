#include "pch.h"
#include "Services/MockServices.h"

#include "Models/Models.h"
#include "Services/SampleData.h"

#include <algorithm>
#include <cwctype>
#include <string>
#include <utility>

namespace
{
    std::wstring Lower(std::wstring_view value)
    {
        std::wstring lowered(value);
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(std::towlower(character));
        });
        return lowered;
    }
}

namespace HaloDesktop::Services
{
    MockCatalogService::MockCatalogService()
        : m_hero(SampleData::Hero()),
          m_continueWatching(SampleData::ContinueWatching()),
          m_shelves(SampleData::Shelves()),
          m_libraryItems(SampleData::LibraryItems()),
          m_searchGroups(SampleData::SearchGroups()),
          m_recentTerms(SampleData::RecentSearchTerms())
    {
    }

    winrt::HaloDesktop::MediaSummary MockCatalogService::Hero() const { return m_hero; }
    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::ContinueItem> MockCatalogService::ContinueWatching() const { return m_continueWatching; }
    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Shelf> MockCatalogService::Shelves() const { return m_shelves; }
    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary> MockCatalogService::LibraryItems() const { return m_libraryItems; }
    winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> MockCatalogService::RecentTerms() const { return m_recentTerms; }

    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::SearchGroup> MockCatalogService::Search(winrt::hstring const& query) const
    {
        auto const normalized = Lower(query.c_str());
        std::vector<winrt::HaloDesktop::SearchGroup> groups;
        if (normalized.empty())
        {
            return winrt::single_threaded_vector<winrt::HaloDesktop::SearchGroup>(std::move(groups)).GetView();
        }

        for (auto const& group : m_searchGroups)
        {
            std::vector<winrt::HaloDesktop::MediaSummary> matches;
            for (auto const& item : group.Items())
            {
                if (Lower(item.Title().c_str()).find(normalized) != std::wstring::npos)
                {
                    matches.push_back(item);
                }
            }
            if (!matches.empty())
            {
                auto const items = winrt::single_threaded_vector<winrt::HaloDesktop::MediaSummary>(std::move(matches)).GetView();
                groups.push_back(winrt::make<winrt::HaloDesktop::implementation::SearchGroup>(group.Title(), group.SourceLabel(), items));
            }
        }
        return winrt::single_threaded_vector<winrt::HaloDesktop::SearchGroup>(std::move(groups)).GetView();
    }

    MockMetadataService::MockMetadataService() : m_detail(SampleData::Detail()) {}
    winrt::HaloDesktop::MediaDetail MockMetadataService::Detail() const { return m_detail; }
    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Episode> MockMetadataService::Episodes(std::int32_t season) const { return SampleData::Episodes(season); }

    MockSourceService::MockSourceService() : m_groups(SampleData::SourceGroups()) {}
    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::SourceGroup> MockSourceService::Groups() const { return m_groups; }

    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::StreamSource> MockSourceService::Filter(winrt::hstring const& quality) const
    {
        std::vector<winrt::HaloDesktop::StreamSource> matches;
        for (auto const& group : m_groups)
        {
            for (auto const& source : group.Sources())
            {
                auto const instant = source.Status() == winrt::HaloDesktop::StreamStatus::Instant
                    || source.Status() == winrt::HaloDesktop::StreamStatus::OnDisk;
                if (quality == L"All"
                    || (quality == L"Instant" && instant)
                    || source.Quality() == quality)
                {
                    matches.push_back(source);
                }
            }
        }
        return winrt::single_threaded_vector<winrt::HaloDesktop::StreamSource>(std::move(matches)).GetView();
    }

    MockAddonService::MockAddonService()
        : m_items(winrt::single_threaded_observable_vector<winrt::HaloDesktop::Addon>(SampleData::Addons()))
    {
    }

    winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::Addon> MockAddonService::Items() const { return m_items; }

    bool MockAddonService::Toggle(winrt::hstring const& name, bool enabled)
    {
        for (auto const& addon : m_items)
        {
            if (addon.Name() == name)
            {
                addon.Enabled(enabled);
                return true;
            }
        }
        return false;
    }

    bool MockAddonService::Remove(winrt::hstring const& name)
    {
        for (std::uint32_t index = 0; index < m_items.Size(); ++index)
        {
            if (m_items.GetAt(index).Name() == name)
            {
                m_items.RemoveAt(index);
                return true;
            }
        }
        return false;
    }

}
