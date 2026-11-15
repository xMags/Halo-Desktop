#include "pch.h"
#include "Services/SettingsSyncService.h"
#include "Services/SettingsSyncPolicy.h"

#include "Api/ApiClient.h"
#include "Api/Dto.h"
#include "Services/QueryCache.h"
#include "Services/Downloads/DownloadTypes.h"
#include "Storage/AppStoragePaths.h"
#include "Storage/FileStorage.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

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
        std::shared_ptr<::HaloDesktop::Storage::AppStoragePaths const> paths,
        winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher)
        : m_apiClient(std::move(apiClient)),
          m_queryCache(std::move(queryCache)),
          m_mirrorRoot(paths ? paths->LocalState() / L"settings" : std::filesystem::path{}),
          m_debounceTimer(dispatcher.CreateTimer())
    {
        if (!m_apiClient || !m_queryCache || !paths || !dispatcher)
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
        auto const accountVersion = m_accountVersion;
        auto const requestWriteVersion = m_writeVersion;
        auto const localUpdatedAt = m_updatedAt;
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
        co_await uiContext;
        if (accountVersion != m_accountVersion)
        {
            co_return;
        }

        auto const action = DecideSettingsLoad({
            .DocumentAlreadyRead = m_documentRead,
            .ReceivedDocument = payload.has_value(),
            .IsLatestRequest = m_queryCache->IsLatest(SettingsCacheKey, requestId),
            .PayloadUpdatedAt = payload ? payload->UpdatedAt : 0,
            .LocalUpdatedAt = localUpdatedAt,
            .RequestWriteVersion = requestWriteVersion,
            .CurrentWriteVersion = m_writeVersion,
        });
        switch (action)
        {
        case SettingsLoadAction::AdoptFirstRead:
            // Committed when this is still the latest request; an older read is
            // as real a document, so it is adopted either way.
            static_cast<void>(m_queryCache->Commit(SettingsCacheKey, requestId, *payload, QueryTtl::Settings));
            Apply(*payload);
            m_documentRead = true;
            static_cast<void>(WriteMirrorAsync(*payload));
            ReplayUnreadEdits();
            break;
        case SettingsLoadAction::ApplyNewer:
            if (m_queryCache->Commit(SettingsCacheKey, requestId, *payload, QueryTtl::Settings))
            {
                Apply(*payload);
                static_cast<void>(WriteMirrorAsync(*payload));
            }
            break;
        case SettingsLoadAction::DiscardUnreadEdits:
            // The waiting edits were only ever shown on an empty document.
            m_unreadEdits.clear();
            m_value = winrt::Windows::Data::Json::JsonObject{};
            break;
        case SettingsLoadAction::Keep:
            break;
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
        ApplyEdit(
            L"subtitleScalePercent",
            winrt::Windows::Data::Json::JsonValue::CreateNumberValue(std::clamp(value, 50, 200)),
            true);
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
    bool SettingsSyncService::SubtitleTrackStyling() const noexcept
    {
        // Defaults to keeping the track's styling, which is what mpv did before this
        // preference existed, so no library changes appearance on upgrade.
        try { return m_value.GetNamedBoolean(L"subtitleTrackStyling", true); }
        catch (...) { return true; }
    }
    void SettingsSyncService::SubtitleTrackStyling(bool value) { SetBoolean(L"subtitleTrackStyling", value); }
    bool SettingsSyncService::AutoplayNextEpisode() const noexcept
    {
        try { return m_value.GetNamedBoolean(L"autoplayNextEpisode", true); }
        catch (...) { return true; }
    }
    void SettingsSyncService::AutoplayNextEpisode(bool value) { SetBoolean(L"autoplayNextEpisode", value); }
    std::optional<winrt::hstring> SettingsSyncService::CatalogLanguage() const { return OptionalString(m_value, L"catalogLanguage", 64); }
    void SettingsSyncService::CatalogLanguage(std::optional<winrt::hstring> value)
    {
        // The server refuses an empty or longer value, so either clears the key.
        if (value && (value->empty() || value->size() > 64)) value.reset();
        SetString(L"catalogLanguage", std::move(value), false);
    }

    concurrency::task<std::optional<::HaloDesktop::Api::Dto::SettingsPayload>> SettingsSyncService::ReadMirrorAsync()
    {
        auto const mirrorPath = m_mirrorPath;
        co_await winrt::resume_background();
        if (mirrorPath.empty() || !std::filesystem::exists(mirrorPath))
        {
            co_return std::nullopt;
        }
        try
        {
            ::HaloDesktop::Storage::FileMutationLock const fileLock{ mirrorPath };
            auto const raw = ::HaloDesktop::Storage::ReadUtf8File(mirrorPath, 1024u * 1024u);
            if (raw.empty())
            {
                co_return std::nullopt;
            }
            co_return ::HaloDesktop::Api::Mappers::ParseSettings(
                winrt::Windows::Data::Json::JsonValue::Parse(winrt::to_hstring(raw)));
        }
        catch (...)
        {
            co_return std::nullopt;
        }
    }

    concurrency::task<void> SettingsSyncService::WriteMirrorAsync(::HaloDesktop::Api::Dto::SettingsPayload payload)
    {
        auto const mirrorPath = m_mirrorPath;
        co_await winrt::resume_background();
        if (mirrorPath.empty())
        {
            co_return;
        }
        try
        {
            winrt::Windows::Data::Json::JsonObject root;
            root.Insert(L"value", payload.Value);
            root.Insert(L"updatedAt", winrt::Windows::Data::Json::JsonValue::CreateNumberValue(
                static_cast<double>(payload.UpdatedAt)));
            auto const encoded = winrt::to_string(root.Stringify());

            std::scoped_lock const lock{ m_mirrorMutex };
            ::HaloDesktop::Storage::FileMutationLock const fileLock{ mirrorPath };
            auto const current = ::HaloDesktop::Storage::ReadUtf8File(mirrorPath, 1024u * 1024u);
            if (!current.empty())
            {
                try
                {
                    auto const currentRoot = winrt::Windows::Data::Json::JsonObject::Parse(
                        winrt::to_hstring(current));
                    auto const diskTimestamp = currentRoot.GetNamedNumber(L"updatedAt", -1);
                    if (std::isfinite(diskTimestamp)
                        && diskTimestamp > static_cast<double>(payload.UpdatedAt))
                    {
                        co_return;
                    }
                }
                catch (...)
                {
                }
            }
            ::HaloDesktop::Storage::WriteUtf8FileAtomic(mirrorPath, encoded);
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
        // Touch advances the generation at the point of the local mutation,
        // including edits waiting for the debounce timer. Saving captures that
        // generation so a load already in flight cannot replace the edit.
        auto const version = m_writeVersion;
        auto const accountVersion = m_accountVersion;
        std::optional<::HaloDesktop::Api::Dto::SettingsPayload> echo;
        try
        {
            echo = co_await m_apiClient->PutSettingsAsync(snapshot, updatedAt);
        }
        catch (...)
        {
        }
        co_await uiContext;
        if (accountVersion != m_accountVersion)
        {
            co_return;
        }
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
        ApplyEdit(
            key,
            value && !value->empty()
                ? winrt::Windows::Data::Json::JsonValue::CreateStringValue(*value)
                : winrt::Windows::Data::Json::IJsonValue{ nullptr },
            debounce);
    }

    void SettingsSyncService::SetBoolean(wchar_t const* key, bool value)
    {
        ApplyEdit(key, winrt::Windows::Data::Json::JsonValue::CreateBooleanValue(value), false);
    }

    void SettingsSyncService::ApplyEdit(
        wchar_t const* key,
        winrt::Windows::Data::Json::IJsonValue const& value,
        bool debounce)
    {
        if (value)
        {
            m_value.Insert(key, value);
        }
        else if (m_value.HasKey(key))
        {
            m_value.Remove(key);
        }
        if (CanSaveSettings(m_documentRead))
        {
            Touch(debounce);
            return;
        }

        // Shown at once, saved only on top of a document that was read.
        auto const existing = std::find_if(m_unreadEdits.begin(), m_unreadEdits.end(), [key](UnreadEdit const& edit)
        {
            return edit.Key == key;
        });
        if (existing != m_unreadEdits.end())
        {
            existing->Value = value;
            return;
        }
        m_unreadEdits.push_back({ key, value });
        if (m_unreadEdits.size() == 1)
        {
            // The first waiting edit starts a read of its own, so it never
            // depends on some page happening to load the settings.
            static_cast<void>(LoadAsync());
        }
    }

    void SettingsSyncService::ReplayUnreadEdits()
    {
        if (m_unreadEdits.empty())
        {
            return;
        }
        auto edits = std::move(m_unreadEdits);
        m_unreadEdits.clear();
        for (auto const& edit : edits)
        {
            if (edit.Value)
            {
                m_value.Insert(edit.Key, edit.Value);
            }
            else if (m_value.HasKey(edit.Key))
            {
                m_value.Remove(edit.Key);
            }
        }
        // One save for all of them, stamped newer than the document read.
        Touch(false);
    }

    void SettingsSyncService::Touch(bool debounce)
    {
        ++m_writeVersion;
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

    void SettingsSyncService::OnAccountChanged(
        winrt::hstring const& serverUrl,
        winrt::hstring const& userId)
    {
        m_debounceTimer.Stop();
        ++m_accountVersion;
        ++m_writeVersion;
        m_value = winrt::Windows::Data::Json::JsonObject{};
        m_updatedAt = 0;
        m_documentRead = false;
        m_unreadEdits.clear();
        m_queryCache->Invalidate(SettingsCacheKey);
        m_mirrorPath = userId.empty()
            ? std::filesystem::path{}
            : m_mirrorRoot / (::HaloDesktop::Services::Downloads::MakeAccountKey(
                std::wstring{ serverUrl.c_str() },
                std::wstring{ userId.c_str() }) + L".json");
    }
}
