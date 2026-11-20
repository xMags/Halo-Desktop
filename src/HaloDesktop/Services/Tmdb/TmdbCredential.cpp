#include "Services/Tmdb/TmdbCredential.h"

#include <algorithm>
#include <cstddef>

namespace
{
    bool IsHex(wchar_t character) noexcept
    {
        return (character >= L'0' && character <= L'9')
            || (character >= L'a' && character <= L'f')
            || (character >= L'A' && character <= L'F');
    }

    bool IsBase64Url(wchar_t character) noexcept
    {
        return (character >= L'0' && character <= L'9')
            || (character >= L'a' && character <= L'z')
            || (character >= L'A' && character <= L'Z')
            || character == L'-' || character == L'_';
    }

    bool IsSpace(wchar_t character) noexcept
    {
        return character == L' ' || character == L'\t' || character == L'\r' || character == L'\n';
    }
}

namespace HaloDesktop::Services::Tmdb
{
    std::optional<TmdbCredential> ParseTmdbCredential(std::wstring_view pasted)
    {
        while (!pasted.empty() && IsSpace(pasted.front())) pasted.remove_prefix(1);
        while (!pasted.empty() && IsSpace(pasted.back())) pasted.remove_suffix(1);

        constexpr std::size_t ApiKeyLength = 32;
        if (pasted.size() == ApiKeyLength && std::all_of(pasted.begin(), pasted.end(), IsHex))
        {
            return TmdbCredential{ .Kind = TmdbCredentialKind::ApiKey, .Value = std::wstring{ pasted } };
        }

        // A JSON Web Token: three non-empty base64url parts joined by dots.
        // TMDB's run to a couple of hundred characters; the bounds only keep
        // out what cannot be one.
        constexpr std::size_t MinimumToken = 100;
        constexpr std::size_t MaximumToken = 2048;
        if (pasted.size() < MinimumToken || pasted.size() > MaximumToken)
        {
            return std::nullopt;
        }
        std::size_t parts = 1;
        std::size_t partLength = 0;
        for (auto const character : pasted)
        {
            if (character == L'.')
            {
                if (partLength == 0) return std::nullopt;
                ++parts;
                partLength = 0;
                continue;
            }
            if (!IsBase64Url(character)) return std::nullopt;
            ++partLength;
        }
        if (parts != 3 || partLength == 0)
        {
            return std::nullopt;
        }
        return TmdbCredential{ .Kind = TmdbCredentialKind::ReadToken, .Value = std::wstring{ pasted } };
    }
}
