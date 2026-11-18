#include "pch.h"
#include "Services/Device/DeviceDocuments.h"

#include "Api/ApiError.h"
#include "Services/Downloads/DownloadTypes.h"

#include <array>
#include <climits>
#include <cmath>
#include <combaseapi.h>
#include <stdexcept>

namespace
{
    namespace Json = winrt::Windows::Data::Json;
    namespace Device = HaloDesktop::Services::Device;
    namespace Dto = HaloDesktop::Api::Dto;

    constexpr double DocumentVersion = 1;
    // 2^63: the first double outside int64_t.
    constexpr double Int64ExclusiveBound = 9223372036854775808.0;

    std::optional<Json::JsonObject> ParseDocument(std::string_view text)
    {
        auto const decoded = Device::DecodeUtf8(text);
        if (!decoded) return std::nullopt;
        Json::JsonObject document{ nullptr };
        if (!Json::JsonObject::TryParse(*decoded, document) || !document) return std::nullopt;
        if (!document.HasKey(L"version")) return std::nullopt;
        auto const version = document.GetNamedValue(L"version");
        if (version.ValueType() != Json::JsonValueType::Number || version.GetNumber() != DocumentVersion) return std::nullopt;
        return document;
    }

    // The document's list, or empty when nothing is stored. Unreadable when
    // the document or its list is not what it should be.
    std::optional<Json::JsonArray> ReadList(
        std::optional<std::string> const& text,
        wchar_t const* field,
        Device::DeviceCollection collection)
    {
        if (!text) return std::nullopt;
        auto const document = ParseDocument(*text);
        if (!document || !document->HasKey(field)) Device::ThrowDeviceDataUnreadable(collection);
        auto const list = document->GetNamedValue(field);
        if (list.ValueType() != Json::JsonValueType::Array) Device::ThrowDeviceDataUnreadable(collection);
        return list.GetArray();
    }

    // Field readers for stored rows. A stored row was valid when it was
    // written, so a row that is not is the document's fault.
    struct RowReader final
    {
        Json::JsonObject Row;
        Device::DeviceCollection Collection;

        [[noreturn]] void Unreadable() const
        {
            Device::ThrowDeviceDataUnreadable(Collection);
        }

        Json::IJsonValue Value(wchar_t const* key) const
        {
            return Row.HasKey(key) ? Row.GetNamedValue(key) : Json::IJsonValue{ nullptr };
        }

        winrt::hstring String(wchar_t const* key) const
        {
            auto const value = Value(key);
            if (!value || value.ValueType() != Json::JsonValueType::String) Unreadable();
            return value.GetString();
        }

        std::optional<winrt::hstring> OptionalString(wchar_t const* key) const
        {
            auto const value = Value(key);
            if (!value || value.ValueType() == Json::JsonValueType::Null) return std::nullopt;
            if (value.ValueType() != Json::JsonValueType::String) Unreadable();
            return value.GetString();
        }

        double Number(wchar_t const* key) const
        {
            auto const value = Value(key);
            if (!value || value.ValueType() != Json::JsonValueType::Number || !std::isfinite(value.GetNumber())) Unreadable();
            return value.GetNumber();
        }

        std::int64_t Integer(wchar_t const* key) const
        {
            auto const number = Number(key);
            if (std::floor(number) != number || number <= -Int64ExclusiveBound || number >= Int64ExclusiveBound) Unreadable();
            return static_cast<std::int64_t>(number);
        }

        std::optional<std::int64_t> OptionalInteger(wchar_t const* key) const
        {
            auto const value = Value(key);
            if (!value || value.ValueType() == Json::JsonValueType::Null) return std::nullopt;
            return Integer(key);
        }

        bool Boolean(wchar_t const* key, std::optional<bool> fallback = std::nullopt) const
        {
            auto const value = Value(key);
            if (!value && fallback) return *fallback;
            if (!value || value.ValueType() != Json::JsonValueType::Boolean) Unreadable();
            return value.GetBoolean();
        }
    };

    RowReader RowAt(Json::IJsonValue const& value, Device::DeviceCollection collection)
    {
        if (!value || value.ValueType() != Json::JsonValueType::Object) Device::ThrowDeviceDataUnreadable(collection);
        return { value.GetObject(), collection };
    }

    std::string Utf8(winrt::hstring const& text)
    {
        return winrt::to_string(text);
    }

    std::string ListDocument(wchar_t const* field, Json::JsonArray const& list)
    {
        Json::JsonObject document;
        document.Insert(L"version", Json::JsonValue::CreateNumberValue(DocumentVersion));
        document.Insert(field, list);
        return Utf8(document.Stringify());
    }

    Json::JsonValue Number(std::int64_t value)
    {
        return Json::JsonValue::CreateNumberValue(static_cast<double>(value));
    }

    Json::JsonObject LibraryRowJson(Dto::LibraryRow const& row)
    {
        Json::JsonObject object;
        object.Insert(L"id", Json::JsonValue::CreateStringValue(row.Id));
        object.Insert(L"type", Json::JsonValue::CreateStringValue(row.Type));
        object.Insert(L"name", Json::JsonValue::CreateStringValue(row.Name));
        if (row.Poster) object.Insert(L"poster", Json::JsonValue::CreateStringValue(*row.Poster));
        object.Insert(L"addedAt", Number(row.AddedAt));
        if (row.RemovedAt) object.Insert(L"removedAt", Number(*row.RemovedAt));
        object.Insert(L"updatedAt", Number(row.UpdatedAt));
        return object;
    }

    Json::JsonObject WatchEntryJson(Dto::WatchEntry const& row)
    {
        Json::JsonObject object;
        object.Insert(L"videoId", Json::JsonValue::CreateStringValue(row.VideoId));
        object.Insert(L"itemId", Json::JsonValue::CreateStringValue(row.ItemId));
        object.Insert(L"positionSec", Json::JsonValue::CreateNumberValue(row.PositionSec));
        object.Insert(L"durationSec", Json::JsonValue::CreateNumberValue(row.DurationSec));
        object.Insert(L"watched", Json::JsonValue::CreateBooleanValue(row.Watched));
        if (row.Name) object.Insert(L"name", Json::JsonValue::CreateStringValue(*row.Name));
        if (row.Poster) object.Insert(L"poster", Json::JsonValue::CreateStringValue(*row.Poster));
        if (row.RemovedAt) object.Insert(L"removedAt", Number(*row.RemovedAt));
        object.Insert(L"updatedAt", Number(row.UpdatedAt));
        return object;
    }

    Json::JsonObject StoredAddonJson(Device::StoredAddon const& addon)
    {
        Json::JsonObject object;
        object.Insert(L"id", Json::JsonValue::CreateStringValue(addon.Id));
        object.Insert(L"transportUrl", Json::JsonValue::CreateStringValue(addon.TransportUrl));
        object.Insert(L"manifest", addon.Manifest);
        object.Insert(L"hideCatalogs", Json::JsonValue::CreateBooleanValue(addon.HideCatalogs));
        object.Insert(L"addedAt", Number(addon.AddedAt));
        return object;
    }
}

namespace HaloDesktop::Services::Device
{
    std::wstring_view CollectionName(DeviceCollection collection) noexcept
    {
        switch (collection)
        {
        case DeviceCollection::Profile: return L"profile";
        case DeviceCollection::Addons: return L"addons";
        case DeviceCollection::Library: return L"library";
        case DeviceCollection::WatchStates: return L"watchStates";
        case DeviceCollection::Settings: return L"settings";
        }
        return L"";
    }

    std::wstring_view CollectionFileName(DeviceCollection collection) noexcept
    {
        switch (collection)
        {
        case DeviceCollection::Profile: return L"profile.json";
        case DeviceCollection::Addons: return L"addons.bin";
        case DeviceCollection::Library: return L"library.json";
        case DeviceCollection::WatchStates: return L"watch-states.json";
        case DeviceCollection::Settings: return L"settings.json";
        }
        return L"";
    }

    void ThrowDeviceDataUnreadable(DeviceCollection collection)
    {
        throw winrt::hresult_error{
            ::HaloDesktop::Api::ApiError::DeviceDataUnreadable,
            winrt::hstring{ std::wstring{ L"The " } + std::wstring{ CollectionName(collection) }
                + L" saved on this device could not be read." } };
    }

    std::optional<winrt::hstring> DecodeUtf8(std::string_view bytes)
    {
        if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF
            && static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF)
        {
            bytes.remove_prefix(3);
        }
        if (bytes.empty()) return winrt::hstring{};
        if (bytes.size() > static_cast<std::size_t>(INT_MAX)) return std::nullopt;
        auto const size = static_cast<int>(bytes.size());
        auto const characters = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), size, nullptr, 0);
        if (characters <= 0) return std::nullopt;
        std::wstring text(static_cast<std::size_t>(characters), L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), size, text.data(), characters) != characters)
        {
            return std::nullopt;
        }
        return winrt::hstring{ text };
    }

    std::vector<StoredAddon> ReadAddonsDocument(std::optional<std::string> const& text)
    {
        std::vector<StoredAddon> addons;
        auto const list = ReadList(text, L"addons", DeviceCollection::Addons);
        if (!list) return addons;
        for (auto const& item : *list)
        {
            auto const row = RowAt(item, DeviceCollection::Addons);
            auto const manifest = row.Value(L"manifest");
            if (!manifest || manifest.ValueType() != Json::JsonValueType::Object) row.Unreadable();
            auto const id = row.String(L"id");
            if (id.empty()) row.Unreadable();
            addons.push_back({
                std::wstring{ id },
                std::wstring{ row.String(L"transportUrl") },
                manifest.GetObject(),
                row.Boolean(L"hideCatalogs", false),
                row.OptionalInteger(L"addedAt").value_or(0),
            });
        }
        return addons;
    }

    std::vector<Dto::LibraryRow> ReadLibraryDocument(std::optional<std::string> const& text)
    {
        std::vector<Dto::LibraryRow> rows;
        auto const list = ReadList(text, L"items", DeviceCollection::Library);
        if (!list) return rows;
        for (auto const& item : *list)
        {
            auto const row = RowAt(item, DeviceCollection::Library);
            rows.push_back({
                row.String(L"id"),
                row.String(L"type"),
                row.String(L"name"),
                row.OptionalString(L"poster"),
                row.Integer(L"addedAt"),
                row.OptionalInteger(L"removedAt"),
                row.Integer(L"updatedAt"),
            });
        }
        return rows;
    }

    std::vector<Dto::WatchEntry> ReadWatchStatesDocument(std::optional<std::string> const& text)
    {
        std::vector<Dto::WatchEntry> rows;
        auto const list = ReadList(text, L"states", DeviceCollection::WatchStates);
        if (!list) return rows;
        for (auto const& item : *list)
        {
            auto const row = RowAt(item, DeviceCollection::WatchStates);
            rows.push_back({
                row.String(L"videoId"),
                row.String(L"itemId"),
                row.Number(L"positionSec"),
                row.Number(L"durationSec"),
                row.Boolean(L"watched"),
                row.OptionalString(L"name"),
                row.OptionalString(L"poster"),
                row.Integer(L"updatedAt"),
                row.OptionalInteger(L"removedAt"),
            });
        }
        return rows;
    }

    std::optional<StoredSettings> ReadSettingsDocument(std::optional<std::string> const& text)
    {
        if (!text) return std::nullopt;
        auto const document = ParseDocument(*text);
        if (!document || !document->HasKey(L"value") || !document->HasKey(L"updatedAt"))
        {
            ThrowDeviceDataUnreadable(DeviceCollection::Settings);
        }
        auto const value = document->GetNamedValue(L"value");
        auto const updatedAt = document->GetNamedValue(L"updatedAt");
        if (value.ValueType() != Json::JsonValueType::Object || updatedAt.ValueType() != Json::JsonValueType::Number)
        {
            ThrowDeviceDataUnreadable(DeviceCollection::Settings);
        }
        return StoredSettings{ value.GetObject(), updatedAt.GetNumber() };
    }

    std::string WriteAddonsDocument(std::vector<StoredAddon> const& addons)
    {
        Json::JsonArray list;
        for (auto const& addon : addons) list.Append(StoredAddonJson(addon));
        return ListDocument(L"addons", list);
    }

    std::string WriteLibraryDocument(std::vector<Dto::LibraryRow> const& items)
    {
        return ListDocument(L"items", LibraryWire(items));
    }

    std::string WriteWatchStatesDocument(std::vector<Dto::WatchEntry> const& states)
    {
        return ListDocument(L"states", WatchStatesWire(states));
    }

    std::string WriteSettingsDocument(Json::JsonObject const& value, std::int64_t updatedAt)
    {
        Json::JsonObject document;
        document.Insert(L"version", Json::JsonValue::CreateNumberValue(DocumentVersion));
        document.Insert(L"value", value);
        document.Insert(L"updatedAt", Number(updatedAt));
        return Utf8(document.Stringify());
    }

    Json::JsonObject AddonsWire(std::vector<StoredAddon> const& addons)
    {
        Json::JsonArray user;
        for (std::size_t position = 0; position < addons.size(); ++position)
        {
            auto const& addon = addons[position];
            Json::JsonObject entry;
            entry.Insert(L"id", Json::JsonValue::CreateStringValue(addon.Id));
            entry.Insert(L"transportUrl", Json::JsonValue::CreateStringValue(addon.TransportUrl));
            if (addon.HideCatalogs)
            {
                // { ...manifest, catalogs: [] }
                Json::JsonObject manifest;
                for (auto const& field : addon.Manifest) manifest.Insert(field.Key(), field.Value());
                manifest.SetNamedValue(L"catalogs", Json::JsonArray{});
                entry.Insert(L"manifest", manifest);
            }
            else
            {
                entry.Insert(L"manifest", addon.Manifest);
            }
            entry.Insert(L"position", Json::JsonValue::CreateNumberValue(static_cast<double>(position)));
            if (addon.HideCatalogs) entry.Insert(L"hideCatalogs", Json::JsonValue::CreateBooleanValue(true));
            user.Append(entry);
        }
        Json::JsonObject payload;
        payload.Insert(L"global", Json::JsonArray{});
        payload.Insert(L"user", user);
        return payload;
    }

    Json::JsonArray LibraryWire(std::vector<Dto::LibraryRow> const& items)
    {
        Json::JsonArray list;
        for (auto const& item : items) list.Append(LibraryRowJson(item));
        return list;
    }

    Json::JsonArray WatchStatesWire(std::vector<Dto::WatchEntry> const& states)
    {
        Json::JsonArray list;
        for (auto const& state : states) list.Append(WatchEntryJson(state));
        return list;
    }

    std::optional<DeviceProfile> ParseDeviceProfile(std::string_view text)
    {
        auto const document = ParseDocument(text);
        if (!document || !document->HasKey(L"id") || !document->HasKey(L"createdAt")) return std::nullopt;
        auto const id = document->GetNamedValue(L"id");
        auto const createdAt = document->GetNamedValue(L"createdAt");
        if (id.ValueType() != Json::JsonValueType::String || id.GetString().empty()) return std::nullopt;
        if (createdAt.ValueType() != Json::JsonValueType::Number) return std::nullopt;
        auto const milliseconds = createdAt.GetNumber();
        if (!std::isfinite(milliseconds) || milliseconds <= -Int64ExclusiveBound || milliseconds >= Int64ExclusiveBound)
        {
            return std::nullopt;
        }
        return DeviceProfile{ std::wstring{ id.GetString() }, static_cast<std::int64_t>(milliseconds) };
    }

    std::string SerializeDeviceProfile(DeviceProfile const& profile)
    {
        Json::JsonObject document;
        document.Insert(L"version", Json::JsonValue::CreateNumberValue(DocumentVersion));
        document.Insert(L"id", Json::JsonValue::CreateStringValue(profile.Id));
        document.Insert(L"createdAt", Number(profile.CreatedAt));
        return Utf8(document.Stringify());
    }

    std::wstring NewDeviceProfileId()
    {
        GUID id{};
        winrt::check_hresult(CoCreateGuid(&id));
        std::array<wchar_t, 40> text{};
        if (StringFromGUID2(id, text.data(), static_cast<int>(text.size())) == 0)
        {
            throw std::runtime_error{ "Windows could not create a profile id." };
        }
        // "{XXXXXXXX-XXXX-...}" to the bare lower-case form.
        std::wstring value{ text.data() };
        value = value.substr(1, value.size() - 2);
        for (auto& character : value)
        {
            if (character >= L'A' && character <= L'Z') character = static_cast<wchar_t>(character - L'A' + L'a');
        }
        return value;
    }

    std::wstring DeviceDownloadOwner(DeviceProfile const& profile)
    {
        return ::HaloDesktop::Services::Downloads::MakeAccountKey(L"device", profile.Id);
    }
}
