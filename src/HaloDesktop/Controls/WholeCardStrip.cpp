#include "pch.h"
#include "Controls/WholeCardStrip.h"

#include "Controls/ShelfScrollPolicy.h"

#include <cstdint>
#include <stdexcept>
#include <utility>

namespace HaloDesktop::Controls
{
    WholeCardStrip::WholeCardStrip(
        winrt::Microsoft::UI::Xaml::Controls::ScrollViewer strip,
        winrt::Microsoft::UI::Xaml::Controls::ItemsRepeater items,
        std::shared_ptr<Shell::LayoutMetricsService> metrics,
        double Shell::LayoutMetrics::* minimumWidth,
        double gap)
        : m_strip(std::move(strip)),
          m_items(std::move(items)),
          m_metrics(std::move(metrics)),
          m_minimumWidth(minimumWidth),
          m_gap(gap),
          m_cardWidth(0.0)
    {
        if (!m_strip || !m_items || !m_metrics || !m_minimumWidth)
        {
            throw std::invalid_argument{ "A whole-card strip needs its strip, its items, the layout metrics and a card width." };
        }
        m_cardWidth = m_metrics->Current().*m_minimumWidth;
        // `this` is sound: the handler is removed and the revokers revoked in the
        // destructor, before anything they reach.
        m_metricsToken = m_metrics->AddChangedHandler([this]() { Refresh(); });
        m_stripSizeChanged = m_strip.SizeChanged(winrt::auto_revoke, [this](auto const&, auto const&) { Refresh(); });
        m_elementPrepared = m_items.ElementPrepared(
            winrt::auto_revoke,
            [this](auto const&, winrt::Microsoft::UI::Xaml::Controls::ItemsRepeaterElementPreparedEventArgs const& args)
            {
                Size(args.Element());
            });
        Refresh();
    }

    WholeCardStrip::~WholeCardStrip()
    {
        m_elementPrepared.revoke();
        m_stripSizeChanged.revoke();
        if (m_metricsToken != 0)
        {
            m_metrics->RemoveChangedHandler(m_metricsToken);
        }
    }

    double WholeCardStrip::CardWidth() const noexcept
    {
        return m_cardWidth;
    }

    void WholeCardStrip::Refresh() noexcept
    {
        try
        {
            auto const width = WholeCardWidth(m_strip.ActualWidth(), m_metrics->Current().*m_minimumWidth, m_gap);
            if (width == m_cardWidth)
            {
                return;
            }
            m_cardWidth = width;
            // The repeater's children are its realised (and recycled) elements;
            // ones realised later are sized as they are prepared.
            auto const count = winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetChildrenCount(m_items);
            for (std::int32_t index = 0; index < count; ++index)
            {
                if (auto const child = winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetChild(m_items, index)
                        .try_as<winrt::Microsoft::UI::Xaml::UIElement>())
                {
                    Size(child);
                }
            }
        }
        catch (winrt::hresult_error const&)
        {
            // A strip being torn down: nothing left to size.
        }
    }

    void WholeCardStrip::Size(winrt::Microsoft::UI::Xaml::UIElement const& element) const noexcept
    {
        try
        {
            if (auto const framework = element.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>())
            {
                framework.Width(m_cardWidth);
            }
        }
        catch (winrt::hresult_error const&)
        {
        }
    }
}
