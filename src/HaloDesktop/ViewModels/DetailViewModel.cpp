#include "pch.h"
#include "ViewModels/DetailViewModel.h"
#if __has_include("DetailEpisodeViewModel.g.cpp")
#include "DetailEpisodeViewModel.g.cpp"
#endif
#if __has_include("DetailViewModel.g.cpp")
#include "DetailViewModel.g.cpp"
#endif
#include "Models/Models.h"
#include "Services/LibraryService.h"
#include "Services/NavigationService.h"
#include "Services/WatchStateService.h"
#include "ViewModels/ObservableHelper.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
namespace{auto const Visible=winrt::Microsoft::UI::Xaml::Visibility::Visible;auto const Collapsed=winrt::Microsoft::UI::Xaml::Visibility::Collapsed;}
namespace winrt::HaloDesktop::implementation
{
    DetailEpisodeViewModel::DetailEpisodeViewModel(winrt::HaloDesktop::Episode e):m_episode(std::move(e)){}winrt::hstring DetailEpisodeViewModel::Tag()const{return m_episode.Tag();}winrt::hstring DetailEpisodeViewModel::Title()const{return m_episode.Title();}winrt::hstring DetailEpisodeViewModel::Blurb()const{return m_episode.Blurb();}winrt::hstring DetailEpisodeViewModel::Runtime()const{return m_episode.Runtime();}winrt::hstring DetailEpisodeViewModel::Aired()const{return m_episode.Aired();}winrt::hstring DetailEpisodeViewModel::VideoId()const{return m_episode.VideoId();}winrt::hstring DetailEpisodeViewModel::Thumbnail()const{return m_episode.Thumbnail();}double DetailEpisodeViewModel::Progress()const noexcept{return m_episode.Progress();}auto DetailEpisodeViewModel::SavedVisibility()const noexcept->Microsoft::UI::Xaml::Visibility{return m_episode.Downloaded()?Visible:Collapsed;}auto DetailEpisodeViewModel::WatchedVisibility()const noexcept->Microsoft::UI::Xaml::Visibility{return m_episode.Watched()?Visible:Collapsed;}auto DetailEpisodeViewModel::InProgressVisibility()const noexcept->Microsoft::UI::Xaml::Visibility{return !m_episode.Watched()&&m_episode.Progress()>0.02?Visible:Collapsed;}auto DetailEpisodeViewModel::IdleVisibility()const noexcept->Microsoft::UI::Xaml::Visibility{return m_episode.Watched()||m_episode.Progress()>0.02?Collapsed:Visible;}winrt::HaloDesktop::Episode DetailEpisodeViewModel::Episode()const{return m_episode;}
    DetailViewModel::DetailViewModel(::HaloDesktop::Services::AppServices const&s):m_metadata(s.Metadata),m_library(s.Library),m_catalog(s.Catalog),m_navigation(s.Navigation),m_downloads(s.Downloads),m_watch(s.WatchState),m_episodes(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),m_facts(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),m_availability(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),m_seasons(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()){}
    DetailViewModel::~DetailViewModel(){Deactivate();}void DetailViewModel::Activate(){if(m_downloadToken)return;m_downloadToken=m_downloads->AddChangedHandler([weak=get_weak()](){if(auto self=weak.get())self->RebuildEpisodes();});}void DetailViewModel::Deactivate()noexcept{if(!m_downloadToken)return;m_downloads->RemoveChangedHandler(m_downloadToken);m_downloadToken=0;}
    winrt::hstring DetailViewModel::Title()const{return m_detail?m_detail.Title():(m_params?m_params.Title():L"");}winrt::hstring DetailViewModel::Kicker()const{return m_detail?m_detail.Kicker():L"";}winrt::hstring DetailViewModel::MetaLine()const{return m_detail?m_detail.MetaLine():L"";}winrt::hstring DetailViewModel::Synopsis()const{return m_detail?m_detail.Synopsis():L"";}winrt::hstring DetailViewModel::Poster()const{return m_detail&&!m_detail.Poster().empty()?m_detail.Poster():(m_params?m_params.Poster():L"");}winrt::hstring DetailViewModel::Background()const{return m_detail&&!m_detail.Background().empty()?m_detail.Background():Poster();}winrt::hstring DetailViewModel::PrimaryActionLabel()const{return m_primaryActionLabel;}winrt::hstring DetailViewModel::SourceActionLabel()const{return m_params&&m_params.Type()==L"movie"?L"Choose a source":L"Choose episode source";}winrt::hstring DetailViewModel::SeasonMeta()const{return m_params&&m_params.Type()==L"series"?winrt::to_hstring(m_episodes.Size())+L" EPISODES":L"";}winrt::hstring DetailViewModel::LibraryLabel()const{return m_inLibrary?L"In library":L"Add to library";}std::int32_t DetailViewModel::SeasonIndex()const noexcept{return m_seasonIndex;}
    winrt::Windows::Foundation::IInspectable DetailViewModel::Episodes()const{return m_episodes;}winrt::Windows::Foundation::IInspectable DetailViewModel::Facts()const{return m_facts;}winrt::Windows::Foundation::IInspectable DetailViewModel::Availability()const{return m_availability;}winrt::Windows::Foundation::IInspectable DetailViewModel::Seasons()const{return m_seasons;}
    auto DetailViewModel::ContentVisibility()const noexcept->Microsoft::UI::Xaml::Visibility{return !m_loading&&!m_error&&m_detail?Visible:Collapsed;}auto DetailViewModel::LoadingVisibility()const noexcept->Microsoft::UI::Xaml::Visibility{return m_loading?Visible:Collapsed;}auto DetailViewModel::ErrorVisibility()const noexcept->Microsoft::UI::Xaml::Visibility{return m_error?Visible:Collapsed;}auto DetailViewModel::EpisodeSectionVisibility()const noexcept->Microsoft::UI::Xaml::Visibility{return m_params&&m_params.Type()==L"series"&&!m_seasonValues.empty()?Visible:Collapsed;}
    void DetailViewModel::Load(winrt::Windows::Foundation::IInspectable const&p){if(auto direct=p.try_as<winrt::HaloDesktop::DetailNavParams>())m_params=direct;else if(auto media=p.try_as<winrt::HaloDesktop::MediaSummary>())m_params=winrt::make<implementation::DetailNavParams>(media.Type(),media.Id(),media.Title(),media.Poster());if(m_params)static_cast<void>(LoadAsync());}
    void DetailViewModel::SelectSeason(std::int32_t i){if(i>=0&&static_cast<std::size_t>(i)<m_seasonValues.size()&&i!=m_seasonIndex){m_seasonIndex=i;RebuildEpisodes();Raise(L"SeasonIndex");}}
    void DetailViewModel::Retry(){static_cast<void>(LoadAsync());}void DetailViewModel::ToggleLibrary(){static_cast<void>(ToggleLibraryAsync());}
    void DetailViewModel::OpenSources(winrt::Windows::Foundation::IInspectable const&e){auto vm=e.try_as<winrt::HaloDesktop::DetailEpisodeViewModel>();auto episode=vm?winrt::get_self<DetailEpisodeViewModel>(vm)->Episode():nullptr;if(episode)OpenEpisodeSources(episode);}
    void DetailViewModel::OpenPrimarySource(){if(m_primaryEpisode)OpenEpisodeSources(m_primaryEpisode);else BrowseSources();}
    void DetailViewModel::BrowseSources(){if(!m_params)return;if(m_params.Type()==L"movie")m_navigation->GoTo(::HaloDesktop::Services::Page::Sources,winrt::make<implementation::SourcesNavParams>(m_params.Type(),m_params.MetaId(),m_params.MetaId(),L"movie:"+m_params.MetaId(),Title(),Title(),L"",Poster()));else if(m_episodes.Size())OpenSources(m_episodes.GetAt(0));}
    void DetailViewModel::OpenDownloads(){m_navigation->GoTo(::HaloDesktop::Services::Page::Downloads);}winrt::event_token DetailViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const&h){return m_propertyChanged.add(h);}void DetailViewModel::PropertyChanged(winrt::event_token const&t)noexcept{m_propertyChanged.remove(t);}
    winrt::Windows::Foundation::IAsyncAction DetailViewModel::LoadAsync(){auto lifetime=get_strong();auto const ui=winrt::apartment_context{};auto const version=++m_loadVersion;m_loading=true;m_error=false;RaiseState();bool failed{};try{co_await m_metadata->LoadAsync(m_params.Type(),m_params.MetaId());co_await m_library->LoadAsync();}catch(...){failed=true;}co_await ui;if(version!=m_loadVersion)co_return;m_loading=false;m_error=failed;if(!failed){m_detail=m_metadata->Detail();m_inLibrary=m_library->Contains(m_params.Type(),m_params.MetaId());m_facts.Clear();for(auto const&x:m_detail.Facts())m_facts.Append(winrt::box_value(x));m_availability.Clear();for(auto const&x:m_detail.Availability())m_availability.Append(winrt::box_value(x));m_seasonValues.clear();m_seasons.Clear();for(auto const&s:m_detail.Seasons()){m_seasonValues.push_back(s);m_seasons.Append(winrt::box_value(s==0?winrt::hstring{L"Specials"}:L"Season "+winrt::to_hstring(s)));}m_seasonIndex=0;RebuildEpisodes();UpdatePrimaryAction();}RaiseState();}
    winrt::Windows::Foundation::IAsyncAction DetailViewModel::ToggleLibraryAsync(){auto lifetime=get_strong();if(!m_params)co_return;auto const ui=winrt::apartment_context{};auto const next=!m_inLibrary;try{co_await m_library->SetMembershipAsync(m_params.Type(),m_params.MetaId(),Title(),m_params.Poster().empty()?std::nullopt:std::optional<winrt::hstring>{m_params.Poster()},next);co_await ui;m_inLibrary=next;m_catalog->RebuildLibrary();Raise(L"LibraryLabel");}catch(...){}}
    void DetailViewModel::RebuildEpisodes(){m_episodes.Clear();if(m_seasonValues.empty())return;for(auto const&e:m_metadata->Episodes(m_seasonValues[m_seasonIndex]))m_episodes.Append(winrt::make<DetailEpisodeViewModel>(e));Raise(L"Episodes");Raise(L"SeasonMeta");}
    void DetailViewModel::OpenEpisodeSources(winrt::HaloDesktop::Episode const&episode){if(!m_params||!episode)return;auto itemId=m_params.Type()+L":"+m_params.MetaId();m_navigation->GoTo(::HaloDesktop::Services::Page::Sources,winrt::make<implementation::SourcesNavParams>(m_params.Type(),m_params.MetaId(),episode.VideoId(),itemId,episode.Title(),Title(),episode.Tag(),Poster()));}
    void DetailViewModel::UpdatePrimaryAction()
    {
        m_primaryEpisode = nullptr;
        m_primaryActionLabel = L"Choose source";
        if (!m_params)
        {
            return;
        }
        if (m_params.Type() == L"movie")
        {
            auto const row = m_watch->Find(m_params.MetaId());
            if (row && !row->Watched && row->DurationSec > 0)
            {
                auto const fraction = row->PositionSec / row->DurationSec;
                if (fraction > 0.02 && fraction < 0.95)
                {
                    auto const minutes = (std::max)(
                        1,
                        static_cast<int>(std::ceil((row->DurationSec - row->PositionSec) / 60.0)));
                    m_primaryActionLabel = L"Resume · " + winrt::to_hstring(minutes) + L" min left";
                }
            }
            return;
        }

        m_primaryActionLabel = L"Choose episode";
        auto const itemId = L"series:" + m_params.MetaId();
        std::optional<::HaloDesktop::Api::Dto::WatchEntry> latest;
        for (auto const& row : m_watch->Rows())
        {
            if (row.ItemId != itemId || row.Watched || row.DurationSec <= 0)
            {
                continue;
            }
            auto const fraction = row.PositionSec / row.DurationSec;
            if (fraction <= 0.02 || fraction >= 0.95
                || (latest && row.UpdatedAt <= latest->UpdatedAt))
            {
                continue;
            }
            latest = row;
        }
        if (!latest)
        {
            return;
        }
        for (auto const season : m_seasonValues)
        {
            for (auto const& episode : m_metadata->Episodes(season))
            {
                if (episode.VideoId() != latest->VideoId)
                {
                    continue;
                }
                m_primaryEpisode = episode;
                auto const minutes = (std::max)(
                    1,
                    static_cast<int>(std::ceil((latest->DurationSec - latest->PositionSec) / 60.0)));
                m_primaryActionLabel = L"Resume " + episode.Tag() + L" · "
                    + winrt::to_hstring(minutes) + L" min left";
                return;
            }
        }
    }
    void DetailViewModel::RaiseState(){for(auto const n:{L"Title",L"Kicker",L"MetaLine",L"Synopsis",L"Poster",L"Background",L"PrimaryActionLabel",L"SourceActionLabel",L"Episodes",L"Facts",L"Availability",L"Seasons",L"SeasonIndex",L"SeasonMeta",L"LibraryLabel",L"ContentVisibility",L"LoadingVisibility",L"ErrorVisibility",L"EpisodeSectionVisibility"})Raise(n);}void DetailViewModel::Raise(wchar_t const*n){::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged,*this,n);}
}
