#pragma once

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
        std::vector<winrt::hstring> SearchHistory;
        winrt::Windows::Data::Json::JsonObject SubtitleSelectionMemory;
        bool SourceRankingTipDismissed{};
        bool ResumePlayback{ true };
        bool HardwareDecoding{ true };
        bool DiscordPresence{ true };
        // Highest download throughput this device has actually reached, in
        // megabits per second. Zero means nothing has been measured yet.
        double MeasuredLineMbps{};
    };

    // Thread-safe and process-safe typed storage for device-only choices. Every
    // setter reloads and changes one key under the named file lock, which avoids
    // lost updates when two Halo processes change different preferences.
    class DevicePreferencesStore final
    {
    public:
        explicit DevicePreferencesStore(std::filesystem::path path);

        [[nodiscard]] std::int32_t Theme() const noexcept;
        void Theme(std::int32_t value);
        [[nodiscard]] std::vector<winrt::hstring> SearchHistory() const;
        void SearchHistory(std::vector<winrt::hstring> value);
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
