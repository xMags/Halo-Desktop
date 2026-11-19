#include "pch.h"
#include "Views/HeroLogoLoader.h"

#include "Api/BoundedHttpContent.h"
#include "Views/HeroLogoPolicy.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <pplawait.h>
#include <stdexcept>
#include <vector>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>

namespace HaloDesktop::Views
{
    concurrency::task<LoadedHeroLogo> LoadHeroLogoAsync(
        winrt::Windows::Web::Http::HttpClient client,
        std::wstring url)
    {
        auto const uiContext = winrt::apartment_context{};
        if (!client || !IsHeroLogoUrl(url))
        {
            throw std::invalid_argument{ "The logo address is not one to fetch." };
        }

        auto const response = co_await client.GetAsync(
            winrt::Windows::Foundation::Uri{ url },
            winrt::Windows::Web::Http::HttpCompletionOption::ResponseHeadersRead);
        response.EnsureSuccessStatusCode();
        auto const bytes = co_await ::HaloDesktop::Api::ReadBoundedBytesAsync(response.Content(), HeroLogoMaximumBytes);
        if (bytes.empty() || bytes.size() > (std::numeric_limits<std::uint32_t>::max)())
        {
            throw std::length_error{ "The logo was empty." };
        }

        winrt::Windows::Storage::Streams::InMemoryRandomAccessStream stream;
        {
            winrt::Windows::Storage::Streams::DataWriter writer{ stream };
            writer.WriteBytes(winrt::array_view<std::uint8_t const>{
                bytes.data(),
                static_cast<std::uint32_t>(bytes.size()) });
            co_await writer.StoreAsync();
            writer.DetachStream();
        }

        // A small sample tells a white mark from a dark one; a decoder that
        // cannot sample it leaves the brightness unknown, not the logo unshown.
        LoadedHeroLogo loaded;
        try
        {
            stream.Seek(0);
            auto const decoder = co_await winrt::Windows::Graphics::Imaging::BitmapDecoder::CreateAsync(stream);
            auto const width = decoder.PixelWidth();
            auto const height = decoder.PixelHeight();
            if (width > 0 && height > 0)
            {
                winrt::Windows::Graphics::Imaging::BitmapTransform transform;
                auto const sampleWidth = (std::min)(width, HeroLogoSampleWidth);
                auto const sampleHeight = (std::max)(1u, static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(height) * sampleWidth / width));
                transform.ScaledWidth(sampleWidth);
                transform.ScaledHeight(sampleHeight);
                auto const pixels = co_await decoder.GetPixelDataAsync(
                    winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8,
                    winrt::Windows::Graphics::Imaging::BitmapAlphaMode::Straight,
                    transform,
                    winrt::Windows::Graphics::Imaging::ExifOrientationMode::IgnoreExifOrientation,
                    winrt::Windows::Graphics::Imaging::ColorManagementMode::DoNotColorManage);
                auto const data = pixels.DetachPixelData();
                loaded.Luminance = HeroLogoLuminance(std::span<std::uint8_t const>{ data.data(), data.size() });
            }
        }
        catch (winrt::hresult_error const&)
        {
            loaded.Luminance.reset();
        }

        co_await uiContext;
        stream.Seek(0);
        winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage bitmap;
        co_await bitmap.SetSourceAsync(stream);
        co_await uiContext;
        loaded.Image = bitmap;
        co_return loaded;
    }
}
