#pragma once

#include "ViewModels/QualityMedalPolicy.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>
#include <winrt/base.h>

namespace HaloDesktop::Sources
{
    // How the sources sheet talks about a source, the reference's
    // `sourcePresentation.ts`: pure functions over already-parsed fields, so it
    // compiles without XAML or generated WinRT types and the tests include it.
    //
    // The field vocabulary is the parser's: quality is `2160p` ... `SD`, range is
    // `SDR`/`DV`/`HDR10`/`HLG`/`HDR`, and anything the parser could not identify
    // is the literal `UNKNOWN`, which the sheet leaves out rather than guess at.

    // How the sheet talks about picture: coarser than the parsed resolution.
    enum class QualityTier
    {
        UltraHd,
        FullHd,
        Hd,
        Lower,
    };

    // The only thing a viewer has to decide between.
    enum class StartSpeed
    {
        Immediate,
        ShortWait,
        NeedsDownload,
    };

    // `Caching` is kept so the palette stays complete, but no resolver assigns it:
    // addons only say cached, not cached, or nothing. A file already saved on this
    // device is not a source; the sheet offers it on a card of its own.
    enum class SourceStatus
    {
        Instant,
        Caching,
        Uncached,
        Unknown,
    };

    // The parser's placeholder for anything it could not identify.
    inline constexpr std::wstring_view NotParsed = L"UNKNOWN";

    // A source flattened out of its addon group with every decision already made.
    struct SourceEntry final
    {
        winrt::hstring Key;
        // Grouping identity; never shown.
        winrt::hstring ProviderId;
        // Sanitized provider name.
        winrt::hstring Provider;
        SourceStatus Status{ SourceStatus::Unknown };
        winrt::hstring Quality;
        winrt::hstring Range;
        winrt::hstring Codec;
        winrt::hstring Audio;
        // Language tokens joined by " \x00B7 ", or UNKNOWN.
        winrt::hstring Languages;
        // `8.42 GB`, or UNKNOWN.
        winrt::hstring Size;
        winrt::hstring File;
        std::vector<winrt::hstring> SubtitleLanguages;
        QualityTier Tier{ QualityTier::Lower };
        StartSpeed Speed{ StartSpeed::NeedsDownload };
        // Zero when unknown.
        std::uint64_t SizeBytes{};
        std::int32_t Rank{};
        // Megabits per second the file needs; zero when size or runtime is unknown.
        double NeededMbps{};
    };

    // What the sheet knows about this device that no single source can say.
    struct DeviceContext final
    {
        // Measured line speed in Mbps; zero means never measured.
        double LineMbps{};
        // Runtime in seconds; zero means unknown.
        double DurationSeconds{};
    };

    // One fact in a source's details: a short label, then what the release says.
    struct DetailRow final
    {
        winrt::hstring Label;
        winrt::hstring Value;

        bool operator==(DetailRow const&) const = default;
    };

    struct BandwidthDetail final
    {
        winrt::hstring Label;
        winrt::hstring Note;
        // Zero to one of the viewer's own line; zero when the line is unmeasured.
        double Fraction{};
    };

    // A source's details as a short list. A fact the release does not name is
    // left out rather than spelled out as "not listed".
    struct SourceDetails final
    {
        std::vector<DetailRow> Rows;
        winrt::hstring Provider;
        // `Cached`, `Caching now`, `Not cached`, or empty when the addon did not say.
        winrt::hstring CacheLabel;
        bool CacheGood{};
        // Empty when the size or runtime is missing, so nothing could be measured.
        std::optional<BandwidthDetail> Bandwidth;
    };

    [[nodiscard]] QualityTier TierOf(std::wstring_view quality) noexcept;
    [[nodiscard]] winrt::hstring TierLabel(QualityTier tier);
    [[nodiscard]] StartSpeed SpeedOf(SourceStatus status) noexcept;
    [[nodiscard]] winrt::hstring StatusLabel(SourceStatus status);
    [[nodiscard]] bool IsHdr(std::wstring_view range) noexcept;
    [[nodiscard]] winrt::hstring LanguageName(winrt::hstring const& code);
    // The phone's medal for a source: plate, tag and metal from its quality and range.
    [[nodiscard]] ViewModels::Medal MedalOf(SourceEntry const& entry);
    [[nodiscard]] winrt::hstring BitrateWarning(SourceEntry const& entry, double lineMbps);
    // The one mono line a card prints beside its medal: codec, sound and size,
    // each left out when the release did not name it, upper case.
    [[nodiscard]] winrt::hstring MetaLine(SourceEntry const& entry);
    [[nodiscard]] SourceDetails DetailsFor(SourceEntry const& entry, DeviceContext const& device);
    [[nodiscard]] winrt::hstring CountLabel(std::size_t count, wchar_t const* singular, wchar_t const* plural);
}
