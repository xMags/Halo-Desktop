#include "pch.h"
#include "ViewModels/LibraryViewModel.h"
#if __has_include("LibraryViewModel.g.cpp")
#include "LibraryViewModel.g.cpp"
#endif
#include "Services/NavigationService.h"
#include "Models/Models.h"
#include "ViewModels/ObservableHelper.h"
#include <algorithm>
#include <string_view>
namespace
{
    auto const Visible=winrt::Microsoft::UI::Xaml::Visibility::Visible; auto const Collapsed=winrt::Microsoft::UI::Xaml::Visibility::Collapsed;

    // The reference counts and filters by the saved type itself, so a title of
    // any other type shows under All only.
    bool IsType(winrt::HaloDesktop::MediaSummary const& item, std::wstring_view type)
    {
        return std::wstring_view{ item.Type() } == type;
    }

    // `a.name.localeCompare(b.name)`: the viewer's own collation, so case and
    // accents sort as people expect rather than by code unit.
    bool NameBefore(winrt::hstring const& first, winrt::hstring const& second)
    {
        auto const result = CompareStringEx(
            LOCALE_NAME_USER_DEFAULT, 0,
            first.c_str(), static_cast<int>(first.size()),
            second.c_str(), static_cast<int>(second.size()),
            nullptr, nullptr, 0);
        if (result == 0) return first < second;
        return result == CSTR_LESS_THAN;
    }
}
namespace winrt::HaloDesktop::implementation
{
    LibraryViewModel::LibraryViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_catalog(services.Catalog), m_session(services.Session), m_navigation(services.Navigation), m_items(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>())
    {
        // A poster's right-click menu can add or remove a title while the grid is
        // on screen, and the account can change under a page kept in the cache.
        // Raw captures rather than weak references, because get_weak has nothing
        // to hand out this early in construction; the destructor always removes
        // the handlers, and the services only call them on this thread.
        if (m_catalog) m_userStateToken = m_catalog->AddUserStateChangedHandler([this]() { ApplyUserStateChange(); });
        if (m_session) m_identityToken = m_session->AddIdentityChangedHandler([this]() { ReloadForAccount(); });
    }
    LibraryViewModel::~LibraryViewModel()
    {
        if (m_catalog && m_userStateToken != 0) m_catalog->RemoveUserStateChangedHandler(m_userStateToken);
        if (m_session && m_identityToken != 0) m_session->RemoveIdentityChangedHandler(m_identityToken);
    }
    // The catalog has already rebuilt its library projection. A load in flight or
    // a failed one owns what the page shows, and replaces it when it finishes.
    void LibraryViewModel::ApplyUserStateChange()
    {
        if (m_loading || m_error) return;
        AdoptLibrary();
        RaiseState();
    }
    // The services were reset for the new account before this runs, so what is
    // shown belongs to the account that left; it goes at once, and a load still
    // out for that account can no longer land.
    void LibraryViewModel::ReloadForAccount()
    {
        ++m_loadVersion;
        m_sourceItems.clear();
        m_items.Clear();
        m_loading = true;
        m_error = false;
        RaiseState();
        if (m_session->IsSignedIn()) static_cast<void>(LoadAsync());
    }
    winrt::Windows::Foundation::IInspectable LibraryViewModel::Items() const { return m_items; }
    std::int32_t LibraryViewModel::FilterIndex() const noexcept { return m_filterIndex; }
    std::int32_t LibraryViewModel::SortIndex() const noexcept { return m_sortIndex; }
    winrt::hstring LibraryViewModel::AllLabel() const { return L"All " + winrt::to_hstring(m_sourceItems.size()); }
    winrt::hstring LibraryViewModel::MoviesLabel() const { return L"Movies " + winrt::to_hstring(std::count_if(m_sourceItems.begin(),m_sourceItems.end(),[](auto const&i){return IsType(i, L"movie");})); }
    winrt::hstring LibraryViewModel::SeriesLabel() const { return L"Series " + winrt::to_hstring(std::count_if(m_sourceItems.begin(),m_sourceItems.end(),[](auto const&i){return IsType(i, L"series");})); }
    Microsoft::UI::Xaml::Visibility LibraryViewModel::ContentVisibility() const noexcept { return !m_loading&&!m_error&&m_items.Size()>0?Visible:Collapsed; }
    Microsoft::UI::Xaml::Visibility LibraryViewModel::LoadingVisibility() const noexcept { return m_loading?Visible:Collapsed; }
    Microsoft::UI::Xaml::Visibility LibraryViewModel::SkeletonVisibility() const noexcept { return m_loading?Visible:Collapsed; }
    Microsoft::UI::Xaml::Visibility LibraryViewModel::ErrorVisibility() const noexcept { return m_error?Visible:Collapsed; }
    Microsoft::UI::Xaml::Visibility LibraryViewModel::EmptyVisibility() const noexcept { return !m_loading&&!m_error&&m_sourceItems.empty()?Visible:Collapsed; }
    Microsoft::UI::Xaml::Visibility LibraryViewModel::FilteredEmptyVisibility() const noexcept { return !m_loading&&!m_error&&!m_sourceItems.empty()&&m_items.Size()==0?Visible:Collapsed; }
    winrt::hstring LibraryViewModel::FilteredEmptyText() const { return m_filterIndex==1?L"No movies in your library.":L"No series in your library."; }
    bool LibraryViewModel::SkeletonsShown() const noexcept { return m_loading; }
    void LibraryViewModel::SetFilter(std::int32_t index){if(index>=0&&index<=2&&index!=m_filterIndex){m_filterIndex=index;Rebuild();Raise(L"FilterIndex");RaiseState();}}
    void LibraryViewModel::SetSort(std::int32_t index){if(index>=0&&index<=2&&index!=m_sortIndex){m_sortIndex=index;Rebuild();Raise(L"SortIndex");}}
    void LibraryViewModel::Retry(){static_cast<void>(LoadAsync());}
    void LibraryViewModel::OpenDetail(winrt::Windows::Foundation::IInspectable const& item){if(item){auto media=item.as<winrt::HaloDesktop::MediaSummary>();m_navigation->GoTo(::HaloDesktop::Services::Page::Detail,winrt::make<winrt::HaloDesktop::implementation::DetailNavParams>(media.Type(),media.Id(),media.Title(),media.Poster()));}}
    winrt::event_token LibraryViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler){return m_propertyChanged.add(handler);}
    void LibraryViewModel::PropertyChanged(winrt::event_token const& token)noexcept{m_propertyChanged.remove(token);}
    winrt::Windows::Foundation::IAsyncAction LibraryViewModel::LoadAsync()
    {
        auto lifetime=get_strong();
        // Already in memory: shown as it is, without the skeletons flashing up
        // for the moment an awaited load that has nothing to do would take.
        if (m_catalog->HasLoaded())
        {
            ++m_loadVersion;
            m_loading=false;m_error=false;AdoptLibrary();RaiseState();
            co_return;
        }
        auto const uiContext=winrt::apartment_context{};auto const version=++m_loadVersion;m_loading=true;m_error=false;RaiseState();bool failed{};
        try{co_await m_catalog->LoadAsync();}catch(...){failed=true;}co_await uiContext;if(version!=m_loadVersion)co_return;m_loading=false;m_error=failed;
        if(!failed)AdoptLibrary();RaiseState();
    }
    void LibraryViewModel::AdoptLibrary()
    {
        m_sourceItems.clear();
        for (auto const& item : m_catalog->LibraryItems()) m_sourceItems.push_back(item);
        Rebuild();
    }
    // Stable sorts, as JavaScript's: titles that tie (never played, under
    // Recently watched) keep the newest-added order the catalog hands over.
    void LibraryViewModel::Rebuild()
    {
        auto items=m_sourceItems;std::erase_if(items,[this](auto const&i){return (m_filterIndex==1&&!IsType(i, L"movie"))||(m_filterIndex==2&&!IsType(i, L"series"));});
        if(m_sortIndex==0)std::stable_sort(items.begin(),items.end(),[](auto const&a,auto const&b){return a.AddedAt()>b.AddedAt();});
        else if(m_sortIndex==1)std::stable_sort(items.begin(),items.end(),[](auto const&a,auto const&b){return NameBefore(a.Title(),b.Title());});
        else std::stable_sort(items.begin(),items.end(),[](auto const&a,auto const&b){return a.UpdatedAt()>b.UpdatedAt();});
        m_items.Clear();for(auto const&i:items)m_items.Append(i);Raise(L"Items");
    }
    void LibraryViewModel::RaiseState(){for(auto const n:{L"Items",L"AllLabel",L"MoviesLabel",L"SeriesLabel",L"ContentVisibility",L"LoadingVisibility",L"SkeletonVisibility",L"ErrorVisibility",L"EmptyVisibility",L"FilteredEmptyVisibility",L"FilteredEmptyText"})Raise(n);}
    void LibraryViewModel::Raise(wchar_t const*n){::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged,*this,n);}
}
