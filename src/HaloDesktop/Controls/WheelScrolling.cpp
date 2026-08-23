#include "pch.h"
#include "Controls/WheelScrolling.h"

#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

namespace HaloDesktop::Controls
{
    void RedirectWheelToVerticalAncestor(
        winrt::Microsoft::UI::Xaml::UIElement const& source,
        winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        namespace Xaml = winrt::Microsoft::UI::Xaml;

        if (!source)
        {
            return;
        }

        // Claimed here, on the way up from the pointer target, so the horizontal
        // scroller that owns this content never receives it.
        args.Handled(true);

        auto const delta = args.GetCurrentPoint(nullptr).Properties().MouseWheelDelta();
        if (delta == 0)
        {
            return;
        }

        for (auto current = Xaml::Media::VisualTreeHelper::GetParent(source); current;
             current = Xaml::Media::VisualTreeHelper::GetParent(current))
        {
            auto const scroller = current.try_as<Xaml::Controls::ScrollViewer>();
            if (!scroller || scroller.VerticalScrollMode() == Xaml::Controls::ScrollMode::Disabled)
            {
                continue;
            }

            scroller.ChangeView(nullptr, scroller.VerticalOffset() - delta, nullptr, true);
            return;
        }
    }
}
