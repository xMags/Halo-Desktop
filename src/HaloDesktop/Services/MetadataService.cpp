#include "pch.h"
#include "Services/MetadataService.h"
#include "Api/ApiClient.h"
#include "Models/Models.h"
#include "Services/WatchStateService.h"
#include <algorithm>
#include <cwctype>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace
{
    winrt::hstring Tag(std::int32_t season, std::int32_t episode)
    {
        std::wostringstream output;
        output << L'S' << std::setw(2) << std::setfill(L'0') << season
            << L'E' << std::setw(2) << episode;
        return winrt::hstring{ output.str() };
    }

    // Addons write the runtime free-form ("48 min", "1 h 52 min", "120"), so the
    // leading run of digits is the only part worth trusting. An hour-and-minute
    // form is folded together; anything else falls back to the first number.
    std::int32_t RuntimeMinutesFrom(std::optional<winrt::hstring> const& value)
    {
        if (!value) return 0;
        std::wstring const text{ value->c_str() };
        std::vector<std::pair<std::int32_t, wchar_t>> parts;
        for (std::size_t index{}; index < text.size();)
        {
            if (!std::iswdigit(text[index])) { ++index; continue; }
            std::int64_t number{};
            while (index < text.size() && std::iswdigit(text[index]) && number < 100000)
            {
                number = number * 10 + (text[index] - L'0');
                ++index;
            }
            while (index < text.size() && std::iswspace(text[index])) ++index;
            auto const unit = index < text.size()
                ? static_cast<wchar_t>(std::towlower(text[index]))
                : L'\0';
            parts.emplace_back(static_cast<std::int32_t>(number), unit);
        }
        if (parts.empty()) return 0;
        std::int32_t minutes{};
        auto matched = false;
        for (auto const& [number, unit] : parts)
        {
            if (unit == L'h') { minutes += number * 60; matched = true; }
            else if (unit == L'm') { minutes += number; matched = true; }
        }
        if (!matched) minutes = parts.front().first;
        return minutes > 0 && minutes < 100000 ? minutes : 0;
    }

    winrt::hstring Join(std::vector<winrt::hstring> const& values)
    {
        std::wstring result;
        for (auto const& value : values)
        {
            if (!result.empty())
            {
                result.append(L", ");
            }
            result.append(value);
        }
        return winrt::hstring{ result };
    }

    winrt::hstring MetaLine(HaloDesktop::Api::Dto::MetaPreview const& preview)
    {
        std::wstring result;
        auto append = [&result](winrt::hstring const& value)
        {
            if (value.empty()) return;
            if (!result.empty()) result.append(L" · ");
            result.append(value);
        };
        append(preview.ReleaseInfo.value_or(L""));
        if (preview.Rating && !preview.Rating->empty()) append(L"★ " + *preview.Rating);
        return winrt::hstring{ result };
    }
}

namespace HaloDesktop::Services
{
    MetadataService::MetadataService(
        std::shared_ptr<::HaloDesktop::Api::ApiClient> api,
        std::shared_ptr<WatchStateService> watch,
        std::shared_ptr<IDownloadService> downloads)
        : m_api(std::move(api)),
          m_watch(std::move(watch)),
          m_downloads(std::move(downloads))
    {
        if (!m_api || !m_watch || !m_downloads)
        {
            throw std::invalid_argument{ "MetadataService requires dependencies." };
        }
    }

    concurrency::task<void> MetadataService::LoadAsync(
        winrt::hstring type,
        winrt::hstring metaId)
    {
        auto const uiContext = winrt::apartment_context{};
        auto const version = ++m_requestVersion;
        auto meta = co_await m_api->GetMetaAsync(type, metaId);
        co_await m_watch->LoadAsync();
        co_await uiContext;
        if (version != m_requestVersion)
        {
            co_return;
        }

        std::vector<std::int32_t> seasons;
        for (auto const& video : meta.Videos)
        {
            if (video.Season
                && std::find(seasons.begin(), seasons.end(), *video.Season) == seasons.end())
            {
                seasons.push_back(*video.Season);
            }
        }
        std::sort(seasons.begin(), seasons.end(), [](auto left, auto right)
        {
            if (left == 0) return false;
            if (right == 0) return true;
            return left < right;
        });

        m_runtimeMinutes = RuntimeMinutesFrom(meta.Runtime);
        std::vector<winrt::hstring> facts;
        if (meta.Runtime) facts.push_back(L"Runtime · " + *meta.Runtime);
        if (!meta.Genres.empty()) facts.push_back(L"Genres · " + Join(meta.Genres));
        if (!meta.Cast.empty()) facts.push_back(L"Cast · " + Join(meta.Cast));

        m_detail = winrt::make<winrt::HaloDesktop::implementation::MediaDetail>(
            meta.Preview.Id,
            meta.Preview.Name,
            type == L"series" ? L"SERIES" : L"MOVIE",
            MetaLine(meta.Preview),
            meta.Preview.Description.value_or(L""),
            winrt::single_threaded_vector(std::move(facts)).GetView(),
            winrt::single_threaded_vector<winrt::hstring>({ L"Sources resolved on demand" }).GetView(),
            winrt::single_threaded_vector(std::move(seasons)).GetView(),
            type,
            meta.Preview.Poster.value_or(L""),
            meta.Preview.Background.value_or(L""));

        // Progress and the watched mark are left out here and read when the
        // episodes are asked for, the way the download state is, so a mark made
        // while the page is open shows without loading the title again.
        m_episodes.clear();
        for (auto const& video : meta.Videos)
        {
            auto const season = video.Season.value_or(0);
            auto const episode = video.Episode.value_or(0);
            m_episodes.push_back(winrt::make<winrt::HaloDesktop::implementation::Episode>(
                Tag(season, episode),
                video.Title,
                video.Overview.value_or(L""),
                meta.Runtime.value_or(L""),
                video.Released.value_or(L""),
                0.0,
                false,
                video.Id,
                season,
                episode,
                video.Thumbnail.value_or(L""),
                false));
        }
        std::sort(m_episodes.begin(), m_episodes.end(), [](auto const& left, auto const& right)
        {
            if (left.Season() == right.Season())
            {
                return left.Number() < right.Number();
            }
            if (left.Season() == 0) return false;
            if (right.Season() == 0) return true;
            return left.Season() < right.Season();
        });
    }

    std::int32_t MetadataService::RuntimeMinutes() const noexcept { return m_runtimeMinutes; }

    concurrency::task<std::int32_t> MetadataService::RuntimeMinutesForAsync(
        winrt::hstring type,
        winrt::hstring metaId)
    {
        if (m_detail && m_detail.Type() == type && m_detail.Id() == metaId)
        {
            co_return m_runtimeMinutes;
        }
        auto const meta = co_await m_api->GetMetaAsync(type, metaId);
        co_return RuntimeMinutesFrom(meta.Runtime);
    }

    winrt::HaloDesktop::MediaDetail MetadataService::Detail() const
    {
        return m_detail;
    }

    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Episode>
        MetadataService::Episodes(std::int32_t season) const
    {
        auto const watchRows = m_watch->Rows();
        std::unordered_map<std::wstring_view, ::HaloDesktop::Api::Dto::WatchEntry const*> rowsByVideo;
        rowsByVideo.reserve(watchRows.size());
        for (auto const& row : watchRows)
        {
            rowsByVideo.insert_or_assign(std::wstring_view{ row.VideoId }, &row);
        }

        std::vector<winrt::HaloDesktop::Episode> result;
        for (auto const& episode : m_episodes)
        {
            if (episode.Season() != season)
            {
                continue;
            }
            // The watched mark stands on its own. A row marked watched from a menu
            // carries no length when the addon gives no runtime, and it is still
            // watched; only the progress fraction needs a length to divide by.
            double progress{};
            bool watched{};
            if (auto const found = rowsByVideo.find(std::wstring_view{ episode.VideoId() }); found != rowsByVideo.end())
            {
                watched = found->second->Watched;
                if (!watched && found->second->DurationSec > 0)
                {
                    progress = found->second->PositionSec / found->second->DurationSec;
                }
            }
            result.push_back(winrt::make<winrt::HaloDesktop::implementation::Episode>(
                episode.Tag(),
                episode.Title(),
                episode.Blurb(),
                episode.Runtime(),
                episode.Aired(),
                progress,
                m_downloads->HasCompleted(episode.VideoId()),
                episode.VideoId(),
                episode.Season(),
                episode.Number(),
                episode.Thumbnail(),
                watched));
        }
        return winrt::single_threaded_vector(std::move(result)).GetView();
    }

    void MetadataService::OnAccountChanged()
    {
        ++m_requestVersion;
        m_detail = nullptr;
        m_episodes.clear();
    }
}
