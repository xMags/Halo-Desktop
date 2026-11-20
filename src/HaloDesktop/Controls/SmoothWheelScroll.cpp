#include "pch.h"
#include "Controls/SmoothWheelScroll.h"

#include <cmath>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.System.h>

namespace
{
    namespace Xaml = winrt::Microsoft::UI::Xaml;

    // One wheel notch, as Windows reports it.
    constexpr double NotchDelta = 120.0;

    // Windows' "lines per notch", read on each turn so a change in Settings
    // applies at once. Three when unknown; a whole page (WHEEL_PAGESCROLL)
    // counts as ten lines.
    double WheelLines() noexcept
    {
        UINT lines = 3;
        if (!SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0)) return 3.0;
        if (lines == WHEEL_PAGESCROLL) return 10.0;
        return static_cast<double>(lines);
    }
}

namespace HaloDesktop::Controls
{
    double WheelVelocityFor(double distance) noexcept
    {
        // Speed falls by (1 - decay) each second, so it decays as
        // v0 * e^(-k t) with k = -ln(1 - decay), and travels v0 / k in all.
        auto const k = -std::log(1.0 - static_cast<double>(WheelInertiaDecay));
        return distance * k;
    }

    void AttachSmoothWheelScroll(winrt::Microsoft::UI::Xaml::Controls::ScrollView const& view)
    {
        if (!view) return;
        // Already attached: the view gave the wheel over the first time.
        if ((view.IgnoredInputKinds() & Xaml::Controls::ScrollingInputKinds::MouseWheel) == Xaml::Controls::ScrollingInputKinds::MouseWheel) return;
        view.IgnoredInputKinds(view.IgnoredInputKinds() | Xaml::Controls::ScrollingInputKinds::MouseWheel);
        auto const handler = Xaml::Input::PointerEventHandler(
            [weak = winrt::make_weak(view)](winrt::Windows::Foundation::IInspectable const&, Xaml::Input::PointerRoutedEventArgs const& args)
            {
                try
                {
                    auto const scroller = weak.get();
                    if (!scroller || args.Handled()) return;
                    if ((args.KeyModifiers() & winrt::Windows::System::VirtualKeyModifiers::Control)
                        != winrt::Windows::System::VirtualKeyModifiers::None)
                    {
                        return;
                    }
                    auto const point = args.GetCurrentPoint(scroller);
                    if (!point || point.Properties().IsHorizontalMouseWheel()) return;
                    auto const delta = static_cast<double>(point.Properties().MouseWheelDelta());
                    if (delta == 0.0) return;
                    // A turn down (negative delta) moves the page down.
                    auto const distance = -delta / NotchDelta * WheelLines() * WheelLineDistance;
                    scroller.AddScrollVelocity(
                        { 0.0F, static_cast<float>(WheelVelocityFor(distance)) },
                        winrt::Windows::Foundation::IReference<winrt::Windows::Foundation::Numerics::float2>{
                            winrt::Windows::Foundation::Numerics::float2{ WheelInertiaDecay, WheelInertiaDecay } });
                    args.Handled(true);
                }
                catch (winrt::hresult_error const&)
                {
                    // A view being torn down: the turn goes nowhere.
                }
            });
        view.AddHandler(Xaml::UIElement::PointerWheelChangedEvent(), winrt::box_value(handler), false);

        // A glide runs on the compositor, apart from the view's layout. A page
        // taken off screen mid-glide (a title opened while the page still
        // moves) came back with the two disagreeing: the view at the top, the
        // compositor still drawing the content where the glide left it, so the
        // page's header showed cut off. Leaving stops the glide where it is,
        // and arriving puts the compositor exactly where the view says it is.
        auto const settle = [weak = winrt::make_weak(view)](winrt::Windows::Foundation::IInspectable const&, Xaml::RoutedEventArgs const&)
        {
            try
            {
                auto const scroller = weak.get();
                if (!scroller) return;
                scroller.ScrollTo(
                    scroller.HorizontalOffset(),
                    scroller.VerticalOffset(),
                    Xaml::Controls::ScrollingScrollOptions{
                        Xaml::Controls::ScrollingAnimationMode::Disabled,
                        Xaml::Controls::ScrollingSnapPointsMode::Ignore });
            }
            catch (winrt::hresult_error const&)
            {
                // A view being torn down has nothing left to put right.
            }
        };
        view.Unloaded(settle);
        view.Loaded(settle);
    }
}
