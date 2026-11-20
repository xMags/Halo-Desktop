#include "pch.h"
#include "Views/AccountPictureDecoder.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>

namespace
{
    // Used when the caller has no scale to give: sharp up to 200%, and only
    // gently downscaled below it.
    constexpr double FallbackScale = 2.0;
}

namespace HaloDesktop::Views
{
    winrt::Windows::Foundation::IAsyncOperation<winrt::Microsoft::UI::Xaml::Media::ImageSource>
        DecodeAccountPictureAsync(
            ::HaloDesktop::Services::AccountPictureBytes bytes,
            std::uint32_t edge,
            double rasterizationScale)
    {
        auto const uiContext = winrt::apartment_context{};
        if (!bytes || bytes->empty() || bytes->size() > (std::numeric_limits<std::uint32_t>::max)())
        {
            throw std::invalid_argument{ "The account picture is empty." };
        }

        winrt::Windows::Storage::Streams::InMemoryRandomAccessStream stream;
        {
            winrt::Windows::Storage::Streams::DataWriter writer{ stream };
            writer.WriteBytes(winrt::array_view<std::uint8_t const>{
                bytes->data(),
                static_cast<std::uint32_t>(bytes->size()) });
            co_await writer.StoreAsync();
            writer.DetachStream();
        }

        // Physical pixels from the caller's own scale, rather than
        // DecodePixelType::Logical, so the decoded size never rests on how
        // WinUI resolves "logical" for a bitmap that is not yet in the tree.
        auto const scale = std::isfinite(rasterizationScale) && rasterizationScale > 0.0
            ? rasterizationScale
            : FallbackScale;
        auto const target = static_cast<std::uint32_t>((std::min)(
            std::ceil(static_cast<double>(edge) * scale),
            static_cast<double>((std::numeric_limits<std::int32_t>::max)())));

        // The shape is read first so the shorter side is decoded at the
        // avatar's size: an upload can be far larger than the avatar, and
        // decoding both sides would stretch it.
        stream.Seek(0);
        auto const decoder = co_await winrt::Windows::Graphics::Imaging::BitmapDecoder::CreateAsync(stream);
        auto const width = decoder.PixelWidth();
        auto const height = decoder.PixelHeight();
        stream.Seek(0);
        co_await uiContext;

        winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage bitmap;
        bitmap.DecodePixelType(winrt::Microsoft::UI::Xaml::Media::Imaging::DecodePixelType::Physical);
        if (width > 0 && height > 0)
        {
            if (width <= height)
            {
                bitmap.DecodePixelWidth(static_cast<std::int32_t>((std::min)(width, target)));
            }
            else
            {
                bitmap.DecodePixelHeight(static_cast<std::int32_t>((std::min)(height, target)));
            }
        }
        co_await bitmap.SetSourceAsync(stream);
        co_await uiContext;
        co_return bitmap;
    }

    double RasterizationScaleOf(winrt::Microsoft::UI::Xaml::UIElement const& element) noexcept
    {
        try
        {
            auto const root = element ? element.XamlRoot() : nullptr;
            return root ? root.RasterizationScale() : 0.0;
        }
        catch (winrt::hresult_error const&)
        {
            return 0.0;
        }
    }
}
