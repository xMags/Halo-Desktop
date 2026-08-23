#pragma once

#include "Addon.g.h"
#include "ContinueItem.g.h"
#include "DownloadItem.g.h"
#include "Episode.g.h"
#include "MediaDetail.g.h"
#include "MediaSummary.g.h"
#include "SearchGroup.g.h"
#include "Shelf.g.h"
#include "SourceGroup.g.h"
#include "StreamSource.g.h"

#include <cstdint>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace winrt::HaloDesktop::implementation
{
    struct MediaSummary : MediaSummaryT<MediaSummary>
    {
        MediaSummary() = default;
        MediaSummary(hstring id, hstring title, hstring meta, HaloDesktop::MediaKind kind);

        [[nodiscard]] hstring Id() const;
        [[nodiscard]] hstring Title() const;
        [[nodiscard]] hstring Meta() const;
        [[nodiscard]] HaloDesktop::MediaKind Kind() const noexcept;
        [[nodiscard]] hstring KindLabel() const;

    private:
        hstring m_id;
        hstring m_title;
        hstring m_meta;
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
            Windows::Foundation::Collections::IVectorView<std::int32_t> seasons);

        [[nodiscard]] hstring Id() const;
        [[nodiscard]] hstring Title() const;
        [[nodiscard]] hstring Kicker() const;
        [[nodiscard]] hstring MetaLine() const;
        [[nodiscard]] hstring Synopsis() const;
        [[nodiscard]] Windows::Foundation::Collections::IVectorView<hstring> Facts() const;
        [[nodiscard]] Windows::Foundation::Collections::IVectorView<hstring> Availability() const;
        [[nodiscard]] Windows::Foundation::Collections::IVectorView<std::int32_t> Seasons() const;

    private:
        hstring m_id;
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
        Episode(hstring tag, hstring title, hstring blurb, hstring runtime, hstring aired, double progress, bool downloaded);

        [[nodiscard]] hstring Tag() const;
        [[nodiscard]] hstring Title() const;
        [[nodiscard]] hstring Blurb() const;
        [[nodiscard]] hstring Runtime() const;
        [[nodiscard]] hstring Aired() const;
        [[nodiscard]] double Progress() const noexcept;
        [[nodiscard]] bool Downloaded() const noexcept;

    private:
        hstring m_tag;
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
        StreamSource(
            hstring quality,
            hstring range,
            hstring file,
            hstring codec,
            hstring audio,
            hstring languages,
            HaloDesktop::StreamStatus status,
            hstring size);

        [[nodiscard]] hstring Quality() const;
        [[nodiscard]] hstring Range() const;
        [[nodiscard]] hstring File() const;
        [[nodiscard]] hstring Codec() const;
        [[nodiscard]] hstring Audio() const;
        [[nodiscard]] hstring Languages() const;
        [[nodiscard]] HaloDesktop::StreamStatus Status() const noexcept;
        [[nodiscard]] hstring Size() const;

    private:
        hstring m_quality;
        hstring m_range;
        hstring m_file;
        hstring m_codec;
        hstring m_audio;
        hstring m_languages;
        HaloDesktop::StreamStatus m_status{ HaloDesktop::StreamStatus::Uncached };
        hstring m_size;
    };

    struct SourceGroup : SourceGroupT<SourceGroup>
    {
        SourceGroup() = default;
        SourceGroup(hstring name, hstring note, std::int32_t count, Windows::Foundation::Collections::IVectorView<HaloDesktop::StreamSource> sources);

        [[nodiscard]] hstring Name() const;
        [[nodiscard]] hstring Note() const;
        [[nodiscard]] std::int32_t Count() const noexcept;
        [[nodiscard]] Windows::Foundation::Collections::IVectorView<HaloDesktop::StreamSource> Sources() const;

    private:
        hstring m_name;
        hstring m_note;
        std::int32_t m_count{};
        Windows::Foundation::Collections::IVectorView<HaloDesktop::StreamSource> m_sources{ nullptr };
    };

    struct DownloadItem : DownloadItemT<DownloadItem>
    {
        DownloadItem() = default;
        DownloadItem(
            hstring id,
            hstring tag,
            hstring name,
            hstring sub,
            HaloDesktop::DownloadState state,
            double progress,
            hstring detail,
            hstring quality,
            hstring codec,
            hstring size,
            hstring subs);

        [[nodiscard]] hstring Id() const;
        [[nodiscard]] hstring Tag() const;
        [[nodiscard]] hstring Name() const;
        [[nodiscard]] hstring Sub() const;
        [[nodiscard]] HaloDesktop::DownloadState State() const noexcept;
        [[nodiscard]] double Progress() const noexcept;
        [[nodiscard]] hstring Detail() const;
        [[nodiscard]] hstring Quality() const;
        [[nodiscard]] hstring Codec() const;
        [[nodiscard]] hstring Size() const;
        [[nodiscard]] hstring Subs() const;

        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

        void UpdateProgress(double progress, hstring detail);
        void UpdateState(HaloDesktop::DownloadState state, hstring detail);

    private:
        void RaisePropertyChanged(wchar_t const* propertyName);

        hstring m_id;
        hstring m_tag;
        hstring m_name;
        hstring m_sub;
        HaloDesktop::DownloadState m_state{ HaloDesktop::DownloadState::Queued };
        double m_progress{};
        hstring m_detail;
        hstring m_quality;
        hstring m_codec;
        hstring m_size;
        hstring m_subs;
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };

    struct Addon : AddonT<Addon>
    {
        Addon() = default;
        Addon(hstring initials, hstring name, hstring version, hstring scope, hstring provides, bool enabled);

        [[nodiscard]] hstring Initials() const;
        [[nodiscard]] hstring Name() const;
        [[nodiscard]] hstring Version() const;
        [[nodiscard]] hstring Scope() const;
        [[nodiscard]] hstring Provides() const;
        [[nodiscard]] bool Enabled() const noexcept;
        void Enabled(bool value) noexcept;

    private:
        hstring m_initials;
        hstring m_name;
        hstring m_version;
        hstring m_scope;
        hstring m_provides;
        bool m_enabled{};
    };

    struct ContinueItem : ContinueItemT<ContinueItem>
    {
        ContinueItem() = default;
        ContinueItem(hstring name, hstring sub, hstring tag, hstring timeLeft, double progress);

        [[nodiscard]] hstring Name() const;
        [[nodiscard]] hstring Sub() const;
        [[nodiscard]] hstring Tag() const;
        [[nodiscard]] hstring TimeLeft() const;
        [[nodiscard]] double Progress() const noexcept;

    private:
        hstring m_name;
        hstring m_sub;
        hstring m_tag;
        hstring m_timeLeft;
        double m_progress{};
    };

    struct SearchGroup : SearchGroupT<SearchGroup>
    {
        SearchGroup() = default;
        SearchGroup(hstring title, hstring sourceLabel, Windows::Foundation::Collections::IVectorView<HaloDesktop::MediaSummary> items);

        [[nodiscard]] hstring Title() const;
        [[nodiscard]] hstring SourceLabel() const;
        [[nodiscard]] Windows::Foundation::Collections::IVectorView<HaloDesktop::MediaSummary> Items() const;

    private:
        hstring m_title;
        hstring m_sourceLabel;
        Windows::Foundation::Collections::IVectorView<HaloDesktop::MediaSummary> m_items{ nullptr };
    };

    struct Shelf : ShelfT<Shelf>
    {
        Shelf() = default;
        Shelf(hstring title, hstring sourceLabel, Windows::Foundation::Collections::IVectorView<HaloDesktop::MediaSummary> items);

        [[nodiscard]] hstring Title() const;
        [[nodiscard]] hstring SourceLabel() const;
        [[nodiscard]] Windows::Foundation::Collections::IVectorView<HaloDesktop::MediaSummary> Items() const;

    private:
        hstring m_title;
        hstring m_sourceLabel;
        Windows::Foundation::Collections::IVectorView<HaloDesktop::MediaSummary> m_items{ nullptr };
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct MediaSummary : MediaSummaryT<MediaSummary, implementation::MediaSummary> {};
    struct MediaDetail : MediaDetailT<MediaDetail, implementation::MediaDetail> {};
    struct Episode : EpisodeT<Episode, implementation::Episode> {};
    struct StreamSource : StreamSourceT<StreamSource, implementation::StreamSource> {};
    struct SourceGroup : SourceGroupT<SourceGroup, implementation::SourceGroup> {};
    struct DownloadItem : DownloadItemT<DownloadItem, implementation::DownloadItem> {};
    struct Addon : AddonT<Addon, implementation::Addon> {};
    struct ContinueItem : ContinueItemT<ContinueItem, implementation::ContinueItem> {};
    struct SearchGroup : SearchGroupT<SearchGroup, implementation::SearchGroup> {};
    struct Shelf : ShelfT<Shelf, implementation::Shelf> {};
}
