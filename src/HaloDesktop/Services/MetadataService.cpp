#include "pch.h"
#include "Services/MetadataService.h"
#include "Api/ApiClient.h"
#include "Models/Models.h"
#include "Services/WatchStateService.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>
namespace
{
    winrt::hstring Tag(std::int32_t s,std::int32_t e){std::wostringstream o;o<<L'S'<<std::setw(2)<<std::setfill(L'0')<<s<<L'E'<<std::setw(2)<<e;return winrt::hstring{o.str()};}
    winrt::hstring Join(std::vector<winrt::hstring>const&v){std::wstring r;for(auto const&i:v){if(!r.empty())r.append(L", ");r.append(i);}return winrt::hstring{r};}
}
namespace HaloDesktop::Services
{
    MetadataService::MetadataService(std::shared_ptr<::HaloDesktop::Api::ApiClient>a,std::shared_ptr<WatchStateService>w):m_api(std::move(a)),m_watch(std::move(w)){if(!m_api||!m_watch)throw std::invalid_argument{"MetadataService requires dependencies."};}
    concurrency::task<void> MetadataService::LoadAsync(winrt::hstring type,winrt::hstring metaId)
    {auto const ui=winrt::apartment_context{};auto meta=co_await m_api->GetMetaAsync(type,metaId);co_await m_watch->LoadAsync();co_await ui;std::vector<std::int32_t>seasons;for(auto const&v:meta.Videos)if(v.Season&&std::find(seasons.begin(),seasons.end(),*v.Season)==seasons.end())seasons.push_back(*v.Season);std::sort(seasons.begin(),seasons.end(),[](auto a,auto b){if(a==0)return false;if(b==0)return true;return a<b;});std::vector<winrt::hstring>facts;if(meta.Runtime)facts.push_back(L"Runtime · "+*meta.Runtime);if(!meta.Genres.empty())facts.push_back(L"Genres · "+Join(meta.Genres));if(!meta.Cast.empty())facts.push_back(L"Cast · "+Join(meta.Cast));m_detail=winrt::make<winrt::HaloDesktop::implementation::MediaDetail>(meta.Preview.Id,meta.Preview.Name,type==L"series"?L"SERIES":L"MOVIE",meta.Preview.ReleaseInfo.value_or(L"")+L" · ★ "+meta.Preview.Rating.value_or(L""),meta.Preview.Description.value_or(L""),winrt::single_threaded_vector(std::move(facts)).GetView(),winrt::single_threaded_vector<winrt::hstring>({L"Sources resolved on demand"}).GetView(),winrt::single_threaded_vector(std::move(seasons)).GetView(),type,meta.Preview.Poster.value_or(L""),meta.Preview.Background.value_or(L""));m_episodes.clear();auto watch=m_watch->Rows();for(auto const&v:meta.Videos){auto const s=v.Season.value_or(0),e=v.Episode.value_or(0);auto found=std::find_if(watch.begin(),watch.end(),[&](auto const&r){return r.VideoId==v.Id;});double progress{};bool watched{};if(found!=watch.end()&&found->DurationSec>0){progress=found->PositionSec/found->DurationSec;watched=found->Watched;}m_episodes.push_back(winrt::make<winrt::HaloDesktop::implementation::Episode>(Tag(s,e),v.Title,v.Overview.value_or(L""),meta.Runtime.value_or(L""),v.Released.value_or(L""),watched?0.0:progress,false,v.Id,s,e,v.Thumbnail.value_or(L""),watched));}std::sort(m_episodes.begin(),m_episodes.end(),[](auto const&a,auto const&b){return a.Season()==b.Season()?a.Number()<b.Number():(a.Season()==0?false:b.Season()==0?true:a.Season()<b.Season());});}
    winrt::HaloDesktop::MediaDetail MetadataService::Detail()const{return m_detail;}
    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Episode> MetadataService::Episodes(std::int32_t season)const{std::vector<winrt::HaloDesktop::Episode>r;for(auto const&e:m_episodes)if(e.Season()==season)r.push_back(e);return winrt::single_threaded_vector(std::move(r)).GetView();}
}
