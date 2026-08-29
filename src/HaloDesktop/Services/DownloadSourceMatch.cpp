#include "Services/DownloadSourceMatch.h"

#include <algorithm>
#include <cwctype>

namespace HaloDesktop::Services
{
    std::wstring_view ReleaseLeafName(std::wstring_view value) noexcept
    {
        auto const separator = value.find_last_of(L"/\\");
        if (separator != std::wstring_view::npos) value.remove_prefix(separator + 1);

        auto const first = value.find_first_not_of(L" \t\r\n");
        if (first == std::wstring_view::npos) return {};
        auto const last = value.find_last_not_of(L" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    bool SameReleaseFile(std::wstring_view saved, std::wstring_view candidate) noexcept
    {
        auto const left = ReleaseLeafName(saved);
        auto const right = ReleaseLeafName(candidate);
        // An addon that named nothing must not match the one download that also
        // named nothing, so an empty name matches no file at all.
        if (left.empty() || right.empty() || left.size() != right.size()) return false;
        return std::equal(left.begin(), left.end(), right.begin(), [](wchar_t leftCharacter, wchar_t rightCharacter)
        {
            return std::towlower(leftCharacter) == std::towlower(rightCharacter);
        });
    }
}
