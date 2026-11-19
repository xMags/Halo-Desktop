#include "pch.h"
#include "Controls/SidewaysWheelGuard.h"

#include "Controls/ShelfScrollPolicy.h"

#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Windows.System.h>

namespace HaloDesktop::Controls
{
    void GuardSidewaysWheel(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args) noexcept
    {
        try
        {
            auto const point = args.GetCurrentPoint(nullptr);
            if (!point)
            {
                return;
            }
            auto const shifted =
                (args.KeyModifiers() & winrt::Windows::System::VirtualKeyModifiers::Shift) !=
                winrt::Windows::System::VirtualKeyModifiers::None;
            if (IsSidewaysWheel(point.Properties().IsHorizontalMouseWheel(), shifted))
            {
                args.Handled(true);
            }
        }
        catch (winrt::hresult_error const&)
        {
            // An event from a strip being torn down: nothing left to scroll.
        }
    }
}
