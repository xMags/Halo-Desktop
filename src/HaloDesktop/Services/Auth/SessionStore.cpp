#include "pch.h"
#include "Services/Auth/SessionStore.h"

#include "Security/Dpapi.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <wil/resource.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Storage.h>

namespace
{
    constexpr std::size_t MaximumStoredStringLength = 65536;

    winrt::hstring RequiredString(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* name)
    {
        auto const value = object.GetNamedString(name);
        if (value.empty() || value.size() > MaximumStoredStringLength)
        {
            throw std::invalid_argument{ "The protected session contains an invalid string." };
        }
        return value;
    }

    std::optional<winrt::hstring> OptionalString(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* name)
    {
        if (!object.HasKey(name))
        {
            return std::nullopt;
        }
        auto const value = object.GetNamedValue(name);
        if (value.ValueType() == winrt::Windows::Data::Json::JsonValueType::Null)
        {
            return std::nullopt;
        }
        if (value.ValueType() != winrt::Windows::Data::Json::JsonValueType::String)
        {
            throw std::invalid_argument{ "The protected session contains an invalid optional string." };
        }
        auto const text = value.GetString();
        if (text.empty() || text.size() > MaximumStoredStringLength)
        {
            throw std::invalid_argument{ "The protected session contains an invalid optional string." };
        }
        return text;
    }

    std::int64_t RequiredEpochMilliseconds(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* name)
    {
        auto const number = object.GetNamedNumber(name);
        if (!std::isfinite(number)
            || number <= 0
            || number > static_cast<double>((std::numeric_limits<std::int64_t>::max)())
            || std::floor(number) != number)
        {
            throw std::invalid_argument{ "The protected session contains an invalid expiry." };
        }
        return static_cast<std::int64_t>(number);
    }

    std::int64_t RequiredNonnegativeEpochMilliseconds(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* name)
    {
        auto const number = object.GetNamedNumber(name);
        if (!std::isfinite(number)
            || number < 0
            || number > static_cast<double>((std::numeric_limits<std::int64_t>::max)())
            || std::floor(number) != number)
        {
            throw std::invalid_argument{ "The protected session contains an invalid expiry." };
        }
        return static_cast<std::int64_t>(number);
    }

    std::optional<::HaloDesktop::Services::Auth::StoredSession> ParseSession(std::string const& raw)
    {
        auto const root = winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(raw));
        auto const kind = root.GetNamedString(L"kind");
        if (kind == L"local")
        {
            auto const local = root.GetNamedObject(L"local");
            return ::HaloDesktop::Services::Auth::StoredSession{
                .Kind = ::HaloDesktop::Services::Auth::StoredSessionKind::Local,
                .Local = ::HaloDesktop::Services::Auth::StoredLocalSession{
                    .Token = RequiredString(local, L"token"),
                    .ExpiresAt = RequiredEpochMilliseconds(local, L"expiresAt"),
                },
            };
        }
        if (kind == L"oidc")
        {
            auto const oidc = root.GetNamedObject(L"oidc");
            return ::HaloDesktop::Services::Auth::StoredSession{
                .Kind = ::HaloDesktop::Services::Auth::StoredSessionKind::Oidc,
                .Oidc = ::HaloDesktop::Services::Auth::StoredOidcSession{
                    .ClientId = RequiredString(oidc, L"clientId"),
                    .TokenEndpoint = RequiredString(oidc, L"tokenEndpoint"),
                    .RevocationEndpoint = OptionalString(oidc, L"revocationEndpoint"),
                    .EndSessionEndpoint = OptionalString(oidc, L"endSessionEndpoint"),
                    .AccessToken = RequiredString(oidc, L"accessToken"),
                    .RefreshToken = OptionalString(oidc, L"refreshToken"),
                    .IdToken = OptionalString(oidc, L"idToken"),
                    .ExpiresAt = RequiredNonnegativeEpochMilliseconds(oidc, L"expiresAt"),
                },
            };
        }
        throw std::invalid_argument{ "The protected session kind is unsupported." };
    }

    void InsertOptionalString(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* name,
        std::optional<winrt::hstring> const& value)
    {
        if (value)
        {
            object.Insert(name, winrt::Windows::Data::Json::JsonValue::CreateStringValue(*value));
        }
    }

    std::string SerializeSession(::HaloDesktop::Services::Auth::StoredSession const& session)
    {
        winrt::Windows::Data::Json::JsonObject root;
        if (session.Kind == ::HaloDesktop::Services::Auth::StoredSessionKind::Local && session.Local)
        {
            winrt::Windows::Data::Json::JsonObject local;
            local.Insert(L"token", winrt::Windows::Data::Json::JsonValue::CreateStringValue(session.Local->Token));
            local.Insert(L"expiresAt", winrt::Windows::Data::Json::JsonValue::CreateNumberValue(
                static_cast<double>(session.Local->ExpiresAt)));
            root.Insert(L"kind", winrt::Windows::Data::Json::JsonValue::CreateStringValue(L"local"));
            root.Insert(L"local", local);
            return winrt::to_string(root.Stringify());
        }
        if (session.Kind == ::HaloDesktop::Services::Auth::StoredSessionKind::Oidc && session.Oidc)
        {
            winrt::Windows::Data::Json::JsonObject oidc;
            oidc.Insert(L"clientId", winrt::Windows::Data::Json::JsonValue::CreateStringValue(session.Oidc->ClientId));
            oidc.Insert(L"tokenEndpoint", winrt::Windows::Data::Json::JsonValue::CreateStringValue(session.Oidc->TokenEndpoint));
            InsertOptionalString(oidc, L"revocationEndpoint", session.Oidc->RevocationEndpoint);
            InsertOptionalString(oidc, L"endSessionEndpoint", session.Oidc->EndSessionEndpoint);
            oidc.Insert(L"accessToken", winrt::Windows::Data::Json::JsonValue::CreateStringValue(session.Oidc->AccessToken));
            InsertOptionalString(oidc, L"refreshToken", session.Oidc->RefreshToken);
            InsertOptionalString(oidc, L"idToken", session.Oidc->IdToken);
            oidc.Insert(L"expiresAt", winrt::Windows::Data::Json::JsonValue::CreateNumberValue(
                static_cast<double>(session.Oidc->ExpiresAt)));
            root.Insert(L"kind", winrt::Windows::Data::Json::JsonValue::CreateStringValue(L"oidc"));
            root.Insert(L"oidc", oidc);
            return winrt::to_string(root.Stringify());
        }
        throw std::invalid_argument{ "A complete session is required for persistence." };
    }

    ::HaloDesktop::Services::Auth::StoredIdentity ParseIdentity(std::string const& raw)
    {
        auto const root = winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(raw));
        return {
            .UserId = RequiredString(root, L"userId"),
            .Username = RequiredString(root, L"username"),
        };
    }

    std::string SerializeIdentity(::HaloDesktop::Services::Auth::StoredIdentity const& identity)
    {
        if (identity.UserId.empty() || identity.Username.empty())
        {
            throw std::invalid_argument{ "A complete identity is required for persistence." };
        }
        winrt::Windows::Data::Json::JsonObject root;
        root.Insert(L"userId", winrt::Windows::Data::Json::JsonValue::CreateStringValue(identity.UserId));
        root.Insert(L"username", winrt::Windows::Data::Json::JsonValue::CreateStringValue(identity.Username));
        return winrt::to_string(root.Stringify());
    }
}

namespace HaloDesktop::Services::Auth
{
    SessionStore::SessionStore()
        : m_path(std::filesystem::path{
            winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str() }
            / L"auth-session.bin"),
          m_identityPath(m_path.parent_path() / L"auth-identity.bin")
    {
    }

    concurrency::task<std::optional<StoredSession>> SessionStore::LoadAsync()
    {
        co_await winrt::resume_background();
        std::optional<std::string> raw;
        bool unreadable{};
        try
        {
            raw = co_await ::HaloDesktop::Security::ReadProtectedTextAsync(m_path);
        }
        catch (...)
        {
            unreadable = true;
        }
        if (unreadable)
        {
            try
            {
                co_await ::HaloDesktop::Security::DeleteProtectedFileAsync(m_path);
            }
            catch (...)
            {
            }
            co_return std::nullopt;
        }
        if (!raw)
        {
            co_return std::nullopt;
        }
        auto wipeRaw = wil::scope_exit([&raw]() noexcept
        {
            if (raw && !raw->empty())
            {
                SecureZeroMemory(raw->data(), raw->size());
            }
        });

        try
        {
            co_return ParseSession(*raw);
        }
        catch (...)
        {
        }

        co_await ::HaloDesktop::Security::DeleteProtectedFileAsync(m_path);
        co_return std::nullopt;
    }

    concurrency::task<void> SessionStore::SaveAsync(StoredSession session)
    {
        co_await winrt::resume_background();
        co_await ::HaloDesktop::Security::WriteProtectedTextAsync(
            m_path,
            SerializeSession(session));
    }

    concurrency::task<void> SessionStore::ClearAsync()
    {
        co_await winrt::resume_background();
        co_await ::HaloDesktop::Security::DeleteProtectedFileAsync(m_path);
    }

    concurrency::task<std::optional<StoredIdentity>> SessionStore::LoadIdentityAsync()
    {
        co_await winrt::resume_background();
        std::optional<std::string> raw;
        try
        {
            raw = co_await ::HaloDesktop::Security::ReadProtectedTextAsync(m_identityPath);
        }
        catch (...)
        {
        }
        if (!raw)
        {
            co_return std::nullopt;
        }
        auto wipeRaw = wil::scope_exit([&raw]() noexcept
        {
            if (raw && !raw->empty()) SecureZeroMemory(raw->data(), raw->size());
        });
        try
        {
            co_return ParseIdentity(*raw);
        }
        catch (...)
        {
        }
        try { co_await ::HaloDesktop::Security::DeleteProtectedFileAsync(m_identityPath); } catch (...) {}
        co_return std::nullopt;
    }

    concurrency::task<void> SessionStore::SaveIdentityAsync(
        StoredIdentity identity,
        std::uint64_t generation)
    {
        co_await winrt::resume_background();
        concurrency::task<void> operation;
        {
            std::scoped_lock const lock{ m_identityQueueMutex };
            operation = m_identityTail.then(
                [this, identity = std::move(identity), generation]() mutable -> concurrency::task<void>
                {
                    if (generation < m_identityGeneration)
                    {
                        return concurrency::task_from_result();
                    }
                    return ::HaloDesktop::Security::WriteProtectedTextAsync(
                        m_identityPath,
                        SerializeIdentity(identity)).then([this, generation](concurrency::task<void> write)
                    {
                        write.get();
                        m_identityGeneration = generation;
                    });
                });
            m_identityTail = operation.then([](concurrency::task<void> completed)
            {
                try { completed.get(); } catch (...) {}
            });
        }
        co_await operation;
    }

    concurrency::task<void> SessionStore::ClearIdentityAsync(std::uint64_t generation)
    {
        co_await winrt::resume_background();
        concurrency::task<void> operation;
        {
            std::scoped_lock const lock{ m_identityQueueMutex };
            operation = m_identityTail.then([this, generation]() -> concurrency::task<void>
            {
                if (generation < m_identityGeneration)
                {
                    return concurrency::task_from_result();
                }
                return ::HaloDesktop::Security::DeleteProtectedFileAsync(m_identityPath).then(
                    [this, generation](concurrency::task<void> remove)
                    {
                        remove.get();
                        m_identityGeneration = generation;
                    });
            });
            m_identityTail = operation.then([](concurrency::task<void> completed)
            {
                try { completed.get(); } catch (...) {}
            });
        }
        co_await operation;
    }
}
