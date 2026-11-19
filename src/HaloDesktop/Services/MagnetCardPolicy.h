#pragma once

#include "Api/Dto.h"

#include <optional>
#include <string>

namespace HaloDesktop::Services
{
    // What Search's action card says about a pasted magnet. Pure: compiles
    // without XAML. Never a file list and never a Play button for an
    // identified title: a ready, identified magnet only opens its title page.

    // Where the app stands with the pasted magnet.
    enum class MagnetPhase
    {
        // The server has no magnets for this account, or Halo has no account.
        Unavailable,
        // Asked, or waiting to learn whether the server has magnets.
        Checking,
        // The server answered; the card follows its request.
        Answered,
        // The server could not be reached or answered badly.
        Failed,
        // The viewer cancelled a download from the card.
        Stopped,
    };

    struct MagnetCardInput final
    {
        MagnetPhase Phase{ MagnetPhase::Checking };
        // The magnet's own `dn`, display only; may be empty.
        std::wstring LinkName;
        // The answer, for Answered.
        std::optional<::HaloDesktop::Api::Dto::MagnetRequest> Request;
    };

    enum class MagnetCardTone
    {
        Accent,
        Warn,
        Bad,
        Mute,
    };

    // The card's one action, if any.
    enum class MagnetCardAction
    {
        None,
        // Accent button with the Enter hint: the matched title's page.
        OpenTitle,
        // Standard buttons, by click only.
        Cancel,
        PlayUnidentified,
        Retry,
    };

    struct MagnetCard final
    {
        MagnetCardTone Tone{ MagnetCardTone::Accent };
        // The matched title's poster leads instead of the magnet glyph.
        bool ShowsPoster{};
        std::wstring Title;
        // The meta row: a value in mono, a plain value, a second value in mono.
        std::wstring Mono;
        std::wstring Plain;
        std::wstring Tail;
        std::wstring Secondary;
        // 0 to 1 while a download is under way.
        std::optional<double> Progress;
        MagnetCardAction Action{ MagnetCardAction::None };
        std::wstring ActionLabel;
        // The matched title, for the poster and Open title.
        std::optional<::HaloDesktop::Api::Dto::MagnetIdentity> Identity;
        // The file Play as unidentified video plays.
        std::optional<::HaloDesktop::Api::Dto::MagnetFile> UnidentifiedFile;

        bool operator==(MagnetCard const&) const = default;
    };

    [[nodiscard]] MagnetCard BuildMagnetCard(MagnetCardInput const& input);
}
