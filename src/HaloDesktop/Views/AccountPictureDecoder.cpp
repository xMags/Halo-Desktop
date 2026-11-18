#include "pch.h"
#include "Views/AccountPictureDecoder.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>

namespace HaloDesktop::Views
{
    winrt::Windows::Foundation::IAsyncOperation<winrt::Microsoft::UI::Xaml::Media::ImageSource>
        DecodeAccountPictureAsync(::HaloDesktop::Services::AccountPictureBytes bytes, std::uint32_t edge)
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
        bitmap.DecodePixelType(winrt::Microsoft::UI::Xaml::Media::Imaging::DecodePixelType::Logical);
        if (width > 0 && height > 0)
        {
            if (width <= height)
            {
                bitmap.DecodePixelWidth(static_cast<std::int32_t>((std::min)(width, edge)));
            }
            else
            {
                bitmap.DecodePixelHeight(static_cast<std::int32_t>((std::min)(height, edge)));
            }
        }
        co_await bitmap.SetSourceAsync(stream);
        co_await uiContext;
        co_return bitmap;
    }
}
