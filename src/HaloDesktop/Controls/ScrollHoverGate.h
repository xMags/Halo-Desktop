#pragma once

#include <functional>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace HaloDesktop::Controls
{
    // Holds a card's hover back while the list under it is scrolling, as
    // browsers do: a wheel scroll carries cards under a pointer that never
    // moved, and lighting each one up in turn reads as flicker. The hover
    // arrives once the scroll settles, if the pointer is still on the card.
    //
    // Scrolling means a ScrollViewer around the card reported a view change
    // within the last ScrollSettle. The first card to ask about a scroller
    // starts watching it, so a scroll already under way when nothing was
    // watching yet goes unnoticed once; every later one is seen.
    //
    // UI thread only. Owned by the card it serves, as a member; its timer is
    // stopped and released with it, so the callback never outlives the card.
    class ScrollHoverGate
    {
    public:
        ScrollHoverGate() = default;
        ScrollHoverGate(ScrollHoverGate const&) = delete;
        ScrollHoverGate& operator=(ScrollHoverGate const&) = delete;
        ~ScrollHoverGate();

        // `apply` is the card's own hover switch; `owner` is the card. Call
        // it from the card's constructor: it holds no reference to the card.
        void Attach(winrt::Microsoft::UI::Xaml::FrameworkElement const& owner, std::function<void(bool)> apply);

        // `element` is whatever the pointer entered: the card or its button.
        void PointerEntered(winrt::Microsoft::UI::Xaml::FrameworkElement const& element);
        void PointerExited();

    private:
        void Wait();
        void StopWaiting() noexcept;
        void OnTick();

        winrt::weak_ref<winrt::Microsoft::UI::Xaml::FrameworkElement> m_owner;
        std::function<void(bool)> m_apply;
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_timer{ nullptr };
        winrt::event_token m_tickToken{};
        bool m_inside{};
    };

    // Whether a ScrollViewer around `element` is scrolling right now.
    [[nodiscard]] bool IsScrollingUnder(winrt::Microsoft::UI::Xaml::DependencyObject const& element);
}
