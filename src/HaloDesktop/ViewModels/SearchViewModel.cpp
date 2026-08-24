#include "pch.h"
#include "ViewModels/SearchViewModel.h"
#if __has_include("RecentSearchViewModel.g.cpp")
#include "RecentSearchViewModel.g.cpp"
#endif
#if __has_include("SearchViewModel.g.cpp")
#include "SearchViewModel.g.cpp"
#endif
#include "Models/Models.h"
#include "Services/CatalogService.h"
#include "Services/NavigationService.h"
#include "ViewModels/ObservableHelper.h"
#include <chrono>
#include <utility>
#include <vector>
namespace { auto const Visible=winrt::Microsoft::UI::Xaml::Visibility::Visible;auto const Collapsed=winrt::Microsoft::UI::Xaml::Visibility::Collapsed; }
namespace winrt::HaloDesktop::implementation
{
    RecentSearchViewModel::RecentSearchViewModel(winrt::hstring term,winrt::hstring age):m_term(std::move(term)),m_age(std::move(age)){} winrt::hstring RecentSearchViewModel::Term()const{return m_term;}winrt::hstring RecentSearchViewModel::Age()const{return m_age;}
    SearchViewModel::SearchViewModel(::HaloDesktop::Services::AppServices const& services)
        :m_catalog(services.Catalog),m_navigation(services.Navigation),m_debounceTimer(Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer()),m_results(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),m_recentItems(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>())
    {m_debounceTimer.Interval(std::chrono::milliseconds{350});m_debounceTimer.IsRepeating(false);m_debounceTimer.Tick([weak=get_weak()](auto const&,auto const&){if(auto self=weak.get())static_cast<void>(self->SearchAsync(false));});LoadRecents();}
    winrt::hstring SearchViewModel::Query()const{return m_query;}
    void SearchViewModel::Query(winrt::hstring const&value){if(m_query==value)return;m_query=value;Raise(L"Query");m_debounceTimer.Stop();if(m_query.size()>=2)m_debounceTimer.Start();else{++m_searchVersion;m_results.Clear();m_topMatch=nullptr;m_movieCount=m_seriesCount=0;RaiseState();}}
    winrt::Windows::Foundation::IInspectable SearchViewModel::Results()const{return m_results;}winrt::Windows::Foundation::IInspectable SearchViewModel::RecentItems()const{return m_recentItems;}
    std::int32_t SearchViewModel::AllCount()const noexcept{return m_movieCount+m_seriesCount;}std::int32_t SearchViewModel::MovieCount()const noexcept{return m_movieCount;}std::int32_t SearchViewModel::SeriesCount()const noexcept{return m_seriesCount;}
    winrt::hstring SearchViewModel::TopMatchTitle()const{return m_topMatch?m_topMatch.Title():L"";}winrt::hstring SearchViewModel::TopMatchMeta()const{return m_topMatch?m_topMatch.KindLabel()+L" · "+m_topMatch.Meta():L"";}winrt::hstring SearchViewModel::TopMatchSynopsis()const{return m_topMatch?m_topMatch.Description():L"";}winrt::hstring SearchViewModel::TopMatchPoster()const{return m_topMatch?m_topMatch.Poster():L"";}
    Microsoft::UI::Xaml::Visibility SearchViewModel::TopMatchVisibility()const noexcept{return m_topMatch&&m_filterIndex!=3?Visible:Collapsed;}Microsoft::UI::Xaml::Visibility SearchViewModel::ResultsVisibility()const noexcept{return !m_loading&&!m_error&&m_results.Size()>0?Visible:Collapsed;}Microsoft::UI::Xaml::Visibility SearchViewModel::RecentVisibility()const noexcept{return m_recentItems.Size()>0?Visible:Collapsed;}Microsoft::UI::Xaml::Visibility SearchViewModel::LoadingVisibility()const noexcept{return m_loading?Visible:Collapsed;}Microsoft::UI::Xaml::Visibility SearchViewModel::ErrorVisibility()const noexcept{return m_error?Visible:Collapsed;}Microsoft::UI::Xaml::Visibility SearchViewModel::EmptyVisibility()const noexcept{return !m_loading&&!m_error&&m_query.size()>=2&&m_results.Size()==0?Visible:Collapsed;}
    void SearchViewModel::SetFilter(std::int32_t index){if(index>=0&&index<=3&&index!=m_filterIndex){m_filterIndex=index;Rebuild();}}
    void SearchViewModel::Submit(winrt::hstring const&query){m_query=query;Raise(L"Query");m_debounceTimer.Stop();static_cast<void>(SearchAsync(true));}
    void SearchViewModel::Clear(){Query(L"");}void SearchViewModel::Retry(){static_cast<void>(SearchAsync(false));}
    void SearchViewModel::OpenDetail(winrt::Windows::Foundation::IInspectable const&item){if(item){m_catalog->RecordRecent(m_query);LoadRecents();auto media=item.as<winrt::HaloDesktop::MediaSummary>();m_navigation->GoTo(::HaloDesktop::Services::Page::Detail,winrt::make<winrt::HaloDesktop::implementation::DetailNavParams>(media.Type(),media.Id(),media.Title(),media.Poster()));}}
    void SearchViewModel::OpenTopMatch(){OpenDetail(m_topMatch);}winrt::event_token SearchViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const&h){return m_propertyChanged.add(h);}void SearchViewModel::PropertyChanged(winrt::event_token const&t)noexcept{m_propertyChanged.remove(t);}
    winrt::Windows::Foundation::IAsyncAction SearchViewModel::SearchAsync(bool deliberate)
    {auto lifetime=get_strong();auto const uiContext=winrt::apartment_context{};auto const version=++m_searchVersion;if(m_query.size()<2){RaiseState();co_return;}m_loading=true;m_error=false;RaiseState();bool failed{};try{co_await m_catalog->SearchAsync(m_query);}catch(...){failed=true;}co_await uiContext;if(version!=m_searchVersion)co_return;m_loading=false;m_error=failed;if(!failed){if(deliberate)m_catalog->RecordRecent(m_query);LoadRecents();Rebuild();}RaiseState();}
    void SearchViewModel::Rebuild(){m_results.Clear();m_movieCount=m_seriesCount=0;m_topMatch=nullptr;for(auto const&group:m_catalog->SearchResults()){std::vector<winrt::HaloDesktop::MediaSummary>items;for(auto const&i:group.Items()){if(i.Kind()==winrt::HaloDesktop::MediaKind::Movie)++m_movieCount;else++m_seriesCount;if(!m_topMatch)m_topMatch=i;if(m_filterIndex==0||(m_filterIndex==1&&i.Kind()==winrt::HaloDesktop::MediaKind::Movie)||(m_filterIndex==2&&i.Kind()==winrt::HaloDesktop::MediaKind::Series))items.push_back(i);}if(!items.empty()&&m_filterIndex!=3)m_results.Append(winrt::make<winrt::HaloDesktop::implementation::SearchGroup>(group.Title(),group.SourceLabel(),winrt::single_threaded_vector(std::move(items)).GetView()));}RaiseState();}
    void SearchViewModel::LoadRecents(){m_recentItems.Clear();for(auto const&t:m_catalog->RecentTerms())m_recentItems.Append(winrt::make<RecentSearchViewModel>(t,L"RECENT"));Raise(L"RecentItems");Raise(L"RecentVisibility");}
    void SearchViewModel::RaiseState(){for(auto const n:{L"Results",L"AllCount",L"MovieCount",L"SeriesCount",L"TopMatchTitle",L"TopMatchMeta",L"TopMatchSynopsis",L"TopMatchPoster",L"TopMatchVisibility",L"ResultsVisibility",L"RecentVisibility",L"LoadingVisibility",L"ErrorVisibility",L"EmptyVisibility"})Raise(n);}void SearchViewModel::Raise(wchar_t const*n){::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged,*this,n);}
}
