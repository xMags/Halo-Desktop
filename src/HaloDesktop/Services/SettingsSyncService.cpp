#include "pch.h"
#include "Services/SettingsSyncService.h"

#include "Api/ApiClient.h"
#include "Api/Dto.h"
#include "Services/QueryCache.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <winrt/Windows.Storage.h>

namespace
{
    constexpr wchar_t SettingsCacheKey[] = L"settings";

    std::int64_t NowMilliseconds()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    std::optional<winrt::hstring> OptionalString(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* key,
        std::size_t maximumLength)
    {
        if (!object.HasKey(key))
        {
            return std::nullopt;
        }
        auto const value = object.GetNamedValue(key);
        if (value.ValueType() != winrt::Windows::Data::Json::JsonValueType::String)
        {
            return std::nullopt;
        }
        auto const text = value.GetString();
        return text.empty() || text.size() > maximumLength
            ? std::nullopt
            : std::optional<winrt::hstring>{ text };
    }
}

namespace HaloDesktop::Services
{
    SettingsSyncService::SettingsSyncService(
        std::shared_ptr<::HaloDesktop::Api::ApiClient> apiClient,
        std::shared_ptr<QueryCache> queryCache,
        winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher)
        : m_apiClient(std::move(apiClient)),
          m_queryCache(std::move(queryCache)),
          m_mirrorPath(std::filesystem::path{
              winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str() }
              / L"settings-mirror.json"),
          m_debounceTimer(dispatcher.CreateTimer())
    {
        if (!m_apiClient || !m_queryCache || !dispatcher)
        {
            throw std::invalid_argument{ "SettingsSyncService requires all dependencies." };
        }
        m_debounceTimer.Interval(std::chrono::milliseconds{ 800 });
        m_debounceTimer.IsRepeating(false);
        m_debounceTimer.Tick([this](auto const&, auto const&)
        {
            static_cast<void>(SaveAsync(Snapshot(), m_updatedAt));
        });
    }

    concurrency::task<void> SettingsSyncService::LoadAsync()
    {
        auto const uiContext = winrt::apartment_context{};
        auto const requestId = m_queryCache->Issue(SettingsCacheKey);
        std::optional<::HaloDesktop::Api::Dto::SettingsPayload> payload;
        try
        {
            payload = co_await m_apiClient->GetSettingsAsync();
        }
        catch (...)
        {
        }
        if (!payload)
        {
            payload = co_await ReadMirrorAsync();
        }
        if (!payload)
        {
            payload = ::HaloDesktop::Api::Dto::SettingsPayload{
                .Value = winrt::Windows::Data::Json::JsonObject{},
                .UpdatedAt = 0,
            };
        }
        co_await uiContext;
        if (m_queryCache->Commit(SettingsCacheKey, requestId, *payload, QueryTtl::Settings))
        {
            Apply(*payload);
            static_cast<void>(WriteMirrorAsync(*payload));
        }
    }

    winrt::Windows::Data::Json::JsonObject SettingsSyncService::RawValue() const
    {
        return Snapshot();
    }

    std::int64_t SettingsSyncService::UpdatedAt() const noexcept { return m_updatedAt; }
    std::optional<winrt::hstring> SettingsSyncService::PreferredAudioLanguage() const { return OptionalString(m_value, L"preferredAudioLang", 8); }
    void SettingsSyncService::PreferredAudioLanguage(std::optional<winrt::hstring> value)
    {
        if (value && value->size() > 8) value.reset();
        SetString(L"preferredAudioLang", std::move(value), false);
    }
    std::optional<winrt::hstring> SettingsSyncService::PreferredSubtitleLanguage() const { return OptionalString(m_value, L"preferredSubtitleLang", 8); }
    void SettingsSyncService::PreferredSubtitleLanguage(std::optional<winrt::hstring> value)
    {
        if (value && value->size() > 8) value.reset();
        SetString(L"preferredSubtitleLang", std::move(value), false);
    }
    std::int32_t SettingsSyncService::SubtitleScalePercent() const noexcept
    {
        try
        {
            auto const value = m_value.GetNamedNumber(L"subtitleScalePercent", 100);
            return std::isfinite(value) && value >= 50 && value <= 200
                ? static_cast<std::int32_t>(value)
                : 100;
        }
        catch (...)
        {
            return 100;
        }
    }
    void SettingsSyncService::SubtitleScalePercent(std::int32_t value)
    {
        m_value.Insert(L"subtitleScalePercent", winrt::Windows::Data::Json::JsonValue::CreateNumberValue(
            std::clamp(value, 50, 200)));
        Touch(true);
    }
    winrt::hstring SettingsSyncService::SubtitleFontFamily() const { return OptionalString(m_value, L"subtitleFontFamily", 64).value_or(L"Segoe UI"); }
    void SettingsSyncService::SubtitleFontFamily(winrt::hstring value)
    {
        if (value.empty() || value.size() > 64)
        {
            value = L"Segoe UI";
        }
        SetString(L"subtitleFontFamily", std::move(value), false);
    }
    winrt::hstring SettingsSyncService::SubtitleOutline() const
    {
        auto const value = OptionalString(m_value, L"subtitleOutline", 16).value_or(L"normal");
        return value == L"none" || value == L"thin" || value == L"normal" || value == L"thick"
            ? value
            : winrt::hstring{ L"normal" };
    }
    void SettingsSyncService::SubtitleOutline(winrt::hstring value)
    {
        if (value != L"none" && value != L"thin" && value != L"normal" && value != L"thick")
        {
            value = L"normal";
        }
        SetString(L"subtitleOutline", std::move(value), false);
    }
    bool SettingsSyncService::SubtitleShadow() const noexcept
    {
        try { return m_value.GetNamedBoolean(L"subtitleShadow", true); }
        catch (...) { return true; }
    }
    void SettingsSyncService::SubtitleShadow(bool value) { SetBoolean(L"subtitleShadow", value); }
    bool SettingsSyncService::AutoplayNextEpisode() const noexcept
    {
        try { return m_value.GetNamedBoolean(L"autoplayNextEpisode", true); }
        catch (...) { return true; }
    }
    void SettingsSyncService::AutoplayNextEpisode(bool value) { SetBoolean(L"autoplayNextEpisode", value); }

    concurrency::task<std::optional<::HaloDesktop::Api::Dto::SettingsPayload>> SettingsSyncService::ReadMirrorAsync()
    {
        co_await winrt::resume_background();
        if (!std::filesystem::exists(m_mirrorPath))
        {
            co_return std::nullopt;
        }
        try
        {
            auto const file = co_await winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(m_mirrorPath.c_str());
            auto const text = co_await winrt::Windows::Storage::FileIO::ReadTextAsync(file);
            co_return ::HaloDesktop::Api::Mappers::ParseSettings(
                winrt::Windows::Data::Json::JsonValue::Parse(text));
        }
        catch (...)
        {
            co_return std::nullopt;
        }
    }

    concurrency::task<void> SettingsSyncService::WriteMirrorAsync(::HaloDesktop::Api::Dto::SettingsPayload payload)
    {
        co_await winrt::resume_background();
        try
        {
            winrt::Windows::Data::Json::JsonObject root;
            root.Insert(L"value", payload.Value);
            root.Insert(L"updatedAt", winrt::Windows::Data::Json::JsonValue::CreateNumberValue(
                static_cast<double>(payload.UpdatedAt)));
            auto const encoded = winrt::to_string(root.Stringify());

            std::scoped_lock const lock{ m_mirrorMutex };
            if (payload.UpdatedAt < m_mirrorWrittenAt)
            {
                co_return;
            }
            auto temporary = m_mirrorPath;
            temporary += L".tmp";
            auto written = false;
            {
                std::ofstream file{ temporary, std::ios::binary | std::ios::trunc };
                if (!file)
                {
                    DeleteFileW(temporary.c_str());
                }
                else
                {
                    file.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
                    file.flush();
                    written = static_cast<bool>(file);
                }
            }
            if (!written)
            {
                DeleteFileW(temporary.c_str());
                co_return;
            }
            if (!MoveFileExW(
                temporary.c_str(),
                m_mirrorPath.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                DeleteFileW(temporary.c_str());
                co_return;
            }
            m_mirrorWrittenAt = payload.UpdatedAt;
        }
        catch (...)
        {
        }
    }

    concurrency::task<void> SettingsSyncService::SaveAsync(
        winrt::Windows::Data::Json::JsonObject snapshot,
        std::int64_t updatedAt)
    {
        auto const uiContext = winrt::apartment_context{};
        auto const version = ++m_writeVersion;
        std::optional<::HaloDesktop::Api::Dto::SettingsPayload> echo;
        try
        {
            echo = co_await m_apiClient->PutSettingsAsync(snapshot, updatedAt);
        }
        catch (...)
        {
        }
        co_await uiContext;
        if (!echo)
        {
            m_queryCache->Invalidate(SettingsCacheKey);
            if (version == m_writeVersion)
            {
                static_cast<void>(LoadAsync());
            }
            co_return;
        }
        if (echo->UpdatedAt >= m_updatedAt)
        {
            Apply(*echo);
        }
        m_queryCache->Invalidate(SettingsCacheKey);
        static_cast<void>(WriteMirrorAsync(::HaloDesktop::Api::Dto::SettingsPayload{
            .Value = Snapshot(),
            .UpdatedAt = m_updatedAt,
        }));
    }

    void SettingsSyncService::Apply(::HaloDesktop::Api::Dto::SettingsPayload payload)
    {
        m_value = winrt::Windows::Data::Json::JsonObject::Parse(payload.Value.Stringify());
        m_updatedAt = payload.UpdatedAt;
    }

    void SettingsSyncService::SetString(
        wchar_t const* key,
        std::optional<winrt::hstring> value,
        bool debounce)
    {
        if (value && !value->empty())
        {
            m_value.Insert(key, winrt::Windows::Data::Json::JsonValue::CreateStringValue(*value));
        }
        else if (m_value.HasKey(key))
        {
            m_value.Remove(key);
        }
        Touch(debounce);
    }

    void SettingsSyncService::SetBoolean(wchar_t const* key, bool value)
    {
        m_value.Insert(key, winrt::Windows::Data::Json::JsonValue::CreateBooleanValue(value));
        Touch(false);
    }

    void SettingsSyncService::Touch(bool debounce)
    {
        m_updatedAt = NextTimestamp();
        static_cast<void>(WriteMirrorAsync(::HaloDesktop::Api::Dto::SettingsPayload{
            .Value = Snapshot(),
            .UpdatedAt = m_updatedAt,
        }));
        if (debounce)
        {
            m_debounceTimer.Stop();
            m_debounceTimer.Start();
        }
        else
        {
            static_cast<void>(SaveAsync(Snapshot(), m_updatedAt));
        }
    }

    std::int64_t SettingsSyncService::NextTimestamp() const noexcept
    {
        return (std::max)(NowMilliseconds(), m_updatedAt + 1);
    }

    winrt::Windows::Data::Json::JsonObject SettingsSyncService::Snapshot() const
    {
        return winrt::Windows::Data::Json::JsonObject::Parse(m_value.Stringify());
    }
}
