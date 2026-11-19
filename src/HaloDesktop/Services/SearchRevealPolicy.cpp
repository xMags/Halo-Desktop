#include "Services/SearchRevealPolicy.h"

namespace HaloDesktop::Services
{
    RevealDecision DecideReveal(
        std::size_t pending,
        std::size_t total,
        std::size_t answeredWithItems,
        std::chrono::milliseconds sinceAsked) noexcept
    {
        // No catalog to ask, or every one has answered or failed.
        if (total == 0 || pending == 0) return RevealDecision::Reveal;
        if (sinceAsked < SearchRevealFloor) return RevealDecision::Wait;
        if (sinceAsked >= SearchRevealDeadline && answeredWithItems > 0) return RevealDecision::Reveal;
        return RevealDecision::Wait;
    }
}
