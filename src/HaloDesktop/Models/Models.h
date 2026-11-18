#pragma once

#include "Addon.g.h"
#include "ContinueItem.g.h"
#include "DetailNavParams.g.h"
#include "Episode.g.h"
#include "FeaturedItem.g.h"
#include "MediaDetail.g.h"
#include "MediaSummary.g.h"
#include "Shelf.g.h"
#include "SourceGroup.g.h"
#include "SourcesNavParams.g.h"
#include "StreamSource.g.h"
#include "PlaybackRequest.g.h"

#include "Services/CatalogPagingPolicy.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace winrt::HaloDesktop::implementation
{
    struct MediaSummary : MediaSummaryT<MediaSummary>
    {
        MediaSummary() = default;
        MediaSummary(
            hstring id,
            hstring title,
            hstring meta,
            HaloDesktop::MediaKind kind,
            hstring type = {},
            hstring poster = {},
            hstring background = {},
            hstring description = {},
            hstring rating = {},
            hstring releaseInfo = {},
            std::int64_t addedAt = 0,
            std::int64_t updatedAt = 0,
            hstring logo = {},
            hstring defaultVideoId = {});

        [[nodiscard]] hstring Id() const;
        [[nodiscard]] hstring Type() const;
        [[nodiscard]] hstring Title() const;
        [[nodiscard]] hstring Meta() const;
        [[nodiscard]] hstring Poster() const;
        [[nodiscard]] hstring Background() const;
        [[nodiscard]] hstring Description() const;
        [[nodiscard]] hstring Rating() const;
        [[nodiscard]] hstring ReleaseInfo() const;
        [[nodiscard]] std::int64_t AddedAt() const noexcept;
        [[nodiscard]] std::int64_t UpdatedAt() const noexcept;
        [[nodiscard]] HaloDesktop::MediaKind Kind() const noexcept;
        [[nodiscard]] hstring KindLabel() const;
        [[nodiscard]] hstring Logo() const;
        [[nodiscard]] hstring DefaultVideoId() const;

    private:
        hstring m_id;
        hstring m_type;
        hstring m_title;
        hstring m_meta;
        hstring m_poster;
        hstring m_background;
        hstring m_description;
        hstring m_rating;
        hstring m_releaseInfo;
        hstring m_logo;
        hstring m_defaultVideoId;
        std::int64_t m_addedAt{};
        std::int64_t m_updatedAt{};
        HaloDesktop::MediaKind m_kind{ HaloDesktop::MediaKind::Movie };
    };

    struct MediaDetail : MediaDetailT<MediaDetail>
    {
        MediaDetail() = default;
        MediaDetail(
            hstring id,
            hstring title,
            hstring kicker,
            hstring metaLine,
            hstring synopsis,
            Windows::Foundation::Collections::IVectorView<hstring> facts,
            Windows::Foundation::Collections::IVectorView<hstring> availability,
            Windows::Foundation::Collections::IVectorView<std::int32_t> seasons,
            hstring type = {}, hstring poster = {}, hstring background = {});

        [[nodiscard]] hstring Id() const;
        [[nodiscard]] hstring Type() const; [[nodiscard]] hstring Poster() const; [[nodiscard]] hstring Background() const;
        [[nodiscard]] hstring Title() const;
        [[nodiscard]] hstring Kicker() const;
        [[nodiscard]] hstring MetaLine() const;
        [[nodiscard]] hstring Synopsis() const;
        [[nodiscard]] Windows::Foundation::Collections::IVectorView<hstring> Facts() const;
        [[nodiscard]] Windows::Foundation::Collections::IVectorView<hstring> Availability() const;
        [[nodiscard]] Windows::Foundation::Collections::IVectorView<std::int32_t> Seasons() const;

    private:
        hstring m_id;
        hstring m_type,m_poster,m_background;
        hstring m_title;
        hstring m_kicker;
        hstring m_metaLine;
        hstring m_synopsis;
        Windows::Foundation::Collections::IVectorView<hstring> m_facts{ nullptr };
        Windows::Foundation::Collections::IVectorView<hstring> m_availability{ nullptr };
        Windows::Foundation::Collections::IVectorView<std::int32_t> m_seasons{ nullptr };
    };

    struct Episode : EpisodeT<Episode>
    {
        Episode() = default;
        Episode(hstring tag, hstring title, hstring blurb, hstring runtime, hstring aired, double progress, bool downloaded,
                hstring videoId = {}, std::int32_t season = 0, std::int32_t number = 0, hstring thumbnail = {}, bool watched = false);

        [[nodiscard]] hstring Tag() const;
        [[nodiscard]] hstring VideoId() const; [[nodiscard]] std::int32_t Season() const noexcept; [[nodiscard]] std::int32_t Number() const noexcept; [[nodiscard]] hstring Thumbnail() const; [[nodiscard]] bool Watched() const noexcept;
        [[nodiscard]] hstring Title() const;
        [[nodiscard]] hstring Blurb() const;
        [[nodiscard]] hstring Runtime() const;
        [[nodiscard]] hstring Aired() const;
        [[nodiscard]] double Progress() const noexcept;
        [[nodiscard]] bool Downloaded() const noexcept;

    private:
        hstring m_tag;
        hstring m_videoId,m_thumbnail; std::int32_t m_season{},m_number{}; bool m_watched{};
        hstring m_title;
        hstring m_blurb;
        hstring m_runtime;
        hstring m_aired;
        double m_progress{};
        bool m_downloaded{};
    };

    struct StreamSource : StreamSourceT<StreamSource>
    {
        StreamSource() = default;
        StreamSource(hstring quality,hstring range,hstring file,hstring codec,hstring audio,hstring languages,HaloDesktop::StreamStatus status,hstring size);
        StreamSource(
            hstring key,
            hstring quality,
            hstring range,
            hstring file,
            hstring codec,
            hstring audio,
            hstring languages,
            HaloDesktop::StreamStatus status,
            hstring size,
            hstring detail,
            std::uint64_t sizeBytes,
            std::int32_t rank,
            Windows::Foundation::Collections::IVectorView<hstring> subtitleLanguages);

        [[nodiscard]] hstring Key() const;
        [[nodiscard]] hstring Quality() const;
        [[nodiscard]] hstring Range() const;
        [[nodiscard]] hstring File() const;
        [[nodiscard]] hstring Codec() const;
        [[nodiscard]] hstring Audio() const;
        [[nodiscard]] hstring Languages() const;
        [[nodiscard]] HaloDesktop::StreamStatus Status() const noexcept;
        [[nodiscard]] hstring Size() const;
        [[nodiscard]] hstring Detail() const;
        // Bytes and rank travel beside their formatted twins because the sources
        // sheet sorts on them; re-parsing "6.20 GB" back into a number would make
        // the sort depend on the display format.
        [[nodiscard]] std::uint64_t SizeBytes() const noexcept;
        // Position in the service's own ranking, ascending, zero for its pick.
        [[nodiscard]] std::int32_t Rank() const noexcept;
        [[nodiscard]] Windows::Foundation::Collections::IVectorView<hstring> SubtitleLanguages() const;

    private:
        hstring m_key;
        hstring m_quality;
        hstring m_range;
        hstring m_file;
        hstring m_codec;
        hstring m_audio;
        hstring m_languages;
        HaloDesktop::StreamStatus m_status{ HaloDesktop::StreamStatus::Uncached };
        hstring m_size;
        hstring m_detail;
        std::uint64_t m_sizeBytes{};
        std::int32_t m_rank{};
        Windows::Foundation::Collections::IVectorView<hstring> m_subtitleLanguages{ nullptr };
    };

    struct SourceGroup : SourceGroupT<SourceGroup>
    {
        SourceGroup() = default;
        SourceGroup(hstring name,hstring note,std::int32_t count,Windows::Foundation::Collections::IVectorView<HaloDesktop::StreamSource> sources);
        SourceGroup(hstring addonId, hstring name, hstring note, std::int32_t count, Windows::Foundation::Collections::IVectorView<HaloDesktop::StreamSource> sources, bool answered);

        [[nodiscard]] hstring AddonId() const;
        [[nodiscard]] hstring Name() const;
        [[nodiscard]] hstring Note() const;
        [[nodiscard]] std::int32_t Count() const noexcept;
        // An addon that answered with nothing and an addon that never answered
        // both hold zero sources, and only this tells the two of them apart.
        [[nodiscard]] bool Answered() const noexcept;
        [[nodiscard]] Windows::Foundation::Collections::IVectorView<HaloDesktop::StreamSource> Sources() const;

    private:
        hstring m_addonId;
        hstring m_name;
        hstring m_note;
        std::int32_t m_count{};
        bool m_answered{};
        Windows::Foundation::Collections::IVectorView<HaloDesktop::StreamSource> m_sources{ nullptr };
    };


    struct Addon : AddonT<Addon>
    {
        Addon() = default;
        Addon(
            hstring id,
            hstring transportUrl,
            hstring initials,
            hstring name,
            hstring version,
            hstring scope,
            hstring provides,
            bool canEdit,
            bool isGlobal,
            bool enabled);

        [[nodiscard]] hstring Id() const;
        [[nodiscard]] hstring TransportUrl() const;
        [[nodiscard]] hstring Initials() const;
        [[nodiscard]] hstring Name() const;
        [[nodiscard]] hstring Version() const;
        [[nodiscard]] hstring Scope() const;
        [[nodiscard]] hstring Provides() const;
        [[nodiscard]] bool CanEdit() const noexcept;
        [[nodiscard]] bool IsGlobal() const noexcept;
        [[nodiscard]] bool Enabled() const noexcept;
        void Enabled(bool value) noexcept;

    private:
        hstring m_id;
        hstring m_transportUrl;
        hstring m_initials;
        hstring m_name;
        hstring m_version;
        hstring m_scope;
        hstring m_provides;
        bool m_canEdit{};
        bool m_isGlobal{};
        bool m_enabled{};
    };

    struct ContinueItem : ContinueItemT<ContinueItem>
    {
        ContinueItem() = default;
        ContinueItem(
            hstring name,
            hstring sub,
            hstring tag,
            hstring timeLeft,
            double progress,
            hstring type = {},
            hstring metaId = {},
            hstring videoId = {},
            hstring itemId = {},
            hstring poster = {});

        [[nodiscard]] hstring Name() const;
        [[nodiscard]] hstring Type() const;
        [[nodiscard]] hstring MetaId() const;
        [[nodiscard]] hstring VideoId() const;
        [[nodiscard]] hstring ItemId() const;
        [[nodiscard]] hstring Poster() const;
        [[nodiscard]] hstring Art() const;
        [[nodiscard]] hstring Sub() const;
        [[nodiscard]] hstring Tag() const;
        [[nodiscard]] hstring TimeLeft() const;
        [[nodiscard]] double Progress() const noexcept;

        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

        [[nodiscard]] hstring EpisodeTitle() const;

        // Not projected: only the artwork service resolves a still, and the shelf
        // is already on screen by the time it does.
        void SetStill(hstring value);
        // Not projected, and raises nothing: no card draws it. The sources sheet
        // and the player read it when the item is opened.
        void SetEpisodeTitle(hstring value);

    private:
        hstring m_name;
        hstring m_type;
        hstring m_metaId;
        hstring m_videoId;
        hstring m_itemId;
        hstring m_poster;
        hstring m_still;
        hstring m_episodeTitle;
        hstring m_sub;
        hstring m_tag;
        hstring m_timeLeft;
        double m_progress{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };

    struct FeaturedItem : FeaturedItemT<FeaturedItem>
    {
        FeaturedItem() = default;
        // The title size scales with the layout step the page is shown at. It
        // rides on the card because a FlipView template cannot see the view model
        // that owns that step, and the page updates every card on resize.
        explicit FeaturedItem(HaloDesktop::MediaSummary media, bool inLibrary, double titleSize = 36.0);

        [[nodiscard]] HaloDesktop::MediaSummary Media() const;
        [[nodiscard]] hstring Title() const;
        [[nodiscard]] hstring Rating() const;
        [[nodiscard]] hstring Meta() const;
        [[nodiscard]] hstring Synopsis() const;
        [[nodiscard]] hstring Background() const;
        [[nodiscard]] hstring ActionLabel() const;
        [[nodiscard]] hstring Logo() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RatingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility MetaVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility FactsSeparatorVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SynopsisVisibility() const noexcept;
        [[nodiscard]] double TitleSize() const noexcept;
        [[nodiscard]] hstring LibraryLabel() const;
        [[nodiscard]] bool InLibrary() const noexcept;
        [[nodiscard]] bool LibraryBusy() const noexcept;
        [[nodiscard]] bool LibraryEnabled() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility InLibraryVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility NotInLibraryVisibility() const noexcept;
        [[nodiscard]] hstring LibraryErrorText() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility LibraryErrorVisibility() const noexcept;

        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

        // Not projected: only the view model driving the save mutates these, and
        // it does so on the UI thread after the library call has completed.
        void SetMedia(HaloDesktop::MediaSummary media);
        void SetInLibrary(bool value);
        void SetLibraryBusy(bool value);
        void SetLibraryError(hstring value);
        void SetTitleSize(double value);

    private:
        void RaiseLibraryState();

        HaloDesktop::MediaSummary m_media{ nullptr };
        hstring m_libraryError;
        double m_titleSize{ 36.0 };
        bool m_inLibrary{};
        bool m_libraryBusy{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };

    // Where a shelf's "See all" leads.
    enum class ShelfSeeAll
    {
        // The shelf's titles as a grid, paging on when the catalog pages.
        Catalog,
        History,
        Library,
    };

    struct Shelf : ShelfT<Shelf>
    {
        Shelf() = default;
        Shelf(hstring title, hstring sourceLabel, Windows::Foundation::Collections::IVectorView<HaloDesktop::MediaSummary> items);
        // Not projected. `items` is what the strip draws; `seeAllItems` is what
        // its grid opens with when the strip shows only the first of them.
        Shelf(
            hstring title,
            hstring sourceLabel,
            Windows::Foundation::Collections::IVectorView<HaloDesktop::MediaSummary> items,
            Windows::Foundation::Collections::IVectorView<HaloDesktop::MediaSummary> seeAllItems,
            ShelfSeeAll seeAll,
            std::shared_ptr<::HaloDesktop::Services::CatalogPagingSource const> paging);

        [[nodiscard]] hstring Title() const;
        [[nodiscard]] hstring SourceLabel() const;
        [[nodiscard]] Windows::Foundation::Collections::IVectorView<HaloDesktop::MediaSummary> Items() const;

        // Not projected: read by the views that open "See all", through get_self.
        [[nodiscard]] Windows::Foundation::Collections::IVectorView<HaloDesktop::MediaSummary> SeeAllItems() const;
        [[nodiscard]] ShelfSeeAll SeeAll() const noexcept;
        // Null when the shelf's catalog does not page.
        [[nodiscard]] std::shared_ptr<::HaloDesktop::Services::CatalogPagingSource const> Paging() const noexcept;
        [[nodiscard]] bool IsLoading() const noexcept;
        // Not projected. Set once, right after construction, by whoever makes
        // the shelf; a shelf is a snapshot and never changes once handed out.
        void SetLoading(bool loading) noexcept;
        // Which row of Home this is (a catalog's request, or the library), so
        // a rebuilt list can tell a kept row from a new one.
        [[nodiscard]] std::wstring const& Key() const noexcept;
        void SetKey(std::wstring key);

    private:
        hstring m_title;
        hstring m_sourceLabel;
        Windows::Foundation::Collections::IVectorView<HaloDesktop::MediaSummary> m_items{ nullptr };
        Windows::Foundation::Collections::IVectorView<HaloDesktop::MediaSummary> m_seeAllItems{ nullptr };
        ShelfSeeAll m_seeAll{ ShelfSeeAll::Catalog };
        std::shared_ptr<::HaloDesktop::Services::CatalogPagingSource const> m_paging;
        std::wstring m_key;
        bool m_loading{};
    };

    struct DetailNavParams : DetailNavParamsT<DetailNavParams>
    {
        DetailNavParams()=default; DetailNavParams(hstring type,hstring metaId,hstring title,hstring poster);
        hstring Type()const;hstring MetaId()const;hstring Title()const;hstring Poster()const;
    private:hstring m_type,m_metaId,m_title,m_poster;
    };
    struct SourcesNavParams : SourcesNavParamsT<SourcesNavParams>
    {
        SourcesNavParams()=default; SourcesNavParams(hstring type,hstring metaId,hstring videoId,hstring itemId,hstring title,hstring showName,hstring episodeLabel,hstring poster);
        hstring Type()const;hstring MetaId()const;hstring VideoId()const;hstring ItemId()const;hstring Title()const;hstring ShowName()const;hstring EpisodeLabel()const;hstring Poster()const;
    private:hstring m_type,m_metaId,m_videoId,m_itemId,m_title,m_showName,m_episodeLabel,m_poster;
    };

    struct PlaybackRequest : PlaybackRequestT<PlaybackRequest>
    {
        PlaybackRequest()=default;
        PlaybackRequest(hstring url,bool isLocalFile,hstring downloadId,hstring subtitleLang,hstring mediaType,hstring videoId,hstring itemId,hstring metaId,hstring title,hstring showName,hstring episodeLabel,hstring poster,hstring addonId,hstring bingeGroup,hstring filename,std::uint64_t videoSize,hstring videoHash,std::vector<std::pair<hstring,hstring>> requestHeaders={});
        [[nodiscard]] hstring Url()const; [[nodiscard]] bool IsLocalFile()const noexcept; [[nodiscard]] hstring DownloadId()const; [[nodiscard]] hstring SubtitleLang()const; [[nodiscard]] hstring MediaType()const; [[nodiscard]] hstring VideoId()const; [[nodiscard]] hstring ItemId()const; [[nodiscard]] hstring MetaId()const; [[nodiscard]] hstring Title()const; [[nodiscard]] hstring ShowName()const; [[nodiscard]] hstring EpisodeLabel()const; [[nodiscard]] hstring Poster()const; [[nodiscard]] hstring AddonId()const; [[nodiscard]] hstring BingeGroup()const; [[nodiscard]] hstring Filename()const; [[nodiscard]] std::uint64_t VideoSize()const noexcept; [[nodiscard]] hstring VideoHash()const;
        [[nodiscard]] std::vector<std::pair<hstring,hstring>> const& RequestHeaders()const noexcept;
    private:
        hstring m_url,m_downloadId,m_subtitleLang,m_mediaType,m_videoId,m_itemId,m_metaId,m_title,m_showName,m_episodeLabel,m_poster,m_addonId,m_bingeGroup,m_filename,m_videoHash; std::vector<std::pair<hstring,hstring>> m_requestHeaders; bool m_isLocalFile{}; std::uint64_t m_videoSize{};
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct MediaSummary : MediaSummaryT<MediaSummary, implementation::MediaSummary> {};
    struct MediaDetail : MediaDetailT<MediaDetail, implementation::MediaDetail> {};
    struct Episode : EpisodeT<Episode, implementation::Episode> {};
    struct StreamSource : StreamSourceT<StreamSource, implementation::StreamSource> {};
    struct SourceGroup : SourceGroupT<SourceGroup, implementation::SourceGroup> {};
    struct Addon : AddonT<Addon, implementation::Addon> {};
    struct ContinueItem : ContinueItemT<ContinueItem, implementation::ContinueItem> {};
    struct FeaturedItem : FeaturedItemT<FeaturedItem, implementation::FeaturedItem> {};
    struct Shelf : ShelfT<Shelf, implementation::Shelf> {};
    struct DetailNavParams : DetailNavParamsT<DetailNavParams, implementation::DetailNavParams> {};
    struct SourcesNavParams : SourcesNavParamsT<SourcesNavParams, implementation::SourcesNavParams> {};
    struct PlaybackRequest : PlaybackRequestT<PlaybackRequest, implementation::PlaybackRequest> {};
}
