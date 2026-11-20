#pragma once

#include "Services/AccountPictureService.h"

#include <cstdint>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.h>

namespace HaloDesktop::Views
{
    // The account picture's bytes as an image for an avatar drawn `edge`
    // logical pixels square on a surface at `rasterizationScale` (the
    // XamlRoot's). The shorter side is decoded at the avatar's size in physical
    // pixels, so a large upload is not decoded whole, the longer side keeps its
    // proportion, and nothing is upscaled at 125% or 150%. A scale that is not
    // a positive number decodes at twice the edge, sharp up to 200%.
    //
    // Called and completed on the UI thread. Throws when the bytes are not an
    // image that can be read; callers show the stock avatar then.
    [[nodiscard]] winrt::Windows::Foundation::IAsyncOperation<winrt::Microsoft::UI::Xaml::Media::ImageSource>
        DecodeAccountPictureAsync(
            ::HaloDesktop::Services::AccountPictureBytes bytes,
            std::uint32_t edge,
            double rasterizationScale);

    // The scale of the window `element` is in, or 0 when it is in none yet
    // (DecodeAccountPictureAsync then uses its fallback).
    [[nodiscard]] double RasterizationScaleOf(winrt::Microsoft::UI::Xaml::UIElement const& element) noexcept;
}
