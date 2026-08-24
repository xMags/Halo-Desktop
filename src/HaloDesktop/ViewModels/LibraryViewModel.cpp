#include "pch.h"
#include "ViewModels/LibraryViewModel.h"
#if __has_include("LibraryViewModel.g.cpp")
#include "LibraryViewModel.g.cpp"
#endif
#include "Services/NavigationService.h"
#include "Models/Models.h"
#include "ViewModels/ObservableHelper.h"
#include <algorithm>
namespace { auto const Visible=winrt::Microsoft::UI::Xaml::Visibility::Visible; auto const Collapsed=winrt::Microsoft::UI::Xaml::Visibility::Collapsed; }
namespace winrt::HaloDesktop::implementation
{
    LibraryViewModel::LibraryViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_catalog(services.Catalog), m_navigation(services.Navigation), m_items(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()) { static_cast<void>(LoadAsync()); }
    winrt::Windows::Foundation::IInspectable LibraryViewModel::Items() const { return m_items; }
    std::int32_t LibraryViewModel::FilterIndex() const noexcept { return m_filterIndex; }
    std::int32_t LibraryViewModel::SortIndex() const noexcept { return m_sortIndex; }
    winrt::hstring LibraryViewModel::AllLabel() const { return L"All " + winrt::to_hstring(m_sourceItems.size()); }
    winrt::hstring LibraryViewModel::MoviesLabel() const { return L"Movies " + winrt::to_hstring(std::count_if(m_sourceItems.begin(),m_sourceItems.end(),[](auto const&i){return i.Kind()==winrt::HaloDesktop::MediaKind::Movie;})); }
    winrt::hstring LibraryViewModel::SeriesLabel() const { return L"Series " + winrt::to_hstring(std::count_if(m_sourceItems.begin(),m_sourceItems.end(),[](auto const&i){return i.Kind()==winrt::HaloDesktop::MediaKind::Series;})); }
    Microsoft::UI::Xaml::Visibility LibraryViewModel::ContentVisibility() const noexcept { return !m_loading&&!m_error&&!m_sourceItems.empty()?Visible:Collapsed; }
    Microsoft::UI::Xaml::Visibility LibraryViewModel::LoadingVisibility() const noexcept { return m_loading?Visible:Collapsed; }
    Microsoft::UI::Xaml::Visibility LibraryViewModel::ErrorVisibility() const noexcept { return m_error?Visible:Collapsed; }
    Microsoft::UI::Xaml::Visibility LibraryViewModel::EmptyVisibility() const noexcept { return !m_loading&&!m_error&&m_sourceItems.empty()?Visible:Collapsed; }
    void LibraryViewModel::SetFilter(std::int32_t index){if(index>=0&&index<=2&&index!=m_filterIndex){m_filterIndex=index;Rebuild();Raise(L"FilterIndex");}}
    void LibraryViewModel::SetSort(std::int32_t index){if(index>=0&&index<=2&&index!=m_sortIndex){m_sortIndex=index;Rebuild();Raise(L"SortIndex");}}
    void LibraryViewModel::Retry(){static_cast<void>(LoadAsync());}
    void LibraryViewModel::OpenDetail(winrt::Windows::Foundation::IInspectable const& item){if(item){auto media=item.as<winrt::HaloDesktop::MediaSummary>();m_navigation->GoTo(::HaloDesktop::Services::Page::Detail,winrt::make<winrt::HaloDesktop::implementation::DetailNavParams>(media.Type(),media.Id(),media.Title(),media.Poster()));}}
    winrt::event_token LibraryViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler){return m_propertyChanged.add(handler);}
    void LibraryViewModel::PropertyChanged(winrt::event_token const& token)noexcept{m_propertyChanged.remove(token);}
    winrt::Windows::Foundation::IAsyncAction LibraryViewModel::LoadAsync()
    {
        auto lifetime=get_strong();auto const uiContext=winrt::apartment_context{};auto const version=++m_loadVersion;m_loading=true;m_error=false;RaiseState();bool failed{};
        try{co_await m_catalog->LoadAsync();}catch(...){failed=true;}co_await uiContext;if(version!=m_loadVersion)co_return;m_loading=false;m_error=failed;
        if(!failed){m_sourceItems.clear();for(auto const&item:m_catalog->LibraryItems())m_sourceItems.push_back(item);Rebuild();}RaiseState();
    }
    void LibraryViewModel::Rebuild()
    {
        auto items=m_sourceItems;std::erase_if(items,[this](auto const&i){return m_filterIndex==1&&i.Kind()!=winrt::HaloDesktop::MediaKind::Movie||m_filterIndex==2&&i.Kind()!=winrt::HaloDesktop::MediaKind::Series;});
        if(m_sortIndex==0)std::sort(items.begin(),items.end(),[](auto const&a,auto const&b){return a.AddedAt()>b.AddedAt();});
        else if(m_sortIndex==1)std::sort(items.begin(),items.end(),[](auto const&a,auto const&b){return a.Title()<b.Title();});
        else std::sort(items.begin(),items.end(),[](auto const&a,auto const&b){return a.UpdatedAt()>b.UpdatedAt();});
        m_items.Clear();for(auto const&i:items)m_items.Append(i);Raise(L"Items");
    }
    void LibraryViewModel::RaiseState(){for(auto const n:{L"Items",L"AllLabel",L"MoviesLabel",L"SeriesLabel",L"ContentVisibility",L"LoadingVisibility",L"ErrorVisibility",L"EmptyVisibility"})Raise(n);}
    void LibraryViewModel::Raise(wchar_t const*n){::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged,*this,n);}
}
