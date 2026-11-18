#pragma once

#include "Services/AccountPictureService.h"

#include <cstdint>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.h>

namespace HaloDesktop::Views
{
    // The account picture's bytes as an image for an avatar drawn `edge`
    // pixels square. The shorter side is decoded at that size, so a large
    // upload is not decoded whole, and the longer side keeps its proportion.
    //
    // Called and completed on the UI thread. Throws when the bytes are not an
    // image that can be read; callers show the stock avatar then.
    [[nodiscard]] winrt::Windows::Foundation::IAsyncOperation<winrt::Microsoft::UI::Xaml::Media::ImageSource>
        DecodeAccountPictureAsync(::HaloDesktop::Services::AccountPictureBytes bytes, std::uint32_t edge);
}
