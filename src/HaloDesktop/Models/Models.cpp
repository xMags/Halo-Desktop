#include "pch.h"
#include "Models/Models.h"

#if __has_include("Addon.g.cpp")
#include "Addon.g.cpp"
#endif
#if __has_include("ContinueItem.g.cpp")
#include "ContinueItem.g.cpp"
#endif
#if __has_include("DownloadItem.g.cpp")
#include "DownloadItem.g.cpp"
#endif
#if __has_include("Episode.g.cpp")
#include "Episode.g.cpp"
#endif
#if __has_include("MediaDetail.g.cpp")
#include "MediaDetail.g.cpp"
#endif
#if __has_include("MediaSummary.g.cpp")
#include "MediaSummary.g.cpp"
#endif
#if __has_include("SearchGroup.g.cpp")
#include "SearchGroup.g.cpp"
#endif
#if __has_include("Shelf.g.cpp")
#include "Shelf.g.cpp"
#endif
#if __has_include("SourceGroup.g.cpp")
#include "SourceGroup.g.cpp"
#endif
#if __has_include("StreamSource.g.cpp")
#include "StreamSource.g.cpp"
#endif

#include <algorithm>
#include <utility>

namespace winrt::HaloDesktop::implementation
{
    MediaSummary::MediaSummary(hstring id, hstring title, hstring meta, HaloDesktop::MediaKind kind)
        : m_id(std::move(id)), m_title(std::move(title)), m_meta(std::move(meta)), m_kind(kind)
    {
    }

    hstring MediaSummary::Id() const { return m_id; }
    hstring MediaSummary::Title() const { return m_title; }
    hstring MediaSummary::Meta() const { return m_meta; }
    HaloDesktop::MediaKind MediaSummary::Kind() const noexcept { return m_kind; }
    hstring MediaSummary::KindLabel() const
    {
        return m_kind == HaloDesktop::MediaKind::Movie ? L"MOVIE" : L"SERIES";
    }

    MediaDetail::MediaDetail(
        hstring id,
        hstring title,
        hstring kicker,
        hstring metaLine,
        hstring synopsis,
        Windows::Foundation::Collections::IVectorView<hstring> facts,
        Windows::Foundation::Collections::IVectorView<hstring> availability,
        Windows::Foundation::Collections::IVectorView<std::int32_t> seasons)
        : m_id(std::move(id)),
          m_title(std::move(title)),
          m_kicker(std::move(kicker)),
          m_metaLine(std::move(metaLine)),
          m_synopsis(std::move(synopsis)),
          m_facts(std::move(facts)),
          m_availability(std::move(availability)),
          m_seasons(std::move(seasons))
    {
    }

    hstring MediaDetail::Id() const { return m_id; }
    hstring MediaDetail::Title() const { return m_title; }
    hstring MediaDetail::Kicker() const { return m_kicker; }
    hstring MediaDetail::MetaLine() const { return m_metaLine; }
    hstring MediaDetail::Synopsis() const { return m_synopsis; }
    Windows::Foundation::Collections::IVectorView<hstring> MediaDetail::Facts() const { return m_facts; }
    Windows::Foundation::Collections::IVectorView<hstring> MediaDetail::Availability() const { return m_availability; }
    Windows::Foundation::Collections::IVectorView<std::int32_t> MediaDetail::Seasons() const { return m_seasons; }

    Episode::Episode(hstring tag, hstring title, hstring blurb, hstring runtime, hstring aired, double progress, bool downloaded)
        : m_tag(std::move(tag)),
          m_title(std::move(title)),
          m_blurb(std::move(blurb)),
          m_runtime(std::move(runtime)),
          m_aired(std::move(aired)),
          m_progress(std::clamp(progress, 0.0, 1.0)),
          m_downloaded(downloaded)
    {
    }

    hstring Episode::Tag() const { return m_tag; }
    hstring Episode::Title() const { return m_title; }
    hstring Episode::Blurb() const { return m_blurb; }
    hstring Episode::Runtime() const { return m_runtime; }
    hstring Episode::Aired() const { return m_aired; }
    double Episode::Progress() const noexcept { return m_progress; }
    bool Episode::Downloaded() const noexcept { return m_downloaded; }

    StreamSource::StreamSource(
        hstring quality,
        hstring range,
        hstring file,
        hstring codec,
        hstring audio,
        hstring languages,
        HaloDesktop::StreamStatus status,
        hstring size)
        : m_quality(std::move(quality)),
          m_range(std::move(range)),
          m_file(std::move(file)),
          m_codec(std::move(codec)),
          m_audio(std::move(audio)),
          m_languages(std::move(languages)),
          m_status(status),
          m_size(std::move(size))
    {
    }

    hstring StreamSource::Quality() const { return m_quality; }
    hstring StreamSource::Range() const { return m_range; }
    hstring StreamSource::File() const { return m_file; }
    hstring StreamSource::Codec() const { return m_codec; }
    hstring StreamSource::Audio() const { return m_audio; }
    hstring StreamSource::Languages() const { return m_languages; }
    HaloDesktop::StreamStatus StreamSource::Status() const noexcept { return m_status; }
    hstring StreamSource::Size() const { return m_size; }

    SourceGroup::SourceGroup(
        hstring name,
        hstring note,
        std::int32_t count,
        Windows::Foundation::Collections::IVectorView<HaloDesktop::StreamSource> sources)
        : m_name(std::move(name)), m_note(std::move(note)), m_count(count), m_sources(std::move(sources))
    {
    }

    hstring SourceGroup::Name() const { return m_name; }
    hstring SourceGroup::Note() const { return m_note; }
    std::int32_t SourceGroup::Count() const noexcept { return m_count; }
    Windows::Foundation::Collections::IVectorView<HaloDesktop::StreamSource> SourceGroup::Sources() const { return m_sources; }

    DownloadItem::DownloadItem(
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
        hstring subs)
        : m_id(std::move(id)),
          m_tag(std::move(tag)),
          m_name(std::move(name)),
          m_sub(std::move(sub)),
          m_state(state),
          m_progress(std::clamp(progress, 0.0, 1.0)),
          m_detail(std::move(detail)),
          m_quality(std::move(quality)),
          m_codec(std::move(codec)),
          m_size(std::move(size)),
          m_subs(std::move(subs))
    {
    }

    hstring DownloadItem::Id() const { return m_id; }
    hstring DownloadItem::Tag() const { return m_tag; }
    hstring DownloadItem::Name() const { return m_name; }
    hstring DownloadItem::Sub() const { return m_sub; }
    HaloDesktop::DownloadState DownloadItem::State() const noexcept { return m_state; }
    double DownloadItem::Progress() const noexcept { return m_progress; }
    hstring DownloadItem::Detail() const { return m_detail; }
    hstring DownloadItem::Quality() const { return m_quality; }
    hstring DownloadItem::Codec() const { return m_codec; }
    hstring DownloadItem::Size() const { return m_size; }
    hstring DownloadItem::Subs() const { return m_subs; }

    winrt::event_token DownloadItem::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }

    void DownloadItem::PropertyChanged(winrt::event_token const& token) noexcept
    {
        m_propertyChanged.remove(token);
    }

    void DownloadItem::UpdateProgress(double progress, hstring detail)
    {
        auto const nextProgress = std::clamp(progress, 0.0, 1.0);
        if (m_progress != nextProgress)
        {
            m_progress = nextProgress;
            RaisePropertyChanged(L"Progress");
        }
        if (m_detail != detail)
        {
            m_detail = std::move(detail);
            RaisePropertyChanged(L"Detail");
        }
    }

    void DownloadItem::UpdateState(HaloDesktop::DownloadState state, hstring detail)
    {
        if (m_state != state)
        {
            m_state = state;
            RaisePropertyChanged(L"State");
        }
        if (m_detail != detail)
        {
            m_detail = std::move(detail);
            RaisePropertyChanged(L"Detail");
        }
    }

    void DownloadItem::RaisePropertyChanged(wchar_t const* propertyName)
    {
        m_propertyChanged(*this, Microsoft::UI::Xaml::Data::PropertyChangedEventArgs{ propertyName });
    }

    Addon::Addon(
        hstring id,
        hstring transportUrl,
        hstring initials,
        hstring name,
        hstring version,
        hstring scope,
        hstring provides,
        bool canEdit,
        bool isGlobal,
        bool enabled)
        : m_id(std::move(id)),
          m_transportUrl(std::move(transportUrl)),
          m_initials(std::move(initials)),
          m_name(std::move(name)),
          m_version(std::move(version)),
          m_scope(std::move(scope)),
          m_provides(std::move(provides)),
          m_canEdit(canEdit),
          m_isGlobal(isGlobal),
          m_enabled(enabled)
    {
    }

    hstring Addon::Id() const { return m_id; }
    hstring Addon::TransportUrl() const { return m_transportUrl; }
    hstring Addon::Initials() const { return m_initials; }
    hstring Addon::Name() const { return m_name; }
    hstring Addon::Version() const { return m_version; }
    hstring Addon::Scope() const { return m_scope; }
    hstring Addon::Provides() const { return m_provides; }
    bool Addon::CanEdit() const noexcept { return m_canEdit; }
    bool Addon::IsGlobal() const noexcept { return m_isGlobal; }
    bool Addon::Enabled() const noexcept { return m_enabled; }
    void Addon::Enabled(bool value) noexcept { m_enabled = value; }

    ContinueItem::ContinueItem(hstring name, hstring sub, hstring tag, hstring timeLeft, double progress)
        : m_name(std::move(name)),
          m_sub(std::move(sub)),
          m_tag(std::move(tag)),
          m_timeLeft(std::move(timeLeft)),
          m_progress(std::clamp(progress, 0.0, 1.0))
    {
    }

    hstring ContinueItem::Name() const { return m_name; }
    hstring ContinueItem::Sub() const { return m_sub; }
    hstring ContinueItem::Tag() const { return m_tag; }
    hstring ContinueItem::TimeLeft() const { return m_timeLeft; }
    double ContinueItem::Progress() const noexcept { return m_progress; }

    SearchGroup::SearchGroup(
        hstring title,
        hstring sourceLabel,
        Windows::Foundation::Collections::IVectorView<HaloDesktop::MediaSummary> items)
        : m_title(std::move(title)), m_sourceLabel(std::move(sourceLabel)), m_items(std::move(items))
    {
    }

    hstring SearchGroup::Title() const { return m_title; }
    hstring SearchGroup::SourceLabel() const { return m_sourceLabel; }
    Windows::Foundation::Collections::IVectorView<HaloDesktop::MediaSummary> SearchGroup::Items() const { return m_items; }

    Shelf::Shelf(
        hstring title,
        hstring sourceLabel,
        Windows::Foundation::Collections::IVectorView<HaloDesktop::MediaSummary> items)
        : m_title(std::move(title)), m_sourceLabel(std::move(sourceLabel)), m_items(std::move(items))
    {
    }

    hstring Shelf::Title() const { return m_title; }
    hstring Shelf::SourceLabel() const { return m_sourceLabel; }
    Windows::Foundation::Collections::IVectorView<HaloDesktop::MediaSummary> Shelf::Items() const { return m_items; }
}
