#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>

namespace HaloDesktop::Diagnostics
{
    // Past this size the log becomes `crash.old.log`, replacing the previous
    // one, and starts over, so it never grows without bound.
    inline constexpr std::uintmax_t CrashLogRotateBytes = 512u * 1024u;

    // `2026-10-02T16:34:05Z`. Times before 1970 print as the epoch.
    [[nodiscard]] std::string CrashLogTimestamp(std::chrono::system_clock::time_point at);

    // One UTF-8 record: the timestamp, a space, the message with CR and LF
    // flattened to spaces, and a newline.
    [[nodiscard]] std::string CrashLogLine(std::chrono::system_clock::time_point at, std::wstring_view message);

    // A local record of what took the app down or had to be recovered. Release
    // builds have no console, so without it a crash leaves no trace. The file
    // stays on this machine in the app's log folder. Callers pass only what
    // failed and where: never URLs, headers, tokens, file contents, paths or
    // account names.
    //
    // Thread-safe. Recording never throws: a log that cannot be written must
    // never take down the app it is recording.
    class CrashLog final
    {
    public:
        explicit CrashLog(std::filesystem::path directory);

        CrashLog(CrashLog const&) = delete;
        CrashLog& operator=(CrashLog const&) = delete;

        [[nodiscard]] std::filesystem::path const& Path() const noexcept;
        [[nodiscard]] std::filesystem::path const& OldPath() const noexcept;

        void Record(std::wstring_view message) noexcept;
        void RecordAt(std::chrono::system_clock::time_point at, std::wstring_view message) noexcept;

    private:
        void Append(std::chrono::system_clock::time_point at, std::wstring_view message);

        std::filesystem::path m_directory;
        std::filesystem::path m_path;
        std::filesystem::path m_oldPath;
        std::mutex m_mutex;
    };
}
