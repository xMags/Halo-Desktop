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
#if __has_include("DetailNavParams.g.cpp")
#include "DetailNavParams.g.cpp"
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
#if __has_include("SourcesNavParams.g.cpp")
#include "SourcesNavParams.g.cpp"
#endif
#if __has_include("PlaybackRequest.g.cpp")
#include "PlaybackRequest.g.cpp"
#endif

#include <algorithm>
#include <utility>

namespace winrt::HaloDesktop::implementation
{
    MediaSummary::MediaSummary(
        hstring id,
        hstring title,
        hstring meta,
        HaloDesktop::MediaKind kind,
        hstring type,
        hstring poster,
        hstring background,
        hstring description,
        hstring rating,
        hstring releaseInfo,
        std::int64_t addedAt,
        std::int64_t updatedAt)
        : m_id(std::move(id)),
          m_type(type.empty() ? (kind == HaloDesktop::MediaKind::Series ? L"series" : L"movie") : std::move(type)),
          m_title(std::move(title)),
          m_meta(std::move(meta)),
          m_poster(std::move(poster)),
          m_background(std::move(background)),
          m_description(std::move(description)),
          m_rating(std::move(rating)),
          m_releaseInfo(std::move(releaseInfo)),
          m_addedAt(addedAt),
          m_updatedAt(updatedAt),
          m_kind(kind)
    {
    }

    hstring MediaSummary::Id() const { return m_id; }
    hstring MediaSummary::Type() const { return m_type; }
    hstring MediaSummary::Title() const { return m_title; }
    hstring MediaSummary::Meta() const { return m_meta; }
    hstring MediaSummary::Poster() const { return m_poster; }
    hstring MediaSummary::Background() const { return m_background; }
    hstring MediaSummary::Description() const { return m_description; }
    hstring MediaSummary::Rating() const { return m_rating; }
    hstring MediaSummary::ReleaseInfo() const { return m_releaseInfo; }
    std::int64_t MediaSummary::AddedAt() const noexcept { return m_addedAt; }
    std::int64_t MediaSummary::UpdatedAt() const noexcept { return m_updatedAt; }
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
        Windows::Foundation::Collections::IVectorView<std::int32_t> seasons,
        hstring type,hstring poster,hstring background)
        : m_id(std::move(id)),
          m_type(std::move(type)),m_poster(std::move(poster)),m_background(std::move(background)),
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
    hstring MediaDetail::Type() const{return m_type;}hstring MediaDetail::Poster()const{return m_poster;}hstring MediaDetail::Background()const{return m_background;}
    hstring MediaDetail::Title() const { return m_title; }
    hstring MediaDetail::Kicker() const { return m_kicker; }
    hstring MediaDetail::MetaLine() const { return m_metaLine; }
    hstring MediaDetail::Synopsis() const { return m_synopsis; }
    Windows::Foundation::Collections::IVectorView<hstring> MediaDetail::Facts() const { return m_facts; }
    Windows::Foundation::Collections::IVectorView<hstring> MediaDetail::Availability() const { return m_availability; }
    Windows::Foundation::Collections::IVectorView<std::int32_t> MediaDetail::Seasons() const { return m_seasons; }

    Episode::Episode(hstring tag, hstring title, hstring blurb, hstring runtime, hstring aired, double progress, bool downloaded,
                     hstring videoId,std::int32_t season,std::int32_t number,hstring thumbnail,bool watched)
        : m_tag(std::move(tag)),
          m_videoId(std::move(videoId)),m_thumbnail(std::move(thumbnail)),m_season(season),m_number(number),m_watched(watched),
          m_title(std::move(title)),
          m_blurb(std::move(blurb)),
          m_runtime(std::move(runtime)),
          m_aired(std::move(aired)),
          m_progress(std::clamp(progress, 0.0, 1.0)),
          m_downloaded(downloaded)
    {
    }

    hstring Episode::Tag() const { return m_tag; }
    hstring Episode::VideoId()const{return m_videoId;}std::int32_t Episode::Season()const noexcept{return m_season;}std::int32_t Episode::Number()const noexcept{return m_number;}hstring Episode::Thumbnail()const{return m_thumbnail;}bool Episode::Watched()const noexcept{return m_watched;}
    hstring Episode::Title() const { return m_title; }
    hstring Episode::Blurb() const { return m_blurb; }
    hstring Episode::Runtime() const { return m_runtime; }
    hstring Episode::Aired() const { return m_aired; }
    double Episode::Progress() const noexcept { return m_progress; }
    bool Episode::Downloaded() const noexcept { return m_downloaded; }

    StreamSource::StreamSource(hstring quality,hstring range,hstring file,hstring codec,hstring audio,hstring languages,HaloDesktop::StreamStatus status,hstring size)
        : StreamSource(L"",std::move(quality),std::move(range),std::move(file),std::move(codec),std::move(audio),std::move(languages),status,std::move(size),L"",0,0,nullptr) {}

    StreamSource::StreamSource(
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
        Windows::Foundation::Collections::IVectorView<hstring> subtitleLanguages)
        : m_key(std::move(key)),
          m_quality(std::move(quality)),
          m_range(std::move(range)),
          m_file(std::move(file)),
          m_codec(std::move(codec)),
          m_audio(std::move(audio)),
          m_languages(std::move(languages)),
          m_status(status),
          m_size(std::move(size)),
          m_detail(std::move(detail)),
          m_sizeBytes(sizeBytes),
          m_rank(rank),
          m_subtitleLanguages(subtitleLanguages
              ? std::move(subtitleLanguages)
              : single_threaded_vector<hstring>().GetView())
    {
    }

    hstring StreamSource::Key() const { return m_key; }
    hstring StreamSource::Quality() const { return m_quality; }
    hstring StreamSource::Range() const { return m_range; }
    hstring StreamSource::File() const { return m_file; }
    hstring StreamSource::Codec() const { return m_codec; }
    hstring StreamSource::Audio() const { return m_audio; }
    hstring StreamSource::Languages() const { return m_languages; }
    HaloDesktop::StreamStatus StreamSource::Status() const noexcept { return m_status; }
    hstring StreamSource::Size() const { return m_size; }
    hstring StreamSource::Detail() const { return m_detail; }
    std::uint64_t StreamSource::SizeBytes() const noexcept { return m_sizeBytes; }
    std::int32_t StreamSource::Rank() const noexcept { return m_rank; }
    Windows::Foundation::Collections::IVectorView<hstring> StreamSource::SubtitleLanguages() const { return m_subtitleLanguages; }

    SourceGroup::SourceGroup(hstring name,hstring note,std::int32_t count,Windows::Foundation::Collections::IVectorView<HaloDesktop::StreamSource> sources)
        : SourceGroup(L"",std::move(name),std::move(note),count,std::move(sources),count > 0) {}

    SourceGroup::SourceGroup(
        hstring addonId,
        hstring name,
        hstring note,
        std::int32_t count,
        Windows::Foundation::Collections::IVectorView<HaloDesktop::StreamSource> sources,
        bool answered)
        : m_addonId(std::move(addonId)), m_name(std::move(name)), m_note(std::move(note)), m_count(count), m_answered(answered), m_sources(std::move(sources))
    {
    }

    hstring SourceGroup::AddonId() const { return m_addonId; }
    hstring SourceGroup::Name() const { return m_name; }
    hstring SourceGroup::Note() const { return m_note; }
    std::int32_t SourceGroup::Count() const noexcept { return m_count; }
    bool SourceGroup::Answered() const noexcept { return m_answered; }
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
        hstring subs,
        hstring videoId,
        hstring poster,
        bool requiresNewSource)
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
          m_subs(std::move(subs)),
          m_videoId(std::move(videoId)),
          m_poster(std::move(poster)),
          m_requiresNewSource(requiresNewSource)
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
    hstring DownloadItem::VideoId() const { return m_videoId; }
    hstring DownloadItem::Poster() const { return m_poster; }
    bool DownloadItem::RequiresNewSource() const noexcept { return m_requiresNewSource; }

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

    ContinueItem::ContinueItem(
        hstring name,
        hstring sub,
        hstring tag,
        hstring timeLeft,
        double progress,
        hstring type,
        hstring metaId,
        hstring videoId,
        hstring itemId,
        hstring poster)
        : m_name(std::move(name)),
          m_type(std::move(type)),
          m_metaId(std::move(metaId)),
          m_videoId(std::move(videoId)),
          m_itemId(std::move(itemId)),
          m_poster(std::move(poster)),
          m_sub(std::move(sub)),
          m_tag(std::move(tag)),
          m_timeLeft(std::move(timeLeft)),
          m_progress(std::clamp(progress, 0.0, 1.0))
    {
    }

    hstring ContinueItem::Name() const { return m_name; }
    hstring ContinueItem::Type() const { return m_type; }
    hstring ContinueItem::MetaId() const { return m_metaId; }
    hstring ContinueItem::VideoId() const { return m_videoId; }
    hstring ContinueItem::ItemId() const { return m_itemId; }
    hstring ContinueItem::Poster() const { return m_poster; }
    hstring ContinueItem::Art() const { return m_still.empty() ? m_poster : m_still; }
    hstring ContinueItem::Sub() const { return m_sub; }
    hstring ContinueItem::Tag() const { return m_tag; }
    hstring ContinueItem::TimeLeft() const { return m_timeLeft; }
    double ContinueItem::Progress() const noexcept { return m_progress; }

    winrt::event_token ContinueItem::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }

    void ContinueItem::PropertyChanged(winrt::event_token const& token) noexcept
    {
        m_propertyChanged.remove(token);
    }

    void ContinueItem::SetStill(hstring value)
    {
        if (m_still == value)
        {
            return;
        }
        m_still = std::move(value);
        m_propertyChanged(*this, Microsoft::UI::Xaml::Data::PropertyChangedEventArgs{ L"Art" });
    }

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

    DetailNavParams::DetailNavParams(hstring type,hstring metaId,hstring title,hstring poster):m_type(std::move(type)),m_metaId(std::move(metaId)),m_title(std::move(title)),m_poster(std::move(poster)){}
    hstring DetailNavParams::Type()const{return m_type;}hstring DetailNavParams::MetaId()const{return m_metaId;}hstring DetailNavParams::Title()const{return m_title;}hstring DetailNavParams::Poster()const{return m_poster;}
    SourcesNavParams::SourcesNavParams(hstring type,hstring metaId,hstring videoId,hstring itemId,hstring title,hstring showName,hstring episodeLabel,hstring poster):m_type(std::move(type)),m_metaId(std::move(metaId)),m_videoId(std::move(videoId)),m_itemId(std::move(itemId)),m_title(std::move(title)),m_showName(std::move(showName)),m_episodeLabel(std::move(episodeLabel)),m_poster(std::move(poster)){}
    hstring SourcesNavParams::Type()const{return m_type;}hstring SourcesNavParams::MetaId()const{return m_metaId;}hstring SourcesNavParams::VideoId()const{return m_videoId;}hstring SourcesNavParams::ItemId()const{return m_itemId;}hstring SourcesNavParams::Title()const{return m_title;}hstring SourcesNavParams::ShowName()const{return m_showName;}hstring SourcesNavParams::EpisodeLabel()const{return m_episodeLabel;}hstring SourcesNavParams::Poster()const{return m_poster;}

    PlaybackRequest::PlaybackRequest(hstring url,bool isLocalFile,hstring downloadId,hstring subtitleLang,hstring mediaType,hstring videoId,hstring itemId,hstring metaId,hstring title,hstring showName,hstring episodeLabel,hstring poster,hstring addonId,hstring bingeGroup,hstring filename,std::uint64_t videoSize,hstring videoHash,hstring sourceTagLine,std::vector<std::pair<hstring,hstring>> requestHeaders):m_url(std::move(url)),m_downloadId(std::move(downloadId)),m_subtitleLang(std::move(subtitleLang)),m_mediaType(std::move(mediaType)),m_videoId(std::move(videoId)),m_itemId(std::move(itemId)),m_metaId(std::move(metaId)),m_title(std::move(title)),m_showName(std::move(showName)),m_episodeLabel(std::move(episodeLabel)),m_poster(std::move(poster)),m_addonId(std::move(addonId)),m_bingeGroup(std::move(bingeGroup)),m_filename(std::move(filename)),m_videoHash(std::move(videoHash)),m_sourceTagLine(std::move(sourceTagLine)),m_requestHeaders(std::move(requestHeaders)),m_isLocalFile(isLocalFile),m_videoSize(videoSize){}
    hstring PlaybackRequest::Url()const{return m_url;}bool PlaybackRequest::IsLocalFile()const noexcept{return m_isLocalFile;}hstring PlaybackRequest::DownloadId()const{return m_downloadId;}hstring PlaybackRequest::SubtitleLang()const{return m_subtitleLang;}hstring PlaybackRequest::MediaType()const{return m_mediaType;}hstring PlaybackRequest::VideoId()const{return m_videoId;}hstring PlaybackRequest::ItemId()const{return m_itemId;}hstring PlaybackRequest::MetaId()const{return m_metaId;}hstring PlaybackRequest::Title()const{return m_title;}hstring PlaybackRequest::ShowName()const{return m_showName;}hstring PlaybackRequest::EpisodeLabel()const{return m_episodeLabel;}hstring PlaybackRequest::Poster()const{return m_poster;}hstring PlaybackRequest::AddonId()const{return m_addonId;}hstring PlaybackRequest::BingeGroup()const{return m_bingeGroup;}hstring PlaybackRequest::Filename()const{return m_filename;}std::uint64_t PlaybackRequest::VideoSize()const noexcept{return m_videoSize;}hstring PlaybackRequest::VideoHash()const{return m_videoHash;}hstring PlaybackRequest::SourceTagLine()const{return m_sourceTagLine;}std::vector<std::pair<hstring,hstring>> const& PlaybackRequest::RequestHeaders()const noexcept{return m_requestHeaders;}
}
