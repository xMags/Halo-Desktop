#include "pch.h"
#include "Storage/LegacyPackageDataSource.h"

#include <algorithm>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Management.Core.h>
#include <winrt/Windows.Management.Deployment.h>
#include <winrt/Windows.Storage.h>

namespace
{
    constexpr wchar_t LegacyPackageName[] = L"56fcb18b-d21c-4111-93fb-bef0ffa36c43";
    constexpr wchar_t LegacyPackagePublisher[] = L"CN=info";
    constexpr wchar_t ThemeKey[] = L"HaloDesktop.Appearance.Theme";
    constexpr wchar_t SearchHistoryKey[] = L"halo.searchHistory.v1";
    constexpr wchar_t ResumeKey[] = L"halo.resumePlayback.v1";
    constexpr wchar_t HardwareDecodingKey[] = L"halo.hardwareDecoding.v1";
    constexpr wchar_t SubtitleMemoryKey[] = L"halo.subtitleMemory.v1";
    constexpr std::size_t MaximumHistoryItems = 20;
    constexpr std::size_t MaximumHistoryLength = 120;
    constexpr std::size_t MaximumSubtitleMemoryCharacters = 512u * 1024u;

    template <typename Value>
    std::optional<Value> Unbox(
        winrt::Windows::Foundation::Collections::IPropertySet const& values,
        wchar_t const* key) noexcept
    {
        try
        {
            auto const value = values.TryLookup(key);
            return value ? std::optional<Value>{ winrt::unbox_value<Value>(value) } : std::nullopt;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    void ImportSearchHistory(
        winrt::Windows::Foundation::Collections::IPropertySet const& values,
        HaloDesktop::Services::DevicePreferences& preferences) noexcept
    {
        try
        {
            auto const raw = Unbox<winrt::hstring>(values, SearchHistoryKey);
            if (!raw)
            {
                return;
            }
            auto const array = winrt::Windows::Data::Json::JsonArray::Parse(*raw);
            auto const count = (std::min)(array.Size(), static_cast<std::uint32_t>(MaximumHistoryItems));
            for (std::uint32_t index{}; index < count; ++index)
            {
                auto const value = array.GetAt(index);
                if (value.ValueType() != winrt::Windows::Data::Json::JsonValueType::String)
                {
                    continue;
                }
                auto const text = value.GetString();
                if (!text.empty() && text.size() <= MaximumHistoryLength)
                {
                    preferences.SearchHistory.push_back(text);
                }
            }
        }
        catch (...)
        {
        }
    }

    void ImportSubtitleMemory(
        winrt::Windows::Foundation::Collections::IPropertySet const& values,
        HaloDesktop::Services::DevicePreferences& preferences) noexcept
    {
        try
        {
            auto const raw = Unbox<winrt::hstring>(values, SubtitleMemoryKey);
            if (!raw || raw->size() > MaximumSubtitleMemoryCharacters)
            {
                return;
            }
            preferences.SubtitleSelectionMemory = winrt::Windows::Data::Json::JsonObject::Parse(*raw);
        }
        catch (...)
        {
        }
    }
}

namespace HaloDesktop::Storage
{
    std::optional<LegacyPackageData> InstalledLegacyPackageDataSource::Read()
    {
        winrt::Windows::ApplicationModel::Package package{ nullptr };
        winrt::Windows::Management::Deployment::PackageManager manager;
        for (auto const& candidate : manager.FindPackagesForUser(
            L"", LegacyPackageName, LegacyPackagePublisher))
        {
            if (candidate.Status().VerifyIsOK())
            {
                package = candidate;
                break;
            }
        }
        if (!package)
        {
            return std::nullopt;
        }

        auto const data = winrt::Windows::Management::Core::ApplicationDataManager::CreateForPackageFamily(
            package.Id().FamilyName());
        LegacyPackageData result{
            .LocalState = std::filesystem::path{ data.LocalFolder().Path().c_str() },
        };
        auto const values = data.LocalSettings().Values();
        if (auto const theme = Unbox<std::int32_t>(values, ThemeKey); theme && *theme >= 0 && *theme <= 2)
        {
            result.Preferences.Theme = *theme;
        }
        if (auto const resume = Unbox<bool>(values, ResumeKey))
        {
            result.Preferences.ResumePlayback = *resume;
        }
        if (auto const hardware = Unbox<bool>(values, HardwareDecodingKey))
        {
            result.Preferences.HardwareDecoding = *hardware;
        }
        ImportSearchHistory(values, result.Preferences);
        ImportSubtitleMemory(values, result.Preferences);
        return result;
    }
}
