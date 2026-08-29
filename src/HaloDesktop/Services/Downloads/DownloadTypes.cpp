#include "pch.h"
#include "Services/Downloads/DownloadTypes.h"

#include "Security/ProtectedHttpHeaders.h"

#include <algorithm>
#include <array>
#include <bcrypt.h>
#include <cwctype>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <wil/resource.h>
#include <winhttp.h>

namespace
{
    constexpr std::uint64_t StorageReserve = 64ull * 1024ull * 1024ull;

    std::string Utf8(std::wstring const& value)
    {
        if (value.empty())
        {
            return {};
        }
        auto const size = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            nullptr,
            0,
            nullptr,
            nullptr);
        if (size <= 0)
        {
            throw std::invalid_argument{ "Text is not valid Unicode." };
        }
        std::string result(static_cast<std::size_t>(size), '\0');
        if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            size,
            nullptr,
            nullptr) != size)
        {
            throw std::invalid_argument{ "Text could not be encoded." };
        }
        return result;
    }

    std::wstring Lower(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(std::towlower(character));
        });
        return value;
    }

    void ValidateUrl(std::wstring const& url)
    {
        if (url.empty() || url.size() > 32768 || url.find(L'\0') != std::wstring::npos)
        {
            throw std::invalid_argument{ "Choose a valid HTTP(S) source." };
        }
        URL_COMPONENTS components{ .dwStructSize = sizeof(URL_COMPONENTS) };
        components.dwSchemeLength = static_cast<DWORD>(-1);
        components.dwHostNameLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components)
            || components.dwHostNameLength == 0
            || (components.nScheme != INTERNET_SCHEME_HTTP
                && components.nScheme != INTERNET_SCHEME_HTTPS))
        {
            throw std::invalid_argument{ "Choose a valid HTTP(S) source." };
        }
    }

    bool AllowedExtension(std::wstring const& extension) noexcept
    {
        constexpr std::array<std::wstring_view, 7> allowed{
            L"mp4", L"mkv", L"webm", L"avi", L"m4v", L"mov", L"ts",
        };
        return std::find(allowed.begin(), allowed.end(), Lower(extension)) != allowed.end();
    }

    std::optional<std::uint64_t> ParseUnsigned(std::wstring_view value) noexcept
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
}

namespace HaloDesktop::Services::Downloads
{
    std::filesystem::path DownloadRecord::TargetPath() const
    {
        return RootPath / FileName;
    }

    std::filesystem::path DownloadRecord::PartialPath() const
    {
        auto result = TargetPath();
        result += L".part";
        return result;
    }

    bool IsActive(DownloadStatus status) noexcept
    {
        return status == DownloadStatus::Queued || status == DownloadStatus::Downloading;
    }

    bool RequiresNewSource(DownloadFailureCode failure) noexcept
    {
        return failure == DownloadFailureCode::SourceExpired
            || failure == DownloadFailureCode::InvalidRange
            || failure == DownloadFailureCode::ProtectedRequestCorrupt
            || failure == DownloadFailureCode::MissingFile;
    }

    DownloadStatus RecoverStatus(DownloadStatus status, bool explicitPause) noexcept
    {
        if (status == DownloadStatus::Downloading)
        {
            return explicitPause ? DownloadStatus::Paused : DownloadStatus::Queued;
        }
        return status;
    }

    std::wstring FailureMessage(DownloadFailureCode failure)
    {
        switch (failure)
        {
        case DownloadFailureCode::SourceExpired:
            return L"This source has expired. Choose a source again to continue.";
        case DownloadFailureCode::StorageFull:
            return L"The device ran out of storage while downloading.";
        case DownloadFailureCode::InvalidRange:
            return L"The source could not safely resume this download.";
        case DownloadFailureCode::MissingFile:
            return L"This download is no longer on the device.";
        case DownloadFailureCode::Network:
            return L"The download could not continue after repeated network failures.";
        case DownloadFailureCode::ServerUnavailable:
            return L"The source is still unavailable after repeated retries.";
        case DownloadFailureCode::SourceRejected:
            return L"The source refused this download.";
        case DownloadFailureCode::ProtectedRequestCorrupt:
            return L"The protected download request could not be read. Choose the source again.";
        case DownloadFailureCode::Unknown:
            return L"This download could not be completed.";
        }
        return L"This download could not be completed.";
    }

    std::wstring Sha256Hex(std::wstring const& value)
    {
        auto const bytes = Utf8(value);
        wil::unique_bcrypt_algorithm algorithm;
        winrt::check_nt(BCryptOpenAlgorithmProvider(
            algorithm.put(), BCRYPT_SHA256_ALGORITHM, nullptr, 0));
        DWORD objectSize{};
        DWORD copied{};
        winrt::check_nt(BCryptGetProperty(
            algorithm.get(),
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectSize),
            sizeof(objectSize),
            &copied,
            0));
        std::vector<std::uint8_t> object(objectSize);
        wil::unique_bcrypt_hash hash;
        winrt::check_nt(BCryptCreateHash(
            algorithm.get(), hash.put(), object.data(), objectSize, nullptr, 0, 0));
        winrt::check_nt(BCryptHashData(
            hash.get(),
            reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
            static_cast<ULONG>(bytes.size()),
            0));
        std::array<std::uint8_t, 32> digest{};
        winrt::check_nt(BCryptFinishHash(
            hash.get(), digest.data(), static_cast<ULONG>(digest.size()), 0));

        constexpr wchar_t digits[] = L"0123456789abcdef";
        std::wstring result;
        result.reserve(digest.size() * 2);
        for (auto const byte : digest)
        {
            result.push_back(digits[byte >> 4]);
            result.push_back(digits[byte & 0x0f]);
        }
        return result;
    }

    std::wstring MakeAccountKey(std::wstring serverUrl, std::wstring const& userId)
    {
        while (!serverUrl.empty() && serverUrl.back() == L'/')
        {
            serverUrl.pop_back();
        }
        if (serverUrl.empty() || userId.empty()
            || serverUrl.size() > 32768 || userId.size() > 4096)
        {
            throw std::invalid_argument{ "A server and user are required for download storage." };
        }
        return Sha256Hex(serverUrl + L"\n" + userId);
    }

    std::wstring MakeDownloadFileName(
        DownloadMedia const& media,
        std::wstring const& sourceFingerprint)
    {
        auto raw = media.FileName.value_or(media.VideoId);
        auto const separator = raw.find_last_of(L"/\\");
        if (separator != std::wstring::npos)
        {
            raw = raw.substr(separator + 1);
        }
        std::replace_if(raw.begin(), raw.end(), [](wchar_t character)
        {
            return !(character >= L'a' && character <= L'z')
                && !(character >= L'A' && character <= L'Z')
                && !(character >= L'0' && character <= L'9')
                && character != L'.'
                && character != L'-'
                && character != L'_';
        }, L'_');
        while (!raw.empty() && raw.front() == L'.')
        {
            raw.erase(raw.begin());
        }
        while (!raw.empty() && raw.back() == L'.')
        {
            raw.pop_back();
        }
        if (raw.empty())
        {
            raw = L"video";
        }

        auto extension = std::filesystem::path{ raw }.extension().wstring();
        if (!extension.empty() && extension.front() == L'.')
        {
            extension.erase(extension.begin());
        }
        if (!AllowedExtension(extension))
        {
            extension = L"mkv";
        }
        auto stem = std::filesystem::path{ raw }.stem().wstring();
        if (stem.empty())
        {
            stem = L"video";
        }
        if (stem.size() > 80)
        {
            stem.resize(80);
        }
        if (sourceFingerprint.size() < 12)
        {
            throw std::invalid_argument{ "A valid source fingerprint is required." };
        }
        return stem + L"-" + sourceFingerprint.substr(0, 12) + L"." + Lower(extension);
    }

    bool IsSafeFileName(std::wstring const& value) noexcept
    {
        if (value.empty() || value == L"." || value == L"..")
        {
            return false;
        }
        auto const path = std::filesystem::path{ value };
        return !path.is_absolute()
            && !path.has_parent_path()
            && path.filename() == path;
    }

    std::optional<ContentRange> ParseContentRange(std::wstring const& value) noexcept
    {
        constexpr std::wstring_view prefix = L"bytes ";
        if (!value.starts_with(prefix))
        {
            return std::nullopt;
        }
        auto const slash = value.find(L'/', prefix.size());
        auto const dash = value.find(L'-', prefix.size());
        if (dash == std::wstring::npos || slash == std::wstring::npos || dash >= slash)
        {
            return std::nullopt;
        }
        auto const start = ParseUnsigned(std::wstring_view{ value }.substr(prefix.size(), dash - prefix.size()));
        auto const end = ParseUnsigned(std::wstring_view{ value }.substr(dash + 1, slash - dash - 1));
        auto const total = ParseUnsigned(std::wstring_view{ value }.substr(slash + 1));
        if (!start || !end || !total || *end < *start || *total == 0 || *end >= *total)
        {
            return std::nullopt;
        }
        return ContentRange{ *start, *end, *total };
    }

    bool HasSufficientSpace(std::uint64_t availableBytes, std::uint64_t videoBytes) noexcept
    {
        if (videoBytes > (std::numeric_limits<std::uint64_t>::max)() - StorageReserve)
        {
            return false;
        }
        return availableBytes >= videoBytes + StorageReserve;
    }

    void ValidateProtectedRequest(ProtectedRequest const& request)
    {
        ValidateUrl(request.Url);
        Security::ValidateProtectedHttpHeaders(request.Headers);
        if (request.Subtitle)
        {
            ValidateUrl(request.Subtitle->Url);
            Security::ValidateProtectedHttpHeaders(request.Subtitle->Headers);
            if (request.Subtitle->Language.empty() || request.Subtitle->Language.size() > 32)
            {
                throw std::invalid_argument{ "The subtitle language is invalid." };
            }
        }
    }
}
