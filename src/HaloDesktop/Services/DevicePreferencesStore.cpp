#include "pch.h"
#include "Services/DevicePreferencesStore.h"

#include "Storage/FileStorage.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <utility>

namespace
{
    constexpr std::uint64_t MaximumPreferencesBytes = 1024u * 1024u;
    constexpr std::size_t MaximumHistoryItems = 20;
    constexpr std::size_t MaximumHistoryItemLength = HaloDesktop::Services::SearchHistoryTermLimit;
    constexpr double MaximumExactMilliseconds = 9007199254740992.0;
    constexpr std::size_t MaximumSubtitleMemoryCharacters = 512u * 1024u;
    // A residential line faster than this is not something the warning needs to
    // model, and it keeps a corrupted file from producing an absurd threshold.
    constexpr double MaximumLineMbps = 100000.0;
    // A new peak has to clear the stored one by a tenth before it is worth a write.
    constexpr double LineMbpsWriteMargin = 1.1;

    using HaloDesktop::Services::DevicePreferences;
    using JsonObject = winrt::Windows::Data::Json::JsonObject;
    using JsonValue = winrt::Windows::Data::Json::JsonValue;
    using JsonValueType = winrt::Windows::Data::Json::JsonValueType;

    DevicePreferences Parse(std::string const& raw)
    {
        DevicePreferences result;
        if (raw.empty())
        {
            return result;
        }
        auto const root = JsonObject::Parse(winrt::to_hstring(raw));
        auto const version = root.GetNamedNumber(L"version");
        if (!std::isfinite(version) || version != 1.0)
        {
            throw std::invalid_argument{ "The device preference version is unsupported." };
        }

        auto const theme = root.GetNamedNumber(L"theme", 2.0);
        if (std::isfinite(theme) && std::floor(theme) == theme && theme >= 0.0 && theme <= 2.0)
        {
            result.Theme = static_cast<std::int32_t>(theme);
        }

        if (root.HasKey(L"searchHistory"))
        {
            auto const history = root.GetNamedArray(L"searchHistory");
            auto const count = (std::min)(history.Size(), static_cast<std::uint32_t>(MaximumHistoryItems));
            result.SearchHistory.reserve(count);
            for (std::uint32_t index{}; index < count; ++index)
            {
                if (auto entry = HaloDesktop::Services::ParseSearchHistoryEntry(history.GetAt(index)))
                {
                    result.SearchHistory.push_back(std::move(*entry));
                }
            }
        }

        if (root.HasKey(L"subtitleSelectionMemory"))
        {
            auto const memory = root.GetNamedObject(L"subtitleSelectionMemory");
            if (memory.Stringify().size() <= MaximumSubtitleMemoryCharacters)
            {
                result.SubtitleSelectionMemory = JsonObject::Parse(memory.Stringify());
            }
        }
        auto const line = root.GetNamedNumber(L"measuredLineMbps", 0.0);
        if (std::isfinite(line) && line > 0.0)
        {
            result.MeasuredLineMbps = (std::min)(line, MaximumLineMbps);
        }
        result.SourceRankingTipDismissed = root.GetNamedBoolean(L"sourceRankingTipDismissed", false);
        result.ResumePlayback = root.GetNamedBoolean(L"resumePlayback", true);
        result.HardwareDecoding = root.GetNamedBoolean(L"hardwareDecoding", true);
        result.DiscordPresence = root.GetNamedBoolean(L"discordPresence", true);
        result.NavPaneOpen = root.GetNamedBoolean(L"navPaneOpen", false);
        result.FillPicture = root.GetNamedBoolean(L"fillPicture", false);
        return result;
    }

    std::string Serialize(DevicePreferences const& value)
    {
        JsonObject root;
        root.Insert(L"version", JsonValue::CreateNumberValue(1));
        root.Insert(L"theme", JsonValue::CreateNumberValue(std::clamp(value.Theme, 0, 2)));

        winrt::Windows::Data::Json::JsonArray history;
        std::size_t count{};
        for (auto const& entry : value.SearchHistory)
        {
            if (count == MaximumHistoryItems)
            {
                break;
            }
            if (entry.Term.empty() || entry.Term.size() > MaximumHistoryItemLength)
            {
                continue;
            }
            JsonObject row;
            row.Insert(L"term", JsonValue::CreateStringValue(entry.Term));
            row.Insert(L"at", JsonValue::CreateNumberValue(static_cast<double>((std::max)(entry.At, std::int64_t{}))));
            history.Append(row);
            ++count;
        }
        root.Insert(L"searchHistory", history);

        auto memory = value.SubtitleSelectionMemory;
        if (memory && memory.Stringify().size() <= MaximumSubtitleMemoryCharacters)
        {
            root.Insert(L"subtitleSelectionMemory", JsonObject::Parse(memory.Stringify()));
        }
        else
        {
            root.Insert(L"subtitleSelectionMemory", JsonObject{});
        }
        root.Insert(
            L"measuredLineMbps",
            JsonValue::CreateNumberValue(std::clamp(value.MeasuredLineMbps, 0.0, MaximumLineMbps)));
        root.Insert(L"sourceRankingTipDismissed", JsonValue::CreateBooleanValue(value.SourceRankingTipDismissed));
        root.Insert(L"resumePlayback", JsonValue::CreateBooleanValue(value.ResumePlayback));
        root.Insert(L"hardwareDecoding", JsonValue::CreateBooleanValue(value.HardwareDecoding));
        root.Insert(L"discordPresence", JsonValue::CreateBooleanValue(value.DiscordPresence));
        root.Insert(L"navPaneOpen", JsonValue::CreateBooleanValue(value.NavPaneOpen));
        root.Insert(L"fillPicture", JsonValue::CreateBooleanValue(value.FillPicture));
        return winrt::to_string(root.Stringify());
    }

    template <typename Mutation>
    void Mutate(
        std::filesystem::path const& path,
        std::mutex& processMutex,
        Mutation&& mutation)
    {
        std::scoped_lock const processLock{ processMutex };
        HaloDesktop::Storage::FileMutationLock const fileLock{ path };
        DevicePreferences current;
        try
        {
            current = Parse(HaloDesktop::Storage::ReadUtf8File(path, MaximumPreferencesBytes));
        }
        catch (...)
        {
            current = {};
        }
        std::invoke(std::forward<Mutation>(mutation), current);
        HaloDesktop::Storage::WriteUtf8FileAtomic(path, Serialize(current));
    }
}

namespace HaloDesktop::Services
{
    std::optional<SearchHistoryEntry> ParseSearchHistoryEntry(
        winrt::Windows::Data::Json::IJsonValue const& value)
    {
        if (!value)
        {
            return std::nullopt;
        }
        SearchHistoryEntry entry;
        if (value.ValueType() == JsonValueType::String)
        {
            entry.Term = std::wstring{ value.GetString() };
        }
        else if (value.ValueType() == JsonValueType::Object)
        {
            auto const object = value.GetObject();
            auto const term = object.TryLookup(L"term");
            if (!term || term.ValueType() != JsonValueType::String)
            {
                return std::nullopt;
            }
            entry.Term = std::wstring{ term.GetString() };
            if (auto const at = object.TryLookup(L"at"); at && at.ValueType() == JsonValueType::Number)
            {
                // Milliseconds since the epoch fit a double exactly up to 2^53.
                auto const number = at.GetNumber();
                if (std::isfinite(number) && number >= 0.0 && number <= MaximumExactMilliseconds)
                {
                    entry.At = static_cast<std::int64_t>(std::floor(number));
                }
            }
        }
        else
        {
            return std::nullopt;
        }
        if (entry.Term.empty() || entry.Term.size() > MaximumHistoryItemLength)
        {
            return std::nullopt;
        }
        return entry;
    }

    DevicePreferencesStore::DevicePreferencesStore(std::filesystem::path path)
        : m_path(std::filesystem::absolute(std::move(path)).lexically_normal())
    {
        if (m_path.empty() || m_path.parent_path().empty())
        {
            throw std::invalid_argument{ "A device preference path is required." };
        }
    }

    std::int32_t DevicePreferencesStore::Theme() const noexcept
    {
        try { return Read().Theme; } catch (...) { return 2; }
    }

    void DevicePreferencesStore::Theme(std::int32_t value)
    {
        Mutate(m_path, m_mutex, [value](DevicePreferences& current)
        {
            current.Theme = std::clamp(value, 0, 2);
        });
    }

    std::vector<SearchHistoryEntry> DevicePreferencesStore::SearchHistory() const
    {
        try { return Read().SearchHistory; } catch (...) { return {}; }
    }

    void DevicePreferencesStore::SearchHistory(std::vector<SearchHistoryEntry> value)
    {
        Mutate(m_path, m_mutex, [value = std::move(value)](DevicePreferences& current) mutable
        {
            current.SearchHistory = std::move(value);
        });
    }

    winrt::Windows::Data::Json::JsonObject DevicePreferencesStore::SubtitleSelectionMemory() const
    {
        try { return Read().SubtitleSelectionMemory; } catch (...) { return {}; }
    }

    void DevicePreferencesStore::SubtitleSelectionMemory(
        winrt::Windows::Data::Json::JsonObject const& value)
    {
        auto const snapshot = value ? JsonObject::Parse(value.Stringify()) : JsonObject{};
        if (snapshot.Stringify().size() > MaximumSubtitleMemoryCharacters)
        {
            throw std::invalid_argument{ "Subtitle selection memory is too large." };
        }
        Mutate(m_path, m_mutex, [snapshot](DevicePreferences& current)
        {
            current.SubtitleSelectionMemory = JsonObject::Parse(snapshot.Stringify());
        });
    }

    bool DevicePreferencesStore::SourceRankingTipDismissed() const noexcept
    {
        try { return Read().SourceRankingTipDismissed; } catch (...) { return false; }
    }

    void DevicePreferencesStore::SourceRankingTipDismissed(bool value)
    {
        Mutate(m_path, m_mutex, [value](DevicePreferences& current)
        {
            current.SourceRankingTipDismissed = value;
        });
    }

    bool DevicePreferencesStore::ResumePlayback() const noexcept
    {
        try { return Read().ResumePlayback; } catch (...) { return true; }
    }

    void DevicePreferencesStore::ResumePlayback(bool value)
    {
        Mutate(m_path, m_mutex, [value](DevicePreferences& current)
        {
            current.ResumePlayback = value;
        });
    }

    bool DevicePreferencesStore::HardwareDecoding() const noexcept
    {
        try { return Read().HardwareDecoding; } catch (...) { return true; }
    }

    void DevicePreferencesStore::HardwareDecoding(bool value)
    {
        Mutate(m_path, m_mutex, [value](DevicePreferences& current)
        {
            current.HardwareDecoding = value;
        });
    }

    bool DevicePreferencesStore::DiscordPresence() const noexcept
    {
        try { return Read().DiscordPresence; } catch (...) { return true; }
    }

    void DevicePreferencesStore::DiscordPresence(bool value)
    {
        Mutate(m_path, m_mutex, [value](DevicePreferences& current)
        {
            current.DiscordPresence = value;
        });
    }

    bool DevicePreferencesStore::NavPaneOpen() const noexcept
    {
        try { return Read().NavPaneOpen; } catch (...) { return false; }
    }

    void DevicePreferencesStore::NavPaneOpen(bool value)
    {
        Mutate(m_path, m_mutex, [value](DevicePreferences& current)
        {
            current.NavPaneOpen = value;
        });
    }

    bool DevicePreferencesStore::FillPicture() const noexcept
    {
        try { return Read().FillPicture; } catch (...) { return false; }
    }

    void DevicePreferencesStore::FillPicture(bool value)
    {
        Mutate(m_path, m_mutex, [value](DevicePreferences& current)
        {
            current.FillPicture = value;
        });
    }

    double DevicePreferencesStore::MeasuredLineMbps() const noexcept
    {
        {
            std::scoped_lock const lock{ m_mutex };
            if (m_lineMbps) return *m_lineMbps;
        }
        auto value = 0.0;
        try { value = Read().MeasuredLineMbps; } catch (...) { value = 0.0; }
        std::scoped_lock const lock{ m_mutex };
        m_lineMbps = value;
        return value;
    }

    void DevicePreferencesStore::RecordMeasuredLineMbps(double megabitsPerSecond)
    {
        if (!std::isfinite(megabitsPerSecond) || megabitsPerSecond <= 0.0) return;
        auto const sample = (std::min)(megabitsPerSecond, MaximumLineMbps);
        if (sample <= MeasuredLineMbps() * LineMbpsWriteMargin) return;
        try
        {
            Mutate(m_path, m_mutex, [sample](DevicePreferences& current)
            {
                if (sample > current.MeasuredLineMbps) current.MeasuredLineMbps = sample;
            });
        }
        catch (...)
        {
            return;
        }
        std::scoped_lock const lock{ m_mutex };
        m_lineMbps = sample;
    }

    bool DevicePreferencesStore::ImportIfMissing(DevicePreferences const& value)
    {
        std::scoped_lock const processLock{ m_mutex };
        HaloDesktop::Storage::FileMutationLock const fileLock{ m_path };
        if (std::filesystem::exists(m_path))
        {
            return false;
        }
        HaloDesktop::Storage::WriteUtf8FileAtomic(m_path, Serialize(value));
        return true;
    }

    std::filesystem::path const& DevicePreferencesStore::Path() const noexcept
    {
        return m_path;
    }

    DevicePreferences DevicePreferencesStore::Read() const
    {
        std::scoped_lock const processLock{ m_mutex };
        HaloDesktop::Storage::FileMutationLock const fileLock{ m_path };
        return Parse(HaloDesktop::Storage::ReadUtf8File(m_path, MaximumPreferencesBytes));
    }

    void DevicePreferencesStore::Write(DevicePreferences const& value)
    {
        std::scoped_lock const processLock{ m_mutex };
        HaloDesktop::Storage::FileMutationLock const fileLock{ m_path };
        HaloDesktop::Storage::WriteUtf8FileAtomic(m_path, Serialize(value));
    }
}
