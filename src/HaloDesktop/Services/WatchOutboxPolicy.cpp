#include "Services/WatchOutboxPolicy.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_map>
#include <utility>

namespace
{
    using HaloDesktop::Api::Dto::WatchEntry;
    using JsonArray = winrt::Windows::Data::Json::JsonArray;
    using JsonObject = winrt::Windows::Data::Json::JsonObject;
    using JsonValue = winrt::Windows::Data::Json::JsonValue;
    using JsonValueType = winrt::Windows::Data::Json::JsonValueType;

    constexpr double OutboxVersion = 1.0;
    constexpr std::size_t MaximumIdLength = 2048;
    constexpr std::size_t MaximumNameLength = 512;
    constexpr std::size_t MaximumPosterLength = 4096;
    // Milliseconds since the epoch fit a double exactly up to 2^53.
    constexpr double MaximumExactInteger = 9007199254740992.0;
    // Generous for any real video, small enough that a corrupted number cannot
    // reach the server as a position.
    constexpr double MaximumSeconds = 100'000'000.0;

    std::optional<winrt::hstring> BoundedString(JsonObject const& object, wchar_t const* key, std::size_t maximum)
    {
        auto const value = object.TryLookup(key);
        if (!value || value.ValueType() != JsonValueType::String) return std::nullopt;
        auto text = value.GetString();
        if (text.empty() || text.size() > maximum) return std::nullopt;
        return text;
    }

    std::optional<double> Seconds(JsonObject const& object, wchar_t const* key)
    {
        auto const value = object.TryLookup(key);
        if (!value || value.ValueType() != JsonValueType::Number) return std::nullopt;
        auto const number = value.GetNumber();
        if (!std::isfinite(number) || number < 0.0 || number > MaximumSeconds) return std::nullopt;
        return number;
    }

    std::optional<WatchEntry> ParseRow(winrt::Windows::Data::Json::IJsonValue const& value)
    {
        if (!value || value.ValueType() != JsonValueType::Object) return std::nullopt;
        auto const object = value.GetObject();
        auto videoId = BoundedString(object, L"videoId", MaximumIdLength);
        auto itemId = BoundedString(object, L"itemId", MaximumIdLength);
        auto const position = Seconds(object, L"positionSec");
        auto const duration = Seconds(object, L"durationSec");
        auto const updatedAt = object.TryLookup(L"updatedAt");
        if (!videoId || !itemId || !position || !duration || !updatedAt || updatedAt.ValueType() != JsonValueType::Number)
        {
            return std::nullopt;
        }
        auto const stamp = updatedAt.GetNumber();
        if (!std::isfinite(stamp) || stamp <= 0.0 || stamp > MaximumExactInteger || std::floor(stamp) != stamp)
        {
            return std::nullopt;
        }
        WatchEntry row;
        row.VideoId = std::move(*videoId);
        row.ItemId = std::move(*itemId);
        row.PositionSec = *position;
        row.DurationSec = *duration;
        if (auto const watched = object.TryLookup(L"watched"); watched && watched.ValueType() == JsonValueType::Boolean)
        {
            row.Watched = watched.GetBoolean();
        }
        row.Name = BoundedString(object, L"name", MaximumNameLength);
        row.Poster = BoundedString(object, L"poster", MaximumPosterLength);
        row.UpdatedAt = static_cast<std::int64_t>(stamp);
        return row;
    }

    JsonObject SerializeRow(WatchEntry const& row)
    {
        JsonObject object;
        object.Insert(L"videoId", JsonValue::CreateStringValue(row.VideoId));
        object.Insert(L"itemId", JsonValue::CreateStringValue(row.ItemId));
        object.Insert(L"positionSec", JsonValue::CreateNumberValue(row.PositionSec));
        object.Insert(L"durationSec", JsonValue::CreateNumberValue(row.DurationSec));
        object.Insert(L"watched", JsonValue::CreateBooleanValue(row.Watched));
        if (row.Name && !row.Name->empty()) object.Insert(L"name", JsonValue::CreateStringValue(*row.Name));
        if (row.Poster && !row.Poster->empty()) object.Insert(L"poster", JsonValue::CreateStringValue(*row.Poster));
        object.Insert(L"updatedAt", JsonValue::CreateNumberValue(static_cast<double>(row.UpdatedAt)));
        return object;
    }
}

namespace HaloDesktop::Services
{
    std::vector<Api::Dto::WatchEntry> MergePendingWatchRows(
        std::vector<Api::Dto::WatchEntry> const& pending,
        std::vector<Api::Dto::WatchEntry> const& incoming)
    {
        // One slot per video in first-seen order, as the reference's Map, so rows
        // with equal times keep the order they arrived in after the stable sort.
        std::vector<WatchEntry> rows;
        rows.reserve(pending.size() + incoming.size());
        std::unordered_map<std::wstring, std::size_t> slots;
        auto const offer = [&rows, &slots](WatchEntry const& row, bool replaceOnlyIfNewer)
        {
            auto const [found, inserted] = slots.try_emplace(std::wstring{ row.VideoId }, rows.size());
            if (inserted)
            {
                rows.push_back(row);
                return;
            }
            if (!replaceOnlyIfNewer || row.UpdatedAt > rows[found->second].UpdatedAt)
            {
                rows[found->second] = row;
            }
        };
        for (auto const& row : pending) offer(row, false);
        for (auto const& row : incoming) offer(row, true);
        std::stable_sort(rows.begin(), rows.end(), [](WatchEntry const& left, WatchEntry const& right)
        {
            return left.UpdatedAt < right.UpdatedAt;
        });
        if (rows.size() > WatchOutboxMaximumRows)
        {
            rows.erase(rows.begin(), rows.end() - static_cast<std::ptrdiff_t>(WatchOutboxMaximumRows));
        }
        return rows;
    }

    std::vector<Api::Dto::WatchEntry> WithoutSettledWatchRows(
        std::vector<Api::Dto::WatchEntry> const& pending,
        std::vector<Api::Dto::WatchEntry> const& sent)
    {
        std::unordered_map<std::wstring, std::int64_t> sentAt;
        for (auto const& row : sent)
        {
            sentAt.insert_or_assign(std::wstring{ row.VideoId }, row.UpdatedAt);
        }
        std::vector<WatchEntry> rows;
        rows.reserve(pending.size());
        for (auto const& row : pending)
        {
            auto const found = sentAt.find(std::wstring{ row.VideoId });
            if (found == sentAt.end() || row.UpdatedAt > found->second)
            {
                rows.push_back(row);
            }
        }
        return rows;
    }

    PendingWatchSend SplitPendingWatchRows(
        std::vector<Api::Dto::WatchEntry> const& pending,
        std::vector<Api::Dto::WatchEntry> const& known)
    {
        std::unordered_map<std::wstring, std::int64_t> newest;
        for (auto const& row : known)
        {
            auto& stamp = newest[std::wstring{ row.VideoId }];
            stamp = (std::max)(stamp, row.UpdatedAt);
        }
        PendingWatchSend split;
        for (auto const& row : pending)
        {
            auto const found = newest.find(std::wstring{ row.VideoId });
            if (found != newest.end() && found->second >= row.UpdatedAt)
            {
                split.AlreadyKnown.push_back(row);
            }
            else
            {
                split.ToSend.push_back(row);
            }
        }
        return split;
    }

    std::string SerializeWatchOutbox(std::wstring_view accountKey, std::vector<Api::Dto::WatchEntry> const& rows)
    {
        JsonObject root;
        root.Insert(L"version", JsonValue::CreateNumberValue(OutboxVersion));
        root.Insert(L"account", JsonValue::CreateStringValue(winrt::hstring{ accountKey }));
        JsonArray array;
        for (auto const& row : rows)
        {
            array.Append(SerializeRow(row));
        }
        root.Insert(L"rows", array);
        return winrt::to_string(root.Stringify());
    }

    std::vector<Api::Dto::WatchEntry> ParseWatchOutbox(std::string const& text, std::wstring_view accountKey)
    {
        if (text.empty() || accountKey.empty() || text.size() > WatchOutboxMaximumBytes) return {};
        try
        {
            JsonObject root{ nullptr };
            if (!JsonObject::TryParse(winrt::to_hstring(text), root) || !root) return {};
            auto const version = root.TryLookup(L"version");
            if (!version || version.ValueType() != JsonValueType::Number || version.GetNumber() != OutboxVersion) return {};
            auto const account = root.TryLookup(L"account");
            if (!account || account.ValueType() != JsonValueType::String
                || std::wstring_view{ account.GetString() } != accountKey)
            {
                return {};
            }
            auto const list = root.TryLookup(L"rows");
            if (!list || list.ValueType() != JsonValueType::Array) return {};
            std::vector<WatchEntry> rows;
            for (auto const& value : list.GetArray())
            {
                if (auto row = ParseRow(value)) rows.push_back(std::move(*row));
            }
            // Deduplicated and bounded again: the file is input like any other.
            return MergePendingWatchRows({}, rows);
        }
        catch (...)
        {
            return {};
        }
    }

    bool SameWatchRows(std::vector<Api::Dto::WatchEntry> const& left, std::vector<Api::Dto::WatchEntry> const& right) noexcept
    {
        if (left.size() != right.size()) return false;
        for (std::size_t index = 0; index < left.size(); ++index)
        {
            auto const& a = left[index];
            auto const& b = right[index];
            if (a.VideoId != b.VideoId || a.ItemId != b.ItemId || a.PositionSec != b.PositionSec
                || a.DurationSec != b.DurationSec || a.Watched != b.Watched || a.Name != b.Name
                || a.Poster != b.Poster || a.UpdatedAt != b.UpdatedAt || a.RemovedAt != b.RemovedAt)
            {
                return false;
            }
        }
        return true;
    }
}
