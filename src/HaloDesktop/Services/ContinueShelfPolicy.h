#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace HaloDesktop::Services
{
    // One watch row, with the name and poster already filled in from the library
    // where the row itself did not carry them. An empty Name means the row cannot
    // be drawn at all and is passed over without speaking for its show.
    struct ContinueWatchRow final
    {
        std::wstring ItemId;
        std::wstring VideoId;
        std::wstring Name;
        std::wstring Poster;
        double PositionSec{};
        double DurationSec{};
        bool Watched{};
        std::int64_t UpdatedAt{};
    };

    enum class NextEpisodeState
    {
        // Never looked up. The shelf asks for it rather than guessing.
        Unknown,
        // Looked up and answered: this episode has no successor, so the series is
        // finished and leaves the shelf.
        None,
        // Looked up and answered with the episode to show.
        Resolved,
    };

    struct NextEpisodeLookup final
    {
        NextEpisodeState State{ NextEpisodeState::Unknown };
        std::wstring NextVideoId;
    };

    enum class ContinueCardKind
    {
        // Stopped partway through. Carries the stored position, so the card keeps
        // its progress bar and its time remaining.
        Resume,
        // The episode after one that was finished. Never started, so it has no
        // position and no duration to count down from.
        NextEpisode,
    };

    struct ContinueCard final
    {
        ContinueCardKind Kind{ ContinueCardKind::Resume };
        std::wstring ItemId;
        std::wstring VideoId;
        std::wstring Name;
        std::wstring Poster;
        double PositionSec{};
        double DurationSec{};
    };

    // A series whose newest row is finished and whose successor is not known yet.
    struct ContinueNextRequest final
    {
        std::wstring ItemId;
        std::wstring Type;
        std::wstring MetaId;
        // The finished episode to step past, which is also the lookup's key.
        std::wstring VideoId;
    };

    struct ContinueShelf final
    {
        std::vector<ContinueCard> Cards;
        std::vector<ContinueNextRequest> Requests;
    };

    // Watch rows are keyed by an item id that carries the type ahead of the meta
    // id. A bare id with no prefix is a film, which is how the oldest rows are
    // shaped.
    [[nodiscard]] std::wstring TypeFromItemId(std::wstring_view itemId);
    [[nodiscard]] std::wstring MetaIdFromItemId(std::wstring_view itemId);

    // Whether there is nothing left of this episode worth returning to. The
    // reporter sets Watched once past ninety percent; the fraction covers a row
    // written by something that did not set it.
    [[nodiscard]] bool IsFinishedWatchRow(ContinueWatchRow const& row) noexcept;

    // The continue shelf, newest activity first.
    //
    // A show is represented by whatever it did most recently, and only once. Left
    // partway through, it resumes; finished, it advances to the next episode,
    // because a series is followed rather than watched once. Without that second
    // rule a series being worked through in order is the one thing that can never
    // appear here, since finishing an episode is what removes it.
    //
    // A finished episode whose successor has not been looked up yet contributes a
    // request instead of a card. That keeps this synchronous and lets the shelf
    // paint from what is already known, gaining the promoted cards when the
    // lookups return.
    [[nodiscard]] ContinueShelf BuildContinueShelf(
        std::vector<ContinueWatchRow> rows,
        std::function<NextEpisodeLookup(std::wstring const&)> const& resolvedNext,
        std::size_t maximumCards,
        std::size_t maximumRequests);
}
