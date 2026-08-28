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
    constexpr std::size_t MaximumHistoryItemLength = 120;
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
                auto const entry = history.GetAt(index);
                if (entry.ValueType() != JsonValueType::String)
                {
                    continue;
                }
                auto const text = entry.GetString();
                if (!text.empty() && text.size() <= MaximumHistoryItemLength)
                {
                    result.SearchHistory.push_back(text);
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
            if (entry.empty() || entry.size() > MaximumHistoryItemLength)
            {
                continue;
            }
            history.Append(JsonValue::CreateStringValue(entry));
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

    std::vector<winrt::hstring> DevicePreferencesStore::SearchHistory() const
    {
        try { return Read().SearchHistory; } catch (...) { return {}; }
    }

    void DevicePreferencesStore::SearchHistory(std::vector<winrt::hstring> value)
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
