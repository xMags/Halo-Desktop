#include "pch.h"
#include "Views/CardLogoLoader.h"

#include "Api/BoundedHttpContent.h"
#include "Views/HeroLogoPolicy.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <pplawait.h>
#include <span>
#include <stdexcept>
#include <vector>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>

namespace
{
    namespace Imaging = winrt::Windows::Graphics::Imaging;
    namespace Streams = winrt::Windows::Storage::Streams;

    // The light ink a dark mark is drawn in: the title page's own ink.
    constexpr std::uint8_t InkRed = 0xF5;
    constexpr std::uint8_t InkGreen = 0xF5;
    constexpr std::uint8_t InkBlue = 0xF7;

    // The saved file's bytes, or none when it is missing, unreadable or too large.
    std::vector<std::uint8_t> ReadSaved(std::filesystem::path const& path)
    {
        std::vector<std::uint8_t> bytes;
        if (path.empty()) return bytes;
        std::error_code error;
        auto const size = std::filesystem::file_size(path, error);
        if (error || size == 0 || size > HaloDesktop::Views::HeroLogoMaximumBytes) return bytes;
        std::ifstream file{ path, std::ios::binary };
        if (!file) return bytes;
        bytes.resize(static_cast<std::size_t>(size));
        if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        {
            bytes.clear();
        }
        return bytes;
    }
}

namespace HaloDesktop::Views
{
    winrt::Windows::Foundation::IAsyncOperation<winrt::Microsoft::UI::Xaml::Media::ImageSource> LoadCardLogoAsync(
        winrt::Windows::Web::Http::HttpClient client,
        std::filesystem::path savedPath,
        std::wstring onlineUrl,
        std::uint32_t decodeHeight)
    {
        auto const uiContext = winrt::apartment_context{};
        co_await winrt::resume_background();
        auto bytes = ReadSaved(savedPath);
        if (bytes.empty())
        {
            if (!client || !IsHeroLogoUrl(onlineUrl))
            {
                throw std::invalid_argument{ "There is no logo to load." };
            }
            auto const response = co_await client.GetAsync(
                winrt::Windows::Foundation::Uri{ onlineUrl },
                winrt::Windows::Web::Http::HttpCompletionOption::ResponseHeadersRead);
            response.EnsureSuccessStatusCode();
            bytes = co_await ::HaloDesktop::Api::ReadBoundedBytesAsync(response.Content(), HeroLogoMaximumBytes);
        }
        if (bytes.empty() || bytes.size() > (std::numeric_limits<std::uint32_t>::max)())
        {
            throw std::length_error{ "The logo was empty." };
        }

        Streams::InMemoryRandomAccessStream stream;
        {
            Streams::DataWriter writer{ stream };
            writer.WriteBytes(winrt::array_view<std::uint8_t const>{ bytes.data(), static_cast<std::uint32_t>(bytes.size()) });
            co_await writer.StoreAsync();
            writer.DetachStream();
        }
        stream.Seek(0);
        auto const decoder = co_await Imaging::BitmapDecoder::CreateAsync(stream);
        auto const width = decoder.PixelWidth();
        auto const height = decoder.PixelHeight();
        if (width == 0 || height == 0)
        {
            throw std::invalid_argument{ "The logo has no size." };
        }
        auto const scaledHeight = (std::min)(height, (std::max)(decodeHeight, 1u));
        auto const scaledWidth = (std::max)(1u, static_cast<std::uint32_t>(static_cast<std::uint64_t>(width) * scaledHeight / height));
        Imaging::BitmapTransform transform;
        transform.ScaledWidth(scaledWidth);
        transform.ScaledHeight(scaledHeight);
        transform.InterpolationMode(Imaging::BitmapInterpolationMode::Fant);

        // Straight alpha to judge it: brightness is the colour, not the colour
        // already darkened by its own transparency.
        auto const straight = (co_await decoder.GetPixelDataAsync(
            Imaging::BitmapPixelFormat::Bgra8,
            Imaging::BitmapAlphaMode::Straight,
            transform,
            Imaging::ExifOrientationMode::IgnoreExifOrientation,
            Imaging::ColorManagementMode::DoNotColorManage)).DetachPixelData();
        std::vector<std::uint8_t> pixels(straight.begin(), straight.end());
        if (pixels.size() != static_cast<std::size_t>(scaledWidth) * scaledHeight * 4u)
        {
            throw std::runtime_error{ "The logo decoded to the wrong size." };
        }
        std::span<std::uint8_t const> const judged{ pixels.data(), pixels.size() };
        if (InkForDarkGround(HeroLogoLuminance(judged), HeroLogoCoverage(judged)))
        {
            InkHeroLogo(pixels, InkRed, InkGreen, InkBlue);
        }
        else
        {
            // As drawn, premultiplied as a writeable bitmap holds it.
            auto const premultiplied = (co_await decoder.GetPixelDataAsync(
                Imaging::BitmapPixelFormat::Bgra8,
                Imaging::BitmapAlphaMode::Premultiplied,
                transform,
                Imaging::ExifOrientationMode::IgnoreExifOrientation,
                Imaging::ColorManagementMode::DoNotColorManage)).DetachPixelData();
            pixels.assign(premultiplied.begin(), premultiplied.end());
        }

        co_await uiContext;
        winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap bitmap{
            static_cast<std::int32_t>(scaledWidth), static_cast<std::int32_t>(scaledHeight) };
        auto const buffer = bitmap.PixelBuffer();
        if (buffer.Capacity() < pixels.size())
        {
            throw std::runtime_error{ "The logo did not fit its bitmap." };
        }
        std::copy(pixels.begin(), pixels.end(), buffer.data());
        buffer.Length(static_cast<std::uint32_t>(pixels.size()));
        bitmap.Invalidate();
        co_return bitmap;
    }
}
