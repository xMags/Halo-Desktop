#pragma once

#include <string_view>

namespace HaloDesktop::Services
{
    // Whether two release file names name the same file. Callers must already have
    // established that both belong to the same video; this only decides whether the
    // copy on disk is the file a stream is offering, and nothing here is strong
    // enough to identify content on its own.
    //
    // The comparison is on the file name rather than the source URL on purpose: a
    // debrid link is minted per resolve, so its hash stops matching the moment the
    // link is re-issued, while the release name is the same thing on both sides.
    [[nodiscard]] bool SameReleaseFile(
        std::wstring_view saved,
        std::wstring_view candidate) noexcept;

    // The file name without any directory an addon prefixed onto it, and without
    // surrounding whitespace. Returns an empty view when nothing is left, which
    // SameReleaseFile treats as no match rather than as a match with everything.
    [[nodiscard]] std::wstring_view ReleaseLeafName(std::wstring_view value) noexcept;
}
