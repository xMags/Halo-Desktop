#include "pch.h"
#include "ViewModels/DetailViewModel.h"
#if __has_include("DetailAboutRowViewModel.g.cpp")
#include "DetailAboutRowViewModel.g.cpp"
#endif
#if __has_include("DetailAvailabilityViewModel.g.cpp")
#include "DetailAvailabilityViewModel.g.cpp"
#endif
#if __has_include("DetailCreditViewModel.g.cpp")
#include "DetailCreditViewModel.g.cpp"
#endif
#if __has_include("DetailEpisodeViewModel.g.cpp")
#include "DetailEpisodeViewModel.g.cpp"
#endif
#if __has_include("DetailFactViewModel.g.cpp")
#include "DetailFactViewModel.g.cpp"
#endif
#if __has_include("DetailViewModel.g.cpp")
#include "DetailViewModel.g.cpp"
#endif

#include "Models/Models.h"
#include "Services/HistoryRowsPolicy.h"
#include "Services/LibraryService.h"
#include "Services/NavigationService.h"
#include "Services/ServerFeaturesService.h"
#include "Services/PlayableVideoPolicy.h"
#include "Services/RatingPolicy.h"
#include "Services/SourceService.h"
#include "Services/WatchStateService.h"
#include "ViewModels/ObservableHelper.h"
#include "ViewModels/QualityMedalProjection.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <winrt/Windows.System.h>

namespace
{
    auto const Visible = winrt::Microsoft::UI::Xaml::Visibility::Visible;
    auto const Collapsed = winrt::Microsoft::UI::Xaml::Visibility::Collapsed;

    winrt::Microsoft::UI::Xaml::Visibility VisibleWhen(bool condition) noexcept
    {
        return condition ? Visible : Collapsed;
    }

    // The meter's floor, so the smallest group still shows a sliver.
    constexpr double MinimumMeter = 0.04;

    // Opens a URL this page built from validated parts (an IMDb search or a
    // YouTube id) in the default browser. Never handed an addon's own URL.
    winrt::fire_and_forget OpenInBrowser(std::wstring url)
    {
        try
        {
            winrt::Windows::Foundation::Uri const uri{ url };
            co_await winrt::Windows::System::Launcher::LaunchUriAsync(uri);
        }
        catch (...)
        {
            // The reference swallows a refused launch the same way.
        }
    }

    std::wstring SeasonLabel(std::int32_t season)
    {
        return season == 0 ? std::wstring{ L"Specials" } : L"Season " + std::to_wstring(season);
    }
}

namespace winrt::HaloDesktop::implementation
{
    DetailEpisodeViewModel::DetailEpisodeViewModel(
        winrt::HaloDesktop::Episode episode,
        ::HaloDesktop::Api::Dto::WatchEntry const* row,
        DetailEpisodeState state)
    {
        Apply(std::move(episode), row, std::move(state));
    }

    void DetailEpisodeViewModel::Apply(
        winrt::HaloDesktop::Episode episode,
        ::HaloDesktop::Api::Dto::WatchEntry const* row,
        DetailEpisodeState state)
    {
        m_episode = std::move(episode);
        auto const hasLength = row && row->DurationSec > 0.0;
        m_progress = !hasLength ? 0.0 : row->Watched ? 1.0 : (std::min)(1.0, row->PositionSec / row->DurationSec);
        m_inProgress = m_progress > 0.0 && m_progress < 1.0;
        m_current = state.Current;
        m_aside = hasLength && !row->Watched
            ? winrt::hstring{ ::HaloDesktop::Services::FormatTimeLeft(row->PositionSec, row->DurationSec) }
            : state.Runtime;
        m_aired = winrt::hstring{ ::HaloDesktop::ViewModels::FormatAirDate(std::wstring_view{ m_episode.Aired() }) };
        // The row's own watch row decides the check, as the reference reads it.
        m_watched = row && row->Watched;
    }

    void DetailEpisodeViewModel::Refresh(
        winrt::HaloDesktop::Episode episode,
        ::HaloDesktop::Api::Dto::WatchEntry const* row,
        DetailEpisodeState state)
    {
        Apply(std::move(episode), row, std::move(state));
        for (auto const name : { L"Aside", L"Progress", L"ProgressVisibility", L"AsideVisibility", L"WatchedVisibility",
                                 L"SavedVisibility", L"CurrentVisibility", L"AccentTagVisibility", L"PlainTagVisibility" })
        {
            ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, name);
        }
    }

    Microsoft::UI::Xaml::Visibility DetailEpisodeViewModel::ProgressVisibility() const noexcept { return VisibleWhen(m_progress > 0.0); }
    Microsoft::UI::Xaml::Visibility DetailEpisodeViewModel::BlurbVisibility() const { return VisibleWhen(!m_episode.Blurb().empty()); }
    Microsoft::UI::Xaml::Visibility DetailEpisodeViewModel::AsideVisibility() const noexcept { return VisibleWhen(!m_aside.empty()); }
    Microsoft::UI::Xaml::Visibility DetailEpisodeViewModel::AiredVisibility() const noexcept { return VisibleWhen(!m_aired.empty()); }
    Microsoft::UI::Xaml::Visibility DetailEpisodeViewModel::WatchedVisibility() const noexcept { return VisibleWhen(m_watched); }
    Microsoft::UI::Xaml::Visibility DetailEpisodeViewModel::SavedVisibility() const { return VisibleWhen(m_episode.Downloaded()); }
    Microsoft::UI::Xaml::Visibility DetailEpisodeViewModel::CurrentVisibility() const noexcept { return VisibleWhen(m_current); }
    Microsoft::UI::Xaml::Visibility DetailEpisodeViewModel::AccentTagVisibility() const noexcept { return VisibleWhen(m_current || m_inProgress); }
    Microsoft::UI::Xaml::Visibility DetailEpisodeViewModel::PlainTagVisibility() const noexcept { return VisibleWhen(!(m_current || m_inProgress)); }

    winrt::event_token DetailEpisodeViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }
    void DetailEpisodeViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }

    DetailFactViewModel::DetailFactViewModel(winrt::hstring text, Kind kind, bool first)
        : m_text(std::move(text)), m_kind(kind), m_first(first)
    {
    }
    Microsoft::UI::Xaml::Visibility DetailFactViewModel::SeparatorVisibility() const noexcept { return VisibleWhen(!m_first); }
    Microsoft::UI::Xaml::Visibility DetailFactViewModel::PlainVisibility() const noexcept { return VisibleWhen(m_kind == Kind::Plain); }
    Microsoft::UI::Xaml::Visibility DetailFactViewModel::CertificateVisibility() const noexcept { return VisibleWhen(m_kind == Kind::Certificate); }
    Microsoft::UI::Xaml::Visibility DetailFactViewModel::ImdbVisibility() const noexcept { return VisibleWhen(m_kind == Kind::Imdb); }

    DetailAvailabilityViewModel::DetailAvailabilityViewModel(::HaloDesktop::ViewModels::QualityGroup const& group, std::size_t most)
        : m_plate(group.Medal.Plate),
          m_tag(group.Medal.Tag),
          m_finish(::HaloDesktop::ViewModels::ToQualityMedalFinish(group.Medal.Finish)),
          m_gold(group.Gold),
          m_count(::HaloDesktop::ViewModels::CountOf(group.Count, L"source", L"sources"))
    {
        auto const share = most == 0 ? 0.0 : static_cast<double>(group.Count) / static_cast<double>(most);
        // `max(4, round(count / most * 100))` percent.
        m_meter = (std::max)(MinimumMeter, std::round(share * 100.0) / 100.0);
    }
    Microsoft::UI::Xaml::Visibility DetailAvailabilityViewModel::GoldMeterVisibility() const noexcept { return VisibleWhen(m_gold); }
    Microsoft::UI::Xaml::Visibility DetailAvailabilityViewModel::PlainMeterVisibility() const noexcept { return VisibleWhen(!m_gold); }

    DetailCreditViewModel::DetailCreditViewModel(::HaloDesktop::ViewModels::Credit const& credit)
        : m_name(credit.Name),
          m_role(credit.Role.value_or(std::wstring{})),
          m_photo(credit.Photo.value_or(std::wstring{})),
          m_initials(::HaloDesktop::ViewModels::Initials(credit.Name)),
          m_tooltip(L"Look up " + credit.Name + L" on IMDb"),
          m_searchUrl(::HaloDesktop::ViewModels::PersonSearchUrl(credit.Name)),
          m_tmdbId(credit.TmdbId)
    {
    }
    Microsoft::UI::Xaml::Visibility DetailCreditViewModel::RoleVisibility() const { return VisibleWhen(!m_role.empty()); }

    DetailAboutRowViewModel::DetailAboutRowViewModel(winrt::hstring label, winrt::hstring value, bool last)
        : m_label(std::move(label)), m_value(std::move(value)), m_last(last)
    {
    }
    Microsoft::UI::Xaml::Visibility DetailAboutRowViewModel::DividerVisibility() const noexcept { return VisibleWhen(!m_last); }

    DetailViewModel::DetailViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_metadata(services.Metadata),
          m_library(services.Library),
          m_catalog(services.Catalog),
          m_navigation(services.Navigation),
          m_downloads(services.Downloads),
          m_watch(services.WatchState),
          m_sources(services.Sources),
          m_features(services.ServerFeatures),
          m_facts(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_availabilityRows(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_availabilityFailures(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_seasonLabels(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_pageLabels(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_episodeRows(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_credits(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_aboutRows(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_similar(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>())
    {
        if (!m_metadata || !m_library || !m_catalog || !m_navigation || !m_downloads || !m_watch || !m_sources || !m_features)
        {
            throw std::invalid_argument{ "DetailViewModel requires its dependencies." };
        }
    }

    DetailViewModel::~DetailViewModel() { Deactivate(); }

    void DetailViewModel::Activate()
    {
        m_active = true;
        if (!m_downloadToken)
        {
            m_downloadToken = m_downloads->AddChangedHandler([weak = get_weak()]()
            {
                if (auto const self = weak.get()) self->RebuildRows();
            });
        }
        // A right-click menu, the player or the pane can mark episodes or change
        // the library while the page is up.
        if (!m_userStateToken)
        {
            m_userStateToken = m_catalog->AddUserStateChangedHandler([weak = get_weak()]()
            {
                if (auto const self = weak.get()) self->ApplyUserStateChange();
            });
        }
        RequestAvailability();
        RequestSimilar();
    }

    void DetailViewModel::Deactivate() noexcept
    {
        m_active = false;
        // Nothing resolves for a page that has gone: answers still on their way
        // are ignored, and a page shown again asks again.
        ++m_availabilityVersion;
        if (m_availabilityLoading)
        {
            m_availabilityLoading = false;
            m_availabilityVideo = winrt::hstring{};
        }
        ++m_similarVersion;
        if (m_similar && m_similar.Size() == 0)
        {
            m_similarRequested = false;
        }
        if (m_downloadToken)
        {
            m_downloads->RemoveChangedHandler(m_downloadToken);
            m_downloadToken = 0;
        }
        if (m_userStateToken)
        {
            m_catalog->RemoveUserStateChangedHandler(m_userStateToken);
            m_userStateToken = 0;
        }
    }

    Microsoft::UI::Xaml::Visibility DetailViewModel::ContentVisibility() const noexcept { return VisibleWhen(!m_loading && !m_error && m_meta.has_value()); }
    Microsoft::UI::Xaml::Visibility DetailViewModel::SkeletonVisibility() const noexcept { return VisibleWhen(m_loading); }
    Microsoft::UI::Xaml::Visibility DetailViewModel::ErrorVisibility() const noexcept { return VisibleWhen(!m_loading && m_error); }

    winrt::hstring DetailViewModel::Backdrop() const
    {
        if (!m_meta) return {};
        if (m_meta->Preview.Background && !m_meta->Preview.Background->empty()) return *m_meta->Preview.Background;
        return m_meta->Preview.Poster.value_or(winrt::hstring{});
    }

    winrt::hstring DetailViewModel::Title() const
    {
        if (m_meta) return m_meta->Preview.Name;
        return m_params ? m_params.Title() : winrt::hstring{};
    }

    winrt::hstring DetailViewModel::Logo() const
    {
        return m_meta ? m_meta->Preview.Logo.value_or(winrt::hstring{}) : winrt::hstring{};
    }

    Microsoft::UI::Xaml::Visibility DetailViewModel::LogoVisibility() const { return VisibleWhen(!Logo().empty() && !m_logoFailed); }
    Microsoft::UI::Xaml::Visibility DetailViewModel::TitleVisibility() const { return VisibleWhen(Logo().empty() || m_logoFailed); }

    winrt::hstring DetailViewModel::Synopsis() const
    {
        return m_meta ? m_meta->Preview.Description.value_or(winrt::hstring{}) : winrt::hstring{};
    }

    Microsoft::UI::Xaml::Visibility DetailViewModel::SynopsisVisibility() const { return VisibleWhen(!Synopsis().empty()); }
    Microsoft::UI::Xaml::Visibility DetailViewModel::InLibraryVisibility() const noexcept { return VisibleWhen(m_inLibrary); }
    Microsoft::UI::Xaml::Visibility DetailViewModel::NotInLibraryVisibility() const noexcept { return VisibleWhen(!m_inLibrary); }
    Microsoft::UI::Xaml::Visibility DetailViewModel::TrailerVisibility() const noexcept { return VisibleWhen(m_trailer.has_value()); }
    Microsoft::UI::Xaml::Visibility DetailViewModel::ProgressVisibility() const noexcept { return VisibleWhen(m_resume.has_value()); }

    double DetailViewModel::ProgressFraction() const noexcept
    {
        if (!m_resume || !(m_resume->DurationSec > 0.0)) return 0.0;
        return (std::min)(1.0, m_resume->PositionSec / m_resume->DurationSec);
    }

    winrt::hstring DetailViewModel::TimeLeft() const
    {
        if (!m_resume) return {};
        return winrt::hstring{ ::HaloDesktop::Services::FormatTimeLeft(m_resume->PositionSec, m_resume->DurationSec) };
    }

    Microsoft::UI::Xaml::Visibility DetailViewModel::ProgressSeparatorVisibility() const noexcept
    {
        return VisibleWhen(m_resume.has_value() && BestVisibility() == Visible);
    }

    Microsoft::UI::Xaml::Visibility DetailViewModel::BestVisibility() const noexcept
    {
        return VisibleWhen(m_availability && !m_availability->Groups.empty());
    }

    winrt::hstring DetailViewModel::BestPlate() const
    {
        return BestVisibility() == Visible ? winrt::hstring{ m_availability->Groups.front().Medal.Plate } : winrt::hstring{};
    }

    winrt::hstring DetailViewModel::BestTag() const
    {
        return BestVisibility() == Visible ? winrt::hstring{ m_availability->Groups.front().Medal.Tag } : winrt::hstring{};
    }

    winrt::HaloDesktop::QualityMedalFinish DetailViewModel::BestFinish() const noexcept
    {
        return BestVisibility() == Visible
            ? ::HaloDesktop::ViewModels::ToQualityMedalFinish(m_availability->Groups.front().Medal.Finish)
            : winrt::HaloDesktop::QualityMedalFinish::Plain;
    }

    winrt::hstring DetailViewModel::SourcesLink() const
    {
        return m_availability
            ? winrt::hstring{ ::HaloDesktop::ViewModels::CountOf(m_availability->Total, L"source", L"sources") }
            : winrt::hstring{};
    }

    Microsoft::UI::Xaml::Visibility DetailViewModel::AvailabilityLoadingVisibility() const noexcept { return VisibleWhen(m_availabilityLoading); }

    Microsoft::UI::Xaml::Visibility DetailViewModel::AvailabilityEmptyVisibility() const noexcept
    {
        return VisibleWhen(!m_availabilityLoading && m_availability && m_availability->Total == 0
            && m_availabilityFailures.Size() == 0);
    }

    Microsoft::UI::Xaml::Visibility DetailViewModel::SubtitlesVisibility() const noexcept
    {
        return VisibleWhen(m_availability && m_availability->SubtitleLanguages > 0);
    }

    winrt::hstring DetailViewModel::SubtitlesText() const
    {
        return m_availability
            ? winrt::hstring{ ::HaloDesktop::ViewModels::CountOf(m_availability->SubtitleLanguages, L"language", L"languages") }
            : winrt::hstring{};
    }

    Microsoft::UI::Xaml::Visibility DetailViewModel::EpisodesVisibility() const noexcept { return VisibleWhen(IsSeries()); }
    std::int32_t DetailViewModel::SeasonIndex() const
    {
        auto const season = ActiveSeason();
        if (!season) return -1;
        auto const found = std::find(m_seasons.begin(), m_seasons.end(), *season);
        return found == m_seasons.end() ? -1 : static_cast<std::int32_t>(std::distance(m_seasons.begin(), found));
    }
    Microsoft::UI::Xaml::Visibility DetailViewModel::SeasonPickerVisibility() const noexcept { return VisibleWhen(m_seasons.size() > 1); }
    std::int32_t DetailViewModel::PageIndex() const noexcept { return m_pages.empty() ? -1 : static_cast<std::int32_t>(m_pageIndex); }
    Microsoft::UI::Xaml::Visibility DetailViewModel::PagePickerVisibility() const noexcept { return VisibleWhen(m_pages.size() > 1); }

    Microsoft::UI::Xaml::Visibility DetailViewModel::CastVisibility() const { return VisibleWhen(m_credits.Size() > 0); }
    winrt::hstring DetailViewModel::CastNote() const
    {
        return winrt::hstring{ ::HaloDesktop::ViewModels::CountOf(m_credits.Size(), L"person", L"people") };
    }
    Microsoft::UI::Xaml::Visibility DetailViewModel::AboutVisibility() const { return VisibleWhen(m_aboutRows.Size() > 0); }
    Microsoft::UI::Xaml::Visibility DetailViewModel::SimilarVisibility() const { return VisibleWhen(m_similar.Size() > 0); }

    void DetailViewModel::Load(winrt::Windows::Foundation::IInspectable const& parameter)
    {
        // A cast click opens a person page only on a server that has them.
        m_features->EnsureLoaded();
        if (auto const direct = parameter.try_as<winrt::HaloDesktop::DetailNavParams>())
        {
            m_params = direct;
        }
        else if (auto const media = parameter.try_as<winrt::HaloDesktop::MediaSummary>())
        {
            m_params = winrt::make<implementation::DetailNavParams>(media.Type(), media.Id(), media.Title(), media.Poster());
        }
        if (!m_params || m_params.Type().empty() || m_params.MetaId().empty())
        {
            m_params = nullptr;
            m_error = true;
            m_errorText = L"Could not load this title: not found";
            RaiseAll();
            return;
        }
        static_cast<void>(LoadAsync());
    }

    void DetailViewModel::Retry()
    {
        if (m_params) static_cast<void>(LoadAsync());
    }

    void DetailViewModel::ToggleLibrary() { static_cast<void>(ToggleLibraryAsync()); }

    void DetailViewModel::Play()
    {
        if (!m_params || !m_meta) return;
        if (m_targetEpisode)
        {
            OpenSourcesFor(m_targetEpisode);
            return;
        }
        auto const name = Title();
        m_navigation->ShowSheet(
            ::HaloDesktop::Services::Page::Sources,
            winrt::make<implementation::SourcesNavParams>(
                m_params.Type(), m_params.MetaId(), MovieVideo(), ItemId(), name, name, L"", PosterUrl()));
    }

    void DetailViewModel::OpenEpisode(winrt::Windows::Foundation::IInspectable const& row)
    {
        auto const model = row.try_as<winrt::HaloDesktop::DetailEpisodeViewModel>();
        if (!model) return;
        OpenSourcesFor(winrt::get_self<DetailEpisodeViewModel>(model)->Episode());
    }

    void DetailViewModel::OpenTrailer()
    {
        if (m_trailer) OpenInBrowser(*m_trailer);
    }

    void DetailViewModel::OpenCredit(winrt::Windows::Foundation::IInspectable const& credit)
    {
        auto const model = credit.try_as<winrt::HaloDesktop::DetailCreditViewModel>();
        if (!model) return;
        auto const self = winrt::get_self<DetailCreditViewModel>(model);
        // A server with people search has a page for them: by their TMDB id
        // when the cast entry gave one, else by name (the page finds them, or
        // falls back to the IMDb search below itself).
        if (m_features->People())
        {
            m_navigation->GoTo(
                ::HaloDesktop::Services::Page::Person,
                winrt::make<implementation::PersonNavParams>(self->TmdbId().value_or(0), self->Name(), self->Photo()));
            return;
        }
        auto const& url = self->SearchUrl();
        if (!url.empty()) OpenInBrowser(url);
    }

    void DetailViewModel::OpenSimilar(winrt::Windows::Foundation::IInspectable const& media)
    {
        auto const item = media.try_as<winrt::HaloDesktop::MediaSummary>();
        if (!item || item.Type().empty() || item.Id().empty()) return;
        m_navigation->GoTo(
            ::HaloDesktop::Services::Page::Detail,
            winrt::make<implementation::DetailNavParams>(item.Type(), item.Id(), item.Title(), item.Poster()));
    }

    void DetailViewModel::SelectSeason(std::int32_t index)
    {
        if (m_rebuildingPickers || index < 0 || static_cast<std::size_t>(index) >= m_seasons.size()) return;
        auto const season = m_seasons[static_cast<std::size_t>(index)];
        if (ActiveSeason() == season) return;
        m_pickedSeason = season;
        ApplyPlayState();
        RaiseHero();
        RaiseEpisodes();
    }

    void DetailViewModel::SelectPage(std::int32_t index)
    {
        if (m_rebuildingPickers || index < 0 || static_cast<std::size_t>(index) >= m_pages.size()) return;
        if (static_cast<std::size_t>(index) == m_pageIndex) return;
        m_pageIndex = static_cast<std::size_t>(index);
        RebuildRows();
        Raise(L"PageIndex");
    }

    void DetailViewModel::LogoFailed()
    {
        if (m_logoFailed) return;
        m_logoFailed = true;
        Raise(L"LogoVisibility");
        Raise(L"TitleVisibility");
    }

    // Detail is only ever reached from another shell page, so there is always
    // something behind it; the guard is for a cold navigation in testing.
    void DetailViewModel::GoBack()
    {
        if (m_navigation->CanGoBack()) m_navigation->GoBack();
        else m_navigation->GoTo(::HaloDesktop::Services::Page::Home);
    }

    winrt::event_token DetailViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }
    void DetailViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }

    std::vector<winrt::HaloDesktop::Episode> DetailViewModel::AllEpisodes() const
    {
        std::vector<winrt::HaloDesktop::Episode> episodes;
        if (!OwnsMetadata()) return episodes;
        for (auto const season : m_seasons)
        {
            for (auto const& episode : m_metadata->Episodes(season)) episodes.push_back(episode);
        }
        return episodes;
    }

    winrt::hstring DetailViewModel::PosterUrl() const
    {
        if (m_meta && m_meta->Preview.Poster && !m_meta->Preview.Poster->empty()) return *m_meta->Preview.Poster;
        return m_params ? m_params.Poster() : winrt::hstring{};
    }

    winrt::Windows::Foundation::IAsyncAction DetailViewModel::LoadAsync()
    {
        auto lifetime = get_strong();
        auto const ui = winrt::apartment_context{};
        auto const version = ++m_loadVersion;
        auto const type = m_params.Type();
        auto const metaId = m_params.MetaId();
        m_loading = true;
        m_error = false;
        m_errorText = {};
        RaiseAll();

        std::wstring failure;
        bool failed{};
        try
        {
            co_await m_metadata->LoadAsync(type, metaId);
            co_await m_library->LoadAsync();
        }
        catch (winrt::hresult_error const& error)
        {
            failed = true;
            failure = error.message();
        }
        catch (...)
        {
            failed = true;
        }
        co_await ui;
        if (version != m_loadVersion) co_return;

        m_loading = false;
        auto meta = failed ? std::nullopt : m_metadata->Meta();
        if (!meta)
        {
            m_error = true;
            m_errorText = L"Could not load this title: " + winrt::hstring{ failure.empty() ? std::wstring{ L"not found" } : failure };
            RaiseAll();
            co_return;
        }
        m_meta = std::move(meta);
        m_inLibrary = m_library->Contains(type, metaId);
        m_logoFailed = false;
        ApplyMeta();
        ApplyPlayState();
        RaiseAll();
        // Both wait for the hero to be up: neither may slow it down.
        RequestAvailability();
        m_similarRequested = false;
        RequestSimilar();
    }

    winrt::Windows::Foundation::IAsyncAction DetailViewModel::ToggleLibraryAsync()
    {
        auto lifetime = get_strong();
        if (!m_params) co_return;
        auto const ui = winrt::apartment_context{};
        auto const next = !m_inLibrary;
        auto const poster = PosterUrl();
        try
        {
            auto const applied = co_await m_library->SetMembershipAsync(
                m_params.Type(),
                m_params.MetaId(),
                Title(),
                poster.empty() ? std::nullopt : std::optional<winrt::hstring>{ poster },
                next);
            co_await ui;
            if (!applied) co_return;
            m_inLibrary = next;
            m_catalog->RebuildLibrary();
            Raise(L"InLibraryVisibility");
            Raise(L"NotInLibraryVisibility");
        }
        catch (...)
        {
        }
    }

    void DetailViewModel::ApplyMeta()
    {
        auto const& meta = *m_meta;
        m_trailer = ::HaloDesktop::ViewModels::TrailerUrl(meta);

        std::vector<::HaloDesktop::ViewModels::EpisodeRef> videos;
        videos.reserve(meta.Videos.size());
        for (auto const& video : meta.Videos)
        {
            videos.push_back({ std::wstring{ video.Id }, video.Episode, video.Season });
        }
        m_seasons = ::HaloDesktop::ViewModels::SeasonOrder(videos);
        m_numberedSeasons = ::HaloDesktop::ViewModels::NumberedSeasonCount(m_seasons);
        if (m_pickedSeason && std::find(m_seasons.begin(), m_seasons.end(), *m_pickedSeason) == m_seasons.end())
        {
            m_pickedSeason.reset();
        }
        RebuildSeasonList();

        m_facts.Clear();
        bool first = true;
        auto const addFact = [this, &first](std::wstring text, DetailFactViewModel::Kind kind)
        {
            m_facts.Append(winrt::make<DetailFactViewModel>(winrt::hstring{ text }, kind, first));
            first = false;
        };
        for (auto& fact : ::HaloDesktop::ViewModels::HeroFacts(meta, m_numberedSeasons))
        {
            addFact(std::move(fact), DetailFactViewModel::Kind::Plain);
        }
        if (meta.AgeRating && !meta.AgeRating->empty())
        {
            addFact(std::wstring{ *meta.AgeRating }, DetailFactViewModel::Kind::Certificate);
        }
        auto const rating = meta.Preview.Rating
            ? ::HaloDesktop::Services::RatedValue(std::wstring_view{ *meta.Preview.Rating })
            : std::nullopt;
        if (rating)
        {
            addFact(*rating, DetailFactViewModel::Kind::Imdb);
        }

        m_credits.Clear();
        for (auto const& credit : ::HaloDesktop::ViewModels::CreditsOf(meta))
        {
            m_credits.Append(winrt::make<DetailCreditViewModel>(credit));
        }

        m_aboutRows.Clear();
        auto const rows = ::HaloDesktop::ViewModels::AboutRows(meta);
        for (std::size_t index{}; index < rows.size(); ++index)
        {
            m_aboutRows.Append(winrt::make<DetailAboutRowViewModel>(
                winrt::hstring{ rows[index].first }, winrt::hstring{ rows[index].second }, index + 1 == rows.size()));
        }
    }

    void DetailViewModel::RebuildSeasonList()
    {
        m_rebuildingPickers = true;
        m_seasonLabels.Clear();
        for (auto const season : m_seasons)
        {
            m_seasonLabels.Append(winrt::box_value(winrt::hstring{ SeasonLabel(season) }));
        }
        m_rebuildingPickers = false;
    }

    void DetailViewModel::ApplyPlayState()
    {
        if (!m_meta) return;
        auto const rows = RowsForItem();
        std::vector<::HaloDesktop::ViewModels::EpisodeRef> videos;
        videos.reserve(m_meta->Videos.size());
        for (auto const& video : m_meta->Videos)
        {
            videos.push_back({ std::wstring{ video.Id }, video.Episode, video.Season });
        }

        m_resume = ::HaloDesktop::ViewModels::ResumeTarget(rows, videos);
        m_resumeEpisode = nullptr;
        if (m_resume && IsSeries())
        {
            for (auto const& episode : AllEpisodes())
            {
                if (episode.VideoId() == m_resume->VideoId)
                {
                    m_resumeEpisode = episode;
                    break;
                }
            }
        }

        auto const active = ActiveSeason();
        auto const seasonChanged = active != m_shownSeason;
        m_shownSeason = active;
        RebuildPages(!seasonChanged);
        RebuildRows();

        auto const shown = ActiveEpisodes();
        m_targetEpisode = m_resumeEpisode ? m_resumeEpisode : (IsSeries() && !shown.empty() ? shown.front() : nullptr);
        auto const tag = m_resume
            ? (m_resumeEpisode ? m_resumeEpisode.Tag() : winrt::hstring{})
            : (m_targetEpisode ? m_targetEpisode.Tag() : winrt::hstring{});
        m_playLabel = winrt::hstring{ m_resume ? L"Resume" : L"Play" };
        if (!tag.empty()) m_playLabel = m_playLabel + L" " + tag;

        m_episodesNote = winrt::hstring{ ::HaloDesktop::ViewModels::CountOf(shown.size(), L"episode", L"episodes") };
        if (!shown.empty())
        {
            auto const year = ::HaloDesktop::ViewModels::AirYear(std::wstring_view{ shown.front().Aired() });
            if (!year.empty()) m_episodesNote = m_episodesNote + L" \x00B7 " + winrt::hstring{ year };
        }

        RequestAvailability();
    }

    void DetailViewModel::RebuildPages(bool keepPage)
    {
        auto const episodes = ActiveEpisodes();
        auto const refs = RefsOf(episodes);
        m_pages = ::HaloDesktop::ViewModels::EpisodePages(refs);
        if (!keepPage || m_pageIndex >= m_pages.size())
        {
            // A season opens on the page holding the episode in progress.
            m_pageIndex = ::HaloDesktop::ViewModels::PageContaining(
                m_pages,
                refs,
                m_resume ? std::optional<std::wstring_view>{ std::wstring_view{ m_resume->VideoId } } : std::nullopt);
        }
        m_rebuildingPickers = true;
        m_pageLabels.Clear();
        for (auto const& page : m_pages)
        {
            m_pageLabels.Append(winrt::box_value(winrt::hstring{ page.Label }));
        }
        m_rebuildingPickers = false;
    }

    void DetailViewModel::RebuildRows()
    {
        if (!m_meta) return;
        auto const episodes = ActiveEpisodes();
        std::vector<winrt::HaloDesktop::Episode> shown;
        if (m_pageIndex < m_pages.size())
        {
            auto const& page = m_pages[m_pageIndex];
            for (std::size_t offset{}; offset < page.Count && page.First + offset < episodes.size(); ++offset)
            {
                shown.push_back(episodes[page.First + offset]);
            }
        }

        auto const rows = RowsForItem();
        auto const rowFor = [&rows](winrt::hstring const& videoId) -> ::HaloDesktop::Api::Dto::WatchEntry const*
        {
            auto const found = std::find_if(rows.begin(), rows.end(), [&videoId](auto const& row) { return row.VideoId == videoId; });
            return found == rows.end() ? nullptr : &*found;
        };
        auto const runtime = m_meta->Runtime.value_or(winrt::hstring{});
        auto const stateFor = [this, &runtime](winrt::HaloDesktop::Episode const& episode)
        {
            return DetailEpisodeState{ m_resume && episode.VideoId() == m_resume->VideoId, runtime };
        };

        // The same episodes as before take their new state in place, so a mark
        // or a finished download does not rebuild the list under the viewer.
        auto sameRows = m_episodeRows.Size() == shown.size();
        for (std::uint32_t index{}; sameRows && index < m_episodeRows.Size(); ++index)
        {
            auto const existing = m_episodeRows.GetAt(index).try_as<winrt::HaloDesktop::DetailEpisodeViewModel>();
            sameRows = existing && existing.VideoId() == shown[index].VideoId();
        }
        if (sameRows)
        {
            for (std::uint32_t index{}; index < m_episodeRows.Size(); ++index)
            {
                auto const existing = m_episodeRows.GetAt(index).as<winrt::HaloDesktop::DetailEpisodeViewModel>();
                auto const& episode = shown[index];
                winrt::get_self<DetailEpisodeViewModel>(existing)->Refresh(episode, rowFor(episode.VideoId()), stateFor(episode));
            }
            return;
        }
        m_episodeRows.Clear();
        for (auto const& episode : shown)
        {
            m_episodeRows.Append(winrt::make<DetailEpisodeViewModel>(episode, rowFor(episode.VideoId()), stateFor(episode)));
        }
    }

    void DetailViewModel::ApplyUserStateChange()
    {
        // Mid-load the title on screen is about to be replaced, and the load reads
        // the same services.
        if (!m_params || !m_meta || m_loading) return;
        m_inLibrary = m_library->Contains(m_params.Type(), m_params.MetaId());
        ApplyPlayState();
        RaiseHero();
        RaiseEpisodes();
    }

    void DetailViewModel::RequestAvailability()
    {
        if (!m_active || !m_meta || m_loading || !m_params) return;
        auto const videoId = TargetVideoId();
        if (videoId.empty()) return;
        if (videoId == m_availabilityVideo && (m_availabilityLoading || m_availability)) return;
        ClearAvailability();
        m_availabilityVideo = videoId;
        m_availabilityLoading = true;
        RaiseAvailability();
        static_cast<void>(LoadAvailabilityAsync(m_availabilityVersion, m_params.Type(), videoId));
    }

    winrt::Windows::Foundation::IAsyncAction DetailViewModel::LoadAvailabilityAsync(
        std::uint64_t version,
        winrt::hstring type,
        winrt::hstring videoId)
    {
        auto const weak = get_weak();
        auto const ui = winrt::apartment_context{};
        auto const sources = m_sources;
        std::optional<::HaloDesktop::Services::StreamFetch> fetched;
        try
        {
            fetched = co_await sources->RequestStreamsAsync(type, videoId);
        }
        catch (...)
        {
            // A failed request reads as no offer, as the reference shows it.
        }

        std::optional<::HaloDesktop::ViewModels::Availability> summary;
        std::vector<winrt::hstring> failures;
        if (fetched)
        {
            // Parsing every release name is regex work; it stays off the UI thread.
            co_await winrt::resume_background();
            std::vector<::HaloDesktop::Api::Dto::StreamRecord> streams;
            for (auto const& group : fetched->Payload.Results)
            {
                streams.insert(streams.end(), group.Streams.begin(), group.Streams.end());
            }
            summary = ::HaloDesktop::ViewModels::SummarizeAvailability(streams);
            for (auto const& failed : fetched->Payload.Errors)
            {
                failures.push_back(::HaloDesktop::Services::SanitizeAddonName(failed.Name) + L" did not answer.");
            }
        }
        co_await ui;
        auto const self = weak.get();
        if (!self || version != self->m_availabilityVersion) co_return;
        self->ApplyAvailability(summary, std::move(failures));
    }

    void DetailViewModel::ApplyAvailability(
        std::optional<::HaloDesktop::ViewModels::Availability> const& summary,
        std::vector<winrt::hstring> failures)
    {
        m_availabilityLoading = false;
        m_availability = summary.value_or(::HaloDesktop::ViewModels::Availability{});
        std::size_t most = 1;
        for (auto const& group : m_availability->Groups) most = (std::max)(most, group.Count);
        m_availabilityRows.Clear();
        for (auto const& group : m_availability->Groups)
        {
            m_availabilityRows.Append(winrt::make<DetailAvailabilityViewModel>(group, most));
        }
        m_availabilityFailures.Clear();
        for (auto const& failure : failures)
        {
            m_availabilityFailures.Append(winrt::box_value(failure));
        }
        RaiseAvailability();
    }

    void DetailViewModel::ClearAvailability()
    {
        ++m_availabilityVersion;
        m_availability.reset();
        m_availabilityVideo = winrt::hstring{};
        m_availabilityLoading = false;
        m_availabilityRows.Clear();
        m_availabilityFailures.Clear();
    }

    void DetailViewModel::RequestSimilar()
    {
        if (!m_active || !m_meta || m_loading || m_similarRequested) return;
        m_similarRequested = true;
        static_cast<void>(LoadSimilarAsync(++m_similarVersion));
    }

    winrt::Windows::Foundation::IAsyncAction DetailViewModel::LoadSimilarAsync(std::uint64_t version)
    {
        auto const weak = get_weak();
        auto const ui = winrt::apartment_context{};
        auto const catalog = m_catalog;
        auto const type = m_params.Type();
        auto const metaId = m_params.MetaId();
        auto genres = m_meta->Preview.Genres;
        std::optional<::HaloDesktop::Services::SimilarShelf> shelf;
        try
        {
            shelf = co_await catalog->FetchSimilarAsync(type, metaId, std::move(genres));
        }
        catch (...)
        {
            // No catalog, a failed request or an empty answer leave no trace.
        }
        co_await ui;
        auto const self = weak.get();
        if (!self || version != self->m_similarVersion) co_return;
        self->m_similar.Clear();
        self->m_similarNote = shelf ? shelf->Genre : winrt::hstring{};
        if (shelf)
        {
            for (auto const& item : shelf->Items) self->m_similar.Append(item);
        }
        self->Raise(L"SimilarNote");
        self->Raise(L"SimilarVisibility");
    }

    void DetailViewModel::OpenSourcesFor(winrt::HaloDesktop::Episode const& episode)
    {
        if (!m_params || !episode) return;
        // An untitled episode goes to the sheet and the player under the show's name.
        auto const episodeTitle = episode.Title().empty() ? Title() : episode.Title();
        m_navigation->ShowSheet(
            ::HaloDesktop::Services::Page::Sources,
            winrt::make<implementation::SourcesNavParams>(
                m_params.Type(), m_params.MetaId(), episode.VideoId(), ItemId(), episodeTitle, Title(), episode.Tag(), PosterUrl()));
    }

    bool DetailViewModel::OwnsMetadata() const
    {
        if (!m_meta) return false;
        auto const detail = m_metadata->Detail();
        return detail && detail.Id() == m_meta->Preview.Id;
    }

    bool DetailViewModel::IsSeries() const noexcept
    {
        return m_meta && !m_meta->Videos.empty();
    }

    winrt::hstring DetailViewModel::ItemId() const
    {
        return m_params ? m_params.Type() + L":" + m_params.MetaId() : winrt::hstring{};
    }

    // A film's sources, progress and download go by the video its addon names.
    winrt::hstring DetailViewModel::MovieVideo() const
    {
        if (!m_meta) return m_params ? m_params.MetaId() : winrt::hstring{};
        return winrt::hstring{ ::HaloDesktop::Services::MovieVideoId(
            std::wstring_view{ m_meta->Preview.Id },
            std::wstring_view{ m_meta->Preview.DefaultVideoId.value_or(winrt::hstring{}) }) };
    }

    winrt::hstring DetailViewModel::TargetVideoId() const
    {
        return m_targetEpisode ? m_targetEpisode.VideoId() : MovieVideo();
    }

    std::vector<::HaloDesktop::Api::Dto::WatchEntry> DetailViewModel::RowsForItem() const
    {
        std::vector<::HaloDesktop::Api::Dto::WatchEntry> rows;
        auto const itemId = ItemId();
        if (itemId.empty()) return rows;
        for (auto& row : m_watch->Rows())
        {
            if (row.ItemId == itemId) rows.push_back(std::move(row));
        }
        return rows;
    }

    std::optional<std::int32_t> DetailViewModel::ActiveSeason() const
    {
        if (m_pickedSeason) return m_pickedSeason;
        if (m_meta)
        {
            std::vector<::HaloDesktop::ViewModels::EpisodeRef> videos;
            videos.reserve(m_meta->Videos.size());
            for (auto const& video : m_meta->Videos)
            {
                videos.push_back({ std::wstring{ video.Id }, video.Episode, video.Season });
            }
            if (auto const watched = ::HaloDesktop::ViewModels::LastWatchedSeason(RowsForItem(), videos)) return watched;
        }
        if (!m_seasons.empty()) return m_seasons.front();
        return std::nullopt;
    }

    std::vector<winrt::HaloDesktop::Episode> DetailViewModel::ActiveEpisodes() const
    {
        std::vector<winrt::HaloDesktop::Episode> episodes;
        auto const season = ActiveSeason();
        if (!season || !OwnsMetadata()) return episodes;
        for (auto const& episode : m_metadata->Episodes(*season)) episodes.push_back(episode);
        return episodes;
    }

    std::vector<::HaloDesktop::ViewModels::EpisodeRef> DetailViewModel::RefsOf(std::vector<winrt::HaloDesktop::Episode> const& episodes) const
    {
        std::vector<::HaloDesktop::ViewModels::EpisodeRef> refs;
        refs.reserve(episodes.size());
        for (auto const& episode : episodes)
        {
            ::HaloDesktop::ViewModels::EpisodeRef ref{ std::wstring{ episode.VideoId() }, std::nullopt, episode.Season() };
            // The model counts a missing number as 0; the meta says whether one was sent.
            if (m_meta)
            {
                auto const video = std::find_if(m_meta->Videos.begin(), m_meta->Videos.end(), [&episode](auto const& item)
                {
                    return item.Id == episode.VideoId();
                });
                if (video != m_meta->Videos.end()) ref.Number = video->Episode;
            }
            refs.push_back(std::move(ref));
        }
        return refs;
    }

    void DetailViewModel::RaiseHero()
    {
        for (auto const name : { L"Title", L"Logo", L"LogoVisibility", L"TitleVisibility", L"Facts", L"Synopsis",
                                 L"SynopsisVisibility", L"PlayLabel", L"InLibraryVisibility", L"NotInLibraryVisibility",
                                 L"TrailerVisibility", L"ProgressVisibility", L"ProgressFraction", L"TimeLeft",
                                 L"ProgressSeparatorVisibility", L"Backdrop" })
        {
            Raise(name);
        }
    }

    void DetailViewModel::RaiseAvailability()
    {
        for (auto const name : { L"Availability", L"AvailabilityFailures", L"AvailabilityLoadingVisibility",
                                 L"AvailabilityEmptyVisibility", L"SubtitlesVisibility", L"SubtitlesText", L"BestVisibility",
                                 L"BestPlate", L"BestTag", L"BestFinish", L"SourcesLink", L"ProgressSeparatorVisibility" })
        {
            Raise(name);
        }
    }

    void DetailViewModel::RaiseEpisodes()
    {
        for (auto const name : { L"EpisodesVisibility", L"EpisodesNote", L"Seasons", L"SeasonIndex", L"SeasonPickerVisibility",
                                 L"Pages", L"PageIndex", L"PagePickerVisibility", L"Episodes" })
        {
            Raise(name);
        }
    }

    void DetailViewModel::RaiseAll()
    {
        for (auto const name : { L"ContentVisibility", L"SkeletonVisibility", L"ErrorVisibility", L"ErrorText",
                                 L"CastVisibility", L"CastNote", L"Credits", L"AboutVisibility", L"AboutRows",
                                 L"SimilarVisibility", L"SimilarNote", L"Similar" })
        {
            Raise(name);
        }
        RaiseHero();
        RaiseAvailability();
        RaiseEpisodes();
    }

    void DetailViewModel::Raise(wchar_t const* name)
    {
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, name);
    }
}
