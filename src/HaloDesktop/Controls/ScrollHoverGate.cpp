#include "pch.h"
#include "Controls/ScrollHoverGate.h"

#include <chrono>
#include <utility>
#include <winrt/Windows.UI.Xaml.Interop.h>

namespace
{
    namespace Xaml = winrt::Microsoft::UI::Xaml;

    // How long after a scroller's last view change it still counts as
    // scrolling. A scroll reports a change every frame while it moves, so
    // this only has to bridge one or two late frames.
    constexpr std::chrono::milliseconds ScrollSettle{ 120 };
    // How often a held hover looks again. Short enough that the hover lands
    // at about the moment the list comes to rest.
    constexpr std::chrono::milliseconds HoldCheck{ 50 };

    // When one scroller last moved. Lives on the scroller itself (an attached
    // value), so it goes with it and every card inside reads the same one.
    struct ScrollActivity : winrt::implements<ScrollActivity, winrt::Windows::Foundation::IInspectable>
    {
        std::chrono::steady_clock::time_point LastChange{};
    };

    // Registered once, on first use, on the UI thread. The owner type only
    // names the property; nothing in markup sets it.
    Xaml::DependencyProperty ActivityProperty()
    {
        static Xaml::DependencyProperty const property = []() -> Xaml::DependencyProperty
        {
            try
            {
                return Xaml::DependencyProperty::RegisterAttached(
                    L"HaloScrollActivity",
                    winrt::xaml_typename<winrt::Windows::Foundation::IInspectable>(),
                    winrt::xaml_typename<Xaml::Controls::ScrollViewer>(),
                    Xaml::PropertyMetadata{ nullptr });
            }
            catch (winrt::hresult_error const&)
            {
                // Without it nothing is held back: hover behaves as before.
                return nullptr;
            }
        }();
        return property;
    }

    // The scroller's activity, watching it from now on if nothing was yet.
    ScrollActivity* ActivityOf(Xaml::Controls::ScrollViewer const& scroller)
    {
        auto const property = ActivityProperty();
        if (!property) return nullptr;
        if (auto const existing = scroller.GetValue(property))
        {
            return winrt::get_self<ScrollActivity>(existing);
        }
        auto const activity = winrt::make_self<ScrollActivity>();
        // The handler holds the activity, not the scroller, so the two never
        // keep each other alive; both go when the scroller does.
        scroller.ViewChanged([activity](winrt::Windows::Foundation::IInspectable const&, Xaml::Controls::ScrollViewerViewChangedEventArgs const&)
        {
            activity->LastChange = std::chrono::steady_clock::now();
        });
        scroller.SetValue(property, activity.as<winrt::Windows::Foundation::IInspectable>());
        return activity.get();
    }
}

namespace HaloDesktop::Controls
{
    bool IsScrollingUnder(winrt::Microsoft::UI::Xaml::DependencyObject const& element)
    {
        try
        {
            auto const now = std::chrono::steady_clock::now();
            auto scrolling = false;
            // Every scroller counts: a shelf scrolls sideways inside a page
            // that scrolls down, and either one moving carries the card.
            for (auto node = Xaml::Media::VisualTreeHelper::GetParent(element); node;
                 node = Xaml::Media::VisualTreeHelper::GetParent(node))
            {
                auto const scroller = node.try_as<Xaml::Controls::ScrollViewer>();
                if (!scroller) continue;
                auto const activity = ActivityOf(scroller);
                if (activity && now - activity->LastChange < ScrollSettle) scrolling = true;
            }
            return scrolling;
        }
        catch (winrt::hresult_error const&)
        {
            return false;
        }
    }

    ScrollHoverGate::~ScrollHoverGate()
    {
        StopWaiting();
        if (m_timer && m_tickToken.value != 0)
        {
            try
            {
                m_timer.Tick(m_tickToken);
            }
            catch (winrt::hresult_error const&)
            {
            }
        }
    }

    void ScrollHoverGate::Attach(winrt::Microsoft::UI::Xaml::FrameworkElement const& owner, std::function<void(bool)> apply)
    {
        m_apply = std::move(apply);
        // Start watching the scrollers as the card arrives, so the first
        // scroll that carries it under the pointer is already seen.
        owner.Loaded([](winrt::Windows::Foundation::IInspectable const& sender, Xaml::RoutedEventArgs const&)
        {
            if (auto const element = sender.try_as<Xaml::DependencyObject>()) static_cast<void>(IsScrollingUnder(element));
        });
    }

    void ScrollHoverGate::PointerEntered(winrt::Microsoft::UI::Xaml::FrameworkElement const& element)
    {
        m_inside = true;
        m_owner = element;
        if (element && IsScrollingUnder(element))
        {
            Wait();
            return;
        }
        StopWaiting();
        if (m_apply) m_apply(true);
    }

    void ScrollHoverGate::PointerExited()
    {
        m_inside = false;
        StopWaiting();
        if (m_apply) m_apply(false);
    }

    void ScrollHoverGate::Wait()
    {
        auto const owner = m_owner.get();
        if (!owner) return;
        try
        {
            if (!m_timer)
            {
                auto const queue = owner.DispatcherQueue();
                if (!queue)
                {
                    if (m_apply) m_apply(true);
                    return;
                }
                m_timer = queue.CreateTimer();
                m_timer.Interval(std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(HoldCheck));
                m_timer.IsRepeating(true);
                // `this` is safe: the gate stops and unhooks the timer in its
                // destructor, on the same thread the timer ticks on.
                m_tickToken = m_timer.Tick([this](auto const&, auto const&) { OnTick(); });
            }
            m_timer.Start();
        }
        catch (winrt::hresult_error const&)
        {
            // No timer: hover now rather than never.
            if (m_apply) m_apply(true);
        }
    }

    void ScrollHoverGate::StopWaiting() noexcept
    {
        if (!m_timer) return;
        try
        {
            m_timer.Stop();
        }
        catch (winrt::hresult_error const&)
        {
        }
    }

    void ScrollHoverGate::OnTick()
    {
        auto const owner = m_owner.get();
        if (!m_inside || !owner)
        {
            StopWaiting();
            return;
        }
        if (IsScrollingUnder(owner)) return;
        StopWaiting();
        if (m_apply) m_apply(true);
    }
}
