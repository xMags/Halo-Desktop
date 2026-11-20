#include "pch.h"
#include "Views/DetailCreditsPanel.xaml.h"
#if __has_include("DetailCreditsPanel.g.cpp")
#include "DetailCreditsPanel.g.cpp"
#endif

#include "Controls/SidewaysWheelGuard.h"

#include <limits>

namespace
{
    constexpr double PanelGap = 20.0;
    constexpr double AboutWidth = 360.0;
}

namespace winrt::HaloDesktop::implementation
{
    void DetailCreditsPanel::InitializeComponent()
    {
        DetailCreditsPanelT::InitializeComponent();
        m_castRow = std::make_unique<::HaloDesktop::Views::DetailScrollRow>(::HaloDesktop::Views::DetailScrollRowParts{
            CastStrip(), CastItems(), CastPager(), CastPrevious(), CastNext() });
    }

    void DetailCreditsPanel::ViewModel(winrt::HaloDesktop::DetailViewModel const& value)
    {
        m_viewModel = value;
        m_viewModelChanged.revoke();
        if (m_viewModel)
        {
            m_viewModelChanged = m_viewModel.PropertyChanged(winrt::auto_revoke, { get_weak(), &DetailCreditsPanel::OnViewModelChanged });
        }
        Arrange();
    }

    void DetailCreditsPanel::ApplyLayout(bool sideBySide)
    {
        if (sideBySide == m_sideBySide) return;
        m_sideBySide = sideBySide;
        Arrange();
    }

    void DetailCreditsPanel::OnCreditClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (!m_viewModel) return;
        m_viewModel.OpenCredit(sender.as<winrt::HaloDesktop::DetailCreditCard>().Credit());
    }

    void DetailCreditsPanel::OnCastSizeChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&)
    {
        MatchAboutToCast();
    }

    void DetailCreditsPanel::OnStripPointerWheelChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        ::HaloDesktop::Controls::GuardSidewaysWheel(args);
    }

    void DetailCreditsPanel::OnViewModelChanged(
        winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::Data::PropertyChangedEventArgs const& args)
    {
        auto const name = args.PropertyName();
        if (name == L"CastVisibility" || name == L"AboutVisibility")
        {
            Arrange();
        }
        else if (name == L"Credits" && m_castRow)
        {
            m_castRow->Refresh();
        }
    }

    // `.detail-split`: both side by side when wide, one column otherwise, and a
    // lone panel taking the whole row. Nothing at all collapses the split.
    void DetailCreditsPanel::Arrange()
    {
        auto const visible = Microsoft::UI::Xaml::Visibility::Visible;
        auto const hasCast = m_viewModel && m_viewModel.CastVisibility() == visible;
        auto const hasAbout = m_viewModel && m_viewModel.AboutVisibility() == visible;
        Visibility(hasCast || hasAbout ? visible : Microsoft::UI::Xaml::Visibility::Collapsed);

        auto const columns = m_sideBySide && hasCast && hasAbout;
        auto const grid = SplitGrid();
        auto const cast = CastPanel();
        auto const about = AboutPanel();
        AboutColumn().Width(Microsoft::UI::Xaml::GridLengthHelper::FromPixels(columns ? AboutWidth : 0.0));
        grid.ColumnSpacing(columns ? PanelGap : 0.0);
        grid.RowSpacing(!columns && hasCast && hasAbout ? PanelGap : 0.0);
        Microsoft::UI::Xaml::Controls::Grid::SetColumn(about, columns ? 1 : 0);
        Microsoft::UI::Xaml::Controls::Grid::SetRow(about, columns || !hasCast ? 0 : 1);
        m_matchHeights = columns;
        MatchAboutToCast();
    }

    // Beside the cast, About takes the cast panel's height; a long list scrolls
    // inside it. Stacked, it takes its own height.
    void DetailCreditsPanel::MatchAboutToCast()
    {
        auto const about = AboutPanel();
        if (!m_matchHeights)
        {
            about.Height(std::numeric_limits<double>::quiet_NaN());
            return;
        }
        auto const height = CastPanel().ActualHeight();
        about.Height(height > 0.0 ? height : std::numeric_limits<double>::quiet_NaN());
    }
}
