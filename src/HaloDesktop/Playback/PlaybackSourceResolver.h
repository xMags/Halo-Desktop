#pragma once

#include "Playback/IPlaybackEngine.h"

namespace HaloDesktop::Playback
{
    // Settles where a network source really lives before libmpv opens it.
    //
    // libmpv keeps custom request headers across redirects, on both its curl and
    // its ffmpeg backend, so handing it a URL that redirects to another origin
    // also hands that origin the addon's credentials. This walks the chain first
    // under Halo's own rules and returns the source libmpv should open: the
    // settled URL, with the headers dropped if any hop changed origin.
    //
    // A source with no headers has nothing to leak and is returned untouched, so
    // ordinary open streams pay nothing for this. If the walk cannot be completed
    // (network error, or an answer that is neither a redirect nor a success) the
    // original source is returned unchanged: refusing to play would be a worse
    // outcome than the behaviour that shipped before this check existed.
    //
    // Blocking and network-bound. Never call it on the UI thread.
    [[nodiscard]] PlaybackSource ResolvePlaybackSource(PlaybackSource source) noexcept;
}
