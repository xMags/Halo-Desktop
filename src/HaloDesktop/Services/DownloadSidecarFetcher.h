#pragma once

#include "Services/Downloads/DownloadTypes.h"

#include <cstdint>
#include <ppltasks.h>
#include <string>
#include <string_view>
#include <vector>
#include <winrt/Windows.Web.Http.h>

namespace HaloDesktop::Services
{
    // Fetches a download's logo or landscape art and turns it into the file
    // saved beside the video: decoded by Windows (so anything that is not a
    // real image is refused), scaled down to what a card needs, and written
    // anew as PNG (the logo, which keeps its transparency) or JPEG (the art).
    // Nothing from the server is written as it came.
    //
    // Thread-agnostic; one instance per owner, not shared across threads at
    // once. Fails (throws) on any address other than https, a body over
    // HaloDesktop::Services::Downloads::TransferEngine::MaximumSidecarBytes, a
    // failed request or an undecodable image.
    class DownloadSidecarFetcher final
    {
    public:
        DownloadSidecarFetcher();

        [[nodiscard]] concurrency::task<std::vector<std::uint8_t>> FetchAsync(
            std::wstring url,
            Downloads::SidecarKind kind);

    private:
        winrt::Windows::Web::Http::HttpClient m_client;
    };

    // The widest a saved picture is kept: twice the card's width, for a dense display.
    inline constexpr std::uint32_t SidecarLogoMaximumWidth = 800;
    inline constexpr std::uint32_t SidecarArtMaximumWidth = 1280;

    // Only https addresses with a host are fetched.
    [[nodiscard]] bool IsSidecarSourceUrl(std::wstring_view url) noexcept;
}
