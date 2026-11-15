#pragma once

#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace HaloDesktop::Controls
{
    // The reference's `skeletonPulse`: opacity from 0.45 to 0.95 and back over
    // 1.4s each way, ease-in-out, for as long as a skeleton is drawn; held at
    // 0.7 when the system has animations turned off.
    //
    // One clock for every skeleton on a page. A single key-frame animation
    // drives a property set, and each element attached here only follows it
    // through an expression, so however many shelves are loading there is one
    // running animation and they all pulse together. UI-thread only; owned by
    // the page whose skeletons it drives. The expressions hold the property set
    // alive on their own, so an element that outlives the owner keeps a still
    // opacity rather than failing.
    //
    // The clock runs only while the owner says skeletons are on screen: a
    // running animation keeps the compositor drawing frames, and Home spends
    // most of its life with no skeleton at all.
    class SkeletonPulse final
    {
    public:
        explicit SkeletonPulse(winrt::Microsoft::UI::Composition::Compositor const& compositor);

        SkeletonPulse(SkeletonPulse const&) = delete;
        SkeletonPulse& operator=(SkeletonPulse const&) = delete;
        SkeletonPulse(SkeletonPulse&&) = delete;
        SkeletonPulse& operator=(SkeletonPulse&&) = delete;

        // Makes the element's opacity follow the pulse. Attaching an element
        // twice is harmless: the second expression replaces the first.
        void Attach(winrt::Microsoft::UI::Xaml::UIElement const& element) const;
        // Starts or stops the one clock. Cheap when nothing changes.
        void SetActive(bool active);

    private:
        winrt::Microsoft::UI::Composition::CompositionPropertySet m_clock{ nullptr };
        winrt::Microsoft::UI::Composition::ExpressionAnimation m_follow{ nullptr };
        // Null when the system has animations turned off.
        winrt::Microsoft::UI::Composition::ScalarKeyFrameAnimation m_pulse{ nullptr };
        bool m_active{};
    };
}
