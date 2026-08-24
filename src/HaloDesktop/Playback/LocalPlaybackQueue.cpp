#include "pch.h"
#include "Playback/LocalPlaybackQueue.h"

#include <algorithm>
#include <cwctype>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    using ExtensionList = HaloDesktop::Playback::LocalPlaybackQueue::ExtensionList;

    constexpr ExtensionList SupportedVideoExtensions{ L".mkv", L".mp4", L".webm", L".mov", L".m4v", L".avi" };

    std::wstring Lowercase(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
        return value;
    }

    bool IsSupportedVideo(std::filesystem::path const& path)
    {
        auto const extension = Lowercase(path.extension().wstring());
        return std::find(SupportedVideoExtensions.begin(), SupportedVideoExtensions.end(), extension) !=
               SupportedVideoExtensions.end();
    }

    int CompareNatural(std::wstring_view left, std::wstring_view right) noexcept
    {
        std::size_t leftIndex{};
        std::size_t rightIndex{};
        while (leftIndex < left.size() && rightIndex < right.size())
        {
            auto const leftIsDigit = std::iswdigit(left[leftIndex]) != 0;
            auto const rightIsDigit = std::iswdigit(right[rightIndex]) != 0;
            if (leftIsDigit && rightIsDigit)
            {
                auto const leftRunStart = leftIndex;
                auto const rightRunStart = rightIndex;
                while (leftIndex < left.size() && left[leftIndex] == L'0')
                {
                    ++leftIndex;
                }
                while (rightIndex < right.size() && right[rightIndex] == L'0')
                {
                    ++rightIndex;
                }

                auto leftRunEnd = leftIndex;
                auto rightRunEnd = rightIndex;
                while (leftRunEnd < left.size() && std::iswdigit(left[leftRunEnd]) != 0)
                {
                    ++leftRunEnd;
                }
                while (rightRunEnd < right.size() && std::iswdigit(right[rightRunEnd]) != 0)
                {
                    ++rightRunEnd;
                }

                auto const leftDigits = leftRunEnd - leftIndex;
                auto const rightDigits = rightRunEnd - rightIndex;
                if (leftDigits != rightDigits)
                {
                    return leftDigits < rightDigits ? -1 : 1;
                }
                for (std::size_t offset = 0; offset < leftDigits; ++offset)
                {
                    if (left[leftIndex + offset] != right[rightIndex + offset])
                    {
                        return left[leftIndex + offset] < right[rightIndex + offset] ? -1 : 1;
                    }
                }

                auto const leftRunLength = leftRunEnd - leftRunStart;
                auto const rightRunLength = rightRunEnd - rightRunStart;
                if (leftRunLength != rightRunLength)
                {
                    return leftRunLength < rightRunLength ? -1 : 1;
                }
                leftIndex = leftRunEnd;
                rightIndex = rightRunEnd;
                continue;
            }

            auto const leftCharacter = static_cast<wchar_t>(std::towlower(left[leftIndex]));
            auto const rightCharacter = static_cast<wchar_t>(std::towlower(right[rightIndex]));
            if (leftCharacter != rightCharacter)
            {
                return leftCharacter < rightCharacter ? -1 : 1;
            }
            ++leftIndex;
            ++rightIndex;
        }

        if (leftIndex != left.size())
        {
            return 1;
        }
        if (rightIndex != right.size())
        {
            return -1;
        }
        return 0;
    }

    bool NaturalPathLess(std::filesystem::path const& left, std::filesystem::path const& right)
    {
        auto const leftName = left.filename().wstring();
        auto const rightName = right.filename().wstring();
        auto const comparison = CompareNatural(leftName, rightName);
        if (comparison != 0)
        {
            return comparison < 0;
        }
        return leftName < rightName;
    }
} // namespace

namespace HaloDesktop::Playback
{
    LocalPlaybackQueue::ExtensionList const& LocalPlaybackQueue::SupportedExtensions() noexcept
    {
        return SupportedVideoExtensions;
    }

    std::optional<std::filesystem::path> LocalPlaybackQueue::NextAfter(
        std::filesystem::path const& currentSource)
    {
        std::error_code error;
        if (!std::filesystem::is_regular_file(currentSource, error) || error)
        {
            return std::nullopt;
        }

        std::vector<std::filesystem::path> sources;
        auto iterator = std::filesystem::directory_iterator(
            currentSource.parent_path(), std::filesystem::directory_options::skip_permission_denied, error);
        auto const end = std::filesystem::directory_iterator{};
        while (!error && iterator != end)
        {
            auto const& entry = *iterator;
            std::error_code entryError;
            if (entry.is_regular_file(entryError) && !entryError && IsSupportedVideo(entry.path()))
            {
                sources.push_back(entry.path());
            }
            iterator.increment(error);
        }
        if (error)
        {
            return std::nullopt;
        }

        std::sort(sources.begin(), sources.end(), NaturalPathLess);
        auto const current = std::find_if(sources.begin(), sources.end(), [&currentSource](auto const& candidate) {
            std::error_code equivalentError;
            return std::filesystem::equivalent(candidate, currentSource, equivalentError) && !equivalentError;
        });
        if (current == sources.end())
        {
            return std::nullopt;
        }

        auto const next = std::next(current);
        return next == sources.end() ? std::nullopt : std::optional<std::filesystem::path>{ *next };
    }
} // namespace HaloDesktop::Playback
