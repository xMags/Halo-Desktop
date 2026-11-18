#pragma once

#include "Api/Dto.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <winrt/Windows.Data.Json.h>

namespace HaloDesktop::Services::Device
{
    // The documents Halo keeps on this PC without an account, in the
    // reference's shapes (`device/deviceBackend.ts`, `device/deviceStore.ts`):
    // `{ version: 1, addons | items | states: [...] }`, `{ version: 1, value,
    // updatedAt }` and the profile `{ version: 1, id, createdAt }`. Also the
    // wire shapes the server answers with, so the app's mappers read a device
    // answer exactly as they read the server's. No I/O.

    enum class DeviceCollection
    {
        Profile,
        Addons,
        Library,
        WatchStates,
        Settings,
    };

    // The reference's collection names (`watchStates`), as its messages use them.
    [[nodiscard]] std::wstring_view CollectionName(DeviceCollection collection) noexcept;
    [[nodiscard]] std::wstring_view CollectionFileName(DeviceCollection collection) noexcept;

    // The document exists but cannot be read back: ApiError::DeviceDataUnreadable.
    [[noreturn]] void ThrowDeviceDataUnreadable(DeviceCollection collection);

    // UTF-8 to text, refusing invalid sequences; a leading byte order mark is skipped.
    [[nodiscard]] std::optional<winrt::hstring> DecodeUtf8(std::string_view bytes);

    struct StoredAddon final
    {
        std::wstring Id;
        std::wstring TransportUrl;
        winrt::Windows::Data::Json::JsonObject Manifest{ nullptr };
        bool HideCatalogs{};
        std::int64_t AddedAt{};
    };

    struct StoredSettings final
    {
        winrt::Windows::Data::Json::JsonObject Value{ nullptr };
        double UpdatedAt{};
    };

    // Readers take the stored text (empty when the document was never
    // written, which reads as nothing stored) and throw DeviceDataUnreadable
    // for anything that is not the document they expect.
    [[nodiscard]] std::vector<StoredAddon> ReadAddonsDocument(std::optional<std::string> const& text);
    [[nodiscard]] std::vector<Api::Dto::LibraryRow> ReadLibraryDocument(std::optional<std::string> const& text);
    [[nodiscard]] std::vector<Api::Dto::WatchEntry> ReadWatchStatesDocument(std::optional<std::string> const& text);
    [[nodiscard]] std::optional<StoredSettings> ReadSettingsDocument(std::optional<std::string> const& text);

    // UTF-8 document text.
    [[nodiscard]] std::string WriteAddonsDocument(std::vector<StoredAddon> const& addons);
    [[nodiscard]] std::string WriteLibraryDocument(std::vector<Api::Dto::LibraryRow> const& items);
    [[nodiscard]] std::string WriteWatchStatesDocument(std::vector<Api::Dto::WatchEntry> const& states);
    [[nodiscard]] std::string WriteSettingsDocument(winrt::Windows::Data::Json::JsonObject const& value, std::int64_t updatedAt);

    // GET /addons as the server answers it: no global entries, and a hidden
    // addon's catalogs stripped.
    [[nodiscard]] winrt::Windows::Data::Json::JsonObject AddonsWire(std::vector<StoredAddon> const& addons);
    [[nodiscard]] winrt::Windows::Data::Json::JsonArray LibraryWire(std::vector<Api::Dto::LibraryRow> const& items);
    [[nodiscard]] winrt::Windows::Data::Json::JsonArray WatchStatesWire(std::vector<Api::Dto::WatchEntry> const& states);

    // Marks that this PC is used without an account; its id partitions the
    // downloads made in that mode.
    struct DeviceProfile final
    {
        std::wstring Id;
        std::int64_t CreatedAt{};
    };

    // Empty for anything but `{ version: 1, id: <non-empty string>, createdAt: <number> }`.
    [[nodiscard]] std::optional<DeviceProfile> ParseDeviceProfile(std::string_view text);
    [[nodiscard]] std::string SerializeDeviceProfile(DeviceProfile const& profile);
    // A random (version 4) UUID in lower case, as crypto.randomUUID writes one.
    [[nodiscard]] std::wstring NewDeviceProfileId();

    // The downloads partition for Halo without an account: the SHA-256 hex
    // of "device\n{id}", the reference's opaqueDownloadOwner("device", id).
    // "device" can never be a server URL, so it never collides with an
    // account's partition.
    [[nodiscard]] std::wstring DeviceDownloadOwner(DeviceProfile const& profile);
}
