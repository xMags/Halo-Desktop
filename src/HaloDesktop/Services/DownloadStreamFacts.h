#pragma once

#include "Services/Downloads/DownloadTypes.h"
#include "ViewModels/DownloadsPresentationPolicy.h"

namespace HaloDesktop::Services
{
    // What a download's release name states about the file (quality, codec,
    // dynamic range), read by the stream parser from the stream name and title
    // the download kept and its file name, as the reference's `rowOf` reads
    // them. A dynamic range stored when the download started wins over the
    // parsed one: the source sheet read it from the whole stream, including
    // the description the download does not keep.
    [[nodiscard]] ViewModels::DownloadsPresentation::StreamFacts DownloadStreamFactsOf(
        Downloads::DownloadRecord const& record);
}
