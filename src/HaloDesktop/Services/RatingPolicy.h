#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace HaloDesktop::Services
{
    // A rating that is one: its text, trimmed, or empty. Addons send "0.0" (or
    // nothing a number can be read from) for a title nobody has rated, and a
    // row of "★ 0.0" reads as a verdict on films that simply have none. The
    // number is read the way JavaScript's parseFloat reads it, so "7.2/10"
    // counts as rated and "N/A" does not.
    [[nodiscard]] std::optional<std::wstring> RatedValue(std::optional<std::wstring_view> rating);

    // "★ 6.8" for a card's or a fact line's rating, or empty when the title has none.
    [[nodiscard]] std::optional<std::wstring> RatingLabel(std::optional<std::wstring_view> rating);
}
