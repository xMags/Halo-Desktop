#include "pch.h"
#include "Views/DownloadReadyCard.xaml.h"
#include "Controls/PressFeedback.h"
#if __has_include("DownloadReadyCard.g.cpp")
#include "DownloadReadyCard.g.cpp"
#endif

#include <cmath>
#include <string>
#include <string_view>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Windows.Foundation.Numerics.h>

namespace
{
    // `aspect-ratio: 16 / 9`.
    constexpr double ArtAspect = 9.0 / 16.0;
    // `.dl-card:hover .dl-card-art img { transform: scale(1.04) }`.
    constexpr float HoverScale = 1.04F;
}

namespace winrt::HaloDesktop::implementation
{
    DownloadReadyCard::DownloadReadyCard()
    {
        InitializeComponent();
        // A press sinks the whole card, art and caption, and springs it back.
        ::HaloDesktop::Controls::AttachPressFeedback(*this, CardButton());
        // A double click on the card plays. The card's button handles the pointer
        // itself, so the gesture is listened for even when already marked
        // handled; the play button is a sibling, so its own clicks stay its own.
        CardButton().AddHandler(
            Microsoft::UI::Xaml::UIElement::DoubleTappedEvent(),
            winrt::box_value(Microsoft::UI::Xaml::Input::DoubleTappedEventHandler(
                [weak = get_weak()](winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& args)
                {
                    if (auto const self = weak.get())
                    {
                        args.Handled(true);
                        self->m_playEvent(*self, Microsoft::UI::Xaml::RoutedEventArgs{});
                    }
                })),
            true);
    }

    void DownloadReadyCard::Row(winrt::HaloDesktop::DownloadRowViewModel const& value)
    {
        if (m_row == value) return;
        if (m_row && m_rowChangedToken.value != 0)
        {
            m_row.PropertyChanged(m_rowChangedToken);
            m_rowChangedToken = {};
        }
        m_row = value;
        if (m_row)
        {
            m_rowChangedToken = m_row.PropertyChanged([weak = get_weak()](
                winrt::Windows::Foundation::IInspectable const&,
                Microsoft::UI::Xaml::Data::PropertyChangedEventArgs const& args)
            {
                auto const self = weak.get();
                if (!self) return;
                auto const name = args.PropertyName();
                if (name == L"AccessibleName") self->UpdatePlayName();
            });
        }
        // The card is handed its row by the grid's template, which can be after
        // its own bindings started, and again whenever the grid recycles it.
        if (Bindings) Bindings->Update();
        UpdatePlayName();
        UpdatePlayButton();
    }

    void DownloadReadyCard::OnCardClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        m_selectedEvent(*this, args);
    }

    void DownloadReadyCard::OnPlayClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        m_playEvent(*this, args);
    }

    void DownloadReadyCard::OnPlayFocusChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (auto const play = PlayButton())
        {
            m_playFocused = play.FocusState() != Microsoft::UI::Xaml::FocusState::Unfocused;
        }
        UpdatePlayButton();
    }

    void DownloadReadyCard::OnHoverStarted(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
    {
        SetHovered(true);
    }

    void DownloadReadyCard::OnHoverEnded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
    {
        SetHovered(false);
    }

    // The art keeps 16:9 at whatever width the stretching column gives the
    // card, and scales about its own centre.
    void DownloadReadyCard::OnSizeChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        try
        {
            auto const width = args.NewSize().Width;
            if (!std::isfinite(width) || width <= 0.0) return;
            auto const height = std::round(width * ArtAspect);
            auto const length = Microsoft::UI::Xaml::GridLengthHelper::FromPixels(height);
            if (auto const row = ButtonArtRow(); row && row.Height().Value != height) row.Height(length);
            if (auto const overlay = ArtOverlay(); overlay && overlay.Height() != height) overlay.Height(height);
            if (auto const art = ArtScale())
            {
                art.CenterPoint({ static_cast<float>(width / 2.0), static_cast<float>(height / 2.0), 0.0F });
            }
        }
        catch (winrt::hresult_error const&)
        {
        }
    }

    // Decoration only: a refusal anywhere here leaves the card as it was rather
    // than letting an exception leave a pointer handler.
    void DownloadReadyCard::SetHovered(bool hovered)
    {
        m_hovered = hovered;
        try
        {
            if (auto const frame = HoverFrame()) frame.Opacity(hovered ? 1.0 : 0.0);
            if (auto const art = ArtScale())
            {
                auto const scale = hovered ? HoverScale : 1.0F;
                art.Scale({ scale, scale, 1.0F });
            }
        }
        catch (winrt::hresult_error const&)
        {
        }
        UpdatePlayButton();
    }

    void DownloadReadyCard::UpdatePlayButton()
    {
        try
        {
            auto const shown = m_hovered || m_playFocused;
            if (auto const play = PlayButton()) play.Opacity(shown ? 1.0 : 0.0);
        }
        catch (winrt::hresult_error const&)
        {
        }
    }

    void DownloadReadyCard::UpdatePlayName()
    {
        if (!m_row) return;
        if (auto const play = PlayButton())
        {
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(
                play,
                winrt::hstring{ L"Play " + std::wstring{ m_row.AccessibleName() } + L" offline" });
        }
    }
}
