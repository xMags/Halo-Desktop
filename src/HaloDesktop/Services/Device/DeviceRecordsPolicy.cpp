#include "pch.h"
#include "Services/Device/DeviceRecordsPolicy.h"

#include "Api/Addons/AddonClientPolicy.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <unordered_map>
#include <unordered_set>

namespace
{
    namespace Json = winrt::Windows::Data::Json;

    bool IsUrl(std::optional<winrt::hstring> const& value)
    {
        return ::HaloDesktop::Api::Addons::IsUrl(std::wstring_view{ *value });
    }

    // A present key must hold a valid value; `null` is not absent.
    bool Optional(
        Json::JsonObject const& settings,
        wchar_t const* key,
        std::function<bool(Json::IJsonValue const&)> const& valid)
    {
        if (!settings.HasKey(key)) return true;
        auto const value = settings.GetNamedValue(key);
        return value && valid(value);
    }

    bool IsString(Json::IJsonValue const& value)
    {
        return value.ValueType() == Json::JsonValueType::String;
    }

    bool IsBoolean(Json::IJsonValue const& value)
    {
        return value.ValueType() == Json::JsonValueType::Boolean;
    }

    std::optional<double> Number(Json::IJsonValue const& value)
    {
        if (value.ValueType() != Json::JsonValueType::Number) return std::nullopt;
        return value.GetNumber();
    }

    bool StringOneOf(Json::IJsonValue const& value, std::initializer_list<wchar_t const*> allowed)
    {
        if (!IsString(value)) return false;
        auto const text = value.GetString();
        return std::any_of(allowed.begin(), allowed.end(), [&text](wchar_t const* candidate) { return text == candidate; });
    }

    bool StringLengthBetween(Json::IJsonValue const& value, std::size_t minimum, std::size_t maximum)
    {
        if (!IsString(value)) return false;
        auto const length = value.GetString().size();
        return length >= minimum && length <= maximum;
    }
}

namespace HaloDesktop::Services::Device
{
    std::optional<std::wstring> AddonUrlsProblem(std::vector<std::wstring> const& urls)
    {
        if (urls.size() > MaximumAddons)
        {
            return L"at most " + std::to_wstring(MaximumAddons) + L" addons can be installed";
        }
        std::unordered_set<std::wstring> seen;
        for (auto const& url : urls)
        {
            if (!::HaloDesktop::Api::Addons::IsHttpUrl(url)) return L"not an addon URL: " + url;
            if (!seen.insert(url).second) return L"duplicate transportUrl: " + url;
        }
        return std::nullopt;
    }

    std::optional<std::wstring> LibraryItemProblem(Api::Dto::LibraryRow const& item)
    {
        if (item.Id.empty() || item.Type.empty() || item.Name.empty()) return L"library item needs an id, type and name";
        if (item.Poster && !IsUrl(item.Poster)) return L"library item poster must be a URL";
        if (item.AddedAt <= 0 || item.UpdatedAt <= 0) return L"library item timestamps must be positive integers";
        if (item.RemovedAt && *item.RemovedAt <= 0) return L"library item removedAt must be a positive integer";
        return std::nullopt;
    }

    std::optional<std::wstring> WatchStateProblem(Api::Dto::WatchEntry const& state)
    {
        if (state.VideoId.empty() || state.ItemId.empty()) return L"watch state needs a videoId and itemId";
        // Finite as well: JSON cannot carry Infinity, so the server would
        // refuse it on the way into an account.
        if (!std::isfinite(state.PositionSec) || state.PositionSec < 0
            || !std::isfinite(state.DurationSec) || state.DurationSec < 0)
        {
            return L"watch state position and duration must be non-negative numbers";
        }
        if (state.Name && (state.Name->empty() || state.Name->size() > MaximumWatchStateNameLength))
        {
            return L"watch state name must be 1 to 512 characters";
        }
        if (state.Poster && !IsUrl(state.Poster)) return L"watch state poster must be a URL";
        if (state.RemovedAt && *state.RemovedAt <= 0) return L"watch state removedAt must be a positive integer";
        if (state.UpdatedAt <= 0) return L"watch state updatedAt must be a positive integer";
        return std::nullopt;
    }

    std::optional<std::wstring> SettingsProblem(Json::JsonObject const& value, std::int64_t updatedAt)
    {
        if (updatedAt <= 0) return L"settings updatedAt must be a positive integer";
        if (!value) return L"settings must be an object";

        using Check = std::pair<wchar_t const*, std::function<bool(Json::IJsonValue const&)>>;
        std::vector<Check> const checks{
            { L"preferredAudioLang", [](Json::IJsonValue const& v) { return IsString(v) && v.GetString().size() <= 8; } },
            { L"preferredSubtitleLang", [](Json::IJsonValue const& v) { return IsString(v) && v.GetString().size() <= 8; } },
            { L"videoFitMode", [](Json::IJsonValue const& v) { return StringOneOf(v, { L"cover", L"contain" }); } },
            { L"subtitleScalePercent", [](Json::IJsonValue const& v)
                {
                    auto const number = Number(v);
                    return number && std::isfinite(*number) && std::floor(*number) == *number
                        && *number >= 50 && *number <= 200;
                } },
            { L"subtitleFontFamily", [](Json::IJsonValue const& v) { return StringLengthBetween(v, 1, 64); } },
            { L"subtitleOutline", [](Json::IJsonValue const& v) { return StringOneOf(v, { L"none", L"thin", L"normal", L"thick" }); } },
            { L"subtitleShadow", [](Json::IJsonValue const& v) { return IsBoolean(v); } },
            { L"playbackRate", [](Json::IJsonValue const& v)
                {
                    auto const number = Number(v);
                    return number && std::isfinite(*number) && *number >= 0.25 && *number <= 4;
                } },
            { L"autoplayNextEpisode", [](Json::IJsonValue const& v) { return IsBoolean(v); } },
            { L"catalogLanguage", [](Json::IJsonValue const& v) { return StringLengthBetween(v, 1, 64); } },
        };
        for (auto const& [key, valid] : checks)
        {
            if (!Optional(value, key, valid)) return std::wstring{ L"invalid setting: " } + key;
        }
        return std::nullopt;
    }

    std::optional<std::wstring> LibraryProblem(std::vector<Api::Dto::LibraryRow> const& items)
    {
        for (auto const& item : items)
        {
            if (auto problem = LibraryItemProblem(item)) return problem;
        }
        return std::nullopt;
    }

    std::optional<std::wstring> WatchStatesProblem(std::vector<Api::Dto::WatchEntry> const& states)
    {
        for (auto const& state : states)
        {
            if (auto problem = WatchStateProblem(state)) return problem;
        }
        return std::nullopt;
    }

    std::vector<Api::Dto::LibraryRow> MergeLibrary(
        std::vector<Api::Dto::LibraryRow> const& stored,
        std::vector<Api::Dto::LibraryRow> const& incoming)
    {
        std::vector<Api::Dto::LibraryRow> merged{ stored };
        std::unordered_map<std::wstring, std::size_t> byId;
        for (std::size_t index = 0; index < merged.size(); ++index)
        {
            byId.insert_or_assign(std::wstring{ merged[index].Id }, index);
        }
        for (auto const& item : incoming)
        {
            auto const found = byId.find(std::wstring{ item.Id });
            if (found == byId.end())
            {
                byId.emplace(std::wstring{ item.Id }, merged.size());
                merged.push_back(item);
                continue;
            }
            auto& existing = merged[found->second];
            if (item.UpdatedAt > existing.UpdatedAt)
            {
                auto const type = existing.Type;
                existing = item;
                existing.Type = type;
            }
        }
        return merged;
    }

    std::vector<Api::Dto::WatchEntry> MergeWatchStates(
        std::vector<Api::Dto::WatchEntry> const& stored,
        std::vector<Api::Dto::WatchEntry> const& incoming)
    {
        std::vector<Api::Dto::WatchEntry> merged{ stored };
        std::unordered_map<std::wstring, std::size_t> byVideo;
        for (std::size_t index = 0; index < merged.size(); ++index)
        {
            byVideo.insert_or_assign(std::wstring{ merged[index].VideoId }, index);
        }
        for (auto const& state : incoming)
        {
            auto const found = byVideo.find(std::wstring{ state.VideoId });
            if (found == byVideo.end())
            {
                byVideo.emplace(std::wstring{ state.VideoId }, merged.size());
                merged.push_back(state);
                continue;
            }
            if (state.UpdatedAt > merged[found->second].UpdatedAt) merged[found->second] = state;
        }
        return merged;
    }

    std::vector<Api::Dto::WatchEntry> LiveWatchStates(std::vector<Api::Dto::WatchEntry> const& states)
    {
        std::vector<Api::Dto::WatchEntry> live;
        std::copy_if(states.begin(), states.end(), std::back_inserter(live), [](Api::Dto::WatchEntry const& state)
        {
            return !state.RemovedAt.has_value();
        });
        return live;
    }
}
