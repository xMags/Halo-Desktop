#include "pch.h"
#include "Services/Device/DeviceStore.h"

#include "Security/Dpapi.h"
#include "Storage/FileStorage.h"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>
#include <wil/resource.h>

namespace
{
    namespace Device = HaloDesktop::Services::Device;

    // DPAPI adds a header and padding to what it protects.
    constexpr std::uint64_t ProtectionAllowanceBytes = 64u * 1024u;

    // The order ClearAsync removes them in. The profile goes last: until it is
    // gone the move into an account is still pending and runs again, and every
    // step of it is safe to repeat. Removed first (the reference's order), a
    // crash part way would leave documents with no profile, and the next
    // profile created here would pick them up as its own.
    constexpr std::array<Device::DeviceCollection, 5> ClearOrder{
        Device::DeviceCollection::Addons,
        Device::DeviceCollection::Library,
        Device::DeviceCollection::WatchStates,
        Device::DeviceCollection::Settings,
        Device::DeviceCollection::Profile,
    };

    [[noreturn]] void ThrowLastError(char const* operation)
    {
        throw std::system_error{ static_cast<int>(GetLastError()), std::system_category(), operation };
    }

    // The file's bytes, or empty when it does not exist. A file larger than
    // a document may be is unreadable.
    std::optional<std::string> ReadPlainFile(std::filesystem::path const& path, Device::DeviceCollection collection)
    {
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
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return std::nullopt;
            ThrowLastError("Could not open this PC's saved data");
        }
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file.get(), &size)) ThrowLastError("Could not inspect this PC's saved data");
        if (size.QuadPart < 0 || static_cast<std::uint64_t>(size.QuadPart) > Device::DeviceStore::MaximumDocumentBytes)
        {
            Device::ThrowDeviceDataUnreadable(collection);
        }
        std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
        std::size_t offset{};
        while (offset < bytes.size())
        {
            auto const chunk = static_cast<DWORD>((std::min)(
                bytes.size() - offset,
                static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
            DWORD read{};
            if (!ReadFile(file.get(), bytes.data() + offset, chunk, &read, nullptr)) ThrowLastError("Could not read this PC's saved data");
            if (read == 0) throw std::runtime_error{ "This PC's saved data ended unexpectedly." };
            offset += read;
        }
        return bytes;
    }

    bool FileExists(std::filesystem::path const& path)
    {
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
        auto const error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return false;
        ThrowLastError("Could not inspect this PC's saved data");
    }

    void DeleteIfPresent(std::filesystem::path const& path)
    {
        if (DeleteFileW(path.c_str())) return;
        auto const error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return;
        ThrowLastError("Could not remove this PC's saved data");
    }

    // The document's text, for a caller holding the file's lock on this
    // thread. An addon list that cannot be decrypted, or one too large, is
    // unreadable and never overwritten.
    std::optional<std::string> ReadLocked(std::filesystem::path const& path, Device::DeviceCollection collection);

    void RequireStorableSize(std::string const& contents)
    {
        if (contents.size() > Device::DeviceStore::MaximumDocumentBytes)
        {
            throw std::length_error{ "This PC's saved data is too large to store." };
        }
    }
}

namespace
{
    std::optional<std::string> ReadLocked(std::filesystem::path const& path, Device::DeviceCollection collection)
    {
        if (collection != Device::DeviceCollection::Addons) return ReadPlainFile(path, collection);
        try
        {
            return ::HaloDesktop::Security::ReadProtectedTextLocked(
                path, Device::DeviceStore::MaximumDocumentBytes + ProtectionAllowanceBytes);
        }
        catch (std::length_error const&)
        {
        }
        catch (winrt::hresult_error const&)
        {
            // Windows could not open or decrypt it; it stays as it is.
        }
        Device::ThrowDeviceDataUnreadable(collection);
    }

    void WriteLocked(std::filesystem::path const& path, Device::DeviceCollection collection, std::string contents)
    {
        if (collection == Device::DeviceCollection::Addons)
        {
            ::HaloDesktop::Security::WriteProtectedTextLocked(path, std::move(contents));
            return;
        }
        ::HaloDesktop::Storage::WriteUtf8FileAtomic(path, contents);
    }
}

namespace HaloDesktop::Services::Device
{
    DeviceStore::DeviceStore(std::filesystem::path folder)
        : m_folder(std::move(folder))
    {
        if (m_folder.empty() || !m_folder.is_absolute())
        {
            throw std::invalid_argument{ "The device store needs an absolute folder." };
        }
    }

    std::filesystem::path const& DeviceStore::Folder() const noexcept
    {
        return m_folder;
    }

    std::filesystem::path DeviceStore::PathFor(DeviceCollection collection) const
    {
        return m_folder / std::wstring{ CollectionFileName(collection) };
    }

    concurrency::task<std::optional<std::string>> DeviceStore::ReadAsync(DeviceCollection collection)
    {
        auto const path = PathFor(collection);
        co_await winrt::resume_background();
        ::HaloDesktop::Storage::FileMutationLock const lock{ path };
        co_return ReadLocked(path, collection);
    }

    concurrency::task<void> DeviceStore::WriteAsync(DeviceCollection collection, std::string contents)
    {
        auto const path = PathFor(collection);
        co_await winrt::resume_background();
        RequireStorableSize(contents);
        ::HaloDesktop::Storage::FileMutationLock const lock{ path };
        WriteLocked(path, collection, std::move(contents));
    }

    concurrency::task<void> DeviceStore::UpdateAsync(DeviceCollection collection, DocumentTransform transform)
    {
        if (!transform) throw std::invalid_argument{ "A device document update needs a transform." };
        auto const path = PathFor(collection);
        co_await winrt::resume_background();
        // From here to the end nothing is awaited, so the lock is released on
        // the thread that took it.
        ::HaloDesktop::Storage::FileMutationLock const lock{ path };
        auto next = transform(ReadLocked(path, collection));
        if (!next) co_return;
        RequireStorableSize(*next);
        WriteLocked(path, collection, std::move(*next));
    }

    concurrency::task<std::optional<DeviceProfile>> DeviceStore::ReadProfileAsync()
    {
        auto const text = co_await ReadAsync(DeviceCollection::Profile);
        if (!text) co_return std::nullopt;
        auto profile = ParseDeviceProfile(*text);
        if (!profile) ThrowDeviceDataUnreadable(DeviceCollection::Profile);
        co_return profile;
    }

    concurrency::task<DeviceProfile> DeviceStore::CreateProfileAsync(std::int64_t createdAt)
    {
        auto const path = PathFor(DeviceCollection::Profile);
        co_await winrt::resume_background();
        DeviceProfile profile{ NewDeviceProfileId(), createdAt };
        auto const contents = SerializeDeviceProfile(profile);
        ::HaloDesktop::Storage::FileMutationLock const lock{ path };
        if (FileExists(path)) throw std::runtime_error{ "This PC already has a profile." };
        ::HaloDesktop::Storage::WriteUtf8FileAtomic(path, contents);
        co_return profile;
    }

    concurrency::task<void> DeviceStore::ClearAsync()
    {
        std::vector<std::pair<DeviceCollection, std::filesystem::path>> paths;
        for (auto const collection : ClearOrder) paths.emplace_back(collection, PathFor(collection));
        co_await winrt::resume_background();
        for (auto const& [collection, path] : paths)
        {
            if (collection == DeviceCollection::Addons)
            {
                co_await ::HaloDesktop::Security::DeleteProtectedFileAsync(path);
                continue;
            }
            ::HaloDesktop::Storage::FileMutationLock const lock{ path };
            DeleteIfPresent(path);
        }
    }
}
