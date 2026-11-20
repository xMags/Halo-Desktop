#include "pch.h"
#include "ViewModels/HomeViewModel.h"
#if __has_include("HomeViewModel.g.cpp")
#include "HomeViewModel.g.cpp"
#endif
#include "Models/Models.h"
#include "Shell/LayoutMetricsService.h"
#include "Services/HomeRowsPolicy.h"
#include "Services/LibraryService.h"
#include "Services/NavigationService.h"
#include "Services/PlayableVideoPolicy.h"
#include "ViewModels/HomeStatePolicy.h"
#include "ViewModels/ObservableHelper.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace
{
    auto const Visible = winrt::Microsoft::UI::Xaml::Visibility::Visible;
    auto const Collapsed = winrt::Microsoft::UI::Xaml::Visibility::Collapsed;
    auto constexpr FeaturedCardCount = 5u;
    // The title logo may stand this many title heights tall.
    auto constexpr HeroLogoHeightFactor = 2.4;

    ::HaloDesktop::ViewModels::HomeMediaKind HomeKind(winrt::HaloDesktop::MediaSummary const& item)
    {
        return item.Kind() == winrt::HaloDesktop::MediaKind::Series
            ? ::HaloDesktop::ViewModels::HomeMediaKind::Series
            : ::HaloDesktop::ViewModels::HomeMediaKind::Movie;
    }

    winrt::hstring MovieVideoId(winrt::HaloDesktop::MediaSummary const& media)
    {
        return winrt::hstring{ ::HaloDesktop::Services::MovieVideoId(media.Id(), media.DefaultVideoId()) };
    }

    bool PlayableFromPreview(winrt::HaloDesktop::MediaSummary const& media)
    {
        return ::HaloDesktop::Services::PlayableFromPreview(media.Id(), media.DefaultVideoId());
    }

    // The rows of Home that are not a catalog's. A catalog row is keyed by its
    // request, which starts with an addon id and so never with a control
    // character, the mappers having stripped them all.
    constexpr std::wstring_view RecentRowKey{ L"\x1Frecent" };
    constexpr std::wstring_view LibraryRowKey{ L"\x1Flibrary" };
    // A loading shelf draws only its labels, which its key already fixes.
    constexpr std::wstring_view LoadingSignature{ L"\x1Floading" };

    std::wstring JoinedKeys(std::vector<std::wstring> const& keys)
    {
        std::wstring joined;
        for (auto const& key : keys)
        {
            joined.append(key);
            joined.push_back(L'\x1E');
        }
        return joined;
    }

    // Everything a shown row draws or hands its See all, so an unchanged row
    // can be told from a changed one without keeping the old titles around.
    std::wstring RowSignature(
        winrt::HaloDesktop::Shelf const& shelf,
        std::vector<winrt::HaloDesktop::MediaSummary> const& filtered,
        std::int32_t filterIndex)
    {
        auto const* const details = winrt::get_self<winrt::HaloDesktop::implementation::Shelf>(shelf);
        std::wstring signature{ shelf.Title() };
        signature.push_back(L'\x1F');
        signature.append(shelf.SourceLabel());
        signature.push_back(L'\x1F');
        signature.append(std::to_wstring(filterIndex));
        signature.push_back(details->Paging() ? L'P' : L'-');
        for (auto const& item : filtered)
        {
            signature.push_back(L'\x1E');
            for (auto const& part : { item.Type(), item.Id(), item.Title(), item.Poster(), item.Meta() })
            {
                signature.append(part);
                signature.push_back(L'\x1D');
            }
        }
        return signature;
    }
}
namespace winrt::HaloDesktop::implementation
{
    HomeViewModel::HomeViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_layout(services.LayoutMetrics), m_catalog(services.Catalog), m_library(services.Library),
          m_session(services.Session),
          m_navigation(services.Navigation),
          m_featured(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_continueItems(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_shelves(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>())
    {
        if (!m_layout) return;
        // Raw capture rather than a weak reference: get_weak has no controlling
        // reference to hand out this early in construction. It is sound because
        // the destructor below always removes the handler, and the service only
        // ever calls back synchronously on the same thread that owns this.
        m_metricsToken = m_layout->AddChangedHandler([this]() { RaiseLayoutMetrics(); });
        // Same reasoning, and the same removal in the destructor. The catalog
        // raises this when a next-episode lookup lands after the shelf was already
        // shown, which is the only way a promoted card reaches a Home already on
        // screen.
        if (m_catalog)
        {
            m_continueToken = m_catalog->AddContinueChangedHandler([this]() { ApplyContinueChange(); });
            // Same reasoning once more. A title's right-click menu changes the
            // library or the watch history from wherever it was opened, Home
            // included, and the shelves, the continue row and the banner's library
            // buttons all show one or the other.
            m_userStateToken = m_catalog->AddUserStateChangedHandler([this]() { AdoptUserStateChange(); });
            // Same reasoning again. Home's rows arrive one at a time: the
            // library and continue rows, the shelf list, then each shelf.
            m_homeToken = m_catalog->AddHomeChangedHandler([this]() { AdoptHome(); });
        }
        // And for the account itself. Home is usually the page already on screen
        // when a sign-in completes, often behind the sign-in page from launch, and
        // landing on it again is no navigation, so nothing else would reload it.
        if (m_session)
        {
            m_identityToken = m_session->AddIdentityChangedHandler([this]() { ReloadForAccount(); });
        }
    }
    HomeViewModel::~HomeViewModel()
    {
        if (m_layout && m_metricsToken != 0) m_layout->RemoveChangedHandler(m_metricsToken);
        if (m_catalog && m_continueToken != 0) m_catalog->RemoveContinueChangedHandler(m_continueToken);
        if (m_catalog && m_userStateToken != 0) m_catalog->RemoveUserStateChangedHandler(m_userStateToken);
        if (m_catalog && m_homeToken != 0) m_catalog->RemoveHomeChangedHandler(m_homeToken);
        if (m_session && m_identityToken != 0) m_session->RemoveIdentityChangedHandler(m_identityToken);
    }
    // Hero art scales with the page because it is a backdrop, and its title with
    // it because that is a display element sized to the art. Body and label text
    // stays put: that is the user's DPI and text-size settings to decide.
    double HomeViewModel::HeroHeight() const noexcept { return m_layout ? m_layout->Current().HeroHeight : 304.0; }
    double HomeViewModel::HeroTitleSize() const noexcept { return m_layout ? m_layout->Current().HeroTitleSize : 36.0; }
    double HomeViewModel::HeroLogoMaxHeight() const noexcept { return HeroTitleSize() * HeroLogoHeightFactor; }
    Microsoft::UI::Xaml::Thickness HomeViewModel::ContentPadding() const noexcept
    {
        return Microsoft::UI::Xaml::Thickness{ Gutter(), 0.0, Gutter(), 0.0 };
    }
    // The page title has to sit on the same line as the hero and the shelves
    // below it, so it takes the same gutter rather than the constant the header
    // control defaults to. Vertical padding is the header's own, not the page's.
    Microsoft::UI::Xaml::Thickness HomeViewModel::HeaderPadding() const noexcept
    {
        return Microsoft::UI::Xaml::Thickness{ Gutter(), 12.0, Gutter(), 12.0 };
    }
    double HomeViewModel::Gutter() const noexcept { return m_layout ? m_layout->Current().Gutter : 24.0; }
    winrt::Windows::Foundation::IInspectable HomeViewModel::FeaturedItems() const { return m_featured; }
    std::int32_t HomeViewModel::FeaturedCount() const noexcept { return static_cast<std::int32_t>(m_featured.Size()); }
    std::int32_t HomeViewModel::FeaturedIndex() const noexcept { return m_featuredIndex; }
    winrt::HaloDesktop::FeaturedItem HomeViewModel::CurrentFeatured() const
    {
        if (m_featuredIndex < 0 || static_cast<std::uint32_t>(m_featuredIndex) >= m_featured.Size())
        {
            return nullptr;
        }
        return m_featured.GetAt(static_cast<std::uint32_t>(m_featuredIndex)).try_as<winrt::HaloDesktop::FeaturedItem>();
    }
    bool HomeViewModel::FeaturedAutoAdvance() const noexcept { return m_featuredAutoAdvance; }

    void HomeViewModel::ShowFeatured(std::int32_t index, bool autoAdvance)
    {
        auto const count = static_cast<std::int32_t>(m_featured.Size());
        if (count == 0 || index < 0 || index >= count)
        {
            return;
        }
        auto const indexChanged = index != m_featuredIndex;
        auto const modeChanged = autoAdvance != m_featuredAutoAdvance;
        m_featuredIndex = index;
        m_featuredAutoAdvance = autoAdvance;
        if (indexChanged)
        {
            Raise(L"FeaturedIndex");
            Raise(L"CurrentFeatured");
        }
        if (modeChanged)
        {
            Raise(L"FeaturedAutoAdvance");
        }
    }

    void HomeViewModel::SelectFeatured(std::int32_t index)
    {
        ShowFeatured(index, false);
    }

    void HomeViewModel::AdvanceFeatured()
    {
        auto const count = static_cast<std::int32_t>(m_featured.Size());
        if (!m_active || !m_featuredAutoAdvance || count < 2)
        {
            return;
        }
        ShowFeatured((m_featuredIndex + 1) % count, true);
    }

    Microsoft::UI::Xaml::Visibility HomeViewModel::FeaturedVisibility() const noexcept
    {
        return m_featured.Size() > 0 ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility HomeViewModel::FeaturedSkeletonVisibility() const noexcept
    {
        auto const status = m_catalog->CatalogStatus();
        if (!status.ListKnown)
        {
            return m_loading ? Visible : Collapsed;
        }
        return status.HasCatalogs && !m_featuredSettled ? Visible : Collapsed;
    }
    // Arrow-key navigation, wrapped so it never leaves the strip. A pick like
    // any other, so it stops the carousel too.
    void HomeViewModel::StepFeatured(std::int32_t direction)
    {
        auto const count = m_featured.Size();
        if (count == 0)
        {
            return;
        }
        auto const index = m_featuredIndex + direction;
        auto const wrapped = ((index % static_cast<std::int32_t>(count)) + static_cast<std::int32_t>(count))
            % static_cast<std::int32_t>(count);
        SelectFeatured(wrapped);
    }
    Microsoft::UI::Xaml::Visibility HomeViewModel::RefreshErrorVisibility() const noexcept { return Failed() && HasUsableContent() ? Visible : Collapsed; }
    winrt::hstring HomeViewModel::ContinueCountLabel() const { return winrt::to_hstring(m_continueItems.Size()) + L" IN PROGRESS"; }
    // Nothing in progress means no section at all. The header carries a count, a
    // See all and two scroll buttons, none of which have anything to act on, and
    // an account that has never played anything would meet all four on arrival.
    Microsoft::UI::Xaml::Visibility HomeViewModel::ContinueVisibility() const noexcept
    {
        return m_continueItems.Size() > 0 ? Visible : Collapsed;
    }
    winrt::Windows::Foundation::IInspectable HomeViewModel::ContinueItems() const { return m_continueItems; }
    winrt::Windows::Foundation::IInspectable HomeViewModel::Shelves() const { return m_shelves; }
    std::int32_t HomeViewModel::FilterIndex() const noexcept { return m_filterIndex; }
    // The page shows its header with the skeletons while nothing is usable
    // yet, as the reference does, rather than a spinner in place of the page.
    Microsoft::UI::Xaml::Visibility HomeViewModel::ContentVisibility() const noexcept
    {
        auto const state = ::HaloDesktop::ViewModels::ResolveHomeVisibility(
            m_loading, Failed(), HasUsableContent());
        return state.ShowContent || state.ShowLoading ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility HomeViewModel::LoadingVisibility() const noexcept
    {
        return m_loading && !m_catalog->CatalogStatus().ListKnown ? Visible : Collapsed;
    }
    bool HomeViewModel::SkeletonsShown() const noexcept
    {
        return ContentVisibility() == Visible
            && (m_loadingRows > 0 || LoadingVisibility() == Visible || FeaturedSkeletonVisibility() == Visible);
    }
    Microsoft::UI::Xaml::Visibility HomeViewModel::ErrorVisibility() const noexcept
    {
        return ::HaloDesktop::ViewModels::ResolveHomeVisibility(
            m_loading, Failed(), HasUsableContent()).ShowError ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility HomeViewModel::EmptyVisibility() const noexcept
    {
        return ::HaloDesktop::ViewModels::ResolveHomeVisibility(
            m_loading, Failed(), HasUsableContent()).ShowEmpty ? Visible : Collapsed;
    }
    void HomeViewModel::SetFilter(std::int32_t index) { if (index >= 0 && index <= 2 && index != m_filterIndex) { m_filterIndex = index; Rebuild(); Raise(L"FilterIndex"); } }
    void HomeViewModel::Retry() { static_cast<void>(LoadAsync()); }

    bool HomeViewModel::HasUsableContent() const noexcept
    {
        // A shelf still loading counts: it is on screen, as its skeleton.
        return m_featured.Size() > 0 || m_shelves.Size() > 0 || m_continueItems.Size() > 0;
    }

    bool HomeViewModel::Failed() const
    {
        return !m_loading && (m_loadFailed || m_catalog->CatalogStatus().EveryShelfFailed);
    }

    void HomeViewModel::EnsureLoaded()
    {
        m_active = true;
        if (!m_catalog->HasLoaded())
        {
            static_cast<void>(LoadAsync());
            return;
        }
        if (m_appliedVersion != m_catalog->SnapshotVersion())
        {
            // Either nothing has been shown yet, or something else reloaded the
            // catalogs while Home was away and this is now the older state.
            AdoptHome();
        }
        m_catalog->RefreshContinue();
        ApplyContinue();
        // The history may have moved on while Home was away (something played),
        // and the recently watched shelf is built from it.
        if (m_sourceShelves && RecentlyWatchedKeys() != m_recentKeys)
        {
            RebuildShelves();
        }
        if (m_catalog->CatalogsDirty())
        {
            static_cast<void>(LoadAsync());
        }
    }

    void HomeViewModel::AdoptHome()
    {
        // Also reached when the catalogs loaded for somebody else (the shell
        // warms them), so what they hold is taken rather than fetched again.
        m_sourceShelves = m_catalog->Shelves();
        m_appliedVersion = m_catalog->SnapshotVersion();
        if (m_catalog->ContinueWatching() != m_continueSource)
        {
            ApplyContinue();
        }
        Rebuild();
        RaiseState();
    }

    // The catalog has already rebuilt its projections; this takes them the way a
    // banner library save does. Rebuild keeps an unchanged banner strip and its
    // place, so a change made from a menu does not restart the carousel.
    void HomeViewModel::AdoptUserStateChange()
    {
        // Nothing has been shown from a snapshot yet, and the load that shows one
        // reads the same rebuilt state.
        if (!m_sourceShelves)
        {
            return;
        }
        m_sourceShelves = m_catalog->Shelves();
        m_appliedVersion = m_catalog->SnapshotVersion();
        ApplyContinue();
        Rebuild();
    }

    // The services were reset for the new account before this runs. Off screen
    // there is nothing to do: the next visit finds the catalog unloaded and loads
    // it. Signed out, a load could only fail behind the sign-in page; the sign-in
    // that follows raises this again.
    void HomeViewModel::ReloadForAccount()
    {
        if (!m_active || m_session->Mode() == ::HaloDesktop::Services::SessionMode::SignedOut)
        {
            return;
        }
        static_cast<void>(LoadAsync());
    }

    void HomeViewModel::ApplyContinue()
    {
        // Replaced in one go. Clear followed by a run of appends raises a
        // notification per item, and the list on the other end tears down and
        // rebuilds its realised containers for every one of them.
        std::vector<winrt::Windows::Foundation::IInspectable> continued;
        m_continueSource = m_catalog->ContinueWatching();
        for (auto const& item : m_continueSource) continued.push_back(item);
        m_continueItems.ReplaceAll(continued);
        for (auto const name : { L"ContinueItems", L"ContinueCountLabel", L"ContinueVisibility", L"ContentVisibility", L"EmptyVisibility" }) Raise(name);
    }

    void HomeViewModel::ApplyContinueChange()
    {
        ApplyContinue();
        if (m_sourceShelves && RecentlyWatchedKeys() != m_recentKeys)
        {
            RebuildShelves();
        }
    }

    std::vector<std::wstring> HomeViewModel::RecentlyWatchedKeys() const
    {
        return KeysOf(m_catalog->RecentlyWatchedForFilter(m_filterIndex));
    }

    std::vector<std::wstring> HomeViewModel::KeysOf(
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary> const& titles)
    {
        std::vector<std::wstring> keys;
        for (auto const& item : titles)
        {
            keys.push_back(std::wstring{ item.Type() } + L':' + std::wstring{ item.Id() } + L'\x1F'
                + std::wstring{ item.Title() } + L'\x1F' + std::wstring{ item.Poster() });
        }
        return keys;
    }

    void HomeViewModel::OpenDetail(winrt::Windows::Foundation::IInspectable const& item) { if(item){auto media=item.as<winrt::HaloDesktop::MediaSummary>();m_navigation->GoTo(::HaloDesktop::Services::Page::Detail,winrt::make<winrt::HaloDesktop::implementation::DetailNavParams>(media.Type(),media.Id(),media.Title(),media.Poster()));} }
    void HomeViewModel::OpenFeaturedDetail(winrt::Windows::Foundation::IInspectable const& item)
    {
        if (auto const featured = item.try_as<winrt::HaloDesktop::FeaturedItem>())
        {
            OpenDetail(featured.Media());
        }
    }
    // The primary action: a series needs an episode chosen, and Detail is the
    // picker; so is a film listed under an id stream addons do not know. Any
    // other film goes straight to its sources.
    void HomeViewModel::OpenFeaturedSources(winrt::Windows::Foundation::IInspectable const& item)
    {
        auto const featured = item.try_as<winrt::HaloDesktop::FeaturedItem>();
        if (!featured)
        {
            return;
        }
        auto const media = featured.Media();
        if (media.Kind() == winrt::HaloDesktop::MediaKind::Series || !PlayableFromPreview(media))
        {
            OpenFeaturedDetail(featured);
            return;
        }
        auto const sources = winrt::make<winrt::HaloDesktop::implementation::SourcesNavParams>(
            media.Type(),
            media.Id(),
            MovieVideoId(media),
            media.Type() + L":" + media.Id(),
            media.Title(),
            media.Title(),
            L"",
            media.Poster());
        sources.ReleaseInfo(media.ReleaseInfo());
        m_navigation->ShowSheet(::HaloDesktop::Services::Page::Sources, sources);
    }
    void HomeViewModel::ToggleFeaturedLibrary(winrt::Windows::Foundation::IInspectable const& item)
    {
        if (auto const featured = item.try_as<winrt::HaloDesktop::FeaturedItem>())
        {
            if (!featured.LibraryBusy())
            {
                static_cast<void>(ToggleFeaturedLibraryAsync(featured));
            }
        }
    }
    void HomeViewModel::OpenContinue(winrt::Windows::Foundation::IInspectable const& item)
    {
        ::HaloDesktop::Services::OpenContinueItem(
            *m_navigation,
            item.try_as<winrt::HaloDesktop::ContinueItem>());
    }
    void HomeViewModel::OpenSearch()
    {
        // The search box's flight into the page is the motion; a page slide on
        // top of it would move the field twice.
        m_navigation->GoTo(
            ::HaloDesktop::Services::Page::Search,
            nullptr,
            ::HaloDesktop::Services::NavigationService::PageTransition::None);
    }
    void HomeViewModel::OpenCatalog(winrt::Windows::Foundation::IInspectable const& shelf)
    {
        auto const snapshot = shelf.try_as<winrt::HaloDesktop::Shelf>();
        if (!snapshot)
        {
            return;
        }
        auto const* const details = winrt::get_self<winrt::HaloDesktop::implementation::Shelf>(snapshot);
        switch (details->SeeAll())
        {
        case ShelfSeeAll::History:
            m_navigation->GoTo(::HaloDesktop::Services::Page::History);
            return;
        case ShelfSeeAll::Library:
            // The Library page is this shelf in full, with its filter and sort,
            // so it opens that rather than a copy of it.
            m_navigation->GoTo(::HaloDesktop::Services::Page::Library);
            return;
        case ShelfSeeAll::Catalog:
            break;
        }
        // Every title the shelf held, not only the ones its strip draws, and the
        // way on for a catalog that pages.
        m_navigation->GoTo(
            ::HaloDesktop::Services::Page::Catalog,
            winrt::make<winrt::HaloDesktop::implementation::Shelf>(
                snapshot.Title(),
                snapshot.SourceLabel(),
                details->SeeAllItems(),
                nullptr,
                ShelfSeeAll::Catalog,
                details->Paging()));
    }
    // The shelf grid speaks MediaSummary, so the in-progress rows are projected
    // onto it. Time left and progress are dropped: the grid has nowhere to put
    // them, and the poster plus episode tag is what identifies a title there.
    // No source line: the page's own count says how many are in progress.
    void HomeViewModel::OpenContinueCatalog()
    {
        std::vector<winrt::HaloDesktop::MediaSummary> items;
        items.reserve(m_continueItems.Size());
        for (auto const& entry : m_continueItems)
        {
            auto const item = entry.try_as<winrt::HaloDesktop::ContinueItem>();
            if (!item || item.MetaId().empty()) continue;
            items.push_back(winrt::make<winrt::HaloDesktop::implementation::MediaSummary>(
                item.MetaId(),
                item.Name(),
                item.Sub(),
                item.Type() == L"series" ? winrt::HaloDesktop::MediaKind::Series : winrt::HaloDesktop::MediaKind::Movie,
                item.Type(),
                item.Poster()));
        }

        m_navigation->GoTo(
            ::HaloDesktop::Services::Page::Catalog,
            winrt::make<winrt::HaloDesktop::implementation::Shelf>(
                L"Continue watching",
                L"",
                winrt::single_threaded_vector(std::move(items)).GetView()));
    }
    void HomeViewModel::RaiseLayoutMetrics()
    {
        for (auto const* property : { L"HeroHeight", L"HeroTitleSize", L"HeroLogoMaxHeight", L"ContentPadding", L"HeaderPadding" }) Raise(property);
        // Every card carries the title size for the bindings that read it from
        // the card rather than this view model.
        auto const titleSize = m_layout ? m_layout->Current().HeroTitleSize : 36.0;
        for (auto const& entry : m_featured)
        {
            if (auto const item = entry.try_as<winrt::HaloDesktop::FeaturedItem>())
            {
                winrt::get_self<winrt::HaloDesktop::implementation::FeaturedItem>(item)->SetTitleSize(titleSize);
            }
        }
    }
    winrt::event_token HomeViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler) { return m_propertyChanged.add(handler); }
    void HomeViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }
    winrt::Windows::Foundation::IAsyncAction HomeViewModel::LoadAsync()
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        auto const version = ++m_loadVersion;
        m_loading = true;
        m_loadFailed = false;
        // An account change emptied the catalog before this ran: the old
        // account's rows must not stay on screen while the new ones load.
        if (m_appliedVersion != m_catalog->SnapshotVersion())
        {
            AdoptHome();
        }
        RaiseState();

        bool failed{};
        try
        {
            if (m_catalog->HasLoaded())
            {
                co_await m_catalog->RefreshCatalogsIfDirtyAsync();
            }
            else
            {
                co_await m_catalog->LoadAsync();
            }
        }
        catch (...)
        {
            failed = true;
        }
        co_await uiContext;
        if (version != m_loadVersion)
        {
            co_return;
        }

        m_loading = false;
        m_loadFailed = failed;
        // Even a failed load may have published the user's own rows before
        // the addons let it down.
        AdoptHome();
    }

    winrt::Windows::Foundation::IAsyncAction HomeViewModel::ToggleFeaturedLibraryAsync(winrt::HaloDesktop::FeaturedItem item)
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        auto const media = item.Media();
        auto const nextMembership = !item.InLibrary();
        {
            // Scoped, not held across the await: the get_self pointer is raw and
            // the object is alive only because the projected handle keeps it so.
            auto const impl = winrt::get_self<winrt::HaloDesktop::implementation::FeaturedItem>(item);
            impl->SetLibraryBusy(true);
            impl->SetLibraryError({});
        }

        bool failed{};
        try
        {
            auto const applied = co_await m_library->SetMembershipAsync(
                media.Type(),
                media.Id(),
                media.Title(),
                media.Poster().empty()
                    ? std::nullopt
                    : std::optional<winrt::hstring>{ media.Poster() },
                nextMembership);
            failed = !applied;
        }
        catch (...)
        {
            failed = true;
        }
        co_await uiContext;

        auto const impl = winrt::get_self<winrt::HaloDesktop::implementation::FeaturedItem>(item);
        impl->SetLibraryBusy(false);
        if (!failed)
        {
            impl->SetInLibrary(nextMembership);
            m_catalog->RebuildLibrary();
            m_sourceShelves = m_catalog->Shelves();
            m_appliedVersion = m_catalog->SnapshotVersion();
            ApplyContinue();
            Rebuild();
        }
        else
        {
            impl->SetLibraryError(L"Library could not be updated. Try again.");
        }
        RaiseState();
    }

    void HomeViewModel::Rebuild()
    {
        RebuildFeatured();
        RebuildShelves();
    }

    // In the reference's order: recently watched, the library, then the
    // catalogs. Continue watching and the hero sit above this list.
    //
    // The list is edited in place rather than replaced: a shelf that did not
    // change keeps its element and where its strip was scrolled to, a new
    // shelf goes in at its own slot, and a shelf that went comes out without
    // the others moving. Shelves land one at a time while the page is in use.
    void HomeViewModel::RebuildShelves()
    {
        auto const filter = ::HaloDesktop::ViewModels::HomeFilterFromIndex(m_filterIndex);
        std::vector<winrt::Windows::Foundation::IInspectable> rebuilt;
        std::vector<::HaloDesktop::Services::HomeRowIdentity> identities;
        std::size_t loadingRows{};

        if (!m_sourceShelves)
        {
            m_recentKeys.clear();
        }
        else
        {
            // History is browsed, so it follows the filter; what continue
            // watching already offers is left out. No source line.
            auto const recent = m_catalog->RecentlyWatchedForFilter(m_filterIndex);
            m_recentKeys = KeysOf(recent);
            if (recent.Size() > 0)
            {
                rebuilt.push_back(winrt::make<winrt::HaloDesktop::implementation::Shelf>(
                    L"Recently watched", L"", recent, nullptr, ShelfSeeAll::History, nullptr));
                identities.push_back({ std::wstring{ RecentRowKey }, JoinedKeys(m_recentKeys) });
            }

            for (auto const& shelf : m_sourceShelves)
            {
                auto const* const details = winrt::get_self<winrt::HaloDesktop::implementation::Shelf>(shelf);
                if (details->IsLoading())
                {
                    // Its real title over skeleton cards, whatever the filter:
                    // there is nothing to filter yet.
                    rebuilt.push_back(shelf);
                    identities.push_back({ details->Key(), std::wstring{ LoadingSignature } });
                    ++loadingRows;
                    continue;
                }

                std::vector<winrt::HaloDesktop::MediaSummary> filtered;
                for (auto const& item : shelf.Items())
                {
                    if (::HaloDesktop::ViewModels::MatchesHomeFilter(filter, HomeKind(item)))
                    {
                        filtered.push_back(item);
                    }
                }
                // A shelf the filter empties does not earn a place.
                if (filtered.empty())
                {
                    continue;
                }
                auto signature = RowSignature(shelf, filtered, m_filterIndex);
                if (details->SeeAll() != ShelfSeeAll::Catalog)
                {
                    // The library shelf: every title, its label counting the
                    // whole library whatever the filter narrows it to.
                    rebuilt.push_back(winrt::make<winrt::HaloDesktop::implementation::Shelf>(
                        shelf.Title(), shelf.SourceLabel(), winrt::single_threaded_vector(std::move(filtered)).GetView(),
                        nullptr, details->SeeAll(), nullptr));
                    identities.push_back({ std::wstring{ LibraryRowKey }, std::move(signature) });
                    continue;
                }

                // A catalog strip draws its first titles; its grid opens with
                // all of them and, when the catalog pages, keeps the filter going.
                auto const shownCount = (std::min)(filtered.size(), ::HaloDesktop::Services::HomeShelfLimit);
                std::vector<winrt::HaloDesktop::MediaSummary> shown{ filtered.begin(), filtered.begin() + static_cast<std::ptrdiff_t>(shownCount) };
                std::shared_ptr<::HaloDesktop::Services::CatalogPagingSource const> paging;
                if (auto const source = details->Paging())
                {
                    auto filteredSource = std::make_shared<::HaloDesktop::Services::CatalogPagingSource>(*source);
                    filteredSource->Filter = filter;
                    paging = std::move(filteredSource);
                }
                rebuilt.push_back(winrt::make<winrt::HaloDesktop::implementation::Shelf>(
                    shelf.Title(),
                    shelf.SourceLabel(),
                    winrt::single_threaded_vector(std::move(shown)).GetView(),
                    winrt::single_threaded_vector(std::move(filtered)).GetView(),
                    ShelfSeeAll::Catalog,
                    std::move(paging)));
                identities.push_back({ details->Key(), std::move(signature) });
            }
        }

        for (auto const& edit : ::HaloDesktop::Services::PlanHomeRowEdits(m_rowIdentities, identities))
        {
            auto const index = static_cast<std::uint32_t>(edit.Index);
            switch (edit.Kind)
            {
            case ::HaloDesktop::Services::HomeRowEditKind::Remove:
                m_shelves.RemoveAt(index);
                break;
            case ::HaloDesktop::Services::HomeRowEditKind::Insert:
                m_shelves.InsertAt(index, rebuilt[edit.Source]);
                break;
            case ::HaloDesktop::Services::HomeRowEditKind::Replace:
                m_shelves.SetAt(index, rebuilt[edit.Source]);
                break;
            }
        }
        m_rowIdentities = std::move(identities);
        m_loadingRows = loadingRows;
        RaiseState();
    }

    void HomeViewModel::RebuildFeatured()
    {
        auto const selection = m_catalog->FeaturedForHome(m_filterIndex, FeaturedCardCount);
        auto const summaries = selection.Items;
        if (selection.Settled != m_featuredSettled)
        {
            m_featuredSettled = selection.Settled;
            Raise(L"FeaturedSkeletonVisibility");
        }
        auto const titleSize = m_layout ? m_layout->Current().HeroTitleSize : 36.0;
        std::vector<winrt::Windows::Foundation::IInspectable> rebuilt;
        rebuilt.reserve(summaries.Size());
        for (auto const& summary : summaries)
        {
            rebuilt.push_back(winrt::make<winrt::HaloDesktop::implementation::FeaturedItem>(
                summary, m_library->Contains(summary.Type(), summary.Id()), titleSize));
        }
        // Membership changes rebuild everything, but the strip itself is usually
        // unchanged. Replacing it then would restart the carousel under the
        // pointer, so an identical run of titles keeps its cards and its place.
        bool unchanged = m_featured.Size() == rebuilt.size();
        if (unchanged)
        {
            for (std::uint32_t index = 0; index < rebuilt.size(); ++index)
            {
                auto const current = m_featured.GetAt(index).try_as<winrt::HaloDesktop::FeaturedItem>();
                auto const next = rebuilt[index].try_as<winrt::HaloDesktop::FeaturedItem>();
                if (!current || !next
                    || current.Media().Type() != next.Media().Type()
                    || current.Media().Id() != next.Media().Id())
                {
                    unchanged = false;
                    break;
                }
            }
        }
        if (!unchanged)
        {
            // A new run of titles starts from its first, rotating again.
            m_featured.ReplaceAll(rebuilt);
            m_featuredIndex = 0;
            m_featuredAutoAdvance = true;
            for (auto const name : { L"FeaturedItems", L"FeaturedCount", L"FeaturedIndex", L"FeaturedAutoAdvance", L"CurrentFeatured", L"FeaturedVisibility" })
            {
                Raise(name);
            }
            return;
        }
        for (std::uint32_t index = 0; index < rebuilt.size(); ++index)
        {
            auto const current = m_featured.GetAt(index).as<winrt::HaloDesktop::FeaturedItem>();
            auto const next = rebuilt[index].as<winrt::HaloDesktop::FeaturedItem>();
            winrt::get_self<winrt::HaloDesktop::implementation::FeaturedItem>(current)
                ->SetMedia(next.Media());
        }
        // New cards carried the fresh membership; the kept ones may hold a
        // flag set before a save went through, so re-derive it for all.
        SynchronizeFeaturedLibraryState();
    }

    void HomeViewModel::SynchronizeFeaturedLibraryState()
    {
        for (auto const& entry : m_featured)
        {
            auto const item = entry.try_as<winrt::HaloDesktop::FeaturedItem>();
            if (!item)
            {
                continue;
            }
            winrt::get_self<winrt::HaloDesktop::implementation::FeaturedItem>(item)
                ->SetInLibrary(m_library->Contains(item.Media().Type(), item.Media().Id()));
        }
    }

    void HomeViewModel::Deactivate()
    {
        m_active = false;
    }

    void HomeViewModel::RaiseState()
    {
        for (auto const name : {
                 L"FeaturedItems",
                 L"FeaturedCount",
                 L"FeaturedIndex",
                 L"CurrentFeatured",
                 L"FeaturedVisibility",
                 L"FeaturedSkeletonVisibility",
                 L"RefreshErrorVisibility",
                 L"ContinueItems",
                 L"ContinueCountLabel",
                 L"ContinueVisibility",
                 L"Shelves",
                 L"ContentVisibility",
                 L"LoadingVisibility",
                 L"ErrorVisibility",
                 L"EmptyVisibility" })
        {
            Raise(name);
        }
    }
    void HomeViewModel::Raise(wchar_t const* name) { ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, name); }
}
