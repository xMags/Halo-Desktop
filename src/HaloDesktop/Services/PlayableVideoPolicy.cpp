#include "Services/PlayableVideoPolicy.h"

#include <cwctype>

namespace HaloDesktop::Services
{
    std::wstring MovieVideoId(std::wstring_view id, std::wstring_view defaultVideoId)
    {
        auto const isSpace = [](wchar_t character) { return std::iswspace(character) != 0; };
        auto first = defaultVideoId.begin();
        auto last = defaultVideoId.end();
        while (first != last && isSpace(*first)) ++first;
        while (last != first && isSpace(*(last - 1))) --last;
        return first == last ? std::wstring{ id } : std::wstring{ first, last };
    }

    bool PlayableFromPreview(std::wstring_view id, std::wstring_view defaultVideoId)
    {
        return id.starts_with(L"tt") || MovieVideoId(id, defaultVideoId) != id;
    }
}
