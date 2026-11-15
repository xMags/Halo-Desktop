#include "Services/RatingPolicy.h"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <string>
#include <system_error>

namespace
{
    bool IsJavaScriptWhitespace(wchar_t value) noexcept
    {
        return value == L' ' || value == L'\t' || value == L'\n' || value == L'\r' || value == L'\v'
            || value == L'\f' || value == 0x00A0 || value == 0x1680 || (value >= 0x2000 && value <= 0x200A)
            || value == 0x2028 || value == 0x2029 || value == 0x202F || value == 0x205F
            || value == 0x3000 || value == 0xFEFF;
    }

    std::wstring_view Trim(std::wstring_view value) noexcept
    {
        while (!value.empty() && IsJavaScriptWhitespace(value.front())) value.remove_prefix(1);
        while (!value.empty() && IsJavaScriptWhitespace(value.back())) value.remove_suffix(1);
        return value;
    }

    bool IsDigit(wchar_t value) noexcept
    {
        return value >= L'0' && value <= L'9';
    }

    // The longest leading decimal literal, as parseFloat accepts it: a sign,
    // digits with an optional fraction (or a bare fraction), then an exponent
    // only when digits follow it. "Infinity" is left out: it is never a
    // finite rating, so it reads the same as text that is not a number.
    std::optional<double> ParseLeadingFloat(std::wstring_view text)
    {
        std::string literal;
        std::size_t index{};
        if (index < text.size() && (text[index] == L'+' || text[index] == L'-'))
        {
            if (text[index] == L'-') literal.push_back('-');
            ++index;
        }

        std::size_t digits{};
        while (index < text.size() && IsDigit(text[index]))
        {
            literal.push_back(static_cast<char>(text[index]));
            ++index;
            ++digits;
        }
        if (index < text.size() && text[index] == L'.')
        {
            std::size_t fraction{};
            std::string fractionText{ "." };
            auto cursor = index + 1;
            while (cursor < text.size() && IsDigit(text[cursor]))
            {
                fractionText.push_back(static_cast<char>(text[cursor]));
                ++cursor;
                ++fraction;
            }
            if (digits > 0 || fraction > 0)
            {
                if (digits == 0) literal.push_back('0');
                literal += fractionText;
                index = cursor;
                digits += fraction;
            }
        }
        if (digits == 0) return std::nullopt;

        if (index < text.size() && (text[index] == L'e' || text[index] == L'E'))
        {
            std::string exponent{ "e" };
            auto cursor = index + 1;
            if (cursor < text.size() && (text[cursor] == L'+' || text[cursor] == L'-'))
            {
                exponent.push_back(static_cast<char>(text[cursor]));
                ++cursor;
            }
            auto const exponentStart = cursor;
            while (cursor < text.size() && IsDigit(text[cursor]))
            {
                exponent.push_back(static_cast<char>(text[cursor]));
                ++cursor;
            }
            if (cursor > exponentStart) literal += exponent;
        }

        double value{};
        auto const [end, error] = std::from_chars(literal.data(), literal.data() + literal.size(), value);
        // Out of range is an overflow to infinity or an underflow to zero; neither is a rating.
        if (error != std::errc{} || end != literal.data() + literal.size()) return std::nullopt;
        return value;
    }
}

namespace HaloDesktop::Services
{
    std::optional<std::wstring> RatedValue(std::optional<std::wstring_view> rating)
    {
        if (!rating) return std::nullopt;
        auto const value = Trim(*rating);
        if (value.empty()) return std::nullopt;
        auto const number = ParseLeadingFloat(value);
        if (!number || !std::isfinite(*number) || *number <= 0.0) return std::nullopt;
        return std::wstring{ value };
    }

    std::optional<std::wstring> RatingLabel(std::optional<std::wstring_view> rating)
    {
        auto const value = RatedValue(rating);
        if (!value) return std::nullopt;
        return L"\x2605 " + *value;
    }
}
