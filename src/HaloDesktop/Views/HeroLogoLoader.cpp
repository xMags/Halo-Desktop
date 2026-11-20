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
                auto const decodeAt = [&decoder, width, height](std::uint32_t limit)
                {
                    winrt::Windows::Graphics::Imaging::BitmapTransform transform;
                    auto const scaledWidth = (std::min)(width, limit);
                    transform.ScaledWidth(scaledWidth);
                    transform.ScaledHeight((std::max)(1u, static_cast<std::uint32_t>(
                        static_cast<std::uint64_t>(height) * scaledWidth / width)));
                    return decoder.GetPixelDataAsync(
                        winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8,
                        winrt::Windows::Graphics::Imaging::BitmapAlphaMode::Straight,
                        transform,
                        winrt::Windows::Graphics::Imaging::ExifOrientationMode::IgnoreExifOrientation,
                        winrt::Windows::Graphics::Imaging::ColorManagementMode::DoNotColorManage);
                };
                auto const sample = (co_await decodeAt(HeroLogoSampleWidth)).DetachPixelData();
                std::span<std::uint8_t const> const sampled{ sample.data(), sample.size() };
                loaded.Luminance = HeroLogoLuminance(sampled);
                loaded.Coverage = HeroLogoCoverage(sampled);

                auto const inkWidth = (std::min)(width, HeroLogoInkWidth);
                auto const inkHeight = (std::max)(1u, static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(height) * inkWidth / width));
                auto const full = (co_await decodeAt(HeroLogoInkWidth)).DetachPixelData();
                if (full.size() == static_cast<std::size_t>(inkWidth) * inkHeight * 4u)
                {
                    loaded.Pixels.assign(full.begin(), full.end());
                    loaded.PixelWidth = inkWidth;
                    loaded.PixelHeight = inkHeight;
                }
            }
        }
        catch (winrt::hresult_error const&)
        {
            loaded.Luminance.reset();
            loaded.Coverage.reset();
            loaded.Pixels.clear();
        }

        co_await uiContext;
        stream.Seek(0);
        winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage bitmap;
        co_await bitmap.SetSourceAsync(stream);
        co_await uiContext;
        loaded.Image = bitmap;
        co_return loaded;
    }

    winrt::Microsoft::UI::Xaml::Media::ImageSource InkedHeroLogo(LoadedHeroLogo& logo, winrt::Windows::UI::Color ink)
    {
        if (logo.Pixels.empty() || logo.PixelWidth == 0 || logo.PixelHeight == 0) return nullptr;
        if (logo.Inked && logo.InkColor == ink) return logo.Inked;
        try
        {
            std::vector<std::uint8_t> pixels = logo.Pixels;
            InkHeroLogo(pixels, ink.R, ink.G, ink.B);
            winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap bitmap{
                static_cast<std::int32_t>(logo.PixelWidth),
                static_cast<std::int32_t>(logo.PixelHeight) };
            auto const buffer = bitmap.PixelBuffer();
            if (buffer.Capacity() < pixels.size()) return nullptr;
            std::copy(pixels.begin(), pixels.end(), buffer.data());
            buffer.Length(static_cast<std::uint32_t>(pixels.size()));
            bitmap.Invalidate();
            logo.Inked = bitmap;
            logo.InkColor = ink;
            return bitmap;
        }
        catch (winrt::hresult_error const&)
        {
            return nullptr;
        }
    }
}
