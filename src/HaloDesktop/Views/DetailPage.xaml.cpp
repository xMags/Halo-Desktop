#include "pch.h"
#include "Views/DetailPage.xaml.h"
#if __has_include("DetailPage.g.cpp")
#include "DetailPage.g.cpp"
#endif

#include "App.xaml.h"
#include "Controls/SkeletonPulse.h"
#include "ViewModels/DetailViewModel.h"
#include "Views/DetailBackdropPresenter.h"
#include "Views/DetailCreditsPanel.xaml.h"
#include "Views/DetailHero.xaml.h"
#include "Views/DetailLayoutPolicy.h"

#include <chrono>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

namespace
{
    // The blurred art is decoded this narrow and stretched over the page: the
    // upscale smooths it into soft colour, which is what the reference's 60px
    // blur leaves of a backdrop, without a blur pass.
    constexpr std::int32_t BlurredArtDecodeWidth = 64;
    // The back button and the gap after it, which the compact bar's content clears.
    constexpr double BackButtonClearance = 38.0 + 16.0;
    // `.compact-bar` slides in over 0.3s, cubic-bezier(0.2, 0.8, 0.2, 1).
    constexpr std::chrono::milliseconds CompactBarSlide{ 300 };

    winrt::Microsoft::UI::Xaml::Visibility const Visible = winrt::Microsoft::UI::Xaml::Visibility::Visible;
}

namespace winrt::HaloDesktop::implementation
{
    DetailPage::DetailPage()
        : m_viewModel(winrt::make<DetailViewModel>(App::Services())),
          m_titleMenu(App::Services())
    {
    }

    DetailPage::~DetailPage() = default;

    void DetailPage::InitializeComponent()
    {
        DetailPageT::InitializeComponent();
        // The sections draw this page's view model; they are handed it before
        // they load, so their bindings start from it.
        Hero().ViewModel(m_viewModel);
        EpisodesPanel().ViewModel(m_viewModel);
        CreditsPanel().ViewModel(m_viewModel);
        SimilarPanel().ViewModel(m_viewModel);
        m_viewModelChanged = m_viewModel.PropertyChanged(winrt::auto_revoke, { get_weak(), &DetailPage::OnViewModelChanged });

        // The bar starts above the page, out of sight; it never takes a
        // composition visual, so its slide is a plain translation transition.
        Microsoft::UI::Xaml::Vector3Transition slide;
        slide.Duration(std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(CompactBarSlide));
        CompactBar().TranslationTransition(slide);
        CompactBar().Translation({ 0.0F, -static_cast<float>(::HaloDesktop::Views::DetailCompactBarHeight), 0.0F });
    }

    winrt::HaloDesktop::DetailViewModel DetailPage::ViewModel() const { return m_viewModel; }

    void DetailPage::OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args)
    {
        m_viewModel.Load(args.Parameter());
    }

    void DetailPage::OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_onScreen = true;
        winrt::get_self<DetailViewModel>(m_viewModel)->Activate();
        if (!m_backdrop)
        {
            m_backdrop = std::make_unique<::HaloDesktop::Views::DetailBackdropPresenter>(ArtHost());
        }
        if (!m_skeletonPulse && !m_pulseUnavailable)
        {
            try
            {
                auto const compositor = Microsoft::UI::Xaml::Hosting::ElementCompositionPreview::GetElementVisual(SkeletonBody()).Compositor();
                m_skeletonPulse = std::make_unique<::HaloDesktop::Controls::SkeletonPulse>(compositor);
                m_skeletonPulse->Attach(SkeletonBody());
            }
            catch (winrt::hresult_error const&)
            {
                // The skeleton simply holds still.
                m_pulseUnavailable = true;
                m_skeletonPulse.reset();
            }
        }
        UpdateSkeletonPulse();
        ApplyLayout();
        ShowArt();
        ShowCompactLogo();
    }

    void DetailPage::OnUnloaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_onScreen = false;
        winrt::get_self<DetailViewModel>(m_viewModel)->Deactivate();
        UpdateSkeletonPulse();
    }

    void DetailPage::OnSizeChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&)
    {
        ApplyLayout();
    }

    void DetailPage::OnPageScrolled(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs const&)
    {
        UpdateCompactBar();
    }

    void DetailPage::OnBackClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.GoBack();
    }

    void DetailPage::OnRetryClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.Retry();
    }

    void DetailPage::OnPlayClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.Play();
    }

    void DetailPage::OnCompactLogoFailed(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::ExceptionRoutedEventArgs const&)
    {
        m_viewModel.LogoFailed();
    }

    void DetailPage::OnContextRequested(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args)
    {
        auto const subject = ::HaloDesktop::Views::TitleMenu::SubjectOf(args);
        if (!subject)
        {
            return;
        }
        if (subject->Item.try_as<winrt::HaloDesktop::MediaSummary>())
        {
            // A "More like this" poster: the same menu as any poster.
            m_titleMenu.ShowForSubject(args);
            return;
        }
        auto const row = subject->Item.try_as<winrt::HaloDesktop::DetailEpisodeViewModel>();
        if (!row)
        {
            return;
        }
        auto const viewModel = winrt::get_self<DetailViewModel>(m_viewModel);
        auto const parameters = viewModel->Parameters();
        if (!parameters)
        {
            return;
        }
        ::HaloDesktop::Views::EpisodeMenuTarget target;
        target.Type = parameters.Type();
        target.MetaId = parameters.MetaId();
        target.ShowName = m_viewModel.Title();
        target.Poster = viewModel->PosterUrl();
        target.Episode = winrt::get_self<DetailEpisodeViewModel>(row)->Episode();
        target.Episodes = viewModel->AllEpisodes();
        target.Play = [owner = m_viewModel, row]() { owner.OpenEpisode(row); };
        m_titleMenu.ShowForEpisode(subject->Anchor, args, std::move(target));
    }

    void DetailPage::OnViewModelChanged(
        winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::Data::PropertyChangedEventArgs const& args)
    {
        auto const name = args.PropertyName();
        if (name == L"Backdrop")
        {
            ShowArt();
        }
        else if (name == L"Logo")
        {
            ShowCompactLogo();
        }
        else if (name == L"SkeletonVisibility")
        {
            UpdateSkeletonPulse();
        }
        else if (name == L"ContentVisibility")
        {
            auto const shown = m_viewModel.ContentVisibility() == Visible;
            // The hero rises in once each time a title's content appears.
            if (shown && !m_contentShown)
            {
                winrt::get_self<DetailHero>(Hero())->PlayEntrance();
            }
            m_contentShown = shown;
            UpdateCompactBar();
        }
    }

    void DetailPage::ApplyLayout()
    {
        auto const width = ActualWidth();
        auto const height = ActualHeight();
        if (!(width > 0.0) || !(height > 0.0))
        {
            return;
        }
        auto const layout = ::HaloDesktop::Views::DetailLayoutFor(width);
        auto const heroHeight = ::HaloDesktop::Views::DetailHeroHeight(height);
        auto const gutter = layout.Gutter;

        winrt::get_self<DetailHero>(Hero())->ApplyLayout(gutter, layout.ShowAside, heroHeight);
        winrt::get_self<DetailCreditsPanel>(CreditsPanel())->ApplyLayout(layout.SplitSideBySide);
        Panels().Padding({ gutter, 0.0, gutter, 56.0 });
        CompactBar().Padding({ gutter + BackButtonClearance, 0.0, gutter, 0.0 });
        BackButton().Margin({ gutter, 13.0, 0.0, 0.0 });
        ErrorLine().Margin({ gutter, 72.0, gutter, 0.0 });
        SkeletonHost().Height(heroHeight);
        SkeletonBody().Margin({ gutter, 0.0, gutter, 40.0 });
        ArtHost().Height(heroHeight);
        if (m_backdrop)
        {
            m_backdrop->Resize(width, heroHeight);
        }
        UpdateCompactBar();
    }

    void DetailPage::UpdateCompactBar()
    {
        auto shown = false;
        if (m_viewModel.ContentVisibility() == Visible)
        {
            try
            {
                auto const button = winrt::get_self<DetailHero>(Hero())->PlayButtonElement();
                auto const origin = button.TransformToVisual(PageScroller()).TransformPoint({ 0.0F, 0.0F });
                shown = ::HaloDesktop::Views::CompactBarShown(origin.Y, button.ActualHeight());
            }
            catch (winrt::hresult_error const&)
            {
                // Mid-layout or leaving the tree: keep the bar where it is.
                return;
            }
        }
        if (shown == m_compactShown)
        {
            return;
        }
        m_compactShown = shown;
        auto const bar = CompactBar();
        bar.Opacity(shown ? 1.0 : 0.0);
        bar.Translation({ 0.0F, shown ? 0.0F : -static_cast<float>(::HaloDesktop::Views::DetailCompactBarHeight), 0.0F });
        bar.IsHitTestVisible(shown);
        CompactPlay().IsTabStop(shown);
    }

    void DetailPage::ShowArt()
    {
        auto const url = m_viewModel.Backdrop();
        if (m_backdrop)
        {
            m_backdrop->SetArt(std::wstring_view{ url });
        }
        if (url == m_blurredUrl)
        {
            return;
        }
        m_blurredUrl = url;
        auto const image = BlurredArt();
        if (url.empty())
        {
            image.Source(nullptr);
            return;
        }
        try
        {
            Microsoft::UI::Xaml::Media::Imaging::BitmapImage bitmap;
            // Set before the URI, or the decode has already been scheduled at full size.
            bitmap.DecodePixelWidth(BlurredArtDecodeWidth);
            bitmap.UriSource(winrt::Windows::Foundation::Uri{ url });
            image.Source(bitmap);
        }
        catch (winrt::hresult_error const&)
        {
            image.Source(nullptr);
        }
    }

    void DetailPage::ShowCompactLogo()
    {
        auto const url = m_viewModel.Logo();
        if (url == m_compactLogoUrl)
        {
            return;
        }
        m_compactLogoUrl = url;
        auto const image = CompactLogo();
        if (url.empty())
        {
            image.Source(nullptr);
            return;
        }
        try
        {
            Microsoft::UI::Xaml::Media::Imaging::BitmapImage bitmap;
            bitmap.UriSource(winrt::Windows::Foundation::Uri{ url });
            image.Source(bitmap);
        }
        catch (winrt::hresult_error const&)
        {
            image.Source(nullptr);
            m_viewModel.LogoFailed();
        }
    }

    void DetailPage::UpdateSkeletonPulse()
    {
        if (!m_skeletonPulse)
        {
            return;
        }
        try
        {
            m_skeletonPulse->SetActive(m_onScreen && m_viewModel.SkeletonVisibility() == Visible);
        }
        catch (winrt::hresult_error const&)
        {
            m_skeletonPulse.reset();
            m_pulseUnavailable = true;
        }
    }
}
