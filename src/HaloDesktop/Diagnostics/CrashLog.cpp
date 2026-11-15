#include "Diagnostics/CrashLog.h"

#include <windows.h>
#include <wil/resource.h>

#include <cstdio>
#include <system_error>
#include <utility>

namespace
{
    struct CivilDate final
    {
        std::uint64_t Year{};
        std::uint64_t Month{};
        std::uint64_t Day{};
    };

    // Days since 1970-01-01 to a Gregorian date, by Howard Hinnant's
    // civil_from_days: eras start on 1 March, so the leap day ends each
    // 400-year era and month lengths repeat every five months.
    CivilDate CivilFromDays(std::uint64_t days) noexcept
    {
        auto const shifted = days + 719'468;
        auto const era = shifted / 146'097;
        auto const dayOfEra = shifted % 146'097;
        auto const yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36'524 - dayOfEra / 146'096) / 365;
        auto const dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
        auto const monthFromMarch = (5 * dayOfYear + 2) / 153;
        auto const day = dayOfYear - (153 * monthFromMarch + 2) / 5 + 1;
        auto const month = monthFromMarch < 10 ? monthFromMarch + 3 : monthFromMarch - 9;
        auto const year = yearOfEra + era * 400 + (month <= 2 ? 1u : 0u);
        return { year, month, day };
    }

    std::string Utf8(std::wstring_view text)
    {
        if (text.empty()) return {};
        auto const length = static_cast<int>(text.size());
        auto const needed = WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
        if (needed <= 0) return {};
        std::string result(static_cast<std::size_t>(needed), '\0');
        auto const written = WideCharToMultiByte(
            CP_UTF8, 0, text.data(), length, result.data(), needed, nullptr, nullptr);
        if (written <= 0) return {};
        result.resize(static_cast<std::size_t>(written));
        return result;
    }
}

namespace HaloDesktop::Diagnostics
{
    std::string CrashLogTimestamp(std::chrono::system_clock::time_point at)
    {
        auto const sinceEpoch = std::chrono::duration_cast<std::chrono::seconds>(at.time_since_epoch()).count();
        auto const seconds = sinceEpoch > 0 ? static_cast<std::uint64_t>(sinceEpoch) : 0u;
        auto const date = CivilFromDays(seconds / 86'400);
        auto const ofDay = seconds % 86'400;
        char buffer[32]{};
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%04llu-%02llu-%02lluT%02llu:%02llu:%02lluZ",
            static_cast<unsigned long long>(date.Year),
            static_cast<unsigned long long>(date.Month),
            static_cast<unsigned long long>(date.Day),
            static_cast<unsigned long long>(ofDay / 3600),
            static_cast<unsigned long long>(ofDay % 3600 / 60),
            static_cast<unsigned long long>(ofDay % 60));
        return std::string{ buffer };
    }

    std::string CrashLogLine(std::chrono::system_clock::time_point at, std::wstring_view message)
    {
        std::wstring flattened{ message };
        for (auto& character : flattened)
        {
            if (character == L'\r' || character == L'\n') character = L' ';
        }
        auto line = CrashLogTimestamp(at);
        line.push_back(' ');
        line += Utf8(flattened);
        line.push_back('\n');
        return line;
    }

    CrashLog::CrashLog(std::filesystem::path directory)
        : m_directory(std::move(directory)),
          m_path(m_directory / L"crash.log"),
          m_oldPath(m_directory / L"crash.old.log")
    {
    }

    std::filesystem::path const& CrashLog::Path() const noexcept { return m_path; }
    std::filesystem::path const& CrashLog::OldPath() const noexcept { return m_oldPath; }

    void CrashLog::Record(std::wstring_view message) noexcept
    {
        RecordAt(std::chrono::system_clock::now(), message);
    }

    void CrashLog::RecordAt(std::chrono::system_clock::time_point at, std::wstring_view message) noexcept
    {
        try
        {
            std::scoped_lock const lock{ m_mutex };
            Append(at, message);
        }
        catch (...)
        {
        }
    }

    void CrashLog::Append(std::chrono::system_clock::time_point at, std::wstring_view message)
    {
        std::error_code error;
        std::filesystem::create_directories(m_directory, error);
        auto const size = std::filesystem::file_size(m_path, error);
        if (!error && size >= CrashLogRotateBytes)
        {
            // Replaces the previous old log; a failed move only means this file
            // keeps growing until the next try.
            static_cast<void>(MoveFileExW(m_path.c_str(), m_oldPath.c_str(), MOVEFILE_REPLACE_EXISTING));
        }

        auto const line = CrashLogLine(at, message);
        wil::unique_hfile file{ CreateFileW(
            m_path.c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr) };
        if (!file) return;
        // One write per line keeps lines from different processes whole.
        DWORD written{};
        static_cast<void>(WriteFile(file.get(), line.data(), static_cast<DWORD>(line.size()), &written, nullptr));
    }
}
