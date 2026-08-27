#include "pch.h"
#include "Security/Dpapi.h"

#include "Storage/FileStorage.h"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <objbase.h>
#include <span>
#include <stdexcept>
#include <system_error>
#include <vector>
#include <wil/resource.h>
#include <wincrypt.h>

namespace
{
    constexpr std::uint64_t MaximumProtectedFileBytes = 2u * 1024u * 1024u;

    struct LocalFreeDeleter final
    {
        void operator()(std::uint8_t* value) const noexcept
        {
            if (value)
            {
                LocalFree(value);
            }
        }
    };

    using LocalBytes = std::unique_ptr<std::uint8_t, LocalFreeDeleter>;

    void ThrowLastError(wchar_t const* message)
    {
        auto const code = HRESULT_FROM_WIN32(GetLastError());
        throw winrt::hresult_error{ code, message };
    }

    std::vector<std::uint8_t> Protect(std::span<std::uint8_t> plaintext)
    {
        if (plaintext.size() > (std::numeric_limits<DWORD>::max)())
        {
            throw std::length_error{ "Protected data is too large." };
        }

        DATA_BLOB input{
            .cbData = static_cast<DWORD>(plaintext.size()),
            .pbData = plaintext.data(),
        };
        DATA_BLOB output{};
        if (!CryptProtectData(
            &input,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            CRYPTPROTECT_UI_FORBIDDEN,
            &output))
        {
            ThrowLastError(L"Windows could not protect local Halo data.");
        }

        LocalBytes owner{ output.pbData };
        return std::vector<std::uint8_t>(output.pbData, output.pbData + output.cbData);
    }

    std::vector<std::uint8_t> Unprotect(std::span<std::uint8_t> protectedBytes)
    {
        if (protectedBytes.size() > (std::numeric_limits<DWORD>::max)())
        {
            throw std::length_error{ "Protected data is too large." };
        }

        DATA_BLOB input{
            .cbData = static_cast<DWORD>(protectedBytes.size()),
            .pbData = protectedBytes.data(),
        };
        DATA_BLOB output{};
        if (!CryptUnprotectData(
            &input,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            CRYPTPROTECT_UI_FORBIDDEN,
            &output))
        {
            ThrowLastError(L"Windows could not read protected Halo data.");
        }

        LocalBytes owner{ output.pbData };
        return std::vector<std::uint8_t>(output.pbData, output.pbData + output.cbData);
    }

    std::filesystem::path TemporaryPath(std::filesystem::path const& target)
    {
        GUID id{};
        winrt::check_hresult(CoCreateGuid(&id));
        std::array<wchar_t, 40> text{};
        if (StringFromGUID2(id, text.data(), static_cast<int>(text.size())) == 0)
        {
            throw winrt::hresult_error{ E_FAIL, L"Windows could not create a temporary filename." };
        }
        auto temporary = target;
        temporary += L".";
        temporary += text.data();
        temporary += L".tmp";
        return temporary;
    }

    void WriteAll(HANDLE file, std::span<std::uint8_t const> bytes)
    {
        std::size_t written{};
        while (written < bytes.size())
        {
            auto const remaining = bytes.size() - written;
            auto const maximum = static_cast<std::size_t>((std::numeric_limits<DWORD>::max)());
            auto const chunk = static_cast<DWORD>(remaining < maximum ? remaining : maximum);
            DWORD chunkWritten{};
            if (!WriteFile(file, bytes.data() + written, chunk, &chunkWritten, nullptr))
            {
                ThrowLastError(L"Windows could not write protected Halo data.");
            }
            if (chunkWritten == 0)
            {
                throw winrt::hresult_error{ E_FAIL, L"Windows stopped writing protected Halo data." };
            }
            written += chunkWritten;
        }
    }

    std::vector<std::uint8_t> ReadAll(HANDLE file)
    {
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file, &size))
        {
            ThrowLastError(L"Windows could not inspect protected Halo data.");
        }
        if (size.QuadPart < 0 || static_cast<std::uint64_t>(size.QuadPart) > MaximumProtectedFileBytes)
        {
            throw std::length_error{ "The protected Halo file is too large." };
        }

        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size.QuadPart));
        std::size_t read{};
        while (read < bytes.size())
        {
            auto const remaining = bytes.size() - read;
            auto const maximum = static_cast<std::size_t>((std::numeric_limits<DWORD>::max)());
            auto const chunk = static_cast<DWORD>(remaining < maximum ? remaining : maximum);
            DWORD chunkRead{};
            if (!ReadFile(file, bytes.data() + read, chunk, &chunkRead, nullptr))
            {
                ThrowLastError(L"Windows could not read protected Halo data.");
            }
            if (chunkRead == 0)
            {
                throw winrt::hresult_error{ E_FAIL, L"The protected Halo file ended unexpectedly." };
            }
            read += chunkRead;
        }
        return bytes;
    }
}

namespace HaloDesktop::Security
{
    concurrency::task<void> WriteProtectedTextAsync(
        std::filesystem::path path,
        std::string plaintext)
    {
        co_await winrt::resume_background();
        if (path.empty())
        {
            throw std::invalid_argument{ "A protected file path is required." };
        }

        ::HaloDesktop::Storage::FileMutationLock const fileLock{ path };

        std::vector<std::uint8_t> plaintextBytes(plaintext.begin(), plaintext.end());
        auto wipePlaintext = wil::scope_exit([&plaintext, &plaintextBytes]() noexcept
        {
            if (!plaintext.empty())
            {
                SecureZeroMemory(plaintext.data(), plaintext.size());
            }
            if (!plaintextBytes.empty())
            {
                SecureZeroMemory(plaintextBytes.data(), plaintextBytes.size());
            }
        });
        auto const protectedBytes = Protect(plaintextBytes);

        std::error_code directoryError;
        if (!path.parent_path().empty())
        {
            std::filesystem::create_directories(path.parent_path(), directoryError);
        }
        if (directoryError)
        {
            throw std::system_error{ directoryError, "Could not create the protected data directory" };
        }

        auto const temporary = TemporaryPath(path);
        auto removeTemporary = wil::scope_exit([&temporary]() noexcept
        {
            DeleteFileW(temporary.c_str());
        });
        wil::unique_hfile file{ CreateFileW(
            temporary.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH,
            nullptr) };
        if (!file)
        {
            ThrowLastError(L"Windows could not create a protected Halo file.");
        }
        WriteAll(file.get(), protectedBytes);
        if (!FlushFileBuffers(file.get()))
        {
            ThrowLastError(L"Windows could not flush protected Halo data.");
        }
        file.reset();

        if (!MoveFileExW(
            temporary.c_str(),
            path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            ThrowLastError(L"Windows could not publish protected Halo data.");
        }
        removeTemporary.release();
    }

    concurrency::task<std::optional<std::string>> ReadProtectedTextAsync(
        std::filesystem::path path)
    {
        co_await winrt::resume_background();
        if (path.empty())
        {
            throw std::invalid_argument{ "A protected file path is required." };
        }

        ::HaloDesktop::Storage::FileMutationLock const fileLock{ path };

        wil::unique_hfile file{ CreateFileW(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
            nullptr) };
        if (!file)
        {
            auto const error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            {
                co_return std::nullopt;
            }
            ThrowLastError(L"Windows could not open protected Halo data.");
        }

        auto protectedBytes = ReadAll(file.get());
        file.reset();
        auto plaintextBytes = Unprotect(protectedBytes);
        auto wipe = wil::scope_exit([&protectedBytes, &plaintextBytes]() noexcept
        {
            if (!protectedBytes.empty())
            {
                SecureZeroMemory(protectedBytes.data(), protectedBytes.size());
            }
            if (!plaintextBytes.empty())
            {
                SecureZeroMemory(plaintextBytes.data(), plaintextBytes.size());
            }
        });
        co_return std::string(plaintextBytes.begin(), plaintextBytes.end());
    }

    concurrency::task<void> DeleteProtectedFileAsync(std::filesystem::path path)
    {
        co_await winrt::resume_background();
        if (path.empty())
        {
            co_return;
        }
        ::HaloDesktop::Storage::FileMutationLock const fileLock{ path };
        if (DeleteFileW(path.c_str()))
        {
            co_return;
        }
        auto const error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
        {
            ThrowLastError(L"Windows could not delete protected Halo data.");
        }
    }
}
