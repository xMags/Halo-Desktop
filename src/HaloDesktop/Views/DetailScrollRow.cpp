#include "pch.h"
#include "Views/DetailScrollRow.h"

#include "Controls/ShelfScrollPolicy.h"

#include <stdexcept>
#include <utility>

namespace
{
    winrt::Microsoft::UI::Xaml::Visibility VisibleWhen(bool condition) noexcept
    {
        return condition ? winrt::Microsoft::UI::Xaml::Visibility::Visible : winrt::Microsoft::UI::Xaml::Visibility::Collapsed;
    }
}

namespace HaloDesktop::Views
{
    DetailScrollRow::DetailScrollRow(DetailScrollRowParts parts)
        : m_parts(std::move(parts))
    {
        if (!m_parts.Strip || !m_parts.Content || !m_parts.Pager || !m_parts.Previous || !m_parts.Next
            || !m_parts.StartFade || !m_parts.EndFade)
        {
            throw std::invalid_argument{ "A title page row needs all of its parts." };
        }
        // `this` is sound: every revoker is a member, revoked before the parts.
        m_viewChanged = m_parts.Strip.ViewChanged(winrt::auto_revoke, [this](auto const&, auto const&) { Refresh(); });
        m_stripSizeChanged = m_parts.Strip.SizeChanged(winrt::auto_revoke, [this](auto const&, auto const&) { Refresh(); });
        m_contentSizeChanged = m_parts.Content.SizeChanged(winrt::auto_revoke, [this](auto const&, auto const&) { Refresh(); });
        m_previousClick = m_parts.Previous.Click(winrt::auto_revoke, [this](auto const&, auto const&) { Page(-1); });
        m_nextClick = m_parts.Next.Click(winrt::auto_revoke, [this](auto const&, auto const&) { Page(1); });
        Refresh();
    }

    void DetailScrollRow::Refresh()
    {
        try
        {
            auto const& strip = m_parts.Strip;
            auto const overflows = ::HaloDesktop::Controls::ShelfOverflows(strip.ViewportWidth(), strip.ExtentWidth());
            auto const edges = ::HaloDesktop::Controls::MeasureShelfEdges(strip.HorizontalOffset(), strip.ViewportWidth(), strip.ExtentWidth());
            m_parts.Pager.Visibility(VisibleWhen(overflows));
            m_parts.Previous.IsEnabled(overflows && !edges.AtStart);
            m_parts.Next.IsEnabled(overflows && !edges.AtEnd);
            m_parts.StartFade.Visibility(VisibleWhen(overflows && !edges.AtStart));
            m_parts.EndFade.Visibility(VisibleWhen(overflows && !edges.AtEnd));
        }
        catch (winrt::hresult_error const&)
        {
            // A strip being torn down: the next measure puts it right.
        }
    }

    void DetailScrollRow::Page(int direction)
    {
        try
        {
            auto const& strip = m_parts.Strip;
            auto const target = ::HaloDesktop::Controls::ScrollRowPageTarget(
                strip.HorizontalOffset(), strip.ViewportWidth(), strip.ExtentWidth(), direction);
            strip.ChangeView(target, nullptr, nullptr, false);
        }
        catch (winrt::hresult_error const&)
        {
        }
    }
}
