#include "pch.h"
#include "Services/Downloads/RequestVault.h"

#include "Security/Dpapi.h"

#include <algorithm>
#include <stdexcept>
#include <system_error>
#include <wil/resource.h>
#include <winrt/Windows.Data.Json.h>

namespace
{
    constexpr std::size_t MaximumVaultText = 65536;

    winrt::Windows::Data::Json::JsonObject SerializeHeaders(
        std::map<std::wstring, std::wstring, std::less<>> const& headers)
    {
        winrt::Windows::Data::Json::JsonObject result;
        for (auto const& [key, value] : headers)
        {
            result.Insert(key, winrt::Windows::Data::Json::JsonValue::CreateStringValue(value));
        }
        return result;
    }

    std::map<std::wstring, std::wstring, std::less<>> ParseHeaders(
        winrt::Windows::Data::Json::JsonObject const& object)
    {
        std::map<std::wstring, std::wstring, std::less<>> result;
        for (auto const& pair : object)
        {
            auto const value = pair.Value();
            if (value.ValueType() != winrt::Windows::Data::Json::JsonValueType::String)
            {
                throw std::invalid_argument{ "A protected request header is invalid." };
            }
            auto const text = value.GetString();
            if (text.size() > 8192)
            {
                throw std::invalid_argument{ "A protected request header is oversized." };
            }
            result.emplace(std::wstring{ pair.Key() }, std::wstring{ text });
        }
        return result;
    }

    std::wstring RequiredText(
        winrt::Windows::Data::Json::JsonObject const& object,
        wchar_t const* name)
    {
        auto const value = object.GetNamedString(name);
        if (value.empty() || value.size() > MaximumVaultText)
        {
            throw std::invalid_argument{ "Protected request data is invalid." };
        }
        return std::wstring{ value };
    }

    std::string Serialize(HaloDesktop::Services::Downloads::ProtectedRequest const& request)
    {
        winrt::Windows::Data::Json::JsonObject root;
        root.Insert(L"url", winrt::Windows::Data::Json::JsonValue::CreateStringValue(request.Url));
        root.Insert(L"headers", SerializeHeaders(request.Headers));
        if (request.Subtitle)
        {
            winrt::Windows::Data::Json::JsonObject subtitle;
            subtitle.Insert(L"url", winrt::Windows::Data::Json::JsonValue::CreateStringValue(request.Subtitle->Url));
            subtitle.Insert(L"language", winrt::Windows::Data::Json::JsonValue::CreateStringValue(request.Subtitle->Language));
            subtitle.Insert(L"id", winrt::Windows::Data::Json::JsonValue::CreateStringValue(request.Subtitle->Id));
            subtitle.Insert(L"headers", SerializeHeaders(request.Subtitle->Headers));
            root.Insert(L"subtitle", subtitle);
        }
        return winrt::to_string(root.Stringify());
    }

    HaloDesktop::Services::Downloads::ProtectedRequest Parse(std::string const& raw)
    {
        auto const root = winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(raw));
        std::optional<HaloDesktop::Services::Downloads::SubtitleRequest> subtitle;
        if (root.HasKey(L"subtitle"))
        {
            auto const object = root.GetNamedObject(L"subtitle");
            subtitle = HaloDesktop::Services::Downloads::SubtitleRequest{
                .Url = RequiredText(object, L"url"),
                .Language = RequiredText(object, L"language"),
                .Id = RequiredText(object, L"id"),
                .Headers = ParseHeaders(object.GetNamedObject(L"headers")),
            };
        }
        HaloDesktop::Services::Downloads::ProtectedRequest result{
            .Url = RequiredText(root, L"url"),
            .Headers = ParseHeaders(root.GetNamedObject(L"headers")),
            .Subtitle = std::move(subtitle),
        };
        HaloDesktop::Services::Downloads::ValidateProtectedRequest(result);
        return result;
    }
}

namespace HaloDesktop::Services::Downloads
{
    RequestVault::RequestVault(std::filesystem::path directory)
        : m_directory(std::move(directory))
    {
        std::error_code error;
        std::filesystem::create_directories(m_directory, error);
        if (error)
        {
            throw std::system_error{ error, "Could not create the download request vault" };
        }
    }

    void RequestVault::Write(
        std::wstring const& jobId,
        ProtectedRequest const& request) const
    {
        ValidateProtectedRequest(request);
        auto raw = Serialize(request);
        auto wipe = wil::scope_exit([&raw]() noexcept
        {
            if (!raw.empty())
            {
                SecureZeroMemory(raw.data(), raw.size());
            }
        });
        ::HaloDesktop::Security::WriteProtectedTextAsync(PathFor(jobId), raw).get();
    }

    ProtectedRequest RequestVault::Read(std::wstring const& jobId) const
    {
        auto raw = ::HaloDesktop::Security::ReadProtectedTextAsync(PathFor(jobId)).get();
        if (!raw)
        {
            throw std::runtime_error{ "The protected download request is missing." };
        }
        auto wipe = wil::scope_exit([&raw]() noexcept
        {
            if (raw && !raw->empty())
            {
                SecureZeroMemory(raw->data(), raw->size());
            }
        });
        return Parse(*raw);
    }

    void RequestVault::Remove(std::wstring const& jobId) const
    {
        ::HaloDesktop::Security::DeleteProtectedFileAsync(PathFor(jobId)).get();
    }

    std::filesystem::path RequestVault::PathFor(std::wstring const& jobId) const
    {
        if (jobId.empty()
            || jobId.size() > 128
            || !std::all_of(jobId.begin(), jobId.end(), [](wchar_t character)
            {
                return (character >= L'0' && character <= L'9')
                    || (character >= L'a' && character <= L'f');
            }))
        {
            throw std::invalid_argument{ "A valid download job identifier is required." };
        }
        return m_directory / (jobId + L".bin");
    }
}
