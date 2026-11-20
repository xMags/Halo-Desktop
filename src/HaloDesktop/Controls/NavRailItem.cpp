#include "pch.h"
#include "Controls/NavRailItem.h"
#if __has_include("NavRailItem.g.cpp")
#include "NavRailItem.g.cpp"
#endif

#include <chrono>
#include <initializer_list>
#include <utility>
#include <vector>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

namespace
{
    namespace Animation = winrt::Microsoft::UI::Xaml::Media::Animation;
    using Milliseconds = std::chrono::milliseconds;

    // A point on a motion's curve: where the value is at a moment.
    struct Beat
    {
        Milliseconds At;
        double Value;
    };

    bool AnimationsEnabled()
    {
        try
        {
            return winrt::Windows::UI::ViewManagement::UISettings{}.AnimationsEnabled();
        }
        catch (winrt::hresult_error const&)
        {
            return false;
        }
    }

    // One property of the figure through its beats, each reached on an ease-out
    // curve, starting from rest (`rest` at 0ms).
    Animation::DoubleAnimationUsingKeyFrames Track(double rest, std::initializer_list<Beat> beats)
    {
        Animation::DoubleAnimationUsingKeyFrames track;
        Animation::DiscreteDoubleKeyFrame start;
        start.KeyTime(Animation::KeyTimeHelper::FromTimeSpan({}));
        start.Value(rest);
        track.KeyFrames().Append(start);
        for (auto const& beat : beats)
        {
            Animation::SplineDoubleKeyFrame frame;
            frame.KeyTime(Animation::KeyTimeHelper::FromTimeSpan(
                std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(beat.At)));
            frame.Value(beat.Value);
            Animation::KeySpline spline;
            spline.ControlPoint1({ 0.25F, 0.8F });
            spline.ControlPoint2({ 0.4F, 1.0F });
            frame.KeySpline(spline);
            track.KeyFrames().Append(frame);
        }
        // Every motion ends where it began, so letting go of the last value
        // shows no jump, and a later state change finds the figure at rest.
        track.FillBehavior(Animation::FillBehavior::Stop);
        return track;
    }

    // A vector, not an initializer_list: a returned list would point at an array
    // that dies with the return statement.
    using Tracks = std::vector<std::pair<wchar_t const*, Animation::DoubleAnimationUsingKeyFrames>>;

    // Each motion lasts 340 to 420ms and returns to rest:
    // a small anticipation, an overshoot, a settle.
    Tracks MotionTracks(winrt::HaloDesktop::NavRailMotion motion)
    {
        using winrt::HaloDesktop::NavRailMotion;
        using namespace std::chrono_literals;
        switch (motion)
        {
        case NavRailMotion::Hop:
            return {
                { L"TranslateY", Track(0.0, { { 110ms, -5.0 }, { 220ms, 0.0 }, { 300ms, 0.6 }, { 380ms, 0.0 } }) },
                { L"ScaleX", Track(1.0, { { 110ms, 0.94 }, { 220ms, 1.0 }, { 270ms, 1.1 }, { 380ms, 1.0 } }) },
                { L"ScaleY", Track(1.0, { { 110ms, 1.08 }, { 220ms, 1.0 }, { 270ms, 0.88 }, { 380ms, 1.0 } }) },
            };
        case NavRailMotion::Tilt:
            return {
                { L"Rotation", Track(0.0, { { 120ms, -18.0 }, { 240ms, 8.0 }, { 360ms, 0.0 } }) },
                { L"ScaleX", Track(1.0, { { 120ms, 1.08 }, { 360ms, 1.0 } }) },
                { L"ScaleY", Track(1.0, { { 120ms, 1.08 }, { 360ms, 1.0 } }) },
            };
        case NavRailMotion::Squash:
            return {
                { L"ScaleX", Track(1.0, { { 100ms, 0.8 }, { 220ms, 1.12 }, { 340ms, 1.0 } }) },
                { L"ScaleY", Track(1.0, { { 100ms, 1.1 }, { 220ms, 0.94 }, { 340ms, 1.0 } }) },
            };
        case NavRailMotion::Rewind:
            return {
                { L"Rotation", Track(0.0, { { 160ms, -40.0 }, { 290ms, 6.0 }, { 400ms, 0.0 } }) },
            };
        case NavRailMotion::Drop:
            return {
                { L"TranslateY", Track(0.0, { { 90ms, -4.0 }, { 200ms, 3.0 }, { 300ms, -1.0 }, { 380ms, 0.0 } }) },
            };
        case NavRailMotion::Turn:
            // One whole turn, overshooting a little: the gear rests where it began.
            return {
                { L"Rotation", Track(0.0, { { 300ms, 372.0 }, { 420ms, 360.0 } }) },
            };
        case NavRailMotion::None:
            break;
        }
        return {};
    }
}

namespace winrt::HaloDesktop::implementation
{
    // Lighting plays the motion; only a lit item that is on screen does, so the
    // shell lighting its first page before the rail has loaded stays still.
    NavRailItem::NavRailItem()
    {
        Checked([this](winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
        {
            // The item raises its own event: `this` outlives the subscription.
            if (IsLoaded()) PlayMotion();
        });
    }

    // Registered on first use, which XAML's type information makes before it
    // sets any of them, rather than during static initialisation, which runs
    // before the XAML runtime is up in a desktop app. The registrations are
    // immutable identities XAML itself holds for the process's lifetime.
    Microsoft::UI::Xaml::DependencyProperty NavRailItem::LabelProperty()
    {
        static Microsoft::UI::Xaml::DependencyProperty const property = Microsoft::UI::Xaml::DependencyProperty::Register(
            L"Label",
            winrt::xaml_typename<winrt::hstring>(),
            winrt::xaml_typename<winrt::HaloDesktop::NavRailItem>(),
            Microsoft::UI::Xaml::PropertyMetadata{ winrt::box_value(winrt::hstring{}) });
        return property;
    }

    Microsoft::UI::Xaml::DependencyProperty NavRailItem::GlyphProperty()
    {
        static Microsoft::UI::Xaml::DependencyProperty const property = Microsoft::UI::Xaml::DependencyProperty::Register(
            L"Glyph",
            winrt::xaml_typename<Microsoft::UI::Xaml::Media::Geometry>(),
            winrt::xaml_typename<winrt::HaloDesktop::NavRailItem>(),
            Microsoft::UI::Xaml::PropertyMetadata{ nullptr });
        return property;
    }

    Microsoft::UI::Xaml::DependencyProperty NavRailItem::SelectedGlyphProperty()
    {
        static Microsoft::UI::Xaml::DependencyProperty const property = Microsoft::UI::Xaml::DependencyProperty::Register(
            L"SelectedGlyph",
            winrt::xaml_typename<Microsoft::UI::Xaml::Media::Geometry>(),
            winrt::xaml_typename<winrt::HaloDesktop::NavRailItem>(),
            Microsoft::UI::Xaml::PropertyMetadata{ nullptr });
        return property;
    }

    winrt::hstring NavRailItem::Label() const
    {
        return winrt::unbox_value_or<winrt::hstring>(GetValue(LabelProperty()), winrt::hstring{});
    }

    void NavRailItem::Label(winrt::hstring const& value)
    {
        SetValue(LabelProperty(), winrt::box_value(value));
    }

    Microsoft::UI::Xaml::Media::Geometry NavRailItem::Glyph() const
    {
        return GetValue(GlyphProperty()).try_as<Microsoft::UI::Xaml::Media::Geometry>();
    }

    void NavRailItem::Glyph(Microsoft::UI::Xaml::Media::Geometry const& value)
    {
        SetValue(GlyphProperty(), value);
    }

    Microsoft::UI::Xaml::Media::Geometry NavRailItem::SelectedGlyph() const
    {
        return GetValue(SelectedGlyphProperty()).try_as<Microsoft::UI::Xaml::Media::Geometry>();
    }

    void NavRailItem::SelectedGlyph(Microsoft::UI::Xaml::Media::Geometry const& value)
    {
        SetValue(SelectedGlyphProperty(), value);
    }

    winrt::HaloDesktop::NavRailMotion NavRailItem::Motion() const noexcept
    {
        return m_motion;
    }

    void NavRailItem::Motion(winrt::HaloDesktop::NavRailMotion value) noexcept
    {
        m_motion = value;
    }

    void NavRailItem::OnApplyTemplate()
    {
        NavRailItemT<NavRailItem>::OnApplyTemplate();
        if (m_running)
        {
            m_running.Stop();
            m_running = nullptr;
        }
        m_figure = GetTemplateChild(L"GlyphMotion").try_as<Microsoft::UI::Xaml::Media::CompositeTransform>();
    }

    void NavRailItem::PlayMotion()
    {
        if (!m_figure || m_motion == winrt::HaloDesktop::NavRailMotion::None || !AnimationsEnabled())
        {
            return;
        }
        if (m_running)
        {
            m_running.Stop();
            m_running = nullptr;
        }
        try
        {
            Animation::Storyboard motion;
            for (auto const& [property, track] : MotionTracks(m_motion))
            {
                Animation::Storyboard::SetTarget(track, m_figure);
                Animation::Storyboard::SetTargetProperty(track, property);
                motion.Children().Append(track);
            }
            motion.Begin();
            m_running = motion;
        }
        catch (winrt::hresult_error const&)
        {
            // A refused animation leaves the glyph still, which is its rest.
        }
    }
}
