#include "pch.h"
#include "Storage/FileStorage.h"

#include <algorithm>
#include <array>
#include <bcrypt.h>
#include <cwctype>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <system_error>
#include <vector>
#include <wil/resource.h>

namespace
{
    [[noreturn]] void ThrowLastError(char const* operation)
    {
        throw std::system_error{ static_cast<int>(GetLastError()), std::system_category(), operation };
    }

    std::wstring LockName(std::filesystem::path const& target)
    {
        auto normalized = std::filesystem::absolute(target).lexically_normal().wstring();
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](wchar_t value)
        {
            return static_cast<wchar_t>(std::towlower(value));
        });

        BCRYPT_ALG_HANDLE rawAlgorithm{};
        winrt::check_nt(BCryptOpenAlgorithmProvider(
            &rawAlgorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_HASH_REUSABLE_FLAG));
        wil::unique_bcrypt_algorithm algorithm{ rawAlgorithm };

        BCRYPT_HASH_HANDLE rawHash{};
        winrt::check_nt(BCryptCreateHash(algorithm.get(), &rawHash, nullptr, 0, nullptr, 0, 0));
        wil::unique_bcrypt_hash hash{ rawHash };
        auto const bytes = std::as_bytes(std::span{ normalized });
        winrt::check_nt(BCryptHashData(
            hash.get(),
            const_cast<PUCHAR>(reinterpret_cast<UCHAR const*>(bytes.data())),
            static_cast<ULONG>(bytes.size()),
            0));
        std::array<std::uint8_t, 32> digest{};
        winrt::check_nt(BCryptFinishHash(hash.get(), digest.data(), static_cast<ULONG>(digest.size()), 0));

        constexpr wchar_t Hex[] = L"0123456789abcdef";
        std::wstring result{ L"Local\\HaloDesktop.FileMutation." };
        result.reserve(result.size() + digest.size() * 2);
        for (auto const value : digest)
        {
            result.push_back(Hex[value >> 4]);
            result.push_back(Hex[value & 0x0f]);
        }
        return result;
    }

    std::filesystem::path TemporaryPath(std::filesystem::path const& target)
    {
        GUID id{};
        winrt::check_hresult(CoCreateGuid(&id));
        std::array<wchar_t, 40> value{};
        if (StringFromGUID2(id, value.data(), static_cast<int>(value.size())) == 0)
        {
            throw std::runtime_error{ "Windows could not create a temporary file name." };
        }
        auto path = target;
        path += L".";
        path += value.data();
        path += L".tmp";
        return path;
    }
}

namespace HaloDesktop::Storage
{
    FileMutationLock::FileMutationLock(
        std::filesystem::path const& target,
        std::chrono::milliseconds timeout)
    {
        if (timeout.count() < 0 || timeout.count() > static_cast<std::int64_t>((std::numeric_limits<DWORD>::max)()))
        {
            throw std::invalid_argument{ "The Halo file lock timeout is invalid." };
        }
        auto const name = LockName(target);
        auto const mutex = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!mutex)
        {
            ThrowLastError("Could not create a Halo file mutation lock");
        }
        m_mutex = mutex;
        auto const result = WaitForSingleObject(mutex, static_cast<DWORD>(timeout.count()));
        if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED)
        {
            m_owned = true;
            return;
        }
        CloseHandle(mutex);
        m_mutex = nullptr;
        if (result == WAIT_TIMEOUT)
        {
            throw std::runtime_error{ "Timed out waiting for another Halo process to finish writing data." };
        }
        ThrowLastError("Could not acquire a Halo file mutation lock");
    }

    FileMutationLock::~FileMutationLock()
    {
        auto const mutex = static_cast<HANDLE>(m_mutex);
        if (m_owned)
        {
            ReleaseMutex(mutex);
        }
        if (mutex)
        {
            CloseHandle(mutex);
        }
    }

    std::string ReadUtf8File(std::filesystem::path const& path, std::uint64_t maximumBytes)
    {
        std::error_code error;
        auto const size = std::filesystem::file_size(path, error);
        if (error)
        {
            if (error == std::errc::no_such_file_or_directory)
            {
                return {};
            }
            throw std::system_error{ error, "Could not inspect a Halo data file" };
        }
        if (size > maximumBytes
            || size > static_cast<std::uint64_t>((std::numeric_limits<std::streamsize>::max)()))
        {
            throw std::length_error{ "A Halo data file exceeds its size limit." };
        }
        std::ifstream input{ path, std::ios::binary };
        if (!input)
        {
            throw std::runtime_error{ "A Halo data file could not be opened." };
        }
        std::string result(static_cast<std::size_t>(size), '\0');
        input.read(result.data(), static_cast<std::streamsize>(result.size()));
        if (!input && !result.empty())
        {
            throw std::runtime_error{ "A Halo data file could not be read completely." };
        }
        return result;
    }

    void WriteUtf8FileAtomic(std::filesystem::path const& target, std::string_view bytes)
    {
        if (target.empty() || target.parent_path().empty())
        {
            throw std::invalid_argument{ "A complete Halo data file path is required." };
        }
        std::error_code directoryError;
        std::filesystem::create_directories(target.parent_path(), directoryError);
        if (directoryError)
        {
            throw std::system_error{ directoryError, "Could not create a Halo data directory" };
        }

        auto const temporary = TemporaryPath(target);
        auto cleanup = wil::scope_exit([&temporary]() noexcept { DeleteFileW(temporary.c_str()); });
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
            ThrowLastError("Could not create a temporary Halo data file");
        }
        std::size_t offset{};
        while (offset < bytes.size())
        {
            auto const count = static_cast<DWORD>((std::min)(
                bytes.size() - offset,
                static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
            DWORD written{};
            if (!WriteFile(file.get(), bytes.data() + offset, count, &written, nullptr) || written == 0)
            {
                ThrowLastError("Could not write a Halo data file");
            }
            offset += written;
        }
        if (!FlushFileBuffers(file.get()))
        {
            ThrowLastError("Could not flush a Halo data file");
        }
        file.reset();
        if (!MoveFileExW(
            temporary.c_str(),
            target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            ThrowLastError("Could not publish a Halo data file");
        }
        cleanup.release();
    }
}
