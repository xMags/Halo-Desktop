#include "pch.h"
#include "Views/PersonPage.xaml.h"
#include "Controls/SmoothWheelScroll.h"
#if __has_include("PersonPage.g.cpp")
#include "PersonPage.g.cpp"
#endif

#include "App.xaml.h"
#include "Shell/LayoutMetricsService.h"
#include "ViewModels/PersonViewModel.h"

#include <string_view>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

namespace
{
    // The caption under the art (title and meta lines with their margins) and
    // the band above the card it lifts into on hover, as Search's grid.
    constexpr double CaptionHeight = 52.0;
    constexpr double LiftBand = 4.0;
    // The header's text goes under the photo when it would be narrower than this.
    constexpr double PhotoWidth = 220.0;
    constexpr double HeadGap = 48.0;
    constexpr double HeadStackedGap = 24.0;
    constexpr double HeadTextMinWidth = 320.0;
}

namespace winrt::HaloDesktop::implementation
{
    PersonPage::PersonPage()
        : m_viewModel(winrt::make<PersonViewModel>(App::Services())),
          m_titleMenu(App::Services())
    {
    }

    winrt::HaloDesktop::PersonViewModel PersonPage::ViewModel() const
    {
        return m_viewModel;
    }

    void PersonPage::OnContextRequested(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args)
    {
        m_titleMenu.ShowForSubject(args);
    }

    void PersonPage::OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args)
    {
        // The button says where it goes: back to Search when that is the page
        // behind this one, else simply back (a title's cast).
        auto label = winrt::hstring{ L"Back" };
        if (auto const frame = Frame())
        {
            auto const backStack = frame.BackStack();
            if (backStack.Size() > 0
                && backStack.GetAt(backStack.Size() - 1).SourcePageType().Name == winrt::xaml_typename<winrt::HaloDesktop::SearchPage>().Name)
            {
                label = L"Search";
            }
        }
        BackLabel().Text(label);
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(BackButton(), label);
        m_viewModel.Load(args.Parameter());
    }

    void PersonPage::OnLoaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        ::HaloDesktop::Controls::AttachSmoothWheelScroll(PersonScroller());
        if (m_metricsToken == 0)
        {
            if (auto const metrics = App::Services().LayoutMetrics)
            {
                m_metricsToken = metrics->AddChangedHandler([weak = get_weak()]()
                {
                    if (auto const self = weak.get()) self->ApplyLayoutMetrics();
                });
            }
        }
        // Sized before the grid gets its items, so the first layout already
        // uses the poster width.
        ApplyLayoutMetrics();
        if (!m_listBound)
        {
            m_listBound = true;
            CreditsGrid().ItemsSource(winrt::get_self<PersonViewModel>(m_viewModel)->ItemsView());
        }
        m_viewModelChanged = m_viewModel.PropertyChanged(winrt::auto_revoke, [weak = get_weak()](
            winrt::Windows::Foundation::IInspectable const&,
            Microsoft::UI::Xaml::Data::PropertyChangedEventArgs const& changed)
        {
            auto const self = weak.get();
            if (self && std::wstring_view{ changed.PropertyName() } == L"FilterIndex")
            {
                self->ApplyFilterChip();
            }
        });
        ApplyFilterChip();
    }

    void PersonPage::OnUnloaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModelChanged.revoke();
        if (m_metricsToken == 0) return;
        if (auto const metrics = App::Services().LayoutMetrics) metrics->RemoveChangedHandler(m_metricsToken);
        m_metricsToken = 0;
    }

    void PersonPage::OnBackClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.GoBack(); }
    void PersonPage::OnRetryClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.Retry(); }
    void PersonPage::OnAllRolesClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(0); }
    void PersonPage::OnActingClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(1); }
    void PersonPage::OnDirectingClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(2); }
    void PersonPage::OnWritingClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(3); }

    void PersonPage::OnPosterClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (auto const card = sender.try_as<winrt::HaloDesktop::PosterCard>())
        {
            m_viewModel.OpenDetail(card.Tag());
        }
    }

    void PersonPage::OnPersonHeadSizeChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        using Microsoft::UI::Xaml::Controls::Grid;
        auto const head = PersonHead();
        auto const text = PersonText();
        auto const stacked = args.NewSize().Width < PhotoWidth + HeadGap + HeadTextMinWidth;
        // Only what changed is set, as this runs on every size change.
        auto const row = stacked ? 1 : 0;
        auto const column = stacked ? 0 : 1;
        auto const span = stacked ? 2 : 1;
        if (Grid::GetRow(text) != row) Grid::SetRow(text, row);
        if (Grid::GetColumn(text) != column) Grid::SetColumn(text, column);
        if (Grid::GetColumnSpan(text) != span) Grid::SetColumnSpan(text, span);
        auto const columnSpacing = stacked ? 0.0 : HeadGap;
        auto const rowSpacing = stacked ? HeadStackedGap : 0.0;
        if (head.ColumnSpacing() != columnSpacing) head.ColumnSpacing(columnSpacing);
        if (head.RowSpacing() != rowSpacing) head.RowSpacing(rowSpacing);
    }

    // Read more shows only while there is more to read, and stays as Show less once open.
    void PersonPage::OnBiographyTrimmedChanged(
        Microsoft::UI::Xaml::Controls::TextBlock const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::IsTextTrimmedChangedEventArgs const&)
    {
        auto const show = m_biographyOpen || sender.IsTextTrimmed();
        ReadMoreButton().Visibility(show ? Microsoft::UI::Xaml::Visibility::Visible : Microsoft::UI::Xaml::Visibility::Collapsed);
    }

    void PersonPage::OnReadMoreClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_biographyOpen = !m_biographyOpen;
        // Zero lifts the limit; four is the closed biography.
        BiographyText().MaxLines(m_biographyOpen ? 0 : 4);
        ReadMoreLabel().Text(m_biographyOpen ? L"Show less" : L"Read more");
    }

    void PersonPage::ApplyLayoutMetrics()
    {
        auto const metrics = App::Services().LayoutMetrics;
        if (!metrics) return;
        auto const current = metrics->Current();
        PageBody().Padding({ current.Gutter, 28.0, current.Gutter, 40.0 });
        auto const layout = CreditsLayout();
        if (!layout) return;
        layout.MinItemWidth(current.PosterWidth);
        layout.MinItemHeight(current.PosterArtHeight() + CaptionHeight + LiftBand);
    }

    void PersonPage::ApplyFilterChip()
    {
        Microsoft::UI::Xaml::Controls::RadioButton chip{ nullptr };
        switch (m_viewModel.FilterIndex())
        {
        case 1: chip = ActingChip(); break;
        case 2: chip = DirectingChip(); break;
        case 3: chip = WritingChip(); break;
        default: chip = AllRolesChip(); break;
        }
        if (!chip) return;
        auto const checked = chip.IsChecked();
        if (!checked || !checked.Value()) chip.IsChecked(true);
    }
}
