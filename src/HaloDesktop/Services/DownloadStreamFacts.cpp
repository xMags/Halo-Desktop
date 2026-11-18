#include "pch.h"
#include "Services/DownloadStreamFacts.h"

#include "Api/Dto.h"
#include "Services/StreamInfo.h"

#include <optional>
#include <string>

namespace
{
    std::optional<winrt::hstring> Text(std::optional<std::wstring> const& value)
    {
        return value ? std::optional<winrt::hstring>{ winrt::hstring{ *value } } : std::nullopt;
    }

    std::optional<std::wstring> Plain(std::optional<winrt::hstring> const& value)
    {
        return value && !value->empty() ? std::optional<std::wstring>{ std::wstring{ value->c_str() } } : std::nullopt;
    }
}

namespace HaloDesktop::Services
{
    ViewModels::DownloadsPresentation::StreamFacts DownloadStreamFactsOf(Downloads::DownloadRecord const& record)
    {
        Api::Dto::StreamRecord stream;
        stream.Name = Text(record.Media.StreamName);
        stream.Title = Text(record.Media.StreamTitle);
        stream.Filename = winrt::hstring{ record.FileName };
        auto const total = record.TotalBytes > 0 ? record.TotalBytes : record.Media.VideoSize.value_or(0);
        if (total > 0) stream.VideoSize = total;
        auto const info = ParseStreamInfo(stream);
        ViewModels::DownloadsPresentation::StreamFacts facts{
            .Quality = Plain(info.Quality),
            .Codec = Plain(info.Codec),
            .DynamicRange = Plain(info.DynamicRange),
        };
        if (record.Media.DynamicRange && !record.Media.DynamicRange->empty())
        {
            facts.DynamicRange = record.Media.DynamicRange;
        }
        return facts;
    }
}
