#pragma once

#include <chrono>
#include <cstddef>

namespace HaloDesktop::Services
{
    // When a query's results are first shown. Until then the page keeps what
    // it showed before (dimmed) or its placeholders, so the grid is laid out
    // once from several answers instead of growing and reshuffling with each.

    // Never sooner, unless every catalog has answered: a fast first answer
    // alone would be laid out before the others had a chance.
    inline constexpr std::chrono::milliseconds SearchRevealFloor{ 250 };
    // No later, once any catalog has answered with titles: a slow addon does
    // not hold the rest back.
    inline constexpr std::chrono::milliseconds SearchRevealDeadline{ 1200 };

    enum class RevealDecision
    {
        Wait,
        Reveal,
    };

    // `pending` of `total` catalogs are still out, `answeredWithItems` have
    // answered with titles, and the query was asked `sinceAsked` ago. Every
    // catalog settled reveals at once; otherwise the deadline reveals once
    // some answer has titles, and with none the page waits for the first one
    // or for every catalog to settle.
    [[nodiscard]] RevealDecision DecideReveal(
        std::size_t pending,
        std::size_t total,
        std::size_t answeredWithItems,
        std::chrono::milliseconds sinceAsked) noexcept;
}
