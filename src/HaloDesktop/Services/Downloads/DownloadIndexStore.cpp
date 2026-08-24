#include "pch.h"
#include "Services/Downloads/DownloadIndexStore.h"

#include <array>
#include <charconv>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <wil/resource.h>
#include <winrt/Windows.Data.Json.h>

namespace
{
    constexpr std::uint64_t IndexVersion = 1;
    constexpr std::uint64_t MaximumIndexBytes = 32ull * 1024ull * 1024ull;
    constexpr std::size_t MaximumTextLength = 65536;

    std::wstring StatusName(HaloDesktop::Services::Downloads::DownloadStatus status)
    {
        using HaloDesktop::Services::Downloads::DownloadStatus;
        switch (status)
        {
        case DownloadStatus::Queued: return L"queued";
        case DownloadStatus::Downloading: return L"downloading";
        case DownloadStatus::Paused: return L"paused";
        case DownloadStatus::Done: return L"done";
        case DownloadStatus::Failed: return L"failed";
        }
        throw std::invalid_argument{ "The download status is invalid." };
    }

    HaloDesktop::Services::Downloads::DownloadStatus ParseStatus(std::wstring const& value)
    {
        using HaloDesktop::Services::Downloads::DownloadStatus;
        if (value == L"queued") return DownloadStatus::Queued;
        if (value == L"downloading") return DownloadStatus::Downloading;
        if (value == L"paused") return DownloadStatus::Paused;
        if (value == L"done") return DownloadStatus::Done;
        if (value == L"failed") return DownloadStatus::Failed;
        throw std::invalid_argument{ "The download index contains an invalid status." };
    }

    std::wstring FailureName(HaloDesktop::Services::Downloads::DownloadFailureCode failure)
    {
        using HaloDesktop::Services::Downloads::DownloadFailureCode;
        switch (failure)
        {
        case DownloadFailureCode::SourceExpired: return L"source_expired";
        case DownloadFailureCode::StorageFull: return L"storage_full";
        case DownloadFailureCode::InvalidRange: return L"invalid_range";
        case DownloadFailureCode::MissingFile: return L"missing_file";
        case DownloadFailureCode::Network: return L"network";
        case DownloadFailureCode::ServerUnavailable: return L"server_unavailable";
        case DownloadFailureCode::SourceRejected: return L"source_rejected";
        case DownloadFailureCode::ProtectedRequestCorrupt: return L"protected_request_corrupt";
        case DownloadFailureCode::Unknown: return L"unknown";
        }
        throw std::invalid_argument{ "The download failure is invalid." };
    }

    HaloDesktop::Services::Downloads::DownloadFailureCode ParseFailure(std::wstring const& value)
    {
        using HaloDesktop::Services::Downloads::DownloadFailureCode;
        if (value == L"source_expired") return DownloadFailureCode::SourceExpired;
        if (value == L"storage_full") return DownloadFailureCode::StorageFull;
        if (value == L"invalid_range") return DownloadFailureCode::InvalidRange;
        if (value == L"missing_file") return DownloadFailureCode::MissingFile;
        if (value == L"network") return DownloadFailureCode::Network;
        if (value == L"server_unavailable") return DownloadFailureCode::ServerUnavailable;
        if (value == L"source_rejected") return DownloadFailureCode::SourceRejected;
        if (value == L"protected_request_corrupt") return DownloadFailureCode::ProtectedRequestCorrupt;
        if (value == L"unknown") return DownloadFailureCode::Unknown;
        throw std::invalid_argument{ "The download index contains an invalid failure." };
    }

    winrt::hstring RequiredString(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* name)
    {
        auto const value = object.GetNamedString(name);
        if (value.empty() || value.size() > MaximumTextLength)
        {
            throw std::invalid_argument{ "The download index contains invalid text." };
        }
        return value;
    }

    std::optional<std::wstring> OptionalString(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* name)
    {
        auto const value = object.GetNamedValue(
            name,
            winrt::Windows::Data::Json::JsonValue::CreateNullValue());
        if (value.ValueType() == winrt::Windows::Data::Json::JsonValueType::Null)
        {
            return std::nullopt;
        }
        if (value.ValueType() != winrt::Windows::Data::Json::JsonValueType::String)
        {
            throw std::invalid_argument{ "The download index contains invalid optional text." };
        }
        auto const text = value.GetString();
        if (text.empty() || text.size() > MaximumTextLength)
        {
            throw std::invalid_argument{ "The download index contains invalid optional text." };
        }
        return std::wstring{ text };
    }

    std::uint64_t ParseUnsigned(std::wstring const& value)
    {
        if (value.empty())
        {
            throw std::invalid_argument{ "The download index contains an invalid number." };
        }
        std::uint64_t result{};
        for (auto const character : value)
        {
            if (character < L'0' || character > L'9')
            {
                throw std::invalid_argument{ "The download index contains an invalid number." };
            }
            auto const digit = static_cast<std::uint64_t>(character - L'0');
            if (result > ((std::numeric_limits<std::uint64_t>::max)() - digit) / 10)
            {
                throw std::invalid_argument{ "The download index contains an oversized number." };
            }
            result = result * 10 + digit;
        }
        return result;
    }

    std::uint64_t RequiredUnsigned(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* name)
    {
        return ParseUnsigned(std::wstring{ RequiredString(object, name) });
    }

    std::optional<std::uint64_t> OptionalUnsigned(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* name)
    {
        auto const value = OptionalString(object, name);
        return value ? std::optional<std::uint64_t>{ ParseUnsigned(*value) } : std::nullopt;
    }

    void InsertString(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* name,
        std::wstring const& value)
    {
        object.Insert(name, winrt::Windows::Data::Json::JsonValue::CreateStringValue(value));
    }

    void InsertUnsigned(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* name,
        std::uint64_t value)
    {
        InsertString(object, name, std::to_wstring(value));
    }

    void InsertOptionalString(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* name,
        std::optional<std::wstring> const& value)
    {
        if (value)
        {
            InsertString(object, name, *value);
        }
    }

    winrt::Windows::Data::Json::JsonObject SerializeMedia(
        HaloDesktop::Services::Downloads::DownloadMedia const& media)
    {
        winrt::Windows::Data::Json::JsonObject object;
        InsertString(object, L"videoId", media.VideoId);
        InsertString(object, L"itemId", media.ItemId);
        InsertString(object, L"mediaType", media.MediaType);
        InsertOptionalString(object, L"metaId", media.MetaId);
        InsertString(object, L"title", media.Title);
        InsertOptionalString(object, L"showName", media.ShowName);
        InsertOptionalString(object, L"episodeLabel", media.EpisodeLabel);
        InsertOptionalString(object, L"poster", media.Poster);
        InsertOptionalString(object, L"addonId", media.AddonId);
        InsertOptionalString(object, L"bingeGroup", media.BingeGroup);
        InsertOptionalString(object, L"fileName", media.FileName);
        if (media.VideoSize) InsertUnsigned(object, L"videoSize", *media.VideoSize);
        InsertOptionalString(object, L"videoHash", media.VideoHash);
        InsertOptionalString(object, L"streamName", media.StreamName);
        InsertOptionalString(object, L"streamTitle", media.StreamTitle);
        return object;
    }

    HaloDesktop::Services::Downloads::DownloadMedia ParseMedia(
        winrt::Windows::Data::Json::JsonObject const& object)
    {
        return HaloDesktop::Services::Downloads::DownloadMedia{
            .VideoId = std::wstring{ RequiredString(object, L"videoId") },
            .ItemId = std::wstring{ RequiredString(object, L"itemId") },
            .MediaType = std::wstring{ RequiredString(object, L"mediaType") },
            .MetaId = OptionalString(object, L"metaId"),
            .Title = std::wstring{ RequiredString(object, L"title") },
            .ShowName = OptionalString(object, L"showName"),
            .EpisodeLabel = OptionalString(object, L"episodeLabel"),
            .Poster = OptionalString(object, L"poster"),
            .AddonId = OptionalString(object, L"addonId"),
            .BingeGroup = OptionalString(object, L"bingeGroup"),
            .FileName = OptionalString(object, L"fileName"),
            .VideoSize = OptionalUnsigned(object, L"videoSize"),
            .VideoHash = OptionalString(object, L"videoHash"),
            .StreamName = OptionalString(object, L"streamName"),
            .StreamTitle = OptionalString(object, L"streamTitle"),
        };
    }

    winrt::Windows::Data::Json::JsonObject SerializeRecord(
        HaloDesktop::Services::Downloads::DownloadRecord const& record)
    {
        winrt::Windows::Data::Json::JsonObject object;
        InsertString(object, L"jobId", record.JobId);
        InsertString(object, L"accountKey", record.AccountKey);
        object.Insert(L"media", SerializeMedia(record.Media));
        InsertString(object, L"fileName", record.FileName);
        InsertOptionalString(object, L"subtitleFileName", record.SubtitleFileName);
        InsertOptionalString(object, L"subtitleLanguage", record.SubtitleLanguage);
        InsertString(object, L"rootPath", record.RootPath.wstring());
        InsertString(object, L"status", StatusName(record.Status));
        InsertUnsigned(object, L"totalBytes", record.TotalBytes);
        InsertUnsigned(object, L"downloadedBytes", record.DownloadedBytes);
        InsertOptionalString(object, L"validator", record.Validator);
        InsertString(object, L"sourceFingerprint", record.SourceFingerprint);
        if (record.Failure) InsertString(object, L"failure", FailureName(*record.Failure));
        object.Insert(L"explicitPause", winrt::Windows::Data::Json::JsonValue::CreateBooleanValue(record.ExplicitPause));
        InsertUnsigned(object, L"createdAt", record.CreatedAt);
        InsertUnsigned(object, L"updatedAt", record.UpdatedAt);
        InsertUnsigned(object, L"bytesPerSecond", record.BytesPerSecond);
        if (record.Replacement)
        {
            winrt::Windows::Data::Json::JsonObject replacement;
            InsertString(replacement, L"jobId", record.Replacement->JobId);
            InsertString(replacement, L"rootPath", record.Replacement->RootPath.wstring());
            InsertString(replacement, L"fileName", record.Replacement->FileName);
            InsertOptionalString(replacement, L"subtitleFileName", record.Replacement->SubtitleFileName);
            object.Insert(L"replacement", replacement);
        }
        return object;
    }

    HaloDesktop::Services::Downloads::DownloadRecord ParseRecord(
        winrt::Windows::Data::Json::JsonObject const& object)
    {
        using HaloDesktop::Services::Downloads::DownloadRecord;
        using HaloDesktop::Services::Downloads::IsSafeFileName;
        using HaloDesktop::Services::Downloads::ReplacementBackup;
        std::optional<ReplacementBackup> replacement;
        if (object.HasKey(L"replacement"))
        {
            auto const value = object.GetNamedObject(L"replacement");
            replacement = ReplacementBackup{
                .JobId = std::wstring{ RequiredString(value, L"jobId") },
                .RootPath = std::filesystem::path{ RequiredString(value, L"rootPath").c_str() },
                .FileName = std::wstring{ RequiredString(value, L"fileName") },
                .SubtitleFileName = OptionalString(value, L"subtitleFileName"),
            };
        }
        DownloadRecord record{
            .JobId = std::wstring{ RequiredString(object, L"jobId") },
            .AccountKey = std::wstring{ RequiredString(object, L"accountKey") },
            .Media = ParseMedia(object.GetNamedObject(L"media")),
            .FileName = std::wstring{ RequiredString(object, L"fileName") },
            .SubtitleFileName = OptionalString(object, L"subtitleFileName"),
            .SubtitleLanguage = OptionalString(object, L"subtitleLanguage"),
            .RootPath = std::filesystem::path{ RequiredString(object, L"rootPath").c_str() },
            .Status = ParseStatus(std::wstring{ RequiredString(object, L"status") }),
            .TotalBytes = RequiredUnsigned(object, L"totalBytes"),
            .DownloadedBytes = RequiredUnsigned(object, L"downloadedBytes"),
            .Validator = OptionalString(object, L"validator"),
            .SourceFingerprint = std::wstring{ RequiredString(object, L"sourceFingerprint") },
            .Failure = object.HasKey(L"failure")
                ? std::optional{ ParseFailure(std::wstring{ RequiredString(object, L"failure") }) }
                : std::nullopt,
            .ExplicitPause = object.GetNamedBoolean(L"explicitPause"),
            .CreatedAt = RequiredUnsigned(object, L"createdAt"),
            .UpdatedAt = RequiredUnsigned(object, L"updatedAt"),
            .BytesPerSecond = RequiredUnsigned(object, L"bytesPerSecond"),
            .Replacement = std::move(replacement),
        };
        if (record.JobId.size() > 128
            || record.AccountKey.size() != 64
            || record.SourceFingerprint.size() != 64
            || !record.RootPath.is_absolute()
            || !IsSafeFileName(record.FileName)
            || (record.SubtitleFileName && !IsSafeFileName(*record.SubtitleFileName))
            || (record.Replacement
                && (!record.Replacement->RootPath.is_absolute()
                    || !IsSafeFileName(record.Replacement->FileName)
                    || (record.Replacement->SubtitleFileName
                        && !IsSafeFileName(*record.Replacement->SubtitleFileName)))))
        {
            throw std::invalid_argument{ "The download index contains an unsafe path." };
        }
        return record;
    }

    std::string ReadFile(std::filesystem::path const& path)
    {
        std::error_code error;
        auto const size = std::filesystem::file_size(path, error);
        if (error)
        {
            if (error == std::errc::no_such_file_or_directory)
            {
                return {};
            }
            throw std::system_error{ error, "Could not inspect the download index" };
        }
        if (size > MaximumIndexBytes)
        {
            throw std::length_error{ "The download index is too large." };
        }
        std::ifstream input(path, std::ios::binary);
        std::string result(static_cast<std::size_t>(size), '\0');
        input.read(result.data(), static_cast<std::streamsize>(result.size()));
        if (!input && !result.empty())
        {
            throw std::runtime_error{ "The download index could not be read." };
        }
        return result;
    }

    void WriteAtomic(std::filesystem::path const& target, std::string const& bytes)
    {
        GUID id{};
        winrt::check_hresult(CoCreateGuid(&id));
        std::array<wchar_t, 40> identifier{};
        if (StringFromGUID2(id, identifier.data(), static_cast<int>(identifier.size())) == 0)
        {
            throw std::runtime_error{ "A temporary download index name could not be created." };
        }
        auto temporary = target;
        temporary += L".";
        temporary += identifier.data();
        temporary += L".tmp";
        auto cleanup = wil::scope_exit([&temporary]() noexcept { DeleteFileW(temporary.c_str()); });
        wil::unique_hfile file{ CreateFileW(
            temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH, nullptr) };
        if (!file)
        {
            throw std::system_error{ static_cast<int>(GetLastError()), std::system_category(), "Could not create download state" };
        }
        std::size_t offset{};
        while (offset < bytes.size())
        {
            auto const remaining = bytes.size() - offset;
            auto const chunk = static_cast<DWORD>((std::min)(
                remaining,
                static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
            DWORD written{};
            if (!WriteFile(file.get(), bytes.data() + offset, chunk, &written, nullptr) || written == 0)
            {
                throw std::system_error{ static_cast<int>(GetLastError()), std::system_category(), "Could not write download state" };
            }
            offset += written;
        }
        if (!FlushFileBuffers(file.get()))
        {
            throw std::system_error{ static_cast<int>(GetLastError()), std::system_category(), "Could not flush download state" };
        }
        file.reset();
        if (!MoveFileExW(
            temporary.c_str(), target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            throw std::system_error{ static_cast<int>(GetLastError()), std::system_category(), "Could not publish download state" };
        }
        cleanup.release();
    }

    std::filesystem::path CanonicalDirectory(std::filesystem::path directory)
    {
        if (directory.empty())
        {
            throw std::invalid_argument{ "A download folder is required." };
        }
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error)
        {
            throw std::system_error{ error, "Could not create the download folder" };
        }
        auto canonical = std::filesystem::canonical(directory, error);
        if (error || !canonical.is_absolute())
        {
            throw std::system_error{ error ? error : std::make_error_code(std::errc::invalid_argument), "Could not use the download folder" };
        }
        return canonical;
    }
}

namespace HaloDesktop::Services::Downloads
{
    DownloadIndexStore::DownloadIndexStore(std::filesystem::path dataRoot)
    {
        m_paths.DataRoot = CanonicalDirectory(std::move(dataRoot));
        m_paths.IndexFile = m_paths.DataRoot / L"downloads-index.json";
        m_paths.ConfigFile = m_paths.DataRoot / L"downloads-config.json";
        m_paths.VaultDirectory = CanonicalDirectory(m_paths.DataRoot / L"download-requests");
        m_paths.DefaultDownloadDirectory = m_paths.DataRoot / L"downloads";

        auto const config = ReadFile(m_paths.ConfigFile);
        if (!config.empty())
        {
            try
            {
                auto const object = winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(config));
                m_downloadDirectory = std::filesystem::path{ RequiredString(object, L"directory").c_str() };
            }
            catch (...)
            {
                m_downloadDirectory.clear();
            }
        }
        if (m_downloadDirectory.empty())
        {
            m_downloadDirectory = CanonicalDirectory(m_paths.DefaultDownloadDirectory);
        }
    }

    std::vector<DownloadRecord> DownloadIndexStore::Load()
    {
        std::scoped_lock const lock{ m_mutex };
        auto const raw = ReadFile(m_paths.IndexFile);
        if (raw.empty())
        {
            return {};
        }
        auto const root = winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(raw));
        if (RequiredUnsigned(root, L"version") != IndexVersion)
        {
            throw std::runtime_error{ "The download index version is unsupported." };
        }
        auto const entries = root.GetNamedArray(L"entries");
        std::vector<DownloadRecord> records;
        records.reserve(entries.Size());
        for (auto const& value : entries)
        {
            auto record = ParseRecord(value.GetObject());
            record.Status = RecoverStatus(record.Status, record.ExplicitPause);
            if (record.Status != DownloadStatus::Downloading)
            {
                record.BytesPerSecond = 0;
            }
            records.push_back(std::move(record));
        }
        return records;
    }

    void DownloadIndexStore::Save(
        std::vector<DownloadRecord> const& records,
        std::uint64_t generation)
    {
        std::scoped_lock const lock{ m_mutex };
        if (generation < m_savedGeneration)
        {
            return;
        }
        winrt::Windows::Data::Json::JsonArray entries;
        for (auto const& record : records)
        {
            entries.Append(SerializeRecord(record));
        }
        winrt::Windows::Data::Json::JsonObject root;
        InsertUnsigned(root, L"version", IndexVersion);
        root.Insert(L"entries", entries);
        WriteAtomic(m_paths.IndexFile, winrt::to_string(root.Stringify()));
        m_savedGeneration = generation;
    }

    std::filesystem::path DownloadIndexStore::DownloadDirectory() const
    {
        std::scoped_lock const lock{ m_mutex };
        return m_downloadDirectory;
    }

    void DownloadIndexStore::SetDownloadDirectory(std::filesystem::path directory)
    {
        auto canonical = CanonicalDirectory(std::move(directory));
        winrt::Windows::Data::Json::JsonObject config;
        InsertString(config, L"directory", canonical.wstring());
        std::scoped_lock const lock{ m_mutex };
        WriteAtomic(m_paths.ConfigFile, winrt::to_string(config.Stringify()));
        m_downloadDirectory = std::move(canonical);
    }

    DownloadStoragePaths const& DownloadIndexStore::Paths() const noexcept
    {
        return m_paths;
    }
}
