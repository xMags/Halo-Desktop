#include "pch.h"
#include "Services/DownloadSidecarFetcher.h"

#include "Api/BoundedHttpContent.h"
#include "Services/Downloads/TransferEngine.h"

#include <algorithm>
#include <limits>
#include <pplawait.h>
#include <stdexcept>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>

namespace
{
    namespace Imaging = winrt::Windows::Graphics::Imaging;
    namespace Streams = winrt::Windows::Storage::Streams;

    constexpr float ArtJpegQuality = 0.88F;

    winrt::Windows::Foundation::IAsyncOperation<Streams::InMemoryRandomAccessStream> StreamOf(
        std::vector<std::uint8_t> const& bytes)
    {
        Streams::InMemoryRandomAccessStream stream;
        Streams::DataWriter writer{ stream };
        writer.WriteBytes(winrt::array_view<std::uint8_t const>{
            bytes.data(),
            static_cast<std::uint32_t>(bytes.size()) });
        co_await writer.StoreAsync();
        writer.DetachStream();
        stream.Seek(0);
        co_return stream;
    }

    concurrency::task<std::vector<std::uint8_t>> ReadAll(Streams::IRandomAccessStream stream)
    {
        auto const size = stream.Size();
        if (size == 0 || size > HaloDesktop::Services::Downloads::TransferEngine::MaximumSidecarBytes)
        {
            throw std::length_error{ "The saved picture came out empty or too large." };
        }
        Streams::DataReader reader{ stream.GetInputStreamAt(0) };
        co_await reader.LoadAsync(static_cast<std::uint32_t>(size));
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        reader.ReadBytes(winrt::array_view<std::uint8_t>{ bytes.data(), static_cast<std::uint32_t>(bytes.size()) });
        co_return bytes;
    }
}

namespace HaloDesktop::Services
{
    bool IsSidecarSourceUrl(std::wstring_view url) noexcept
    {
        try
        {
            if (url.empty() || url.size() > 4096 || !url.starts_with(L"https://"))
            {
                return false;
            }
            winrt::Windows::Foundation::Uri const uri{ winrt::hstring{ url } };
            return uri.SchemeName() == L"https" && !uri.Host().empty();
        }
        catch (...)
        {
            return false;
        }
    }

    DownloadSidecarFetcher::DownloadSidecarFetcher() = default;

    concurrency::task<std::vector<std::uint8_t>> DownloadSidecarFetcher::FetchAsync(
        std::wstring url,
        Downloads::SidecarKind kind)
    {
        if (!IsSidecarSourceUrl(url))
        {
            throw std::invalid_argument{ "The picture's address is not one to fetch." };
        }
        auto const response = co_await m_client.GetAsync(
            winrt::Windows::Foundation::Uri{ url },
            winrt::Windows::Web::Http::HttpCompletionOption::ResponseHeadersRead);
        response.EnsureSuccessStatusCode();
        auto const bytes = co_await ::HaloDesktop::Api::ReadBoundedBytesAsync(
            response.Content(),
            Downloads::TransferEngine::MaximumSidecarBytes);
        if (bytes.empty() || bytes.size() > (std::numeric_limits<std::uint32_t>::max)())
        {
            throw std::length_error{ "The picture was empty." };
        }

        auto const source = co_await StreamOf(bytes);
        auto const decoder = co_await Imaging::BitmapDecoder::CreateAsync(source);
        auto const width = decoder.PixelWidth();
        auto const height = decoder.PixelHeight();
        if (width == 0 || height == 0)
        {
            throw std::invalid_argument{ "The picture has no size." };
        }
        auto const isLogo = kind == Downloads::SidecarKind::Logo;
        auto const limit = isLogo ? SidecarLogoMaximumWidth : SidecarArtMaximumWidth;
        Imaging::BitmapTransform transform;
        auto const scaledWidth = (std::min)(width, limit);
        transform.ScaledWidth(scaledWidth);
        transform.ScaledHeight((std::max)(1u, static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(height) * scaledWidth / width)));
        transform.InterpolationMode(Imaging::BitmapInterpolationMode::Fant);
        // The logo keeps its transparency; the art is opaque, as JPEG must be.
        auto const pixels = co_await decoder.GetSoftwareBitmapAsync(
            Imaging::BitmapPixelFormat::Bgra8,
            isLogo ? Imaging::BitmapAlphaMode::Premultiplied : Imaging::BitmapAlphaMode::Ignore,
            transform,
            Imaging::ExifOrientationMode::RespectExifOrientation,
            Imaging::ColorManagementMode::ColorManageToSRgb);

        Streams::InMemoryRandomAccessStream output;
        Imaging::BitmapEncoder encoder{ nullptr };
        if (isLogo)
        {
            encoder = co_await Imaging::BitmapEncoder::CreateAsync(Imaging::BitmapEncoder::PngEncoderId(), output);
        }
        else
        {
            Imaging::BitmapPropertySet options;
            options.Insert(L"ImageQuality", Imaging::BitmapTypedValue{
                winrt::box_value(ArtJpegQuality),
                winrt::Windows::Foundation::PropertyType::Single });
            encoder = co_await Imaging::BitmapEncoder::CreateAsync(Imaging::BitmapEncoder::JpegEncoderId(), output, options);
        }
        encoder.SetSoftwareBitmap(pixels);
        co_await encoder.FlushAsync();

        co_return co_await ReadAll(output);
    }
}
