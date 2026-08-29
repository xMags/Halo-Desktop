#pragma once

#include <span>

namespace HaloDesktop::Playback
{
    struct ScrubPreviewOption final
    {
        char const* Name;
        char const* Value;
    };

    // The pointer position mapped onto the file's timeline. Invalid means the caller
    // has nothing to preview yet: no duration, or a track too narrow to divide.
    struct ScrubPreviewTime final
    {
        bool Valid{};
        double Seconds{};

        bool operator==(ScrubPreviewTime const&) const = default;
    };

    // A preview decode costs a seek plus a keyframe decode, and on a remote source that
    // is a fresh HTTP range request. Targets closer together than this usually resolve
    // to the same keyframe, so decoding both spends bandwidth for an identical picture.
    inline constexpr double ScrubPreviewMinimumDeltaSeconds = 0.25;

    // The seek slider's thumb centre travels between half a thumb width from each end,
    // so the timeline is spread over a track narrower than the control itself. Mapping
    // against the full width would make both ends unreachable.
    [[nodiscard]] ScrubPreviewTime ScrubPreviewTimeFromPointer(
        double pointerX,
        double trackWidth,
        double thumbWidth,
        double durationSeconds) noexcept;

    // Centres the preview card on the pointer, held inside the host so neither edge of
    // the card can leave the overlay. A card wider than the host is pinned to the left.
    [[nodiscard]] double ClampScrubPreviewOffset(
        double pointerX,
        double cardWidth,
        double hostWidth) noexcept;

    [[nodiscard]] bool ShouldIssueScrubPreview(
        double requestedSeconds,
        double lastIssuedSeconds,
        bool hasIssued) noexcept;

    // The libmpv configuration a preview instance runs under, kept here as plain data so
    // the integration test can prove the real option set against the shipped library
    // rather than against a copy that can drift.
    [[nodiscard]] std::span<ScrubPreviewOption const> ScrubPreviewMpvOptions() noexcept;
    // Tried in order. mpv accepts the first form directly; the second is the explicit
    // libavfilter bridge for a build that does not alias it. Scaling inside mpv is what
    // keeps a grab from costing a full-resolution frame.
    [[nodiscard]] std::span<char const* const> ScrubPreviewScaleFilters() noexcept;
}
