#include "pch.h"
#include "ViewModels/SourcesViewModel.h"
#if __has_include("SourceDisplayItemViewModel.g.cpp")
#include "SourceDisplayItemViewModel.g.cpp"
#endif
#if __has_include("SourceResolutionItemViewModel.g.cpp")
#include "SourceResolutionItemViewModel.g.cpp"
#endif
#if __has_include("SourceQualityItemViewModel.g.cpp")
#include "SourceQualityItemViewModel.g.cpp"
#endif
#if __has_include("SourcePickerRuleViewModel.g.cpp")
#include "SourcePickerRuleViewModel.g.cpp"
#endif
#if __has_include("SourcesViewModel.g.cpp")
#include "SourcesViewModel.g.cpp"
#endif

#include "Models/Models.h"

#include <algorithm>
#include "Services/NavigationService.h"
#include "Services/SettingsSyncService.h"
#include "Services/DevicePreferencesStore.h"
#include "ViewModels/ObservableHelper.h"

#include <utility>

namespace
{
    auto const Visible = winrt::Microsoft::UI::Xaml::Visibility::Visible;
    auto const Collapsed = winrt::Microsoft::UI::Xaml::Visibility::Collapsed;

    winrt::hstring StatusLabel(winrt::HaloDesktop::StreamStatus status)
    {
        switch (status)
        {
        case winrt::HaloDesktop::StreamStatus::Instant: return L"INSTANT";
        case winrt::HaloDesktop::StreamStatus::Caching: return L"CACHING";
        case winrt::HaloDesktop::StreamStatus::Uncached: return L"UNCACHED";
        case winrt::HaloDesktop::StreamStatus::OnDisk: return L"ON DISK";
        case winrt::HaloDesktop::StreamStatus::Unknown: return L"UNKNOWN";
        }
        return L"UNKNOWN";
    }

    winrt::hstring FilterLabel(wchar_t const* name, std::int32_t count)
    {
        return winrt::hstring{ name } + L" " + winrt::to_hstring(count);
    }
}

namespace winrt::HaloDesktop::implementation
{
    SourceResolutionItemViewModel::SourceResolutionItemViewModel(winrt::hstring name, winrt::hstring value, bool resolved)
        : m_name(std::move(name)), m_value(std::move(value)), m_resolved(resolved) {}
    winrt::hstring SourceResolutionItemViewModel::Name() const { return m_name; }
    winrt::hstring SourceResolutionItemViewModel::Value() const { return m_value; }
    Microsoft::UI::Xaml::Visibility SourceResolutionItemViewModel::ResolvedVisibility() const noexcept { return m_resolved ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourceResolutionItemViewModel::FailedVisibility() const noexcept { return m_resolved ? Collapsed : Visible; }

    SourceQualityItemViewModel::SourceQualityItemViewModel(winrt::hstring label, std::int32_t count, std::int32_t largest)
        : m_label(std::move(label)), m_count(count), m_largest(largest) {}
    winrt::hstring SourceQualityItemViewModel::Label() const { return m_label; }
    winrt::hstring SourceQualityItemViewModel::Count() const { return winrt::to_hstring(m_count); }
    // Scaled against the largest bucket rather than the total: with one dominant
    // quality every other bar would otherwise collapse to an unreadable sliver.
    double SourceQualityItemViewModel::Share() const noexcept { return m_largest > 0 ? static_cast<double>(m_count) / static_cast<double>(m_largest) : 0.0; }
    Microsoft::UI::Xaml::Visibility SourceQualityItemViewModel::Quality2160Visibility() const noexcept { return m_label == L"2160p" ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourceQualityItemViewModel::Quality1080Visibility() const noexcept { return m_label == L"1080p" ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourceQualityItemViewModel::QualityOtherVisibility() const noexcept { return m_label != L"2160p" && m_label != L"1080p" ? Visible : Collapsed; }

    SourcePickerRuleViewModel::SourcePickerRuleViewModel(winrt::hstring name, winrt::hstring value)
        : m_name(std::move(name)), m_value(std::move(value)) {}
    winrt::hstring SourcePickerRuleViewModel::Name() const { return m_name; }
    winrt::hstring SourcePickerRuleViewModel::Value() const { return m_value; }

    SourceDisplayItemViewModel::SourceDisplayItemViewModel(winrt::hstring groupName, winrt::hstring groupNote, winrt::hstring groupCount)
        : m_groupName(std::move(groupName)), m_groupNote(std::move(groupNote)), m_groupCount(std::move(groupCount)), m_isHeader(true) {}
    SourceDisplayItemViewModel::SourceDisplayItemViewModel(winrt::HaloDesktop::StreamSource source, bool detailColumns) : m_source(std::move(source)), m_detailColumns(detailColumns) {}
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::HeaderVisibility() const noexcept { return m_isHeader ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::RowVisibility() const noexcept { return m_isHeader ? Collapsed : Visible; }
    winrt::hstring SourceDisplayItemViewModel::Key() const { return m_source ? m_source.Key() : L""; }
    winrt::hstring SourceDisplayItemViewModel::GroupName() const { return m_groupName; }
    winrt::hstring SourceDisplayItemViewModel::GroupNote() const { return m_groupNote; }
    winrt::hstring SourceDisplayItemViewModel::GroupCount() const { return m_groupCount; }
    winrt::hstring SourceDisplayItemViewModel::Quality() const { return m_source ? m_source.Quality() : L""; }
    winrt::hstring SourceDisplayItemViewModel::Range() const { return m_source ? m_source.Range() : L""; }
    winrt::hstring SourceDisplayItemViewModel::File() const { return m_source ? m_source.File() : L""; }
    winrt::hstring SourceDisplayItemViewModel::Codec() const { return m_source ? m_source.Codec() : L""; }
    winrt::hstring SourceDisplayItemViewModel::Audio() const { return m_source ? m_source.Audio() : L""; }
    winrt::hstring SourceDisplayItemViewModel::Languages() const { return m_source ? m_source.Languages() : L""; }
    winrt::hstring SourceDisplayItemViewModel::StatusLabel() const { return m_source ? ::StatusLabel(m_source.Status()) : L""; }
    winrt::hstring SourceDisplayItemViewModel::Size() const { return m_source ? m_source.Size() : L""; }
    winrt::hstring SourceDisplayItemViewModel::Detail() const { return m_source ? m_source.Detail() : L""; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::Quality2160Visibility() const noexcept { return m_source && m_source.Quality() == L"2160p" ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::Quality1080Visibility() const noexcept { return m_source && m_source.Quality() == L"1080p" ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::QualityOtherVisibility() const noexcept { return m_source && m_source.Quality() != L"2160p" && m_source.Quality() != L"1080p" ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::InstantVisibility() const noexcept { return m_source && m_source.Status() == winrt::HaloDesktop::StreamStatus::Instant ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::CachingVisibility() const noexcept { return m_source && m_source.Status() == winrt::HaloDesktop::StreamStatus::Caching ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::UncachedVisibility() const noexcept { return m_source && m_source.Status() == winrt::HaloDesktop::StreamStatus::Uncached ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::OnDiskVisibility() const noexcept { return m_source && m_source.Status() == winrt::HaloDesktop::StreamStatus::OnDisk ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::UnknownVisibility() const noexcept { return m_source && m_source.Status() == winrt::HaloDesktop::StreamStatus::Unknown ? Visible : Collapsed; }
    // The technical detail is shown either as three columns or as one summary
    // line under the file name, never both, so the two are exact opposites.
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::DetailColumnVisibility() const noexcept { return m_detailColumns ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::CompactSummaryVisibility() const noexcept { return m_detailColumns ? Collapsed : Visible; }

    SourcesViewModel::SourcesViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_sources(services.Sources), m_navigation(services.Navigation), m_settings(services.SettingsSync), m_downloads(services.Downloads),
          m_devicePreferences(services.DevicePreferences),
          m_sourceGroups(services.Sources->Groups()),
          m_items(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_resolutionItems(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_qualityItems(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_pickerRules(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>())
    {
        if (!m_devicePreferences)
        {
            throw std::invalid_argument{ "SourcesViewModel requires device preferences." };
        }
        m_teachingTipOpen = !m_devicePreferences->SourceRankingTipDismissed();
    }

    SourcesViewModel::~SourcesViewModel() { Deactivate(); }
    void SourcesViewModel::Activate()
    {
        if (m_downloadToken != 0) return;
        m_downloadToken = m_downloads->AddChangedHandler([weak = get_weak()]()
        {
            if (auto const self = weak.get())
            {
                self->m_sources->RefreshDownloadStates();
                self->m_sourceGroups = self->m_sources->Groups();
                self->m_bestSource = self->m_sources->BestSource();
                self->Rebuild();
                self->RaiseState();
            }
        });
    }
    void SourcesViewModel::Deactivate() noexcept
    {
        if (m_downloadToken == 0) return;
        m_downloads->RemoveChangedHandler(m_downloadToken);
        m_downloadToken = 0;
    }

    winrt::Windows::Foundation::IInspectable SourcesViewModel::Items() const { return m_items; }
    winrt::Windows::Foundation::IInspectable SourcesViewModel::ResolutionItems() const { return m_resolutionItems; }
    winrt::Windows::Foundation::IInspectable SourcesViewModel::QualityItems() const { return m_qualityItems; }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> SourcesViewModel::ItemsView() const { return m_items; }
    winrt::hstring SourcesViewModel::Title() const { return m_parameters ? m_parameters.ShowName() : L""; }
    winrt::hstring SourcesViewModel::Poster() const { return m_parameters ? m_parameters.Poster() : L""; }
    winrt::hstring SourcesViewModel::EpisodeLabel() const { return m_parameters && !m_parameters.EpisodeLabel().empty() ? m_parameters.EpisodeLabel() + L" \x00B7 " + m_parameters.Title() : L"MOVIE"; }
    winrt::hstring SourcesViewModel::ResolveSummary() const { return m_sources->ResolveSummary(); }
    winrt::hstring SourcesViewModel::AllFilterLabel() const { return FilterLabel(L"All", m_sources->Count(L"All")); }
    winrt::hstring SourcesViewModel::InstantFilterLabel() const { return FilterLabel(L"Instant", m_sources->Count(L"Instant")); }
    winrt::hstring SourcesViewModel::Quality2160FilterLabel() const { return FilterLabel(L"2160p", m_sources->Count(L"2160p")); }
    winrt::hstring SourcesViewModel::Quality1080FilterLabel() const { return FilterLabel(L"1080p", m_sources->Count(L"1080p")); }
    winrt::hstring SourcesViewModel::BestQuality() const { return m_bestSource ? m_bestSource.Quality() : L""; }
    winrt::hstring SourcesViewModel::BestRange() const { return m_bestSource ? m_bestSource.Range() : L""; }
    winrt::hstring SourcesViewModel::BestFile() const { return m_bestSource ? m_bestSource.File() : L""; }
    winrt::hstring SourcesViewModel::BestCodec() const { return m_bestSource ? m_bestSource.Codec() : L""; }
    winrt::hstring SourcesViewModel::BestAudio() const { return m_bestSource ? m_bestSource.Audio() : L""; }
    winrt::hstring SourcesViewModel::BestLanguages() const { return m_bestSource ? m_bestSource.Languages() : L""; }
    winrt::hstring SourcesViewModel::BestSize() const { return m_bestSource ? m_bestSource.Size() : L""; }
    winrt::hstring SourcesViewModel::BestStatusLine() const { if(!m_bestSource)return L"";if(m_bestSource.Status()==winrt::HaloDesktop::StreamStatus::OnDisk)return L"Stored on this device";if(m_bestSource.Status()==winrt::HaloDesktop::StreamStatus::Instant)return L"Instant · reported cached";if(m_bestSource.Status()==winrt::HaloDesktop::StreamStatus::Uncached)return L"Addon reports a fetch is required";return L"Cache status was not provided"; }
    winrt::hstring SourcesViewModel::BestStatusBadge() const { return m_bestSource ? ::StatusLabel(m_bestSource.Status()) : L""; }
    winrt::Windows::Foundation::IInspectable SourcesViewModel::PickerRules() const { return m_pickerRules; }
    winrt::hstring SourcesViewModel::TeachingTipTitle() const { return L"How the best source is chosen"; }
    winrt::hstring SourcesViewModel::TeachingTipBody() const { return L"Ready-to-play sources rank first, followed by quality and file size."; }
    bool SourcesViewModel::TeachingTipOpen() const noexcept { return m_teachingTipOpen; }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::ContentVisibility() const noexcept { return !m_loading && !m_error && m_bestSource ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::LoadingVisibility() const noexcept { return m_loading ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::ErrorVisibility() const noexcept { return m_error ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::EmptyVisibility() const noexcept { return !m_loading && !m_error && !m_bestSource ? Visible : Collapsed; }

    void SourcesViewModel::Load(winrt::Windows::Foundation::IInspectable const& parameter)
    {
        m_parameters = parameter.try_as<winrt::HaloDesktop::SourcesNavParams>();
        if (!m_parameters)
        {
            if (auto const item = parameter.try_as<winrt::HaloDesktop::ContinueItem>())
            {
                m_parameters = winrt::make<winrt::HaloDesktop::implementation::SourcesNavParams>(
                    item.Type(),
                    item.MetaId(),
                    item.VideoId(),
                    item.ItemId(),
                    item.Name(),
                    item.Name(),
                    item.Tag(),
                    item.Poster());
            }
        }
        if (!m_parameters)
        {
            m_error = true;
            RaiseState();
            return;
        }
        static_cast<void>(LoadAsync());
    }
    void SourcesViewModel::Retry() { if (m_parameters) static_cast<void>(LoadAsync()); }
    void SourcesViewModel::SetFilter(std::int32_t index) { if (index < 0 || index > 3 || index == m_filterIndex) return; m_filterIndex = index; Rebuild(); }
    void SourcesViewModel::DismissTeachingTip() { if (!m_teachingTipOpen) return; m_teachingTipOpen = false; m_devicePreferences->SourceRankingTipDismissed(true); Raise(L"TeachingTipOpen"); }
    void SourcesViewModel::OpenPlayer(winrt::hstring const& key) { auto request=m_sources->BuildPlaybackRequest(key); if(request)m_navigation->ShowOverlay(::HaloDesktop::Services::Page::Player,request); }
    void SourcesViewModel::OpenBest() { if (m_bestSource) OpenPlayer(m_bestSource.Key()); }
    void SourcesViewModel::OpenSettings() { m_navigation->GoTo(::HaloDesktop::Services::Page::Settings); }
    concurrency::task<::HaloDesktop::Services::DownloadStartOutcome> SourcesViewModel::StartDownloadAsync(
        winrt::hstring key,
        bool replaceExisting)
    {
        co_return co_await m_sources->StartDownloadAsync(std::move(key), replaceExisting);
    }
    winrt::hstring SourcesViewModel::BestKey() const { return m_bestSource ? m_bestSource.Key() : L""; }
    winrt::event_token SourcesViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler) { return m_propertyChanged.add(handler); }
    void SourcesViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }

    winrt::Windows::Foundation::IAsyncAction SourcesViewModel::LoadAsync()
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        auto const version = ++m_loadVersion;
        m_loading = true;
        m_error = false;
        RaiseState();
        bool failed{};
        try { co_await m_sources->LoadAsync(m_parameters); }
        catch (...) { failed = true; }
        try { co_await m_settings->LoadAsync(); }
        catch (...) {}
        co_await uiContext;
        if (version != m_loadVersion) co_return;
        m_loading = false;
        m_error = failed;
        if (!failed)
        {
            m_sourceGroups = m_sources->Groups();
            m_bestSource = m_sources->BestSource();
            Rebuild();
            RebuildAside();
        }
        RaiseState();
    }

    void SourcesViewModel::Rebuild()
    {
        m_items.Clear();
        for (auto const& group : m_sourceGroups)
        {
            if (group.Sources().Size() == 0)
            {
                m_items.Append(winrt::make<SourceDisplayItemViewModel>(group.Name(), group.Note(), L"ERROR"));
                continue;
            }
            bool headerAdded{};
            for (auto const& source : group.Sources())
            {
                if (!MatchesFilter(source)) continue;
                if (!headerAdded)
                {
                    m_items.Append(winrt::make<SourceDisplayItemViewModel>(group.Name(), group.Note(), winrt::to_hstring(group.Count()) + L" SOURCES"));
                    headerAdded = true;
                }
                m_items.Append(winrt::make<SourceDisplayItemViewModel>(source, m_detailColumns));
            }
        }
        Raise(L"Items");
    }

    Microsoft::UI::Xaml::Visibility SourcesViewModel::DetailColumnVisibility() const noexcept { return m_detailColumns ? Visible : Collapsed; }

    void SourcesViewModel::SetListWidth(double width)
    {
        // Everything but the file name is fixed width, so the threshold is that
        // fixed cost plus a file column wide enough to still read a release name.
        // The flag cannot feed back into the measurement: the list column is
        // sized by its parent, not by what these rows ask for.
        constexpr double DetailColumnsRequire = 980.0;
        auto const detailColumns = width >= DetailColumnsRequire;
        if (detailColumns == m_detailColumns) return;
        m_detailColumns = detailColumns;
        Raise(L"DetailColumnVisibility");
        Rebuild();
    }

    void SourcesViewModel::RebuildAside()
    {
        m_resolutionItems.Clear();
        for (auto const& group : m_sourceGroups)
        {
            // An addon that returned nothing puts its reason in the note; the dot
            // beside the row is the only thing that separates the two cases.
            auto const resolved = group.Sources().Size() > 0;
            auto const value = resolved ? winrt::to_hstring(group.Count()) + L" sources" : group.Note();
            m_resolutionItems.Append(winrt::make<SourceResolutionItemViewModel>(group.Name(), value, resolved));
        }

        constexpr wchar_t const* Qualities[]{ L"2160p", L"1440p", L"1080p", L"720p", L"480p", L"SD" };
        std::int32_t largest{};
        // Parenthesised so the windows.h max macro cannot swallow the call.
        for (auto const* quality : Qualities) largest = (std::max)(largest, m_sources->Count(quality));
        m_qualityItems.Clear();
        for (auto const* quality : Qualities)
        {
            auto const count = m_sources->Count(quality);
            if (count > 0) m_qualityItems.Append(winrt::make<SourceQualityItemViewModel>(quality, count, largest));
        }

        m_pickerRules.Clear();
        m_pickerRules.Append(winrt::make<SourcePickerRuleViewModel>(L"Cached first", L"ON"));
        m_pickerRules.Append(winrt::make<SourcePickerRuleViewModel>(L"Audio preference", m_settings->PreferredAudioLanguage().value_or(L"Automatic")));
        m_pickerRules.Append(winrt::make<SourcePickerRuleViewModel>(L"Subtitle preference", m_settings->PreferredSubtitleLanguage().value_or(L"Off")));
        m_pickerRules.Append(winrt::make<SourcePickerRuleViewModel>(L"Autoplay next", m_settings->AutoplayNextEpisode() ? L"ON" : L"OFF"));

        Raise(L"ResolutionItems");
        Raise(L"QualityItems");
        Raise(L"PickerRules");
    }

    void SourcesViewModel::RaiseState()
    {
        for (auto const* property : { L"Title",L"Poster",L"EpisodeLabel",L"ResolveSummary",L"AllFilterLabel",L"InstantFilterLabel",L"Quality2160FilterLabel",L"Quality1080FilterLabel",L"BestQuality",L"BestRange",L"BestFile",L"BestCodec",L"BestAudio",L"BestLanguages",L"BestSize",L"BestStatusLine",L"BestStatusBadge",L"ContentVisibility",L"LoadingVisibility",L"ErrorVisibility",L"EmptyVisibility" }) Raise(property);
    }
    void SourcesViewModel::Raise(wchar_t const* property) { ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, property); }
    bool SourcesViewModel::MatchesFilter(winrt::HaloDesktop::StreamSource const& source) const
    {
        if (m_filterIndex == 0) return true;
        if (m_filterIndex == 1) return source.Status() == winrt::HaloDesktop::StreamStatus::Instant || source.Status() == winrt::HaloDesktop::StreamStatus::OnDisk;
        if (m_filterIndex == 2) return source.Quality() == L"2160p";
        return source.Quality() == L"1080p";
    }
}
