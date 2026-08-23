#pragma once

#include "TagChip.g.h"

namespace winrt::HaloDesktop::implementation
{
    struct TagChip : TagChipT<TagChip>
    {
        TagChip();
        [[nodiscard]] winrt::hstring Text() const;
        void Text(winrt::hstring const& value);

    private:
        winrt::hstring m_text;
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct TagChip : TagChipT<TagChip, implementation::TagChip> {};
}
