#include "pch.h"
#include "Services/TitleActions.h"

#include "Services/ContinueShelfPolicy.h"
#include "Services/LibraryService.h"
#include "Services/ServiceInterfaces.h"
#include "Services/WatchStateService.h"

#include <chrono>
#include <functional>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace
{
    std::int64_t NowMilliseconds()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    std::set<std::wstring, std::less<>> WatchedVideoIds(
        std::vector<::HaloDesktop::Api::Dto::WatchEntry> const& rows)
    {
        std::set<std::wstring, std::less<>> watched;
        for (auto const& row : rows)
        {
            if (row.Watched)
            {
                watched.emplace(row.VideoId);
            }
        }
        return watched;
    }
}

namespace HaloDesktop::Services
{
    TitleActions::TitleActions(
        std::shared_ptr<WatchStateService> watchState,
        std::shared_ptr<LibraryService> library,
        std::shared_ptr<ICatalogService> catalog,
        std::shared_ptr<IMetadataService> metadata,
        std::shared_ptr<ISessionService> session)
        : m_watchState(std::move(watchState)),
          m_library(std::move(library)),
          m_catalog(std::move(catalog)),
          m_metadata(std::move(metadata)),
          m_session(std::move(session))
    {
        if (!m_watchState || !m_library || !m_catalog || !m_metadata || !m_session)
        {
            throw std::invalid_argument{ "TitleActions requires its services." };
        }
    }

    bool TitleActions::WatchStateKnown() const noexcept { return m_watchState->HasLoaded(); }
    bool TitleActions::LibraryKnown() const noexcept { return m_library->HasLoaded(); }

    bool TitleActions::IsWatched(winrt::hstring const& videoId) const
    {
        auto const row = m_watchState->Find(videoId);
        return row && row->Watched;
    }

    bool TitleActions::IsResumable(winrt::hstring const& videoId) const
    {
        auto const row = m_watchState->Find(videoId);
        return row && !row->Watched && row->DurationSec > 0.0 && row->PositionSec > 0.0;
    }

    bool TitleActions::InLibrary(winrt::hstring const& type, winrt::hstring const& metaId) const
    {
        return m_library->Contains(type, metaId);
    }

    std::vector<winrt::HaloDesktop::Episode> TitleActions::UnwatchedEarlier(
        std::vector<winrt::HaloDesktop::Episode> const& episodes,
        winrt::HaloDesktop::Episode const& episode) const
    {
        std::vector<winrt::HaloDesktop::Episode> result;
        if (!episode)
        {
            return result;
        }
        std::vector<EpisodeSlot> slots;
        slots.reserve(episodes.size());
        std::optional<std::size_t> target;
        for (std::size_t index = 0; index < episodes.size(); ++index)
        {
            slots.push_back({ episodes[index].Season(), episodes[index].Number() });
            if (!target && episodes[index].VideoId() == episode.VideoId())
            {
                target = index;
            }
        }
        if (!target)
        {
            return result;
        }

        auto const watched = WatchedVideoIds(m_watchState->Rows());
        // An addon that lists a video twice must not have it counted or written twice.
        std::unordered_set<std::wstring> seen;
        for (auto const index : EarlierEpisodes(slots, *target))
        {
            auto const& earlier = episodes[index];
            std::wstring videoId{ earlier.VideoId() };
            if (videoId.empty() || watched.contains(videoId) || !seen.insert(videoId).second)
            {
                continue;
            }
            result.push_back(earlier);
        }
        return result;
    }

    concurrency::task<void> TitleActions::SetWatchedAsync(WatchMarkTarget target, bool watched)
    {
        auto const uiContext = winrt::apartment_context{};
        auto const generation = m_session->Generation();
        double runtime{};
        if (watched)
        {
            // Only asked for when there is no measured length to keep, which is
            // the only case WatchedRow would use it.
            auto const existing = m_watchState->Find(target.VideoId);
            if (!existing || existing->DurationSec <= 0.0)
            {
                runtime = co_await RuntimeSecondsAsync(target.ItemId);
                co_await uiContext;
            }
        }
        if (m_session->Generation() != generation)
        {
            co_return;
        }

        // Read again after the runtime lookup, so the row is stamped against
        // whatever arrived meanwhile.
        auto const existing = m_watchState->Find(target.VideoId);
        auto row = watched
            ? WatchedRow(target, existing, runtime, NowMilliseconds())
            : UnwatchedRow(target, existing, NowMilliseconds());
        co_await m_watchState->PutAsync(std::move(row));
        co_await uiContext;
        if (m_session->Generation() == generation)
        {
            m_catalog->PublishUserStateChange();
        }
    }

    concurrency::task<void> TitleActions::MarkWatchedInOrderAsync(std::vector<WatchMarkTarget> targets)
    {
        if (targets.empty())
        {
            co_return;
        }
        auto const uiContext = winrt::apartment_context{};
        auto const generation = m_session->Generation();
        // Every target is an episode of the same show, so one runtime serves all.
        auto const runtime = co_await RuntimeSecondsAsync(targets.front().ItemId);
        co_await uiContext;
        if (m_session->Generation() != generation)
        {
            co_return;
        }

        auto const rows = m_watchState->Rows();
        auto const watched = WatchedVideoIds(rows);
        std::erase_if(targets, [&watched](WatchMarkTarget const& target)
        {
            return watched.contains(std::wstring_view{ target.VideoId });
        });
        if (targets.empty())
        {
            co_return;
        }
        co_await m_watchState->PutRowsAsync(WatchedRowsInOrder(targets, rows, runtime, NowMilliseconds()));
        co_await uiContext;
        if (m_session->Generation() == generation)
        {
            m_catalog->PublishUserStateChange();
        }
    }

    concurrency::task<bool> TitleActions::SetInLibraryAsync(
        winrt::hstring type,
        winrt::hstring metaId,
        winrt::hstring name,
        winrt::hstring poster,
        bool wanted)
    {
        if (m_library->Contains(type, metaId) == wanted)
        {
            co_return true;
        }
        auto const uiContext = winrt::apartment_context{};
        // The library service carries its own account guard and serialises its
        // writes, so nothing here needs to.
        auto const applied = co_await m_library->SetMembershipAsync(
            type,
            metaId,
            name,
            poster.empty() ? std::nullopt : std::optional<winrt::hstring>{ poster },
            wanted);
        co_await uiContext;
        if (applied)
        {
            m_catalog->PublishUserStateChange();
        }
        co_return applied;
    }

    concurrency::task<double> TitleActions::RuntimeSecondsAsync(winrt::hstring itemId)
    {
        std::wstring_view const id{ itemId };
        auto const type = winrt::hstring{ TypeFromItemId(id) };
        auto const metaId = winrt::hstring{ MetaIdFromItemId(id) };
        try
        {
            auto const minutes = co_await m_metadata->RuntimeMinutesForAsync(type, metaId);
            co_return minutes > 0 ? minutes * 60.0 : 0.0;
        }
        catch (...)
        {
            co_return 0.0;
        }
    }
}
