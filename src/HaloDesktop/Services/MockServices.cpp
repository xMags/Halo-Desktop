#include "pch.h"
#include "Services/MockServices.h"

#include "Models/Models.h"
#include "Services/SampleData.h"

#include <algorithm>
#include <cwctype>
#include <string>
#include <utility>

namespace HaloDesktop::Services
{
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

}
