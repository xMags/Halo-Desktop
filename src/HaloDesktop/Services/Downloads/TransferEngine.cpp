#include "pch.h"
#include "Services/Downloads/TransferEngine.h"

#include "Storage/FileStorage.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <limits>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>
#include <wil/resource.h>
#include <winhttp.h>

namespace
{
    constexpr std::size_t TransferChunkSize = 64u * 1024u;
    constexpr std::uint64_t MaximumSubtitleBytes = 32ull * 1024ull * 1024ull;
    constexpr auto ProgressInterval = std::chrono::milliseconds{ 250 };
    constexpr int TransferAttempts = 3;
    constexpr int MaximumRedirects = 5;

    struct RedirectError final
    {
    };

    struct HttpCloser final
    {
        void operator()(void* value) const noexcept
        {
            if (value)
            {
                WinHttpCloseHandle(value);
            }
        }
    };
    using UniqueHttp = std::unique_ptr<void, HttpCloser>;

    struct HttpResponse final
    {
        UniqueHttp Session;
        UniqueHttp Connection;
        UniqueHttp Request;
        std::uint32_t Status{};
        std::optional<std::wstring> ETag;
        std::optional<std::wstring> LastModified;
        std::optional<std::wstring> Location;
        std::optional<std::wstring> ContentRange;
        std::optional<std::uint64_t> ContentLength;
    };

    std::uint64_t NowMilliseconds() noexcept
    {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    }

    [[noreturn]] void ThrowSystem(char const* message)
    {
        throw std::system_error{
            static_cast<int>(GetLastError()),
            std::system_category(),
            message };
    }

    std::wstring NewJobId()
    {
        GUID id{};
        winrt::check_hresult(CoCreateGuid(&id));
        std::array<wchar_t, 40> value{};
        if (StringFromGUID2(id, value.data(), static_cast<int>(value.size())) == 0)
        {
            throw std::runtime_error{ "A download job identifier could not be created." };
        }
        std::wstring result;
        result.reserve(32);
        for (auto const character : value)
        {
            if ((character >= L'0' && character <= L'9')
                || (character >= L'a' && character <= L'f')
                || (character >= L'A' && character <= L'F'))
            {
                result.push_back(static_cast<wchar_t>(std::towlower(character)));
            }
        }
        if (result.size() != 32)
        {
            throw std::runtime_error{ "A download job identifier is invalid." };
        }
        return result;
    }

    std::optional<std::uint64_t> ParseUnsigned(std::wstring const& value) noexcept
    {
        if (value.empty())
        {
            return std::nullopt;
        }
        std::uint64_t result{};
        for (auto const character : value)
        {
            if (character < L'0' || character > L'9')
            {
                return std::nullopt;
            }
            auto const digit = static_cast<std::uint64_t>(character - L'0');
            if (result > ((std::numeric_limits<std::uint64_t>::max)() - digit) / 10)
            {
                return std::nullopt;
            }
            result = result * 10 + digit;
        }
        return result;
    }

    std::optional<std::wstring> QueryHeader(
        HINTERNET request,
        wchar_t const* headerName)
    {
        DWORD bytes{};
        WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_CUSTOM,
            headerName,
            WINHTTP_NO_OUTPUT_BUFFER,
            &bytes,
            WINHTTP_NO_HEADER_INDEX);
        auto const error = GetLastError();
        if (error == ERROR_WINHTTP_HEADER_NOT_FOUND)
        {
            return std::nullopt;
        }
        if (error != ERROR_INSUFFICIENT_BUFFER || bytes < sizeof(wchar_t))
        {
            ThrowSystem("A download response header could not be inspected");
        }
        if (bytes > 64u * 1024u)
        {
            throw std::length_error{ "A download response header is oversized." };
        }
        std::wstring value(bytes / sizeof(wchar_t), L'\0');
        if (!WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_CUSTOM,
            headerName,
            value.data(),
            &bytes,
            WINHTTP_NO_HEADER_INDEX))
        {
            ThrowSystem("A download response header could not be read");
        }
        value.resize(bytes / sizeof(wchar_t));
        while (!value.empty() && value.back() == L'\0')
        {
            value.pop_back();
        }
        return value.empty() ? std::nullopt : std::optional<std::wstring>{ std::move(value) };
    }

    HttpResponse OpenGetOnce(
        std::wstring const& url,
        std::map<std::wstring, std::wstring, std::less<>> const& headers,
        std::optional<std::uint64_t> partial,
        std::optional<std::wstring> const& validator)
    {
        URL_COMPONENTS parts{ .dwStructSize = sizeof(URL_COMPONENTS) };
        parts.dwSchemeLength = static_cast<DWORD>(-1);
        parts.dwHostNameLength = static_cast<DWORD>(-1);
        parts.dwUrlPathLength = static_cast<DWORD>(-1);
        parts.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts))
        {
            ThrowSystem("A download source URL could not be parsed");
        }
        std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        std::wstring target;
        if (parts.dwUrlPathLength > 0)
        {
            target.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
        }
        if (parts.dwExtraInfoLength > 0)
        {
            target.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        }
        if (target.empty())
        {
            target = L"/";
        }

        HttpResponse response;
        response.Session.reset(WinHttpOpen(
            L"Halo Desktop/1.0",
            WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0));
        if (!response.Session)
        {
            ThrowSystem("The download HTTP session could not be created");
        }
        if (!WinHttpSetTimeouts(
            response.Session.get(),
            30000,
            30000,
            30000,
            45000))
        {
            ThrowSystem("The download HTTP timeouts could not be configured");
        }
        response.Connection.reset(WinHttpConnect(
            response.Session.get(), host.c_str(), parts.nPort, 0));
        if (!response.Connection)
        {
            ThrowSystem("The download source could not be connected");
        }
        auto const flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
        response.Request.reset(WinHttpOpenRequest(
            response.Connection.get(),
            L"GET",
            target.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            flags));
        if (!response.Request)
        {
            ThrowSystem("The download request could not be created");
        }
        DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if (!WinHttpSetOption(
            response.Request.get(),
            WINHTTP_OPTION_REDIRECT_POLICY,
            &redirectPolicy,
            sizeof(redirectPolicy)))
        {
            ThrowSystem("The download redirect policy could not be configured");
        }
        for (auto const& [key, value] : headers)
        {
            auto const line = key + L": " + value;
            if (!WinHttpAddRequestHeaders(
                response.Request.get(),
                line.c_str(),
                static_cast<DWORD>(line.size()),
                WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE))
            {
                ThrowSystem("A protected download header was rejected");
            }
        }
        if (partial)
        {
            auto const range = L"Range: bytes=" + std::to_wstring(*partial) + L"-";
            if (!WinHttpAddRequestHeaders(
                response.Request.get(), range.c_str(), static_cast<DWORD>(range.size()),
                WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE))
            {
                ThrowSystem("The download range could not be applied");
            }
            if (validator)
            {
                auto const ifRange = L"If-Range: " + *validator;
                if (!WinHttpAddRequestHeaders(
                    response.Request.get(), ifRange.c_str(), static_cast<DWORD>(ifRange.size()),
                    WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE))
                {
                    ThrowSystem("The download validator could not be applied");
                }
            }
        }
        if (!WinHttpSendRequest(
            response.Request.get(),
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0)
            || !WinHttpReceiveResponse(response.Request.get(), nullptr))
        {
            ThrowSystem("The download response could not be received");
        }
        DWORD status{};
        DWORD statusBytes = sizeof(status);
        if (!WinHttpQueryHeaders(
            response.Request.get(),
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &statusBytes,
            WINHTTP_NO_HEADER_INDEX))
        {
            ThrowSystem("The download response status could not be read");
        }
        response.Status = status;
        response.ETag = QueryHeader(response.Request.get(), L"ETag");
        response.LastModified = QueryHeader(response.Request.get(), L"Last-Modified");
        response.Location = QueryHeader(response.Request.get(), L"Location");
        response.ContentRange = QueryHeader(response.Request.get(), L"Content-Range");
        if (auto const length = QueryHeader(response.Request.get(), L"Content-Length"))
        {
            response.ContentLength = ParseUnsigned(*length);
        }
        return response;
    }

    bool IsRedirectStatus(std::uint32_t status) noexcept
    {
        return status == 301 || status == 302 || status == 303
            || status == 307 || status == 308;
    }

    bool SameOrigin(
        winrt::Windows::Foundation::Uri const& left,
        winrt::Windows::Foundation::Uri const& right) noexcept
    {
        return _wcsicmp(left.SchemeName().c_str(), right.SchemeName().c_str()) == 0
            && _wcsicmp(left.Host().c_str(), right.Host().c_str()) == 0
            && left.Port() == right.Port();
    }

    HttpResponse OpenGet(
        std::wstring const& url,
        std::map<std::wstring, std::wstring, std::less<>> const& headers,
        std::optional<std::uint64_t> partial,
        std::optional<std::wstring> const& validator)
    {
        auto currentUrl = url;
        std::map<std::wstring, std::wstring, std::less<>> const emptyHeaders;
        bool forwardProtectedHeaders = true;
        for (int redirectCount = 0;; ++redirectCount)
        {
            auto response = OpenGetOnce(
                currentUrl,
                forwardProtectedHeaders ? headers : emptyHeaders,
                partial,
                validator);
            if (!IsRedirectStatus(response.Status))
            {
                return response;
            }
            if (redirectCount >= MaximumRedirects || !response.Location
                || response.Location->empty() || response.Location->size() > 32768
                || response.Location->find(L'\0') != std::wstring::npos)
            {
                throw RedirectError{};
            }

            try
            {
                winrt::Windows::Foundation::Uri const current{ currentUrl };
                auto const next = current.CombineUri(*response.Location);
                auto const nextScheme = next.SchemeName();
                if (next.Host().empty()
                    || (_wcsicmp(nextScheme.c_str(), L"http") != 0
                        && _wcsicmp(nextScheme.c_str(), L"https") != 0)
                    || (_wcsicmp(current.SchemeName().c_str(), L"https") == 0
                        && _wcsicmp(nextScheme.c_str(), L"https") != 0))
                {
                    throw RedirectError{};
                }
                if (!SameOrigin(current, next))
                {
                    forwardProtectedHeaders = false;
                }
                currentUrl = next.AbsoluteUri();
            }
            catch (RedirectError const&)
            {
                throw;
            }
            catch (...)
            {
                throw RedirectError{};
            }
        }
    }

    std::optional<std::uint64_t> DiskFree(std::filesystem::path const& path) noexcept
    {
        ULARGE_INTEGER available{};
        if (!GetDiskFreeSpaceExW(path.c_str(), &available, nullptr, nullptr))
        {
            return std::nullopt;
        }
        return available.QuadPart;
    }

    void RemoveFileIfPresent(std::filesystem::path const& path)
    {
        std::error_code error;
        auto const removed = std::filesystem::remove(path, error);
        if (error)
        {
            throw std::system_error{ error, "Could not remove a download file" };
        }
        if (!removed && std::filesystem::exists(path, error))
        {
            throw std::runtime_error{ "A download file could not be removed." };
        }
        if (error)
        {
            throw std::system_error{ error, "Could not verify download file removal" };
        }
    }

    std::optional<bool> PathsMatch(
        std::filesystem::path const& left,
        std::filesystem::path const& right) noexcept
    {
        try
        {
            auto const normalizedLeft = std::filesystem::absolute(left).lexically_normal().wstring();
            auto const normalizedRight = std::filesystem::absolute(right).lexically_normal().wstring();
            if (normalizedLeft.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())
                || normalizedRight.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            {
                return std::nullopt;
            }
            auto const comparison = CompareStringOrdinal(
                normalizedLeft.c_str(),
                static_cast<int>(normalizedLeft.size()),
                normalizedRight.c_str(),
                static_cast<int>(normalizedRight.size()),
                TRUE);
            if (comparison == 0)
            {
                return std::nullopt;
            }
            return comparison == CSTR_EQUAL;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool RecordMayOwnPath(
        HaloDesktop::Services::Downloads::DownloadRecord const& record,
        std::filesystem::path const& path)
    {
        auto matchesPath = [&path](std::filesystem::path const& candidate)
        {
            auto const matches = PathsMatch(candidate, path);
            if (!matches)
            {
                throw std::runtime_error{ "A download path could not be compared safely." };
            }
            return *matches;
        };
        if (matchesPath(record.TargetPath()) || matchesPath(record.PartialPath()))
        {
            return true;
        }
        return record.SubtitleFileName
            && matchesPath(record.RootPath / *record.SubtitleFileName);
    }

    void RemoveRecordFiles(
        HaloDesktop::Services::Downloads::DownloadRecord const& record,
        HaloDesktop::Services::Downloads::DownloadRecord const* preservedOwner = nullptr)
    {
        auto removeUnlessPreserved = [preservedOwner](std::filesystem::path const& path)
        {
            if (!preservedOwner || !RecordMayOwnPath(*preservedOwner, path))
            {
                RemoveFileIfPresent(path);
            }
        };
        removeUnlessPreserved(record.PartialPath());
        if (record.SubtitleFileName)
        {
            removeUnlessPreserved(record.RootPath / *record.SubtitleFileName);
        }
        // Delete the playable file last. If an auxiliary cleanup fails, the
        // indexed download still has its primary content and can be retried.
        removeUnlessPreserved(record.TargetPath());
    }

    std::wstring SubtitleExtension(std::wstring const& url)
    {
        auto end = url.find_first_of(L"?#");
        auto path = std::filesystem::path{ url.substr(0, end) };
        auto extension = path.extension().wstring();
        if (!extension.empty() && extension.front() == L'.')
        {
            extension.erase(extension.begin());
        }
        std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(std::towlower(character));
        });
        if (extension != L"srt" && extension != L"vtt"
            && extension != L"ass" && extension != L"ssa")
        {
            return L"srt";
        }
        return extension;
    }

    std::wstring SafeLanguage(std::wstring language)
    {
        std::replace_if(language.begin(), language.end(), [](wchar_t character)
        {
            return !(character >= L'a' && character <= L'z')
                && !(character >= L'A' && character <= L'Z')
                && !(character >= L'0' && character <= L'9')
                && character != L'-'
                && character != L'_';
        }, L'_');
        if (language.empty()) language = L"und";
        if (language.size() > 24) language.resize(24);
        return language;
    }
}

namespace HaloDesktop::Services::Downloads
{
    struct TransferEngine::TransferResult final
    {
        std::uint64_t Bytes{};
        std::uint64_t Total{};
        std::optional<std::wstring> Validator;
        std::optional<std::pair<std::wstring, std::wstring>> Subtitle;
    };

    struct TransferEngine::TransferError final
    {
        enum class Kind
        {
            Permanent,
            Retryable,
            Paused,
            Shutdown,
        };

        Kind Type{ Kind::Permanent };
        DownloadFailureCode Failure{ DownloadFailureCode::Unknown };
    };

    TransferEngine::TransferEngine(std::filesystem::path dataRoot)
        : m_store(std::move(dataRoot)),
          m_vault(m_store.Paths().VaultDirectory)
    {
        for (auto& record : m_store.Load())
        {
            m_records.emplace(record.JobId, std::move(record));
        }
        std::vector<std::wstring> pendingDeletionIds;
        for (auto const& [jobId, record] : m_records)
        {
            if (record.PendingDeletion) pendingDeletionIds.push_back(jobId);
        }
        for (auto const& jobId : pendingDeletionIds)
        {
            try
            {
                auto const pending = m_records.at(jobId);
                auto owner = std::find_if(m_records.begin(), m_records.end(), [&jobId](auto const& pair)
                {
                    return pair.second.Replacement && pair.second.Replacement->JobId == jobId;
                });
                auto updatedOwner = owner == m_records.end()
                    ? std::optional<DownloadRecord>{}
                    : std::optional<DownloadRecord>{ owner->second };
                if (updatedOwner)
                {
                    // A pending backup is written only after the replacement file
                    // has completed. Recover that finalization without downloading
                    // the already-finished replacement a second time.
                    updatedOwner->Status = DownloadStatus::Done;
                    updatedOwner->ExplicitPause = false;
                    updatedOwner->BytesPerSecond = 0;
                    updatedOwner->Replacement.reset();
                }
                RemoveRecordFiles(pending, updatedOwner ? &*updatedOwner : nullptr);
                m_vault.Remove(jobId);
                m_store.Apply(updatedOwner ? std::vector<DownloadRecord>{ *updatedOwner }
                                           : std::vector<DownloadRecord>{},
                    { jobId });
                if (updatedOwner) owner->second = std::move(*updatedOwner);
                m_records.erase(jobId);
            }
            catch (...)
            {
            }
        }
        m_worker = std::jthread{ [this](std::stop_token stopToken)
        {
            Worker(stopToken);
        } };
    }

    TransferEngine::~TransferEngine()
    {
        m_worker.request_stop();
        {
            std::scoped_lock const lock{ m_mutex };
            for (auto const& [jobId, flag] : m_cancel)
            {
                static_cast<void>(jobId);
                flag->store(true, std::memory_order_release);
            }
        }
        m_condition.notify_all();
        if (m_worker.joinable())
        {
            m_worker.join();
        }
    }

    void TransferEngine::SetAccount(std::wstring serverUrl, std::wstring userId)
    {
        auto const key = MakeAccountKey(std::move(serverUrl), userId);
        std::vector<std::wstring> pauseIds;
        {
            std::scoped_lock const lock{ m_mutex };
            if (m_activeAccount && *m_activeAccount != key)
            {
                for (auto const& [jobId, record] : m_records)
                {
                    if (record.AccountKey == *m_activeAccount && IsActive(record.Status))
                    {
                        pauseIds.push_back(jobId);
                    }
                }
            }
        }
        std::exception_ptr pauseFailure;
        for (auto const& jobId : pauseIds)
        {
            try
            {
                Pause(jobId);
            }
            catch (...)
            {
                if (!pauseFailure) pauseFailure = std::current_exception();
            }
        }

        auto latest = m_store.Load(false);
        {
            std::scoped_lock const lock{ m_mutex };
            std::optional<DownloadRecord> activeRecord;
            if (m_activeJob)
            {
                if (auto const active = m_records.find(*m_activeJob); active != m_records.end())
                {
                    activeRecord = active->second;
                }
            }
            m_records.clear();
            for (auto& record : latest)
            {
                m_records.emplace(record.JobId, std::move(record));
            }
            if (activeRecord)
            {
                // The job lease prevents another process from changing this row
                // while our worker owns it. Keep the live progress state instead
                // of downgrading the durable "downloading" marker to queued.
                m_records.insert_or_assign(activeRecord->JobId, std::move(*activeRecord));
            }
            m_activeAccount = key;
            m_queue.clear();
            std::vector<std::reference_wrapper<DownloadRecord const>> queued;
            for (auto& [jobId, record] : m_records)
            {
                static_cast<void>(jobId);
                if (record.AccountKey == key
                    && (!m_activeJob || record.JobId != *m_activeJob)
                    && record.Status == DownloadStatus::Downloading)
                {
                    record.Status = DownloadStatus::Queued;
                    record.BytesPerSecond = 0;
                }
                if (record.AccountKey == key
                    && record.Status == DownloadStatus::Queued
                    && !record.ExplicitPause
                    && !record.PendingDeletion
                    && !IsHiddenBackupLocked(record.JobId))
                {
                    queued.emplace_back(record);
                }
            }
            std::sort(queued.begin(), queued.end(), [](auto const& left, auto const& right)
            {
                auto const& a = left.get();
                auto const& b = right.get();
                return std::tie(a.CreatedAt, a.JobId) < std::tie(b.CreatedAt, b.JobId);
            });
            for (auto const& record : queued)
            {
                m_queue.push_back(record.get().JobId);
            }
        }
        m_condition.notify_all();
        if (pauseFailure)
        {
            OutputDebugStringW(L"Halo could not pause every old-account download.\n");
        }
    }

    void TransferEngine::ClearAccount()
    {
        std::vector<std::wstring> pauseIds;
        {
            std::scoped_lock const lock{ m_mutex };
            if (!m_activeAccount)
            {
                return;
            }
            for (auto const& [jobId, record] : m_records)
            {
                if (record.AccountKey == *m_activeAccount && IsActive(record.Status))
                {
                    pauseIds.push_back(jobId);
                }
            }
        }
        std::exception_ptr pauseFailure;
        for (auto const& jobId : pauseIds)
        {
            try
            {
                Pause(jobId);
            }
            catch (...)
            {
                if (!pauseFailure) pauseFailure = std::current_exception();
            }
        }
        {
            std::scoped_lock const lock{ m_mutex };
            m_activeAccount.reset();
            m_queue.clear();
        }
        if (pauseFailure)
        {
            OutputDebugStringW(L"Halo could not pause every signed-out download.\n");
        }
    }

    std::vector<DownloadRecord> TransferEngine::List() const
    {
        std::scoped_lock const lock{ m_mutex };
        if (!m_activeAccount)
        {
            return {};
        }
        std::vector<DownloadRecord> result;
        for (auto const& [jobId, record] : m_records)
        {
            if (record.AccountKey == *m_activeAccount
                && !record.PendingDeletion
                && !IsHiddenBackupLocked(jobId))
            {
                result.push_back(record);
            }
        }
        std::sort(result.begin(), result.end(), [](DownloadRecord const& left, DownloadRecord const& right)
        {
            return std::tie(left.CreatedAt, left.JobId) < std::tie(right.CreatedAt, right.JobId);
        });
        return result;
    }

    DownloadRecord TransferEngine::Start(DownloadStartRequest request)
    {
        if (request.Media.VideoId.empty()
            || request.Media.VideoId.size() > 1024
            || request.Media.ItemId.empty()
            || request.Media.Title.empty())
        {
            throw std::invalid_argument{ "Complete media metadata is required for a download." };
        }
        ValidateProtectedRequest(request.Request);
        auto const fingerprint = Sha256Hex(request.Request.Url);

        std::optional<DownloadRecord> existing;
        std::filesystem::path directory;
        std::wstring account;
        {
            std::scoped_lock const lock{ m_mutex };
            if (!m_activeAccount)
            {
                throw std::runtime_error{ "Sign in before downloading." };
            }
            account = *m_activeAccount;
        }
        ::HaloDesktop::Storage::FileMutationLock const preparationLease{
            m_store.Paths().DataRoot / (L"download-start-" + Sha256Hex(account + L"\n" + request.Media.VideoId)) };
        auto latest = m_store.Load(false);
        std::set<std::wstring, std::less<>> latestIds;
        for (auto const& record : latest) latestIds.insert(record.JobId);
        {
            std::scoped_lock const lock{ m_mutex };
            if (!m_activeAccount || *m_activeAccount != account)
            {
                throw std::runtime_error{ "The active account changed while starting the download." };
            }
            for (auto& record : latest)
            {
                if (!m_activeJob || *m_activeJob != record.JobId)
                {
                    m_records.insert_or_assign(record.JobId, std::move(record));
                }
            }
            std::erase_if(m_records, [this, &latestIds](auto const& pair)
            {
                return !latestIds.contains(pair.first)
                    && (!m_activeJob || *m_activeJob != pair.first);
            });
            if (!m_pendingVideoIds.insert(request.Media.VideoId).second)
            {
                throw std::runtime_error{ "This video is already being prepared for download." };
            }
            existing = VisibleRecordForVideoLocked(request.Media.VideoId);
            directory = m_store.DownloadDirectory();
        }
        auto clearPending = wil::scope_exit([this, videoId = request.Media.VideoId]() noexcept
        {
            std::scoped_lock const lock{ m_mutex };
            m_pendingVideoIds.erase(videoId);
        });

        if (existing && existing->SourceFingerprint == fingerprint
            && existing->Status != DownloadStatus::Failed)
        {
            return *existing;
        }
        if (existing && existing->SourceFingerprint != fingerprint && !request.ReplaceExisting)
        {
            throw std::runtime_error{ "A different source is already saved for this video. Confirm replacement first." };
        }
        if (existing && existing->Status == DownloadStatus::Failed
            && existing->Failure && RequiresNewSource(*existing->Failure)
            && !request.ReplaceExisting)
        {
            throw std::runtime_error{ "Choose a source again and confirm replacement before retrying this download." };
        }
        if (existing && existing->Status == DownloadStatus::Failed
            && (!existing->Failure || !RequiresNewSource(*existing->Failure))
            && !request.ReplaceExisting)
        {
            Resume(existing->JobId);
            auto const records = List();
            auto const resumed = std::find_if(records.begin(), records.end(), [&existing](DownloadRecord const& record)
            {
                return record.JobId == existing->JobId;
            });
            if (resumed == records.end())
            {
                throw std::runtime_error{ "Download state changed. Try again." };
            }
            return *resumed;
        }
        std::unique_ptr<::HaloDesktop::Storage::FileMutationLock> replacementLease;
        std::optional<DownloadRecord> replacementRollback;
        bool replacementWasActive{};
        if (existing && request.ReplaceExisting)
        {
            replacementWasActive = IsActive(existing->Status);
            {
                std::scoped_lock const lock{ m_mutex };
                if (auto const flag = m_cancel.find(existing->JobId); flag != m_cancel.end())
                {
                    flag->second->store(true, std::memory_order_release);
                }
            }
            replacementLease = std::make_unique<::HaloDesktop::Storage::FileMutationLock>(
                m_store.Paths().DataRoot / (L"download-job-" + existing->JobId));
            latest = m_store.Load(false);
            auto const current = std::find_if(latest.begin(), latest.end(), [&existing](DownloadRecord const& record)
            {
                return record.JobId == existing->JobId;
            });
            if (current == latest.end())
            {
                throw std::runtime_error{ "The existing download changed while preparing its replacement." };
            }
            existing = *current;
            replacementRollback = existing;
            std::scoped_lock const lock{ m_mutex };
            m_records.insert_or_assign(existing->JobId, *existing);
        }
        if (!std::filesystem::is_directory(directory))
        {
            throw std::runtime_error{ "The download folder is unavailable. Choose another folder to continue." };
        }
        if (request.Media.VideoSize)
        {
            auto const free = DiskFree(directory);
            if (free && !HasSufficientSpace(*free, *request.Media.VideoSize))
            {
                throw std::runtime_error{ "There is not enough free space for this video." };
            }
        }

        auto const jobId = NewJobId();
        auto const fileName = MakeDownloadFileName(request.Media, fingerprint);
        if (!IsSafeFileName(fileName))
        {
            throw std::runtime_error{ "A safe download filename could not be created." };
        }
        auto const target = directory / fileName;
        if (std::filesystem::exists(target))
        {
            throw std::runtime_error{ "A file with this download name already exists." };
        }

        m_vault.Write(jobId, request.Request);
        auto removeVault = wil::scope_exit([this, &jobId]() noexcept
        {
            try { m_vault.Remove(jobId); } catch (...) {}
        });
        auto const now = NowMilliseconds();
        DownloadRecord record{
            .JobId = jobId,
            .AccountKey = account,
            .Media = std::move(request.Media),
            .FileName = fileName,
            .RootPath = directory,
            .Status = DownloadStatus::Queued,
            .SourceFingerprint = fingerprint,
            .CreatedAt = now,
            .UpdatedAt = now,
        };
        if (existing && request.ReplaceExisting)
        {
            record.Replacement = ReplacementBackup{
                .JobId = existing->JobId,
                .RootPath = existing->RootPath,
                .FileName = existing->FileName,
                .SubtitleFileName = existing->SubtitleFileName,
            };
        }

        std::vector<DownloadRecord> snapshot;
        std::optional<DownloadRecord> previousExisting;
        std::uint64_t generation{};
        {
            std::scoped_lock const lock{ m_mutex };
            if (!m_activeAccount || *m_activeAccount != account)
            {
                throw std::runtime_error{ "The active account changed while starting the download." };
            }
            if (existing)
            {
                auto const found = m_records.find(existing->JobId);
                if (found == m_records.end()
                    || found->second.SourceFingerprint != existing->SourceFingerprint)
                {
                    throw std::runtime_error{ "Download state changed. Try again." };
                }
                if (request.ReplaceExisting && IsActive(found->second.Status))
                {
                    previousExisting = found->second;
                    found->second.Status = DownloadStatus::Paused;
                    found->second.ExplicitPause = true;
                    found->second.BytesPerSecond = 0;
                    if (auto const flag = m_cancel.find(found->second.JobId); flag != m_cancel.end())
                    {
                        flag->second->store(true, std::memory_order_release);
                    }
                    std::erase(m_queue, found->second.JobId);
                }
            }
            m_records.emplace(record.JobId, record);
            m_queue.push_back(record.JobId);
            generation = ++m_generation;
            snapshot = { record };
            if (previousExisting)
            {
                snapshot.push_back(m_records.at(previousExisting->JobId));
            }
        }
        try
        {
            Persist(std::move(snapshot), generation);
        }
        catch (...)
        {
            std::optional<DownloadRecord> restoredForPersistence;
            {
                std::scoped_lock const lock{ m_mutex };
                m_records.erase(record.JobId);
                std::erase(m_queue, record.JobId);
                auto rollback = replacementRollback
                    ? replacementRollback
                    : previousExisting;
                if (rollback)
                {
                    if (replacementWasActive
                        && (IsActive(rollback->Status)
                            || (rollback->Status == DownloadStatus::Paused
                                && !rollback->ExplicitPause)))
                    {
                        rollback->Status = DownloadStatus::Queued;
                        rollback->ExplicitPause = false;
                        rollback->BytesPerSecond = 0;
                        rollback->UpdatedAt = NowMilliseconds();
                        restoredForPersistence = rollback;
                    }
                    m_records.insert_or_assign(rollback->JobId, *rollback);
                    if (rollback->Status == DownloadStatus::Queued
                        && !rollback->ExplicitPause
                        && std::find(m_queue.begin(), m_queue.end(), rollback->JobId) == m_queue.end())
                    {
                        m_queue.push_front(rollback->JobId);
                    }
                }
            }
            if (restoredForPersistence)
            {
                try { m_store.Apply({ *restoredForPersistence }); } catch (...) {}
                m_condition.notify_all();
            }
            throw;
        }
        removeVault.release();
        std::vector<DownloadChangedHandler> handlers;
        {
            std::scoped_lock const lock{ m_mutex };
            handlers = HandlersLocked();
        }
        Notify(handlers, record);
        m_condition.notify_all();
        return record;
    }

    void TransferEngine::Pause(std::wstring const& jobId)
    {
        {
            std::scoped_lock const lock{ m_mutex };
            if (auto const flag = m_cancel.find(jobId); flag != m_cancel.end())
            {
                flag->second->store(true, std::memory_order_release);
            }
        }
        ::HaloDesktop::Storage::FileMutationLock const jobLease{
            m_store.Paths().DataRoot / (L"download-job-" + jobId) };
        auto latest = m_store.Load(false);
        auto const persisted = std::find_if(latest.begin(), latest.end(), [&jobId](DownloadRecord const& record)
        {
            return record.JobId == jobId;
        });
        if (persisted == latest.end())
        {
            std::scoped_lock const lock{ m_mutex };
            m_records.erase(jobId);
            std::erase(m_queue, jobId);
            return;
        }
        auto changed = *persisted;
        {
            std::scoped_lock const lock{ m_mutex };
            if (!m_activeAccount || changed.AccountKey != *m_activeAccount)
            {
                throw std::runtime_error{ "This download belongs to another account." };
            }
        }
        if (changed.Status == DownloadStatus::Done || changed.PendingDeletion)
        {
            std::scoped_lock const lock{ m_mutex };
            m_records.insert_or_assign(jobId, changed);
            std::erase(m_queue, jobId);
            return;
        }
        changed.Status = DownloadStatus::Paused;
        changed.ExplicitPause = true;
        changed.BytesPerSecond = 0;
        changed.UpdatedAt = NowMilliseconds();
        m_store.Apply({ changed });
        std::vector<DownloadChangedHandler> handlers;
        {
            std::scoped_lock const lock{ m_mutex };
            m_records.insert_or_assign(jobId, changed);
            std::erase(m_queue, jobId);
            handlers = HandlersLocked();
        }
        Notify(handlers, changed);
    }

    void TransferEngine::Resume(std::wstring const& jobId)
    {
        ::HaloDesktop::Storage::FileMutationLock const jobLease{
            m_store.Paths().DataRoot / (L"download-job-" + jobId) };
        auto latest = m_store.Load(false);
        auto const persisted = std::find_if(latest.begin(), latest.end(), [&jobId](DownloadRecord const& record)
        {
            return record.JobId == jobId;
        });
        if (persisted == latest.end())
        {
            std::scoped_lock const lock{ m_mutex };
            m_records.erase(jobId);
            std::erase(m_queue, jobId);
            return;
        }
        auto changed = *persisted;
        if (changed.Failure && RequiresNewSource(*changed.Failure))
        {
            throw std::runtime_error{ "Choose the source again before retrying this download." };
        }
        {
            std::scoped_lock const lock{ m_mutex };
            if (!m_activeAccount || changed.AccountKey != *m_activeAccount)
            {
                throw std::runtime_error{ "This download belongs to another account." };
            }
        }
        if (changed.Status == DownloadStatus::Done || changed.PendingDeletion)
        {
            std::scoped_lock const lock{ m_mutex };
            m_records.insert_or_assign(jobId, changed);
            std::erase(m_queue, jobId);
            return;
        }
        changed.Status = DownloadStatus::Queued;
        changed.ExplicitPause = false;
        changed.Failure.reset();
        changed.BytesPerSecond = 0;
        changed.UpdatedAt = NowMilliseconds();
        m_store.Apply({ changed });
        std::vector<DownloadChangedHandler> handlers;
        {
            std::scoped_lock const lock{ m_mutex };
            m_records.insert_or_assign(jobId, changed);
            if (std::find(m_queue.begin(), m_queue.end(), jobId) == m_queue.end())
            {
                m_queue.push_back(jobId);
            }
            handlers = HandlersLocked();
        }
        Notify(handlers, changed);
        m_condition.notify_all();
    }

    void TransferEngine::Remove(std::wstring const& jobId)
    {
        {
            std::scoped_lock const lock{ m_mutex };
            if (auto const flag = m_cancel.find(jobId); flag != m_cancel.end())
            {
                flag->second->store(true, std::memory_order_release);
            }
        }
        ::HaloDesktop::Storage::FileMutationLock const jobLease{
            m_store.Paths().DataRoot / (L"download-job-" + jobId) };
        auto latest = m_store.Load(false);
        if (std::any_of(latest.begin(), latest.end(), [&jobId](DownloadRecord const& record)
            {
                return record.Replacement && record.Replacement->JobId == jobId;
            }))
        {
            throw std::runtime_error{ "The previous file is retained until its replacement finishes." };
        }
        auto const persisted = std::find_if(latest.begin(), latest.end(), [&jobId](DownloadRecord const& record)
        {
            return record.JobId == jobId;
        });
        if (persisted == latest.end())
        {
            std::scoped_lock const lock{ m_mutex };
            m_records.erase(jobId);
            std::erase(m_queue, jobId);
            return;
        }
        auto removed = *persisted;
        {
            std::scoped_lock const lock{ m_mutex };
            if (!m_activeAccount || removed.AccountKey != *m_activeAccount)
            {
                throw std::runtime_error{ "This download belongs to another account." };
            }
        }
        removed.PendingDeletion = true;
        removed.Status = DownloadStatus::Paused;
        removed.ExplicitPause = true;
        removed.BytesPerSecond = 0;
        removed.UpdatedAt = NowMilliseconds();
        m_store.Apply({ removed });
        {
            std::scoped_lock const lock{ m_mutex };
            m_records.insert_or_assign(jobId, removed);
            std::erase(m_queue, jobId);
        }
        try
        {
            m_vault.Remove(jobId);
            RemoveRecordFiles(removed);
            m_store.Apply({}, { jobId });
            std::scoped_lock const lock{ m_mutex };
            m_records.erase(jobId);
        }
        catch (...)
        {
            throw;
        }
    }

    PlaybackFiles TransferEngine::FilesForPlayback(std::wstring const& jobId) const
    {
        std::scoped_lock const lock{ m_mutex };
        auto const found = m_records.find(jobId);
        if (found == m_records.end()
            || !m_activeAccount
            || found->second.AccountKey != *m_activeAccount
            || found->second.Status != DownloadStatus::Done
            || found->second.PendingDeletion
            || !std::filesystem::is_regular_file(found->second.TargetPath()))
        {
            throw std::runtime_error{ "This download is no longer on the device." };
        }
        PlaybackFiles result{ .VideoPath = found->second.TargetPath() };
        if (found->second.SubtitleFileName)
        {
            auto const subtitle = found->second.RootPath / *found->second.SubtitleFileName;
            if (std::filesystem::is_regular_file(subtitle))
            {
                result.SubtitlePath = subtitle;
            }
        }
        return result;
    }

    std::filesystem::path TransferEngine::DownloadDirectory() const
    {
        return m_store.DownloadDirectory();
    }

    void TransferEngine::SetDownloadDirectory(std::filesystem::path directory)
    {
        m_store.SetDownloadDirectory(std::move(directory));
    }

    std::optional<std::uint64_t> TransferEngine::FreeBytes() const noexcept
    {
        try
        {
            return DiskFree(m_store.DownloadDirectory());
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    DownloadChangedToken TransferEngine::AddChangedHandler(DownloadChangedHandler handler)
    {
        if (!handler)
        {
            throw std::invalid_argument{ "A download changed handler is required." };
        }
        std::scoped_lock const lock{ m_mutex };
        auto const token = m_nextHandlerToken++;
        m_handlers.emplace(token, std::move(handler));
        return token;
    }

    void TransferEngine::RemoveChangedHandler(DownloadChangedToken token) noexcept
    {
        std::scoped_lock const lock{ m_mutex };
        m_handlers.erase(token);
    }

    void TransferEngine::Worker(std::stop_token stopToken)
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        while (!stopToken.stop_requested())
        {
            std::wstring jobId;
            std::unique_ptr<::HaloDesktop::Storage::FileMutationLock> jobLease;
            std::shared_ptr<std::atomic_bool> cancel;
            std::vector<DownloadRecord> snapshot;
            std::uint64_t generation{};
            {
                std::unique_lock lock{ m_mutex };
                m_condition.wait(lock, stopToken, [this]
                {
                    return !m_activeJob && m_activeAccount && !m_queue.empty();
                });
                if (stopToken.stop_requested())
                {
                    break;
                }
                while (!m_queue.empty())
                {
                    jobId = std::move(m_queue.front());
                    m_queue.pop_front();
                    auto const found = m_records.find(jobId);
                    if (found != m_records.end()
                        && m_activeAccount
                        && found->second.AccountKey == *m_activeAccount
                        && found->second.Status == DownloadStatus::Queued
                        && !found->second.PendingDeletion
                        && !found->second.ExplicitPause)
                    {
                        break;
                    }
                    jobId.clear();
                }
                if (jobId.empty())
                {
                    continue;
                }
            }
            try
            {
                jobLease = std::make_unique<::HaloDesktop::Storage::FileMutationLock>(
                    m_store.Paths().DataRoot / (L"download-job-" + jobId),
                    std::chrono::milliseconds{ 0 });
            }
            catch (...)
            {
                {
                    std::scoped_lock const lock{ m_mutex };
                    auto const found = m_records.find(jobId);
                    if (found != m_records.end()
                        && m_activeAccount
                        && found->second.AccountKey == *m_activeAccount
                        && found->second.Status == DownloadStatus::Queued
                        && !found->second.ExplicitPause)
                    {
                        m_queue.push_back(jobId);
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{ 100 });
                continue;
            }
            auto latest = m_store.Load(false);
            {
                std::scoped_lock const lock{ m_mutex };
                auto const persisted = std::find_if(latest.begin(), latest.end(), [&jobId](DownloadRecord const& record)
                {
                    return record.JobId == jobId;
                });
                if (persisted == latest.end())
                {
                    m_records.erase(jobId);
                    continue;
                }
                auto refreshed = *persisted;
                if (refreshed.Status == DownloadStatus::Downloading)
                {
                    // Acquiring the lease proves no live worker owns this job.
                    // A persisted downloading state therefore came from an
                    // interrupted or exited process and is safe to resume.
                    refreshed.Status = DownloadStatus::Queued;
                    refreshed.BytesPerSecond = 0;
                }
                m_records.insert_or_assign(jobId, std::move(refreshed));
                auto const found = m_records.find(jobId);
                if (m_activeJob
                    || found == m_records.end()
                    || !m_activeAccount
                    || found->second.AccountKey != *m_activeAccount
                    || found->second.Status != DownloadStatus::Queued
                    || found->second.ExplicitPause
                    || found->second.PendingDeletion)
                {
                    continue;
                }
                cancel = std::make_shared<std::atomic_bool>(false);
                m_activeJob = jobId;
                m_cancel[jobId] = cancel;
                auto& record = m_records.at(jobId);
                record.Status = DownloadStatus::Downloading;
                record.UpdatedAt = NowMilliseconds();
                generation = ++m_generation;
                snapshot = { record };
            }
            try
            {
                Persist(std::move(snapshot), generation);
                try
                {
                    auto result = TransferWithRetries(jobId, cancel, stopToken);
                    Finish(jobId, std::move(result), std::nullopt);
                }
                catch (TransferError const& error)
                {
                    Finish(jobId, std::nullopt, error);
                }
                catch (...)
                {
                    Finish(jobId, std::nullopt, TransferError{
                        .Type = stopToken.stop_requested()
                            ? TransferError::Kind::Shutdown
                            : TransferError::Kind::Permanent,
                        .Failure = DownloadFailureCode::Unknown,
                    });
                }
            }
            catch (...)
            {
                DownloadRecord changed;
                bool hasChanged{};
                std::vector<DownloadRecord> recoverySnapshot;
                std::vector<DownloadChangedHandler> handlers;
                std::uint64_t recoveryGeneration{};
                {
                    std::scoped_lock const lock{ m_mutex };
                    if (auto const found = m_records.find(jobId); found != m_records.end())
                    {
                        found->second.BytesPerSecond = 0;
                        found->second.UpdatedAt = NowMilliseconds();
                        if (!stopToken.stop_requested()
                            && found->second.Status == DownloadStatus::Downloading)
                        {
                            found->second.Status = DownloadStatus::Failed;
                            found->second.Failure = DownloadFailureCode::Unknown;
                        }
                        changed = found->second;
                        hasChanged = true;
                    }
                    m_cancel.erase(jobId);
                    m_activeJob.reset();
                    recoveryGeneration = ++m_generation;
                    if (hasChanged)
                    {
                        recoverySnapshot = { changed };
                    }
                    handlers = HandlersLocked();
                }
                try { Persist(std::move(recoverySnapshot), recoveryGeneration); } catch (...) {}
                if (hasChanged) Notify(handlers, changed);
                m_condition.notify_all();
            }
        }
        winrt::uninit_apartment();
    }

    TransferEngine::TransferResult TransferEngine::TransferWithRetries(
        std::wstring const& jobId,
        std::shared_ptr<std::atomic_bool> const& cancel,
        std::stop_token stopToken)
    {
        ProtectedRequest request;
        try
        {
            request = m_vault.Read(jobId);
        }
        catch (...)
        {
            throw TransferError{
                .Type = TransferError::Kind::Permanent,
                .Failure = DownloadFailureCode::ProtectedRequestCorrupt,
            };
        }
        for (int attempt = 0; attempt < TransferAttempts; ++attempt)
        {
            try
            {
                return TransferOnce(jobId, request, cancel, stopToken);
            }
            catch (TransferError const& error)
            {
                if (error.Type != TransferError::Kind::Retryable || attempt + 1 == TransferAttempts)
                {
                    throw;
                }
                auto const delay = std::chrono::milliseconds{ 250 * (1 << attempt) };
                auto elapsed = std::chrono::milliseconds::zero();
                while (elapsed < delay)
                {
                    if (stopToken.stop_requested())
                    {
                        throw TransferError{ .Type = TransferError::Kind::Shutdown };
                    }
                    if (cancel->load(std::memory_order_acquire))
                    {
                        throw TransferError{ .Type = TransferError::Kind::Paused };
                    }
                    auto const slice = (std::min)(std::chrono::milliseconds{ 25 }, delay - elapsed);
                    std::this_thread::sleep_for(slice);
                    elapsed += slice;
                }
            }
        }
        throw TransferError{
            .Type = TransferError::Kind::Retryable,
            .Failure = DownloadFailureCode::Network,
        };
    }

    TransferEngine::TransferResult TransferEngine::TransferOnce(
        std::wstring const& jobId,
        ProtectedRequest const& request,
        std::shared_ptr<std::atomic_bool> const& cancel,
        std::stop_token stopToken)
    {
        DownloadRecord record;
        {
            std::scoped_lock const lock{ m_mutex };
            auto const found = m_records.find(jobId);
            if (found == m_records.end())
            {
                throw TransferError{ .Type = TransferError::Kind::Permanent };
            }
            record = found->second;
        }
        std::error_code sizeError;
        auto partial = std::filesystem::file_size(record.PartialPath(), sizeError);
        if (sizeError)
        {
            partial = 0;
        }
        if (record.TotalBytes > 0 && partial > record.TotalBytes)
        {
            throw TransferError{
                .Type = TransferError::Kind::Permanent,
                .Failure = DownloadFailureCode::InvalidRange,
            };
        }
        if (partial > 0 && record.TotalBytes > 0 && partial == record.TotalBytes)
        {
            if (!MoveFileExW(
                record.PartialPath().c_str(),
                record.TargetPath().c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                throw TransferError{
                    .Type = TransferError::Kind::Permanent,
                    .Failure = DownloadFailureCode::StorageFull,
                };
            }
            return TransferResult{
                .Bytes = partial,
                .Total = record.TotalBytes,
                .Validator = record.Validator,
            };
        }
        if (partial > 0 && !record.Validator)
        {
            partial = 0;
        }
        std::optional<std::uint64_t> requestedPartial;
        if (partial > 0)
        {
            requestedPartial = partial;
        }

        HttpResponse response;
        try
        {
            response = OpenGet(request.Url, request.Headers, requestedPartial, record.Validator);
        }
        catch (RedirectError const&)
        {
            throw TransferError{
                .Type = TransferError::Kind::Permanent,
                .Failure = DownloadFailureCode::SourceRejected,
            };
        }
        catch (...)
        {
            throw TransferError{
                .Type = TransferError::Kind::Retryable,
                .Failure = DownloadFailureCode::Network,
            };
        }
        if (response.Status == 401 || response.Status == 403)
        {
            throw TransferError{
                .Type = TransferError::Kind::Permanent,
                .Failure = DownloadFailureCode::SourceExpired,
            };
        }
        if (response.Status == 416)
        {
            throw TransferError{
                .Type = TransferError::Kind::Permanent,
                .Failure = DownloadFailureCode::InvalidRange,
            };
        }
        if (response.Status == 408 || response.Status == 425 || response.Status == 429
            || (response.Status >= 500 && response.Status <= 599))
        {
            throw TransferError{
                .Type = TransferError::Kind::Retryable,
                .Failure = DownloadFailureCode::ServerUnavailable,
            };
        }
        if (response.Status != 200 && response.Status != 206)
        {
            throw TransferError{
                .Type = TransferError::Kind::Permanent,
                .Failure = DownloadFailureCode::SourceRejected,
            };
        }
        if (partial > 0 && response.Status == 200)
        {
            partial = 0;
        }
        if (partial > 0 && response.Status != 206)
        {
            throw TransferError{
                .Type = TransferError::Kind::Permanent,
                .Failure = DownloadFailureCode::InvalidRange,
            };
        }

        std::optional<ContentRange> contentRange;
        if (response.Status == 206)
        {
            contentRange = response.ContentRange ? ParseContentRange(*response.ContentRange) : std::nullopt;
            if (!contentRange
                || contentRange->Start != partial
                || (record.TotalBytes > 0 && contentRange->Total != record.TotalBytes)
                || (response.ContentLength
                    && *response.ContentLength != contentRange->End - contentRange->Start + 1))
            {
                throw TransferError{
                    .Type = TransferError::Kind::Permanent,
                    .Failure = DownloadFailureCode::InvalidRange,
                };
            }
        }
        auto const responseValidator = response.ETag ? response.ETag : response.LastModified;
        if (partial > 0 && record.Validator && responseValidator
            && *record.Validator != *responseValidator)
        {
            throw TransferError{
                .Type = TransferError::Kind::Permanent,
                .Failure = DownloadFailureCode::InvalidRange,
            };
        }
        auto const total = contentRange
            ? contentRange->Total
            : response.ContentLength.value_or(record.Media.VideoSize.value_or(0));
        UpdateResponseMetadata(jobId, responseValidator, total);

        std::error_code directoryError;
        std::filesystem::create_directories(record.RootPath, directoryError);
        if (directoryError)
        {
            throw TransferError{
                .Type = TransferError::Kind::Permanent,
                .Failure = DownloadFailureCode::StorageFull,
            };
        }
        if (auto const free = DiskFree(record.RootPath))
        {
            auto const remaining = total > partial ? total - partial : 0;
            if (!HasSufficientSpace(*free, remaining))
            {
                throw TransferError{
                    .Type = TransferError::Kind::Permanent,
                    .Failure = DownloadFailureCode::StorageFull,
                };
            }
        }
        auto const disposition = partial == 0 ? CREATE_ALWAYS : OPEN_ALWAYS;
        wil::unique_hfile output{ CreateFileW(
            record.PartialPath().c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_DELETE,
            nullptr,
            disposition,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
            nullptr) };
        if (!output)
        {
            throw TransferError{
                .Type = TransferError::Kind::Permanent,
                .Failure = DownloadFailureCode::StorageFull,
            };
        }
        if (partial > 0)
        {
            LARGE_INTEGER distance{ .QuadPart = static_cast<LONGLONG>(partial) };
            if (!SetFilePointerEx(output.get(), distance, nullptr, FILE_BEGIN))
            {
                throw TransferError{
                    .Type = TransferError::Kind::Permanent,
                    .Failure = DownloadFailureCode::StorageFull,
                };
            }
        }

        std::array<std::uint8_t, TransferChunkSize> buffer{};
        auto written = partial;
        auto lastBytes = written;
        auto lastProgress = std::chrono::steady_clock::now();
        for (;;)
        {
            if (stopToken.stop_requested())
            {
                throw TransferError{ .Type = TransferError::Kind::Shutdown };
            }
            if (cancel->load(std::memory_order_acquire))
            {
                throw TransferError{ .Type = TransferError::Kind::Paused };
            }
            if (auto const free = DiskFree(record.RootPath))
            {
                auto const remaining = total > written ? total - written : 0;
                if (!HasSufficientSpace(*free, remaining))
                {
                    throw TransferError{
                        .Type = TransferError::Kind::Permanent,
                        .Failure = DownloadFailureCode::StorageFull,
                    };
                }
            }
            DWORD read{};
            if (!WinHttpReadData(
                response.Request.get(),
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &read))
            {
                throw TransferError{
                    .Type = TransferError::Kind::Retryable,
                    .Failure = DownloadFailureCode::Network,
                };
            }
            if (read == 0)
            {
                break;
            }
            // Chunked responses do not reveal their final size. Recheck after
            // receiving this chunk, before committing it to disk, so an
            // unknown-size transfer cannot consume the 64 MiB safety reserve.
            if (auto const free = DiskFree(record.RootPath);
                free && !HasSufficientSpace(*free, read))
            {
                throw TransferError{
                    .Type = TransferError::Kind::Permanent,
                    .Failure = DownloadFailureCode::StorageFull,
                };
            }
            if (stopToken.stop_requested())
            {
                throw TransferError{ .Type = TransferError::Kind::Shutdown };
            }
            if (cancel->load(std::memory_order_acquire))
            {
                throw TransferError{ .Type = TransferError::Kind::Paused };
            }
            DWORD chunkWritten{};
            if (!WriteFile(output.get(), buffer.data(), read, &chunkWritten, nullptr)
                || chunkWritten != read)
            {
                throw TransferError{
                    .Type = TransferError::Kind::Permanent,
                    .Failure = DownloadFailureCode::StorageFull,
                };
            }
            written += chunkWritten;
            auto const now = std::chrono::steady_clock::now();
            auto const elapsed = now - lastProgress;
            if (elapsed >= ProgressInterval)
            {
                if (!FlushFileBuffers(output.get()))
                {
                    throw TransferError{
                        .Type = TransferError::Kind::Permanent,
                        .Failure = DownloadFailureCode::StorageFull,
                    };
                }
                auto const seconds = std::chrono::duration<double>(elapsed).count();
                auto const rate = static_cast<std::uint64_t>(
                    static_cast<double>(written - lastBytes) / (std::max)(0.001, seconds));
                UpdateProgress(jobId, written, total, rate);
                lastProgress = now;
                lastBytes = written;
            }
        }
        if (stopToken.stop_requested())
        {
            throw TransferError{ .Type = TransferError::Kind::Shutdown };
        }
        if (cancel->load(std::memory_order_acquire))
        {
            throw TransferError{ .Type = TransferError::Kind::Paused };
        }
        if (!FlushFileBuffers(output.get()))
        {
            throw TransferError{
                .Type = TransferError::Kind::Permanent,
                .Failure = DownloadFailureCode::StorageFull,
            };
        }
        output.reset();
        if (total > 0 && written < total)
        {
            throw TransferError{
                .Type = TransferError::Kind::Retryable,
                .Failure = DownloadFailureCode::Network,
            };
        }
        if (total > 0 && written > total)
        {
            throw TransferError{
                .Type = TransferError::Kind::Permanent,
                .Failure = DownloadFailureCode::InvalidRange,
            };
        }
        if (!MoveFileExW(
            record.PartialPath().c_str(),
            record.TargetPath().c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            throw TransferError{
                .Type = TransferError::Kind::Permanent,
                .Failure = DownloadFailureCode::StorageFull,
            };
        }
        std::optional<std::pair<std::wstring, std::wstring>> subtitle;
        if (request.Subtitle && !stopToken.stop_requested()
            && !cancel->load(std::memory_order_acquire))
        {
            subtitle = DownloadSubtitle(*request.Subtitle, record.TargetPath(), cancel, stopToken);
        }
        return TransferResult{
            .Bytes = written,
            .Total = (std::max)(total, written),
            .Validator = responseValidator,
            .Subtitle = std::move(subtitle),
        };
    }

    std::optional<std::pair<std::wstring, std::wstring>> TransferEngine::DownloadSubtitle(
        SubtitleRequest const& request,
        std::filesystem::path const& target,
        std::shared_ptr<std::atomic_bool> const& cancel,
        std::stop_token stopToken) const noexcept
    {
        auto language = SafeLanguage(request.Language);
        auto name = target.stem().wstring() + L"." + language + L"." + SubtitleExtension(request.Url);
        auto path = target.parent_path() / name;
        auto temporary = path;
        temporary += L".part";
        auto cleanup = wil::scope_exit([&temporary]() noexcept
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
        });
        try
        {
            auto response = OpenGet(request.Url, request.Headers, std::nullopt, std::nullopt);
            if (response.Status != 200 || (response.ContentLength && *response.ContentLength > MaximumSubtitleBytes))
            {
                return std::nullopt;
            }
            wil::unique_hfile output{ CreateFileW(
                temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr) };
            if (!output)
            {
                return std::nullopt;
            }
            std::array<std::uint8_t, TransferChunkSize> buffer{};
            std::uint64_t written{};
            for (;;)
            {
                if (stopToken.stop_requested() || cancel->load(std::memory_order_acquire))
                {
                    return std::nullopt;
                }
                DWORD read{};
                if (!WinHttpReadData(response.Request.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read))
                {
                    return std::nullopt;
                }
                if (read == 0) break;
                if (written + read > MaximumSubtitleBytes) return std::nullopt;
                DWORD chunkWritten{};
                if (!WriteFile(output.get(), buffer.data(), read, &chunkWritten, nullptr)
                    || chunkWritten != read)
                {
                    return std::nullopt;
                }
                written += chunkWritten;
            }
            if (!FlushFileBuffers(output.get())) return std::nullopt;
            output.reset();
            if (!MoveFileExW(
                temporary.c_str(), path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                return std::nullopt;
            }
            cleanup.release();
            return std::pair{ name, language };
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    void TransferEngine::Finish(
        std::wstring const& jobId,
        std::optional<TransferResult> result,
        std::optional<TransferError> error)
    {
        DownloadRecord changed;
        bool hasChanged{};
        std::optional<DownloadRecord> replaced;
        std::optional<DownloadRecord> originalCurrent;
        std::optional<DownloadRecord> originalReplaced;
        std::vector<DownloadRecord> snapshot;
        std::vector<DownloadChangedHandler> handlers;
        std::uint64_t generation{};
        {
            std::scoped_lock const lock{ m_mutex };
            auto const found = m_records.find(jobId);
            if (found != m_records.end())
            {
                auto& record = found->second;
                originalCurrent = record;
                if (result)
                {
                    record.Status = DownloadStatus::Done;
                    record.DownloadedBytes = result->Bytes;
                    record.TotalBytes = result->Total;
                    record.Validator = std::move(result->Validator);
                    record.BytesPerSecond = 0;
                    record.Failure.reset();
                    record.ExplicitPause = false;
                    if (result->Subtitle)
                    {
                        record.SubtitleFileName = result->Subtitle->first;
                        record.SubtitleLanguage = result->Subtitle->second;
                    }
                    if (record.Replacement)
                    {
                        if (auto const old = m_records.find(record.Replacement->JobId); old != m_records.end())
                        {
                            originalReplaced = old->second;
                            old->second.PendingDeletion = true;
                            old->second.Status = DownloadStatus::Paused;
                            old->second.ExplicitPause = true;
                            old->second.BytesPerSecond = 0;
                            old->second.UpdatedAt = NowMilliseconds();
                            replaced = old->second;
                        }
                    }
                }
                else if (error && error->Type == TransferError::Kind::Paused)
                {
                    record.Status = DownloadStatus::Paused;
                    record.BytesPerSecond = 0;
                }
                else if (error && error->Type == TransferError::Kind::Shutdown)
                {
                    record.BytesPerSecond = 0;
                }
                else
                {
                    record.Status = DownloadStatus::Failed;
                    record.Failure = error ? error->Failure : DownloadFailureCode::Unknown;
                    record.BytesPerSecond = 0;
                }
                record.UpdatedAt = NowMilliseconds();
                if (result && replaced)
                {
                    // Keep both the live and durable projections non-terminal until
                    // the old files have either been removed or deliberately retained
                    // behind their tombstone.
                    record.Status = DownloadStatus::Downloading;
                }
                changed = record;
                hasChanged = true;
            }
            m_cancel.erase(jobId);
            m_activeJob.reset();
            generation = ++m_generation;
            if (hasChanged)
            {
                snapshot = { changed };
                if (replaced)
                {
                    snapshot.push_back(*replaced);
                }
            }
            handlers = HandlersLocked();
        }
        try
        {
            Persist(std::move(snapshot), generation);
        }
        catch (...)
        {
            std::scoped_lock const lock{ m_mutex };
            if (originalCurrent)
            {
                m_records.insert_or_assign(originalCurrent->JobId, *originalCurrent);
            }
            if (originalReplaced)
            {
                m_records.insert_or_assign(originalReplaced->JobId, *originalReplaced);
            }
            throw;
        }
        if (result || (error && RequiresNewSource(error->Failure)))
        {
            try { m_vault.Remove(jobId); } catch (...) {}
        }
        if (replaced)
        {
            try
            {
                RemoveRecordFiles(*replaced, hasChanged ? &changed : nullptr);
                m_vault.Remove(replaced->JobId);
                auto completed = changed;
                completed.Status = DownloadStatus::Done;
                completed.Replacement.reset();
                m_store.Apply({ completed }, { replaced->JobId });
                std::scoped_lock const lock{ m_mutex };
                m_records.insert_or_assign(completed.JobId, completed);
                m_records.erase(replaced->JobId);
                changed = std::move(completed);
            }
            catch (...)
            {
                OutputDebugStringW(L"Halo retained a replacement cleanup tombstone for retry.\n");
                auto completed = changed;
                completed.Status = DownloadStatus::Done;
                try { m_store.Apply({ completed }); } catch (...) {}
                std::scoped_lock const lock{ m_mutex };
                m_records.insert_or_assign(completed.JobId, completed);
                changed = std::move(completed);
            }
        }
        if (hasChanged)
        {
            Notify(handlers, changed);
        }
        m_condition.notify_all();
    }

    void TransferEngine::UpdateResponseMetadata(
        std::wstring const& jobId,
        std::optional<std::wstring> validator,
        std::uint64_t totalBytes)
    {
        std::vector<DownloadRecord> snapshot;
        std::uint64_t generation{};
        {
            std::scoped_lock const lock{ m_mutex };
            auto const found = m_records.find(jobId);
            if (found == m_records.end()) return;
            if (validator) found->second.Validator = std::move(validator);
            if (totalBytes > 0) found->second.TotalBytes = totalBytes;
            found->second.UpdatedAt = NowMilliseconds();
            generation = ++m_generation;
            snapshot = { found->second };
        }
        Persist(std::move(snapshot), generation);
    }

    void TransferEngine::UpdateProgress(
        std::wstring const& jobId,
        std::uint64_t downloadedBytes,
        std::uint64_t totalBytes,
        std::uint64_t bytesPerSecond)
    {
        DownloadRecord changed;
        std::vector<DownloadRecord> snapshot;
        std::vector<DownloadChangedHandler> handlers;
        std::uint64_t generation{};
        {
            std::scoped_lock const lock{ m_mutex };
            auto const found = m_records.find(jobId);
            if (found == m_records.end()) return;
            found->second.DownloadedBytes = downloadedBytes;
            if (totalBytes > 0) found->second.TotalBytes = totalBytes;
            found->second.BytesPerSecond = bytesPerSecond;
            found->second.UpdatedAt = NowMilliseconds();
            changed = found->second;
            generation = ++m_generation;
            snapshot = { changed };
            handlers = HandlersLocked();
        }
        Persist(std::move(snapshot), generation);
        Notify(handlers, changed);
    }

    std::vector<DownloadRecord> TransferEngine::SnapshotLocked() const
    {
        std::vector<DownloadRecord> result;
        result.reserve(m_records.size());
        for (auto const& [jobId, record] : m_records)
        {
            static_cast<void>(jobId);
            result.push_back(record);
        }
        return result;
    }

    std::vector<DownloadChangedHandler> TransferEngine::HandlersLocked() const
    {
        std::vector<DownloadChangedHandler> result;
        result.reserve(m_handlers.size());
        for (auto const& [token, handler] : m_handlers)
        {
            static_cast<void>(token);
            result.push_back(handler);
        }
        return result;
    }

    std::optional<DownloadRecord> TransferEngine::VisibleRecordForVideoLocked(
        std::wstring const& videoId) const
    {
        if (!m_activeAccount) return std::nullopt;
        for (auto const& [jobId, record] : m_records)
        {
            if (record.AccountKey == *m_activeAccount
                && record.Media.VideoId == videoId
                && !record.PendingDeletion
                && !IsHiddenBackupLocked(jobId))
            {
                return record;
            }
        }
        return std::nullopt;
    }

    bool TransferEngine::IsHiddenBackupLocked(std::wstring const& jobId) const
    {
        return std::any_of(m_records.begin(), m_records.end(), [&jobId](auto const& pair)
        {
            return !pair.second.PendingDeletion
                && pair.second.Replacement
                && pair.second.Replacement->JobId == jobId;
        });
    }

    void TransferEngine::Persist(
        std::vector<DownloadRecord> records,
        std::uint64_t generation)
    {
        static_cast<void>(generation);
        m_store.Apply(records);
    }

    void TransferEngine::Notify(
        std::vector<DownloadChangedHandler> const& handlers,
        DownloadRecord const& record) noexcept
    {
        for (auto const& handler : handlers)
        {
            try { handler(record); } catch (...) {}
        }
    }

    void RunDownloadEngineUnitChecks()
    {
        if (!RequiresNewSource(DownloadFailureCode::SourceExpired)
            || !RequiresNewSource(DownloadFailureCode::InvalidRange)
            || !RequiresNewSource(DownloadFailureCode::ProtectedRequestCorrupt)
            || RequiresNewSource(DownloadFailureCode::Network))
        {
            throw std::runtime_error{ "The download failure retry policy is invalid." };
        }
        if (RecoverStatus(DownloadStatus::Downloading, false) != DownloadStatus::Queued
            || RecoverStatus(DownloadStatus::Downloading, true) != DownloadStatus::Paused)
        {
            throw std::runtime_error{ "The download recovery policy is invalid." };
        }
        auto const range = ParseContentRange(L"bytes 10-99/100");
        if (!range || range->Start != 10 || range->End != 99 || range->Total != 100
            || ParseContentRange(L"bytes 10-x/100")
            || ParseContentRange(L"bytes 10-100/100"))
        {
            throw std::runtime_error{ "The download content range parser is invalid." };
        }
        DownloadMedia media{ .VideoId = L"movie:1", .ItemId = L"movie:1", .MediaType = L"movie", .Title = L"Test", .FileName = L"../../bad name.exe" };
        auto const name = MakeDownloadFileName(media, Sha256Hex(L"https://example.test/video"));
        if (!IsSafeFileName(name) || !name.ends_with(L".mkv") || name.find(L"..") != std::wstring::npos)
        {
            throw std::runtime_error{ "The download filename policy is invalid." };
        }
        ProtectedRequest denied{
            .Url = L"https://example.test/video",
            .Headers = { { L"Range", L"bytes=0-1" } },
        };
        bool rejected{};
        try { ValidateProtectedRequest(denied); } catch (std::invalid_argument const&) { rejected = true; }
        if (!rejected)
        {
            throw std::runtime_error{ "A caller-controlled range header was accepted." };
        }
        if (HasSufficientSpace(64ull * 1024ull * 1024ull + 99, 100)
            || !HasSufficientSpace(64ull * 1024ull * 1024ull + 100, 100))
        {
            throw std::runtime_error{ "The download free-space reserve is invalid." };
        }
        if (HasSufficientSpace(64ull * 1024ull * 1024ull - 1, 0)
            || !HasSufficientSpace(64ull * 1024ull * 1024ull, 0))
        {
            throw std::runtime_error{ "Unknown-size downloads did not enforce the free-space reserve." };
        }
    }
}
