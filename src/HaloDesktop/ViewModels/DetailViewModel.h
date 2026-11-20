#pragma once

#include "DetailAboutRowViewModel.g.h"
#include "DetailAvailabilityViewModel.g.h"
#include "DetailCreditViewModel.g.h"
#include "DetailEpisodeViewModel.g.h"
#include "DetailFactViewModel.g.h"
#include "DetailViewModel.g.h"

#include "Api/Dto.h"
#include "Services/AppServices.h"
#include "Services/ServiceInterfaces.h"
#include "ViewModels/DetailFactsPolicy.h"
#include "ViewModels/EpisodePagesPolicy.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <winrt/Microsoft.UI.Xaml.Data.h>

namespace HaloDesktop::Services
{
    class LibraryService;
    class NavigationService;
    class WatchStateService;
}

namespace winrt::HaloDesktop::implementation
{
    // What an episode row shows beyond the episode itself, decided by the page.
    struct DetailEpisodeState final
    {
        bool Current{};
        // The addon's runtime for the title, shown when the episode is not under way.
        winrt::hstring Runtime;
    };

    struct DetailEpisodeViewModel : DetailEpisodeViewModelT<DetailEpisodeViewModel>
    {
        DetailEpisodeViewModel(winrt::HaloDesktop::Episode episode, ::HaloDesktop::Api::Dto::WatchEntry const* row, DetailEpisodeState state);

        [[nodiscard]] winrt::hstring Tag() const { return m_episode.Tag(); }
        // An untitled episode is shown by its id, as the reference's episode rows show it.
        [[nodiscard]] winrt::hstring Title() const { return m_episode.Title().empty() ? m_episode.VideoId() : m_episode.Title(); }
        [[nodiscard]] winrt::hstring Blurb() const { return m_episode.Blurb(); }
        [[nodiscard]] winrt::hstring Aside() const { return m_aside; }
        [[nodiscard]] winrt::hstring Aired() const { return m_aired; }
        [[nodiscard]] winrt::hstring VideoId() const { return m_episode.VideoId(); }
        [[nodiscard]] winrt::hstring Thumbnail() const { return m_episode.Thumbnail(); }
        [[nodiscard]] double Progress() const noexcept { return m_progress; }
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ProgressVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility BlurbVisibility() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility AsideVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility AiredVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility WatchedVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SavedVisibility() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility CurrentVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility AccentTagVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PlainTagVisibility() const noexcept;

        // Takes the episode's newer watch and download state in place, so the
        // row (and the scroll position around it) stays put.
        void Refresh(winrt::HaloDesktop::Episode episode, ::HaloDesktop::Api::Dto::WatchEntry const* row, DetailEpisodeState state);
        [[nodiscard]] winrt::HaloDesktop::Episode Episode() const { return m_episode; }

        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        void Apply(winrt::HaloDesktop::Episode episode, ::HaloDesktop::Api::Dto::WatchEntry const* row, DetailEpisodeState state);

        winrt::HaloDesktop::Episode m_episode{ nullptr };
        winrt::hstring m_aside;
        winrt::hstring m_aired;
        double m_progress{};
        bool m_inProgress{};
        bool m_current{};
        bool m_watched{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };

    struct DetailFactViewModel : DetailFactViewModelT<DetailFactViewModel>
    {
        enum class Kind
        {
            Plain,
            Certificate,
            Imdb,
        };
        DetailFactViewModel(winrt::hstring text, Kind kind, bool first);
        [[nodiscard]] winrt::hstring Text() const { return m_text; }
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SeparatorVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PlainVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility CertificateVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ImdbVisibility() const noexcept;

    private:
        winrt::hstring m_text;
        Kind m_kind{ Kind::Plain };
        bool m_first{};
    };

    struct DetailAvailabilityViewModel : DetailAvailabilityViewModelT<DetailAvailabilityViewModel>
    {
        DetailAvailabilityViewModel(::HaloDesktop::ViewModels::QualityGroup const& group, std::size_t most);
        [[nodiscard]] winrt::hstring Plate() const { return m_plate; }
        [[nodiscard]] winrt::hstring TagText() const { return m_tag; }
        [[nodiscard]] winrt::HaloDesktop::QualityMedalFinish Finish() const noexcept { return m_finish; }
        [[nodiscard]] double Meter() const noexcept { return m_meter; }
        [[nodiscard]] Microsoft::UI::Xaml::Visibility GoldMeterVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PlainMeterVisibility() const noexcept;
        [[nodiscard]] winrt::hstring Count() const { return m_count; }

    private:
        winrt::hstring m_plate;
        winrt::hstring m_tag;
        winrt::HaloDesktop::QualityMedalFinish m_finish{ winrt::HaloDesktop::QualityMedalFinish::Plain };
        double m_meter{};
        bool m_gold{};
        winrt::hstring m_count;
    };

    struct DetailCreditViewModel : DetailCreditViewModelT<DetailCreditViewModel>
    {
        explicit DetailCreditViewModel(::HaloDesktop::ViewModels::Credit const& credit);
        [[nodiscard]] winrt::hstring Name() const { return m_name; }
        [[nodiscard]] winrt::hstring Role() const { return m_role; }
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RoleVisibility() const;
        [[nodiscard]] winrt::hstring Photo() const { return m_photo; }
        [[nodiscard]] winrt::hstring Initials() const { return m_initials; }
        [[nodiscard]] winrt::hstring Tooltip() const { return m_tooltip; }
        // The IMDb people search for this name, built from the name alone.
        [[nodiscard]] std::wstring const& SearchUrl() const noexcept { return m_searchUrl; }
        // The person's TMDB id, when the cast entry carried one.
        [[nodiscard]] std::optional<std::int64_t> TmdbId() const noexcept { return m_tmdbId; }

    private:
        winrt::hstring m_name;
        winrt::hstring m_role;
        winrt::hstring m_photo;
        winrt::hstring m_initials;
        winrt::hstring m_tooltip;
        std::wstring m_searchUrl;
        std::optional<std::int64_t> m_tmdbId;
    };

    struct DetailAboutRowViewModel : DetailAboutRowViewModelT<DetailAboutRowViewModel>
    {
        DetailAboutRowViewModel(winrt::hstring label, winrt::hstring value, bool last);
        [[nodiscard]] winrt::hstring Label() const { return m_label; }
        [[nodiscard]] winrt::hstring Value() const { return m_value; }
        [[nodiscard]] Microsoft::UI::Xaml::Visibility DividerVisibility() const noexcept;

    private:
        winrt::hstring m_label;
        winrt::hstring m_value;
        bool m_last{};
    };

    // The title page. UI-thread-only. Each open of a title is a fresh page and
    // a fresh view model (the page is not cached), so nothing here carries over
    // from another title; within one visit, the season, page and the rows stay
    // as the viewer left them while the sheet or the player is up.
    struct DetailViewModel : DetailViewModelT<DetailViewModel>
    {
        explicit DetailViewModel(::HaloDesktop::Services::AppServices const& services);
        ~DetailViewModel();

        // While the page is on screen: listens for watch, library and download
        // changes, and may ask the addons what they offer. Not projected.
        void Activate();
        void Deactivate() noexcept;

        [[nodiscard]] Microsoft::UI::Xaml::Visibility ContentVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SkeletonVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ErrorVisibility() const noexcept;
        [[nodiscard]] winrt::hstring ErrorText() const { return m_errorText; }
        [[nodiscard]] winrt::hstring Backdrop() const;

        [[nodiscard]] winrt::hstring Title() const;
        [[nodiscard]] winrt::hstring Logo() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility LogoVisibility() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility TitleVisibility() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable Facts() const { return m_facts; }
        [[nodiscard]] winrt::hstring Synopsis() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SynopsisVisibility() const;
        [[nodiscard]] winrt::hstring PlayLabel() const { return m_playLabel; }
        [[nodiscard]] Microsoft::UI::Xaml::Visibility InLibraryVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility NotInLibraryVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility TrailerVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ProgressVisibility() const noexcept;
        [[nodiscard]] double ProgressFraction() const noexcept;
        [[nodiscard]] winrt::hstring TimeLeft() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ProgressSeparatorVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility BestVisibility() const noexcept;
        [[nodiscard]] winrt::hstring BestPlate() const;
        [[nodiscard]] winrt::hstring BestTag() const;
        [[nodiscard]] winrt::HaloDesktop::QualityMedalFinish BestFinish() const noexcept;
        [[nodiscard]] winrt::hstring SourcesLink() const;

        [[nodiscard]] winrt::Windows::Foundation::IInspectable Availability() const { return m_availabilityRows; }
        [[nodiscard]] winrt::Windows::Foundation::IInspectable AvailabilityFailures() const { return m_availabilityFailures; }
        [[nodiscard]] Microsoft::UI::Xaml::Visibility AvailabilityLoadingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility AvailabilityEmptyVisibility() const noexcept;
        // No installed addon offers streams at all (Halo ships none), rather
        // than none of them having this title: the card says so and offers
        // to add one instead of browsing sources that cannot exist.
        [[nodiscard]] Microsoft::UI::Xaml::Visibility NoStreamAddonsVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility BrowseSourcesVisibility() const noexcept;
        // Settings, at the box that adds an addon.
        void OpenAddonSettings();
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SubtitlesVisibility() const noexcept;
        [[nodiscard]] winrt::hstring SubtitlesText() const;

        [[nodiscard]] Microsoft::UI::Xaml::Visibility EpisodesVisibility() const noexcept;
        [[nodiscard]] winrt::hstring EpisodesNote() const { return m_episodesNote; }
        [[nodiscard]] winrt::Windows::Foundation::IInspectable Seasons() const { return m_seasonLabels; }
        [[nodiscard]] std::int32_t SeasonIndex() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SeasonPickerVisibility() const noexcept;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable Pages() const { return m_pageLabels; }
        [[nodiscard]] std::int32_t PageIndex() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PagePickerVisibility() const noexcept;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable Episodes() const { return m_episodeRows; }

        [[nodiscard]] Microsoft::UI::Xaml::Visibility CastVisibility() const;
        [[nodiscard]] winrt::hstring CastNote() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable Credits() const { return m_credits; }
        [[nodiscard]] Microsoft::UI::Xaml::Visibility AboutVisibility() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable AboutRows() const { return m_aboutRows; }

        [[nodiscard]] Microsoft::UI::Xaml::Visibility SimilarVisibility() const;
        [[nodiscard]] winrt::hstring SimilarNote() const { return m_similarNote; }
        [[nodiscard]] winrt::Windows::Foundation::IInspectable Similar() const { return m_similar; }

        void Load(winrt::Windows::Foundation::IInspectable const& parameter);
        void Retry();
        void ToggleLibrary();
        void Play();
        void OpenEpisode(winrt::Windows::Foundation::IInspectable const& row);
        void OpenTrailer();
        void OpenCredit(winrt::Windows::Foundation::IInspectable const& credit);
        void OpenSimilar(winrt::Windows::Foundation::IInspectable const& media);
        void SelectSeason(std::int32_t index);
        void SelectPage(std::int32_t index);
        void LogoFailed();
        void GoBack();

        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

        // What an episode row's menu needs beyond the row: the title it belongs
        // to and every season's episodes, which "earlier" is counted against.
        [[nodiscard]] winrt::HaloDesktop::DetailNavParams Parameters() const noexcept { return m_params; }
        [[nodiscard]] std::vector<winrt::HaloDesktop::Episode> AllEpisodes() const;
        [[nodiscard]] winrt::hstring PosterUrl() const;

    private:
        [[nodiscard]] winrt::Windows::Foundation::IAsyncAction LoadAsync();
        [[nodiscard]] winrt::Windows::Foundation::IAsyncAction ToggleLibraryAsync();
        [[nodiscard]] winrt::Windows::Foundation::IAsyncAction LoadAvailabilityAsync(
            std::uint64_t version,
            winrt::hstring type,
            winrt::hstring videoId);
        [[nodiscard]] winrt::Windows::Foundation::IAsyncAction LoadSimilarAsync(std::uint64_t version);

        // Rebuilds what follows from the meta: facts, seasons, credits, About.
        void ApplyMeta();
        // Rebuilds what follows from the watch history and the chosen season:
        // the main button, the season shown, its pages and rows.
        void ApplyPlayState();
        void RebuildSeasonList();
        void RebuildPages(bool keepPage);
        void RebuildRows();
        void ApplyUserStateChange();
        void RequestSimilar();
        // Whether the shared metadata service still holds this page's title, so
        // its episodes are this title's.
        [[nodiscard]] bool OwnsMetadata() const;
        // Asks the addons about the video the main button plays, unless the
        // answer for it is already shown or on its way.
        void RequestAvailability();
        void ApplyAvailability(std::optional<::HaloDesktop::ViewModels::Availability> const& summary, std::vector<winrt::hstring> failures);
        void ClearAvailability();
        void OpenSourcesFor(winrt::HaloDesktop::Episode const& episode);

        [[nodiscard]] bool IsSeries() const noexcept;
        [[nodiscard]] winrt::hstring ItemId() const;
        [[nodiscard]] winrt::hstring MovieVideo() const;
        [[nodiscard]] winrt::hstring TargetVideoId() const;
        [[nodiscard]] std::vector<::HaloDesktop::Api::Dto::WatchEntry> RowsForItem() const;
        [[nodiscard]] std::optional<std::int32_t> ActiveSeason() const;
        [[nodiscard]] std::vector<winrt::HaloDesktop::Episode> ActiveEpisodes() const;
        [[nodiscard]] std::vector<::HaloDesktop::ViewModels::EpisodeRef> RefsOf(std::vector<winrt::HaloDesktop::Episode> const& episodes) const;
        void RaiseAll();
        void RaiseHero();
        void RaiseAvailability();
        void RaiseEpisodes();
        void Raise(wchar_t const* name);

        std::shared_ptr<::HaloDesktop::Services::IMetadataService> m_metadata;
        std::shared_ptr<::HaloDesktop::Services::LibraryService> m_library;
        std::shared_ptr<::HaloDesktop::Services::ICatalogService> m_catalog;
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        std::shared_ptr<::HaloDesktop::Services::IDownloadService> m_downloads;
        std::shared_ptr<::HaloDesktop::Services::WatchStateService> m_watch;
        std::shared_ptr<::HaloDesktop::Services::ISourceService> m_sources;
        std::shared_ptr<::HaloDesktop::Services::ServerFeaturesService> m_features;
        std::shared_ptr<::HaloDesktop::Services::IAddonService> m_addons;
        // Read once per availability answer, not per binding (Records copies the list).
        [[nodiscard]] bool HasNoStreamAddons() const;

        winrt::HaloDesktop::DetailNavParams m_params{ nullptr };
        std::optional<::HaloDesktop::Api::Dto::MetaDetail> m_meta;

        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_facts{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_availabilityRows{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_availabilityFailures{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_seasonLabels{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_pageLabels{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_episodeRows{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_credits{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_aboutRows{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_similar{ nullptr };

        // The seasons in display order, and the one the viewer picked, if any.
        // Until they pick, the season follows the watch history.
        std::vector<std::int32_t> m_seasons;
        std::optional<std::int32_t> m_pickedSeason;
        // The season the rows were built for; a change of season resets the page.
        std::optional<std::int32_t> m_shownSeason;
        std::vector<::HaloDesktop::ViewModels::EpisodePage> m_pages;
        std::size_t m_pageIndex{};
        std::size_t m_numberedSeasons{};

        std::optional<::HaloDesktop::Api::Dto::WatchEntry> m_resume;
        winrt::HaloDesktop::Episode m_resumeEpisode{ nullptr };
        winrt::HaloDesktop::Episode m_targetEpisode{ nullptr };
        winrt::hstring m_playLabel{ L"Play" };
        winrt::hstring m_episodesNote;
        std::optional<std::wstring> m_trailer;

        std::optional<::HaloDesktop::ViewModels::Availability> m_availability;
        winrt::hstring m_availabilityVideo;
        bool m_availabilityLoading{};

        winrt::hstring m_similarNote;
        winrt::hstring m_errorText;

        std::uint64_t m_loadVersion{};
        std::uint64_t m_availabilityVersion{};
        std::uint64_t m_similarVersion{};
        ::HaloDesktop::Services::DownloadChangedToken m_downloadToken{};
        ::HaloDesktop::Services::CatalogChangedToken m_userStateToken{};
        bool m_active{};
        bool m_loading{};
        bool m_error{};
        bool m_inLibrary{};
        bool m_logoFailed{};
        bool m_similarRequested{};
        // Set while the season and page lists are refilled, when the pickers
        // report selections nobody made.
        bool m_rebuildingPickers{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
