#pragma once

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace HaloDesktop::Views
{
    // The parts of one title page row (`components/ScrollRow.tsx`), all from the
    // panel's markup.
    struct DetailScrollRowParts final
    {
        winrt::Microsoft::UI::Xaml::Controls::ScrollViewer Strip{ nullptr };
        // The strip's content, whose size changes as cards arrive.
        winrt::Microsoft::UI::Xaml::FrameworkElement Content{ nullptr };
        // The two arrows' container, shown only when the strip overflows.
        winrt::Microsoft::UI::Xaml::UIElement Pager{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Button Previous{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Button Next{ nullptr };
        winrt::Microsoft::UI::Xaml::UIElement StartFade{ nullptr };
        winrt::Microsoft::UI::Xaml::UIElement EndFade{ nullptr };
    };

    // Drives a row's arrows and edge fades from the strip's own offset and
    // size: the arrows appear only when the strip overflows and dim at either
    // end, a click moves one page (ShelfScrollPolicy), and the edge with more to
    // show fades. UI-thread-only; owned by the panel whose parts it holds, and
    // not copyable, because its handlers refer back to it. Every handler is
    // revoked before the rest of it is destroyed.
    class DetailScrollRow final
    {
    public:
        explicit DetailScrollRow(DetailScrollRowParts parts);

        DetailScrollRow(DetailScrollRow const&) = delete;
        DetailScrollRow& operator=(DetailScrollRow const&) = delete;
        DetailScrollRow(DetailScrollRow&&) = delete;
        DetailScrollRow& operator=(DetailScrollRow&&) = delete;

        // Re-measures the edges, as when the panel's items change.
        void Refresh();

    private:
        void Page(int direction);

        DetailScrollRowParts m_parts;
        // Last, so they are revoked before the parts they reach.
        winrt::Microsoft::UI::Xaml::Controls::ScrollViewer::ViewChanged_revoker m_viewChanged;
        winrt::Microsoft::UI::Xaml::FrameworkElement::SizeChanged_revoker m_stripSizeChanged;
        winrt::Microsoft::UI::Xaml::FrameworkElement::SizeChanged_revoker m_contentSizeChanged;
        winrt::Microsoft::UI::Xaml::Controls::Button::Click_revoker m_previousClick;
        winrt::Microsoft::UI::Xaml::Controls::Button::Click_revoker m_nextClick;
    };
}
