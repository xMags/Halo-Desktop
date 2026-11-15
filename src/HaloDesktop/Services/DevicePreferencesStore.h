#pragma once

#include "Services/SearchHistoryPolicy.h"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <winrt/Windows.Data.Json.h>

namespace HaloDesktop::Services
{
    struct DevicePreferences final
    {
        std::int32_t Theme{ 2 };
        // Written as `{ term, at }` objects. A file from an older build holds
        // plain strings, which are read as terms searched at an unknown time.
        std::vector<SearchHistoryEntry> SearchHistory;
        winrt::Windows::Data::Json::JsonObject SubtitleSelectionMemory;
        bool SourceRankingTipDismissed{};
        bool ResumePlayback{ true };
        bool HardwareDecoding{ true };
        bool DiscordPresence{ true };
        // The navigation pane is expanded. Closed until the viewer first opens
        // it, then as they last left it.
        bool NavPaneOpen{};
        // Highest download throughput this device has actually reached, in
        // megabits per second. Zero means nothing has been measured yet.
        double MeasuredLineMbps{};
    };

    // One stored search-history row: a plain string (the older shape) or a
    // `{ term, at }` object. Empty for anything else, or a term that is empty
    // or longer than the box accepts. A missing or unusable `at` reads as zero.
    [[nodiscard]] std::optional<SearchHistoryEntry> ParseSearchHistoryEntry(
        winrt::Windows::Data::Json::IJsonValue const& value);

    // Thread-safe and process-safe typed storage for device-only choices. Every
    // setter reloads and changes one key under the named file lock, which avoids
    // lost updates when two Halo processes change different preferences.
    class DevicePreferencesStore final
    {
    public:
        explicit DevicePreferencesStore(std::filesystem::path path);

        [[nodiscard]] std::int32_t Theme() const noexcept;
        void Theme(std::int32_t value);
        [[nodiscard]] std::vector<SearchHistoryEntry> SearchHistory() const;
        void SearchHistory(std::vector<SearchHistoryEntry> value);
        [[nodiscard]] winrt::Windows::Data::Json::JsonObject SubtitleSelectionMemory() const;
        void SubtitleSelectionMemory(winrt::Windows::Data::Json::JsonObject const& value);
        [[nodiscard]] bool SourceRankingTipDismissed() const noexcept;
        void SourceRankingTipDismissed(bool value);
        [[nodiscard]] bool ResumePlayback() const noexcept;
        void ResumePlayback(bool value);
        [[nodiscard]] bool HardwareDecoding() const noexcept;
        void HardwareDecoding(bool value);
        [[nodiscard]] bool DiscordPresence() const noexcept;
        void DiscordPresence(bool value);
        [[nodiscard]] bool NavPaneOpen() const noexcept;
        void NavPaneOpen(bool value);
        // Zero until a transfer has been observed. Cached in memory after the
        // first read, because callers ask per rendered row.
        [[nodiscard]] double MeasuredLineMbps() const noexcept;
        // Keeps the running peak. A sample that does not beat the stored value by
        // a clear margin is dropped without touching the file, so a live transfer
        // cannot rewrite preferences on every progress tick.
        void RecordMeasuredLineMbps(double megabitsPerSecond);

        // Migration only writes when the standalone preference file is absent.
        [[nodiscard]] bool ImportIfMissing(DevicePreferences const& value);
        [[nodiscard]] std::filesystem::path const& Path() const noexcept;

    private:
        [[nodiscard]] DevicePreferences Read() const;
        void Write(DevicePreferences const& value);

        std::filesystem::path m_path;
        mutable std::mutex m_mutex;
        mutable std::optional<double> m_lineMbps;
    };
}
