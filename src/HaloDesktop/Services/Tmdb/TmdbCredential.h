#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace HaloDesktop::Services::Tmdb
{
    // The two kinds of TMDB credential a viewer can copy from their TMDB
    // account's API page. Both reach the same API.
    enum class TmdbCredentialKind
    {
        // "API Key": 32 hex characters, sent as the `api_key` query parameter.
        ApiKey,
        // "API Read Access Token": a long dotted token, sent as a Bearer header.
        ReadToken,
    };

    struct TmdbCredential final
    {
        TmdbCredentialKind Kind{ TmdbCredentialKind::ApiKey };
        std::wstring Value;
    };

    // What was pasted, trimmed, as a credential when it has either shape;
    // empty otherwise. Only the shape is judged here: whether TMDB accepts it
    // is for TMDB to say.
    [[nodiscard]] std::optional<TmdbCredential> ParseTmdbCredential(std::wstring_view pasted);
}
