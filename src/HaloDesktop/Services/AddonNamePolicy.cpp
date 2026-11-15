#include "pch.h"
#include "Services/AddonNamePolicy.h"

#include <algorithm>
#include <cwctype>
#include <regex>
#include <string>
#include <string_view>

namespace
{
    std::wstring Trim(std::wstring value)
    {
        auto const first = std::find_if_not(value.begin(), value.end(), [](wchar_t item) { return std::iswspace(item) != 0; });
        if (first == value.end()) return {};
        auto const last = std::find_if_not(value.rbegin(), value.rend(), [](wchar_t item) { return std::iswspace(item) != 0; }).base();
        return std::wstring(first, last);
    }
}

namespace HaloDesktop::Services
{
    winrt::hstring SanitizeAddonName(std::optional<winrt::hstring> const& name)
    {
        if (!name) return L"An addon";
        std::wstring cleaned;
        cleaned.reserve(name->size());
        for (auto const value : std::wstring_view(name->c_str(), name->size()))
        {
            if (value >= 0x20 && value != 0x7F) cleaned.push_back(value);
        }
        cleaned = Trim(std::move(cleaned));
        if (cleaned.empty() || cleaned.find(L"://") != std::wstring::npos || cleaned.find(L'/') != std::wstring::npos || cleaned.find(L'\\') != std::wstring::npos) return L"An addon";
        static std::wregex const token(LR"([A-Za-z0-9_-]{32,})");
        if (std::regex_search(cleaned, token)) return L"An addon";
        if (cleaned.size() > 80) cleaned.resize(80);
        return winrt::hstring{ cleaned };
    }

    winrt::hstring AddonFailureCopy(std::optional<winrt::hstring> const& code)
    {
        if (!code) return L"is unavailable";
        if (*code == L"timeout") return L"did not answer in time";
        if (*code == L"upstream_http") return L"returned an error";
        if (*code == L"blocked_target") return L"was blocked for safety";
        if (*code == L"invalid_response") return L"returned invalid data";
        return L"is unavailable";
    }
}
