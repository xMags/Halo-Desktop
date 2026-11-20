#pragma once

#include <cstdint>
#include <filesystem>
#include <winrt/Windows.Foundation.h>
#include <string>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Web.Http.h>

namespace HaloDesktop::Views
{
    // A download card's title logo, ready to draw over the card's dark fade:
    // read from the file saved beside the video when there is one, else from
    // `onlineUrl` (https, at most HeroLogoMaximumBytes), decoded at no more than
    // `decodeHeight` pixels tall, and judged as Home's banner judges its logo.
    // A dark mark (InkForDarkGround) comes back in light ink; any other logo as
    // drawn.
    //
    // Call on the UI thread; it finishes there, since the bitmap belongs to it.
    // Throws when neither source gives a logo that decodes.
    [[nodiscard]] winrt::Windows::Foundation::IAsyncOperation<winrt::Microsoft::UI::Xaml::Media::ImageSource> LoadCardLogoAsync(
        winrt::Windows::Web::Http::HttpClient client,
        std::filesystem::path savedPath,
        std::wstring onlineUrl,
        std::uint32_t decodeHeight);
}
