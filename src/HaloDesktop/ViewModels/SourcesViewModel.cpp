#include "pch.h"
#include "ViewModels/SourcesViewModel.h"
#if __has_include("SourceSaveViewModel.g.cpp")
#include "SourceSaveViewModel.g.cpp"
#endif
#if __has_include("SourceFactViewModel.g.cpp")
#include "SourceFactViewModel.g.cpp"
#endif
#if __has_include("SourceFactLineViewModel.g.cpp")
#include "SourceFactLineViewModel.g.cpp"
#endif
#if __has_include("SourceDetailsViewModel.g.cpp")
#include "SourceDetailsViewModel.g.cpp"
#endif
#if __has_include("SourceDisplayItemViewModel.g.cpp")
#include "SourceDisplayItemViewModel.g.cpp"
#endif
#if __has_include("SourceProviderItemViewModel.g.cpp")
#include "SourceProviderItemViewModel.g.cpp"
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
#include "Services/DevicePreferencesStore.h"
#include "Services/NavigationService.h"
#include "Services/SettingsSyncService.h"
#include "Services/WatchStateService.h"
#include "ViewModels/ObservableHelper.h"
#include "ViewModels/QualityMedalPolicy.h"
#include "ViewModels/QualityMedalProjection.h"

#include <algorithm>
#include <cwctype>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
    namespace Sources = ::HaloDesktop::Sources;
    using SaveKind = ::HaloDesktop::Sources::SaveState::Kind;

    auto const Visible = winrt::Microsoft::UI::Xaml::Visibility::Visible;
    auto const Collapsed = winrt::Microsoft::UI::Xaml::Visibility::Collapsed;

    winrt::Microsoft::UI::Xaml::Visibility VisibleIf(bool value) noexcept
    {
        return value ? Visible : Collapsed;
    }

    // The sheet's filters and sorts as the page's indices.
    constexpr std::int32_t FilterCount = static_cast<std::int32_t>(Sources::Filters.size());
    constexpr std::int32_t SortCount = static_cast<std::int32_t>(Sources::Sorts.size());

    Sources::SourceFilter FilterAt(std::int32_t index) noexcept
    {
        return index >= 0 && index < FilterCount ? Sources::Filters[static_cast<std::size_t>(index)] : Sources::SourceFilter::All;
    }

    Sources::SourceSort SortAt(std::int32_t index) noexcept
    {
        return index >= 0 && index < SortCount ? Sources::Sorts[static_cast<std::size_t>(index)] : Sources::SourceSort::Recommended;
    }

    std::wstring ToKey(winrt::hstring const& key)
    {
        return std::wstring{ key.c_str(), key.size() };
    }

    winrt::hstring PlayNameFor(Sources::ListItem const& item, ::HaloDesktop::ViewModels::Medal const& medal)
    {
        // "Play 4K UHD, Plays instantly, HEVC . DDP 5.1 . 8.42 GB": the medal is
        // left out when the release named no quality, since its plate says nothing.
        std::wstring name{ L"Play" };
        auto separator = L" ";
        if (std::wstring_view{ item.Entry.Quality.c_str(), item.Entry.Quality.size() } != Sources::NotParsed)
        {
            name.append(separator).append(medal.Plate);
            if (!medal.Tag.empty()) name.append(L" ").append(medal.Tag);
            separator = L", ";
        }
        name.append(separator).append(item.Status.c_str());
        if (!item.Meta.empty()) name.append(L", ").append(item.Meta.c_str());
        return winrt::hstring{ name };
    }
}

namespace winrt::HaloDesktop::implementation
{
    // --- Save state -------------------------------------------------------

    SourceSaveViewModel::SourceSaveViewModel(winrt::hstring key, Sources::SaveState state)
        : m_key(std::move(key)), m_state(std::move(state)) {}
    winrt::hstring SourceSaveViewModel::Key() const { return m_key; }
    winrt::hstring SourceSaveViewModel::Label() const { return Sources::SaveLabel(m_state); }
    bool SourceSaveViewModel::IsReady() const noexcept { return m_state.State == SaveKind::Ready; }
    Microsoft::UI::Xaml::Visibility SourceSaveViewModel::ButtonVisibility() const noexcept { return VisibleIf(m_state.State != SaveKind::Unavailable); }
    Microsoft::UI::Xaml::Visibility SourceSaveViewModel::ArrowVisibility() const noexcept { return VisibleIf(m_state.State == SaveKind::Ready); }
    Microsoft::UI::Xaml::Visibility SourceSaveViewModel::RingVisibility() const noexcept { return VisibleIf(m_state.State == SaveKind::Saving); }
    Microsoft::UI::Xaml::Visibility SourceSaveViewModel::CheckVisibility() const noexcept { return VisibleIf(m_state.State == SaveKind::Saved); }
    Microsoft::UI::Xaml::Visibility SourceSaveViewModel::ReadyLabelVisibility() const noexcept { return VisibleIf(m_state.State == SaveKind::Ready); }
    Microsoft::UI::Xaml::Visibility SourceSaveViewModel::SavingLabelVisibility() const noexcept { return VisibleIf(m_state.State == SaveKind::Saving); }
    Microsoft::UI::Xaml::Visibility SourceSaveViewModel::SavedLabelVisibility() const noexcept { return VisibleIf(m_state.State == SaveKind::Saved); }
    Sources::SaveState const& SourceSaveViewModel::State() const noexcept { return m_state; }

    void SourceSaveViewModel::SetState(Sources::SaveState state)
    {
        if (state == m_state) return;
        auto const kindChanged = state.State != m_state.State;
        m_state = std::move(state);
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, L"Label");
        if (!kindChanged) return;
        for (auto const* property : {
                 L"IsReady", L"ButtonVisibility", L"ArrowVisibility", L"RingVisibility", L"CheckVisibility",
                 L"ReadyLabelVisibility", L"SavingLabelVisibility", L"SavedLabelVisibility" })
        {
            ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, property);
        }
    }

    winrt::event_token SourceSaveViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler) { return m_propertyChanged.add(handler); }
    void SourceSaveViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }

    // --- Facts ------------------------------------------------------------

    SourceFactViewModel::SourceFactViewModel(winrt::hstring label, winrt::hstring value)
        : m_label(std::move(label)), m_value(std::move(value)) {}
    SourceFactViewModel::SourceFactViewModel(winrt::hstring provider, winrt::hstring cacheLabel, bool cacheGood)
        : m_label(L"FROM"), m_from(true), m_provider(std::move(provider)), m_cacheLabel(std::move(cacheLabel)), m_cacheGood(cacheGood) {}
    winrt::hstring SourceFactViewModel::Label() const { return m_label; }
    winrt::hstring SourceFactViewModel::Value() const { return m_value; }
    Microsoft::UI::Xaml::Visibility SourceFactViewModel::ValueVisibility() const noexcept { return VisibleIf(!m_from); }
    Microsoft::UI::Xaml::Visibility SourceFactViewModel::ProviderVisibility() const noexcept { return VisibleIf(m_from); }
    winrt::hstring SourceFactViewModel::Provider() const { return m_provider; }
    winrt::hstring SourceFactViewModel::CacheLabel() const { return m_cacheLabel; }
    Microsoft::UI::Xaml::Visibility SourceFactViewModel::CacheGoodVisibility() const noexcept { return VisibleIf(m_from && !m_cacheLabel.empty() && m_cacheGood); }
    Microsoft::UI::Xaml::Visibility SourceFactViewModel::CacheNeutralVisibility() const noexcept { return VisibleIf(m_from && !m_cacheLabel.empty() && !m_cacheGood); }

    SourceFactLineViewModel::SourceFactLineViewModel(winrt::HaloDesktop::SourceFactViewModel left, winrt::HaloDesktop::SourceFactViewModel right)
        : m_left(std::move(left)), m_right(std::move(right)) {}
    winrt::HaloDesktop::SourceFactViewModel SourceFactLineViewModel::Left() const { return m_left; }
    winrt::HaloDesktop::SourceFactViewModel SourceFactLineViewModel::Right() const { return m_right; }
    std::int32_t SourceFactLineViewModel::LeftSpan() const noexcept { return m_right ? 1 : 2; }
    Microsoft::UI::Xaml::Visibility SourceFactLineViewModel::RightVisibility() const noexcept { return VisibleIf(m_right != nullptr); }

    // --- Details ----------------------------------------------------------

    SourceDetailsViewModel::SourceDetailsViewModel(
        winrt::hstring key,
        winrt::hstring fileName,
        winrt::hstring warning,
        Sources::SourceDetails const& details,
        winrt::HaloDesktop::SourceSaveViewModel save)
        : m_key(std::move(key)),
          m_fileName(std::move(fileName)),
          m_warning(std::move(warning)),
          m_factLines(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_bandwidth(details.Bandwidth),
          m_save(std::move(save))
    {
        // The cells pair up two to a line; the From cell, which always comes last,
        // takes the whole line when it is the odd one out.
        std::vector<winrt::HaloDesktop::SourceFactViewModel> cells;
        for (auto const& row : details.Rows)
        {
            // The label is set in capitals, the reference's text-transform.
            std::wstring label{ row.Label.c_str() };
            std::transform(label.begin(), label.end(), label.begin(), [](wchar_t character)
            {
                return static_cast<wchar_t>(std::towupper(character));
            });
            cells.push_back(winrt::make<SourceFactViewModel>(winrt::hstring{ label }, row.Value));
        }
        cells.push_back(winrt::make<SourceFactViewModel>(details.Provider, details.CacheLabel, details.CacheGood));
        for (std::size_t index{}; index < cells.size(); index += 2)
        {
            auto right = index + 1 < cells.size() ? cells[index + 1] : winrt::HaloDesktop::SourceFactViewModel{ nullptr };
            m_factLines.Append(winrt::make<SourceFactLineViewModel>(cells[index], std::move(right)));
        }
    }
    winrt::hstring SourceDetailsViewModel::Key() const { return m_key; }
    winrt::hstring SourceDetailsViewModel::FileName() const { return m_fileName; }
    winrt::hstring SourceDetailsViewModel::Warning() const { return m_warning; }
    Microsoft::UI::Xaml::Visibility SourceDetailsViewModel::WarningVisibility() const noexcept { return VisibleIf(!m_warning.empty()); }
    winrt::Windows::Foundation::IInspectable SourceDetailsViewModel::FactLines() const { return m_factLines; }
    Microsoft::UI::Xaml::Visibility SourceDetailsViewModel::BandwidthVisibility() const noexcept { return VisibleIf(m_bandwidth.has_value()); }
    winrt::hstring SourceDetailsViewModel::BandwidthLabel() const { return m_bandwidth ? m_bandwidth->Label : winrt::hstring{}; }
    winrt::hstring SourceDetailsViewModel::BandwidthNote() const { return m_bandwidth ? m_bandwidth->Note : winrt::hstring{}; }
    Microsoft::UI::Xaml::Visibility SourceDetailsViewModel::BandwidthNoteVisibility() const noexcept { return VisibleIf(m_bandwidth && !m_bandwidth->Note.empty()); }
    double SourceDetailsViewModel::MeterFraction() const noexcept { return m_bandwidth ? std::clamp(m_bandwidth->Fraction, 0.0, 1.0) : 0.0; }
    Microsoft::UI::Xaml::Visibility SourceDetailsViewModel::MeterVisibility() const noexcept { return VisibleIf(m_bandwidth && m_bandwidth->Fraction > 0.0); }
    // More than the line can carry: the bar takes the warning's colour.
    Microsoft::UI::Xaml::Visibility SourceDetailsViewModel::MeterNormalVisibility() const noexcept { return VisibleIf(m_warning.empty()); }
    Microsoft::UI::Xaml::Visibility SourceDetailsViewModel::MeterOverVisibility() const noexcept { return VisibleIf(!m_warning.empty()); }
    winrt::HaloDesktop::SourceSaveViewModel SourceDetailsViewModel::Save() const { return m_save; }
    Microsoft::UI::Xaml::Visibility SourceDetailsViewModel::SaveVisibility() const noexcept { return VisibleIf(m_save != nullptr); }
    winrt::hstring SourceDetailsViewModel::CopyLabel() const { return m_copied ? winrt::hstring{ L"Copied" } : winrt::hstring{ L"Copy file name" }; }
    Microsoft::UI::Xaml::Visibility SourceDetailsViewModel::CopyGlyphVisibility() const noexcept { return VisibleIf(!m_copied); }
    Microsoft::UI::Xaml::Visibility SourceDetailsViewModel::CopiedGlyphVisibility() const noexcept { return VisibleIf(m_copied); }
    void SourceDetailsViewModel::SetCopied(bool copied)
    {
        if (m_copied == copied) return;
        m_copied = copied;
        for (auto const* property : { L"CopyLabel", L"CopyGlyphVisibility", L"CopiedGlyphVisibility" })
        {
            ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, property);
        }
    }
    winrt::event_token SourceDetailsViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler) { return m_propertyChanged.add(handler); }
    void SourceDetailsViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }

    // --- Footer -----------------------------------------------------------

    SourceProviderItemViewModel::SourceProviderItemViewModel(winrt::hstring name, winrt::hstring value, bool answered)
        : m_name(std::move(name)), m_value(std::move(value)), m_answered(answered) {}
    winrt::hstring SourceProviderItemViewModel::Name() const { return m_name; }
    winrt::hstring SourceProviderItemViewModel::Value() const { return m_value; }
    Microsoft::UI::Xaml::Visibility SourceProviderItemViewModel::AnsweredVisibility() const noexcept { return VisibleIf(m_answered); }
    Microsoft::UI::Xaml::Visibility SourceProviderItemViewModel::FailedVisibility() const noexcept { return VisibleIf(!m_answered); }

    SourceQualityItemViewModel::SourceQualityItemViewModel(Sources::QualityTier tier, std::size_t count, double share)
        : m_tier(tier), m_count(count), m_share(share) {}
    winrt::hstring SourceQualityItemViewModel::Label() const { return Sources::TierLabel(m_tier); }
    winrt::hstring SourceQualityItemViewModel::Count() const { return winrt::to_hstring(m_count); }
    double SourceQualityItemViewModel::Share() const noexcept { return m_share; }
    Microsoft::UI::Xaml::Visibility SourceQualityItemViewModel::TopTierVisibility() const noexcept { return VisibleIf(m_tier == Sources::QualityTier::UltraHd); }
    Microsoft::UI::Xaml::Visibility SourceQualityItemViewModel::MidTierVisibility() const noexcept { return VisibleIf(m_tier == Sources::QualityTier::FullHd); }
    Microsoft::UI::Xaml::Visibility SourceQualityItemViewModel::LowTierVisibility() const noexcept
    {
        return VisibleIf(m_tier != Sources::QualityTier::UltraHd && m_tier != Sources::QualityTier::FullHd);
    }

    SourcePickerRuleViewModel::SourcePickerRuleViewModel(winrt::hstring name, winrt::hstring value)
        : m_name(std::move(name)), m_value(std::move(value)) {}
    winrt::hstring SourcePickerRuleViewModel::Name() const { return m_name; }
    winrt::hstring SourcePickerRuleViewModel::Value() const { return m_value; }

    // --- List item --------------------------------------------------------

    SourceDisplayItemViewModel::SourceDisplayItemViewModel(
        Sources::ListItem const& item,
        winrt::HaloDesktop::SourceSaveViewModel save,
        winrt::HaloDesktop::SourceDetailsViewModel details)
        : m_kind(item.Type),
          m_groupName(item.Name),
          m_groupCount(winrt::to_hstring(item.Count)),
          m_showMoreLabel(item.Label),
          m_key(item.Entry.Key),
          m_medal(Sources::MedalOf(item.Entry)),
          m_status(item.Entry.Status),
          m_statusLabel(item.Status),
          m_meta(item.Meta),
          m_save(std::move(save)),
          m_details(std::move(details))
    {
        if (m_kind == Sources::ListItem::Kind::Row) m_playName = PlayNameFor(item, m_medal);
    }

    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::HeaderVisibility() const noexcept { return VisibleIf(m_kind == Sources::ListItem::Kind::Header); }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::RowVisibility() const noexcept { return VisibleIf(m_kind == Sources::ListItem::Kind::Row); }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::ShowMoreVisibility() const noexcept { return VisibleIf(m_kind == Sources::ListItem::Kind::More); }
    winrt::hstring SourceDisplayItemViewModel::GroupName() const { return m_groupName; }
    winrt::hstring SourceDisplayItemViewModel::GroupCount() const { return m_groupCount; }
    winrt::hstring SourceDisplayItemViewModel::ShowMoreLabel() const { return m_showMoreLabel; }
    winrt::hstring SourceDisplayItemViewModel::Key() const { return m_key; }
    winrt::hstring SourceDisplayItemViewModel::MedalPlate() const { return winrt::hstring{ m_medal.Plate }; }
    winrt::hstring SourceDisplayItemViewModel::MedalTag() const { return winrt::hstring{ m_medal.Tag }; }
    winrt::HaloDesktop::QualityMedalFinish SourceDisplayItemViewModel::MedalFinish() const { return ::HaloDesktop::ViewModels::ToQualityMedalFinish(m_medal.Finish); }
    winrt::hstring SourceDisplayItemViewModel::StatusLabel() const { return m_statusLabel; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::InstantVisibility() const noexcept { return VisibleIf(m_status == Sources::SourceStatus::Instant); }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::CachingVisibility() const noexcept { return VisibleIf(m_status == Sources::SourceStatus::Caching); }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::ColdVisibility() const noexcept
    {
        return VisibleIf(m_status == Sources::SourceStatus::Uncached || m_status == Sources::SourceStatus::Unknown);
    }
    winrt::hstring SourceDisplayItemViewModel::MetaLine() const { return m_meta; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::MetaVisibility() const noexcept { return VisibleIf(!m_meta.empty()); }
    winrt::hstring SourceDisplayItemViewModel::PlayName() const { return m_playName; }
    winrt::HaloDesktop::SourceSaveViewModel SourceDisplayItemViewModel::Save() const { return m_save; }
    winrt::HaloDesktop::SourceDetailsViewModel SourceDisplayItemViewModel::Details() const { return m_details; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::ExpandedVisibility() const noexcept { return VisibleIf(m_expanded); }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::ExpandGlyphVisibility() const noexcept { return VisibleIf(!m_expanded); }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::CollapseGlyphVisibility() const noexcept { return VisibleIf(m_expanded); }
    winrt::hstring SourceDisplayItemViewModel::ChevronName() const { return m_expanded ? L"Hide source details" : L"Show source details"; }
    winrt::hstring SourceDisplayItemViewModel::ChevronTip() const { return m_expanded ? L"Hide details" : L"Details"; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::SelectionVisibility() const noexcept { return VisibleIf(m_selected); }
    bool SourceDisplayItemViewModel::IsRow() const noexcept { return m_kind == Sources::ListItem::Kind::Row; }

    void SourceDisplayItemViewModel::SetExpanded(bool value)
    {
        if (m_expanded == value) return;
        m_expanded = value;
        for (auto const* property : { L"ExpandedVisibility", L"ExpandGlyphVisibility", L"CollapseGlyphVisibility", L"ChevronName", L"ChevronTip" })
        {
            Raise(property);
        }
    }

    void SourceDisplayItemViewModel::SetSelected(bool value)
    {
        if (m_selected == value) return;
        m_selected = value;
        Raise(L"SelectionVisibility");
    }

    winrt::event_token SourceDisplayItemViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler) { return m_propertyChanged.add(handler); }
    void SourceDisplayItemViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }
    void SourceDisplayItemViewModel::Raise(wchar_t const* property) { ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, property); }

    // --- Sheet ------------------------------------------------------------

    SourcesViewModel::SourcesViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_sources(services.Sources),
          m_metadata(services.Metadata),
          m_navigation(services.Navigation),
          m_settings(services.SettingsSync),
          m_downloads(services.Downloads),
          m_devicePreferences(services.DevicePreferences),
          m_watch(services.WatchState),
          m_items(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_providerItems(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_qualityItems(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_pickerRules(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>())
    {
        if (!m_sources || !m_navigation || !m_settings || !m_downloads || !m_devicePreferences || !m_watch || !m_metadata)
        {
            throw std::invalid_argument{ "SourcesViewModel requires its dependencies." };
        }
    }

    SourcesViewModel::~SourcesViewModel() { Deactivate(); }

    void SourcesViewModel::Activate()
    {
        if (m_downloadToken != 0) return;
        // A download moving changes the saved copy card and which source wears a
        // save state; the ranked list itself is the addons' and stays as it is.
        m_downloadToken = m_downloads->AddChangedHandler([weak = get_weak()]()
        {
            auto const self = weak.get();
            if (!self) return;
            self->ReadDownloads();
            self->RefreshSaveStates();
            self->RaiseOffline();
        });
        ReadDownloads();
        RefreshSaveStates();
        RaiseOffline();
    }

    void SourcesViewModel::Deactivate() noexcept
    {
        if (m_downloadToken == 0) return;
        m_downloads->RemoveChangedHandler(m_downloadToken);
        m_downloadToken = 0;
    }

    winrt::Windows::Foundation::IInspectable SourcesViewModel::Items() const { return m_items; }
    winrt::Windows::Foundation::IInspectable SourcesViewModel::ProviderItems() const { return m_providerItems; }
    winrt::Windows::Foundation::IInspectable SourcesViewModel::QualityItems() const { return m_qualityItems; }
    winrt::Windows::Foundation::IInspectable SourcesViewModel::PickerRules() const { return m_pickerRules; }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> SourcesViewModel::ItemsView() const { return m_items; }

    winrt::hstring SourcesViewModel::Kicker() const
    {
        if (!m_parameters) return L"";
        return Sources::KickerFor(m_parameters.Title(), m_parameters.ShowName(), m_parameters.EpisodeLabel());
    }

    Microsoft::UI::Xaml::Visibility SourcesViewModel::KickerVisibility() const noexcept { return VisibleIf(!Kicker().empty()); }

    winrt::hstring SourcesViewModel::Heading() const
    {
        auto const title = m_parameters ? m_parameters.Title() : winrt::hstring{};
        return title.empty() ? winrt::hstring{ L"Choose a source" } : title;
    }

    winrt::hstring SourcesViewModel::CountLine() const
    {
        if (m_loading) return L"Asking your addons\x2026";
        if (m_error) return L"No addon could be reached";
        return m_resolve.Summary;
    }

    Microsoft::UI::Xaml::Visibility SourcesViewModel::OfflineVisibility() const noexcept { return VisibleIf(m_offline.has_value()); }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::OfflineMedalVisibility() const noexcept { return VisibleIf(m_offline && m_offline->Medal); }
    winrt::hstring SourcesViewModel::OfflinePlate() const { return m_offline && m_offline->Medal ? winrt::hstring{ m_offline->Medal->Plate } : winrt::hstring{}; }
    winrt::hstring SourcesViewModel::OfflineTag() const { return m_offline && m_offline->Medal ? winrt::hstring{ m_offline->Medal->Tag } : winrt::hstring{}; }
    winrt::HaloDesktop::QualityMedalFinish SourcesViewModel::OfflineFinish() const
    {
        return m_offline && m_offline->Medal
            ? ::HaloDesktop::ViewModels::ToQualityMedalFinish(m_offline->Medal->Finish)
            : winrt::HaloDesktop::QualityMedalFinish::Plain;
    }
    winrt::hstring SourcesViewModel::OfflineMeta() const { return m_offline ? m_offline->Meta : winrt::hstring{}; }

    winrt::hstring SourcesViewModel::AllFilterCount() const { return winrt::to_hstring(m_counts.All); }
    winrt::hstring SourcesViewModel::UltraHdFilterCount() const { return winrt::to_hstring(m_counts.UltraHd); }
    winrt::hstring SourcesViewModel::FullHdFilterCount() const { return winrt::to_hstring(m_counts.FullHd); }
    winrt::hstring SourcesViewModel::HdFilterCount() const { return winrt::to_hstring(m_counts.Hd); }
    winrt::hstring SourcesViewModel::SortLabel() const { return winrt::hstring{ L"Sort: " } + Sources::SortName(SortAt(m_sortIndex)); }
    std::int32_t SourcesViewModel::FilterIndex() const noexcept { return m_filterIndex; }
    std::int32_t SourcesViewModel::SortIndex() const noexcept { return m_sortIndex; }

    bool SourcesViewModel::PoolEmpty() const noexcept { return m_resolve.Pool.empty(); }
    bool SourcesViewModel::FilteredEmpty() const noexcept { return m_counts.For(FilterAt(m_filterIndex)) == 0; }

    Microsoft::UI::Xaml::Visibility SourcesViewModel::FilterRowVisibility() const noexcept { return VisibleIf(!m_loading && !PoolEmpty()); }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::ListVisibility() const noexcept { return VisibleIf(!m_loading && !PoolEmpty() && !FilteredEmpty()); }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::ResolvingVisibility() const noexcept { return VisibleIf(m_loading); }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::EmptyVisibility() const noexcept { return VisibleIf(!m_loading && (PoolEmpty() || FilteredEmpty())); }

    winrt::hstring SourcesViewModel::EmptyTitle() const
    {
        if (m_error) return L"Nothing could be reached";
        if (PoolEmpty()) return L"Nothing playable came back";
        return L"No source matches that filter";
    }

    winrt::hstring SourcesViewModel::EmptyBody() const
    {
        if (m_error)
        {
            return m_offline
                ? winrt::hstring{ L"Halo could not reach any provider. The copy saved on this device still plays without a connection." }
                : winrt::hstring{ L"Halo could not reach any provider, and nothing for this is saved on this device. "
                                  L"Anything you download appears here and plays without a connection." };
        }
        if (PoolEmpty())
        {
            return L"Every provider answered, and none of them had a file for this. "
                   L"This usually clears up within a day of release.";
        }
        return L"Try All, or widen the quality you are willing to accept.";
    }

    Microsoft::UI::Xaml::Visibility SourcesViewModel::BannerVisibility() const noexcept { return VisibleIf(!m_loading && (m_error || m_resolve.FailedProviders > 0)); }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::BannerCautionVisibility() const noexcept { return VisibleIf(!m_loading && !m_error && m_resolve.FailedProviders > 0); }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::BannerInfoVisibility() const noexcept { return VisibleIf(!m_loading && m_error); }

    winrt::hstring SourcesViewModel::BannerTitle() const
    {
        if (m_error) return L"You are offline";
        if (m_resolve.FailedProviders == 1) return m_resolve.FirstFailure;
        return winrt::to_hstring(m_resolve.FailedProviders) + L" providers did not answer";
    }

    winrt::hstring SourcesViewModel::BannerBody() const
    {
        if (m_error) return L"Only files already downloaded to this device can play right now.";
        if (m_resolve.AnsweredProviders == 0) return L"Nothing was returned, so some sources may be missing.";
        return L"Showing what the other "
            + Sources::CountLabel(m_resolve.AnsweredProviders, L"provider", L"providers")
            + L" returned. Some sources may be missing.";
    }

    winrt::hstring SourcesViewModel::BannerAction() const
    {
        return m_error ? winrt::hstring{ L"Retry connection" } : winrt::hstring{ L"Ask them again" };
    }

    bool SourcesViewModel::HasPick() const noexcept
    {
        return !m_loading && m_listing.Best.has_value();
    }

    Microsoft::UI::Xaml::Visibility SourcesViewModel::PickVisibility() const noexcept { return VisibleIf(HasPick()); }
    winrt::hstring SourcesViewModel::PickPlate() const { return HasPick() ? winrt::hstring{ Sources::MedalOf(m_listing.Best->Entry).Plate } : winrt::hstring{}; }
    winrt::hstring SourcesViewModel::PickTag() const { return HasPick() ? winrt::hstring{ Sources::MedalOf(m_listing.Best->Entry).Tag } : winrt::hstring{}; }
    winrt::HaloDesktop::QualityMedalFinish SourcesViewModel::PickFinish() const
    {
        return HasPick()
            ? ::HaloDesktop::ViewModels::ToQualityMedalFinish(Sources::MedalOf(m_listing.Best->Entry).Finish)
            : winrt::HaloDesktop::QualityMedalFinish::Plain;
    }
    winrt::hstring SourcesViewModel::PickStatusLabel() const { return HasPick() ? m_listing.Best->Status : winrt::hstring{}; }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::PickInstantVisibility() const noexcept
    {
        return VisibleIf(HasPick() && m_listing.Best->Entry.Status == Sources::SourceStatus::Instant);
    }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::PickCachingVisibility() const noexcept
    {
        return VisibleIf(HasPick() && m_listing.Best->Entry.Status == Sources::SourceStatus::Caching);
    }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::PickColdVisibility() const noexcept
    {
        return VisibleIf(HasPick()
            && (m_listing.Best->Entry.Status == Sources::SourceStatus::Uncached
                || m_listing.Best->Entry.Status == Sources::SourceStatus::Unknown));
    }
    winrt::hstring SourcesViewModel::PickMetaLine() const { return HasPick() ? m_listing.Best->Meta : winrt::hstring{}; }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::PickMetaVisibility() const noexcept { return VisibleIf(HasPick() && !m_listing.Best->Meta.empty()); }
    winrt::hstring SourcesViewModel::PickFileName() const { return HasPick() ? m_listing.Best->Entry.File : winrt::hstring{}; }
    winrt::HaloDesktop::SourceSaveViewModel SourcesViewModel::PickSave() const { return m_pickSave; }
    winrt::HaloDesktop::SourceDetailsViewModel SourcesViewModel::PickDetails() const { return m_pickDetails; }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::PickSelectionVisibility() const noexcept { return VisibleIf(HasPick() && m_selectedIndex < 0); }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::PickExpandedVisibility() const noexcept { return VisibleIf(HasPick() && m_pickExpanded); }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::PickExpandGlyphVisibility() const noexcept { return VisibleIf(!(HasPick() && m_pickExpanded)); }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::PickCollapseGlyphVisibility() const noexcept { return VisibleIf(HasPick() && m_pickExpanded); }
    winrt::hstring SourcesViewModel::PickChevronName() const { return m_pickExpanded ? L"Hide source details" : L"Show source details"; }
    winrt::hstring SourcesViewModel::PickChevronTip() const { return m_pickExpanded ? L"Hide details" : L"Details"; }

    Microsoft::UI::Xaml::Visibility SourcesViewModel::InfoVisibility() const noexcept { return VisibleIf(m_infoOpen); }
    // The footer's chevron points down while open, as the reference's does.
    Microsoft::UI::Xaml::Visibility SourcesViewModel::InfoOpenGlyphVisibility() const noexcept { return VisibleIf(m_infoOpen); }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::InfoClosedGlyphVisibility() const noexcept { return VisibleIf(!m_infoOpen); }

    std::int32_t SourcesViewModel::SelectedIndex() const noexcept { return m_selectedIndex; }

    void SourcesViewModel::Load(winrt::Windows::Foundation::IInspectable const& parameter)
    {
        m_parameters = parameter.try_as<winrt::HaloDesktop::SourcesNavParams>();
        if (!m_parameters)
        {
            if (auto const item = parameter.try_as<winrt::HaloDesktop::ContinueItem>())
            {
                // The episode's title once the item's meta has named it, which makes
                // this the same sheet the title page opens for that episode. Until
                // then the show name stands in, as it always did.
                auto const title = item.EpisodeTitle().empty() ? item.Name() : item.EpisodeTitle();
                m_parameters = winrt::make<winrt::HaloDesktop::implementation::SourcesNavParams>(
                    item.Type(),
                    item.MetaId(),
                    item.VideoId(),
                    item.ItemId(),
                    title,
                    item.Name(),
                    item.Tag(),
                    item.Poster());
            }
        }
        if (!m_parameters)
        {
            m_loading = false;
            m_error = true;
            RaiseState();
            return;
        }
        static_cast<void>(LoadAsync(::HaloDesktop::Services::StreamReuse::Recent));
    }

    void SourcesViewModel::Retry()
    {
        if (m_parameters) static_cast<void>(LoadAsync(::HaloDesktop::Services::StreamReuse::Fresh));
    }

    void SourcesViewModel::ResetView()
    {
        m_expandedKey = L"";
        m_pickExpanded = false;
        m_selectedIndex = -1;
        m_copiedKey = L"";
    }

    void SourcesViewModel::SetFilter(std::int32_t index)
    {
        if (index < 0 || index >= FilterCount || index == m_filterIndex) return;
        m_filterIndex = index;
        ResetView();
        Rebuild();
        RaiseState();
    }

    void SourcesViewModel::SetSort(std::int32_t index)
    {
        if (index < 0 || index >= SortCount || index == m_sortIndex) return;
        m_sortIndex = index;
        ResetView();
        Rebuild();
        RaiseState();
    }

    void SourcesViewModel::ToggleInfo()
    {
        m_infoOpen = !m_infoOpen;
        for (auto const* property : { L"InfoVisibility", L"InfoOpenGlyphVisibility", L"InfoClosedGlyphVisibility" }) Raise(property);
    }

    void SourcesViewModel::ToggleExpanded(winrt::hstring const& key)
    {
        if (key.empty() || IndexOfKey(key) < 0) return;
        MarkCopied(L"");
        m_pickExpanded = false;
        m_expandedKey = m_expandedKey == key ? winrt::hstring{} : key;
        ApplyExpansion();
        SelectKey(key);
    }

    void SourcesViewModel::TogglePickExpanded()
    {
        if (!HasPick()) return;
        MarkCopied(L"");
        m_pickExpanded = !m_pickExpanded;
        m_expandedKey = L"";
        ApplyExpansion();
        SelectPick();
    }

    void SourcesViewModel::ApplyExpansion()
    {
        for (std::uint32_t index{}; index < m_items.Size(); ++index)
        {
            auto* const row = RowAt(index);
            if (row && row->IsRow()) row->SetExpanded(!m_expandedKey.empty() && row->Key() == m_expandedKey);
        }
        for (auto const* property : {
                 L"PickExpandedVisibility", L"PickExpandGlyphVisibility", L"PickCollapseGlyphVisibility",
                 L"PickChevronName", L"PickChevronTip" })
        {
            Raise(property);
        }
    }

    void SourcesViewModel::SelectKey(winrt::hstring const& key)
    {
        auto const index = IndexOfKey(key);
        if (index < 0 || index == m_selectedIndex) return;
        m_selectedIndex = index;
        ApplySelection();
    }

    void SourcesViewModel::SelectPick()
    {
        if (!HasPick() || m_selectedIndex < 0) return;
        m_selectedIndex = -1;
        ApplySelection();
    }

    void SourcesViewModel::MoveSelection(std::int32_t delta)
    {
        if (delta == 0) return;
        // The pick is index -1, then every source row in list order.
        std::vector<std::int32_t> stops;
        if (HasPick()) stops.push_back(-1);
        for (std::uint32_t index{}; index < m_items.Size(); ++index)
        {
            auto* const row = RowAt(index);
            if (row && row->IsRow()) stops.push_back(static_cast<std::int32_t>(index));
        }
        if (stops.empty()) return;

        // Nothing selected yet lands on the first stop rather than past it.
        auto const found = std::find(stops.begin(), stops.end(), m_selectedIndex);
        auto position = found == stops.end()
            ? 0
            : std::clamp(static_cast<std::int32_t>(std::distance(stops.begin(), found)) + delta, 0, static_cast<std::int32_t>(stops.size()) - 1);
        m_selectedIndex = stops[static_cast<std::size_t>(position)];
        ApplySelection();
    }

    void SourcesViewModel::ExpandSelected()
    {
        auto const key = SelectedKey();
        if (key.empty())
        {
            if (HasPick() && !m_pickExpanded) TogglePickExpanded();
            return;
        }
        if (m_expandedKey != key) ToggleExpanded(key);
    }

    void SourcesViewModel::CollapseSelected()
    {
        if (m_pickExpanded)
        {
            TogglePickExpanded();
            return;
        }
        if (!m_expandedKey.empty()) ToggleExpanded(m_expandedKey);
    }

    void SourcesViewModel::RevealCold()
    {
        if (m_coldRevealed) return;
        // Rebuilt rows are new items, so the selection is carried over by key.
        auto const selected = SelectedKey();
        m_coldRevealed = true;
        Rebuild();
        m_selectedIndex = selected.empty() ? -1 : IndexOfKey(selected);
        ApplySelection();
    }

    void SourcesViewModel::PlaySelected()
    {
        auto const key = SelectedKey();
        OpenPlayer(key.empty() ? PickKey() : key);
    }

    void SourcesViewModel::OpenPlayer(winrt::hstring const& key)
    {
        if (key.empty()) return;
        auto const request = m_sources->BuildPlaybackRequest(key);
        if (!request) return;
        m_navigation->ShowOverlay(::HaloDesktop::Services::Page::Player, request);
        m_navigation->CloseSheet();
    }

    bool SourcesViewModel::PlayOffline()
    {
        // Read again rather than trusted from the card: the file may have been
        // deleted while the sheet was open. Played through the download service,
        // which carries the download id the offline subtitle and up-next key off,
        // and never through a network source, whatever the resolve answered.
        ReadDownloads();
        RaiseOffline();
        if (!m_videoDownloads.Ready) return false;
        auto const request = m_downloads->BuildPlaybackRequest(winrt::hstring{ m_videoDownloads.Ready->JobId });
        if (!request) return false;
        m_navigation->ShowOverlay(::HaloDesktop::Services::Page::Player, request);
        m_navigation->CloseSheet();
        return true;
    }

    void SourcesViewModel::OpenSettings()
    {
        m_navigation->CloseSheet();
        m_navigation->GoTo(::HaloDesktop::Services::Page::Settings);
    }

    void SourcesViewModel::Close() { m_navigation->CloseSheet(); }

    winrt::hstring SourcesViewModel::FileNameFor(winrt::hstring const& key) const
    {
        auto const* record = m_resolve.Find(key);
        return record ? record->Info.Filename : winrt::hstring{};
    }

    void SourcesViewModel::MarkCopied(winrt::hstring const& key)
    {
        m_copiedKey = key;
        if (m_pickDetails)
        {
            winrt::get_self<SourceDetailsViewModel>(m_pickDetails)->SetCopied(!key.empty() && m_pickDetails.Key() == key);
        }
        for (std::uint32_t index{}; index < m_items.Size(); ++index)
        {
            auto* const row = RowAt(index);
            if (row && row->IsRow() && row->Details())
            {
                winrt::get_self<SourceDetailsViewModel>(row->Details())->SetCopied(!key.empty() && row->Key() == key);
            }
        }
    }

    void SourcesViewModel::SetStartingKey(winrt::hstring const& key)
    {
        if (m_startingKey == key) return;
        m_startingKey = key;
        RefreshSaveStates();
    }

    concurrency::task<::HaloDesktop::Services::DownloadStartResult> SourcesViewModel::StartDownloadAsync(
        winrt::hstring key,
        bool replaceExisting)
    {
        co_return co_await m_sources->StartDownloadAsync(std::move(key), replaceExisting);
    }

    winrt::hstring SourcesViewModel::SelectedKey() const
    {
        if (m_selectedIndex < 0) return {};
        auto* const row = RowAt(static_cast<std::uint32_t>(m_selectedIndex));
        return row && row->IsRow() ? row->Key() : winrt::hstring{};
    }

    winrt::hstring SourcesViewModel::PickKey() const
    {
        return HasPick() ? m_listing.Best->Entry.Key : winrt::hstring{};
    }

    winrt::event_token SourcesViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }
    void SourcesViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }

    winrt::Windows::Foundation::IAsyncAction SourcesViewModel::LoadAsync(::HaloDesktop::Services::StreamReuse reuse)
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        auto const version = ++m_loadVersion;
        m_loading = true;
        m_error = false;
        m_filterIndex = 0;
        m_sortIndex = 0;
        m_coldRevealed = false;
        m_startingKey = L"";
        ResetView();
        m_resolve = {};
        m_counts = {};
        m_listing = {};
        Rebuild();
        // The saved copy is shown whatever the addons answer, so it is read before
        // they are asked.
        ReadDownloads();
        RaiseState();
        RaiseOffline();

        bool failed{};
        try { co_await m_sources->LoadAsync(m_parameters, reuse); }
        catch (...) { failed = true; }
        try { co_await m_settings->LoadAsync(); }
        catch (...) {}
        co_await uiContext;
        if (version != m_loadVersion) co_return;

        m_loading = false;
        m_error = failed;
        AdoptResolve();
        ReadDownloads();
        Rebuild();
        RebuildFooter();
        RaiseState();
        RaiseOffline();
    }

    // Ranks what the service answered against this device. Cheap: the release
    // names were parsed off the UI thread when the answer arrived.
    void SourcesViewModel::AdoptResolve()
    {
        m_device = {};
        m_device.LineMbps = m_devicePreferences->MeasuredLineMbps();
        if (m_parameters)
        {
            if (auto const row = m_watch->Find(m_parameters.VideoId()); row && row->DurationSec > 0.0)
            {
                m_device.DurationSeconds = row->DurationSec;
            }
            // Only the metadata that belongs to this title may stand in for the
            // runtime; the service holds whichever title was opened last.
            if (m_device.DurationSeconds <= 0.0)
            {
                auto const detail = m_metadata->Detail();
                if (detail && detail.Id() == m_parameters.MetaId())
                {
                    m_device.DurationSeconds = m_metadata->RuntimeMinutes() * 60.0;
                }
            }
        }

        // A failed resolve shows none of the addons' answer, the service's last
        // one included: it may belong to another title.
        auto const parsed = m_sources->Current();
        m_resolve = m_error || !parsed
            ? Sources::Resolve{ .Summary = Sources::BuildResolveSummary(0, 0, 0) }
            : Sources::ResolveSources(*parsed, m_device.DurationSeconds);
        m_counts = Sources::CountFilters(m_resolve.Pool);
    }

    void SourcesViewModel::ReadDownloads()
    {
        m_videoDownloads = m_parameters
            ? Sources::ChooseVideoDownloads(m_downloads->DownloadsFor(m_parameters.VideoId()))
            : Sources::VideoDownloads{};
        auto const& ready = m_videoDownloads.Ready;
        if (!ready)
        {
            m_offline.reset();
            m_offlineSignature.clear();
            return;
        }
        auto signature = ready->JobId + L'\n' + ready->ReleaseName + L'\n'
            + ready->StreamName.value_or(L"") + L'\n' + ready->StreamTitle.value_or(L"") + L'\n'
            + std::to_wstring(ready->SizeBytes) + L'\n' + ready->SubtitleLanguage.value_or(L"");
        if (m_offline && signature == m_offlineSignature) return;
        m_offline = Sources::OfflineCopyFor(*ready);
        m_offlineSignature = std::move(signature);
    }

    Sources::SaveState SourcesViewModel::SaveStateOf(winrt::hstring const& key) const
    {
        auto const& download = m_videoDownloads.Current;
        return Sources::SaveStateFor(
            m_resolve.Find(key),
            download ? &*download : nullptr,
            !m_startingKey.empty() && m_startingKey == key);
    }

    winrt::HaloDesktop::SourceSaveViewModel SourcesViewModel::SaveFor(winrt::hstring const& key)
    {
        auto const found = m_saves.find(ToKey(key));
        if (found != m_saves.end()) return found->second;
        auto save = winrt::make<SourceSaveViewModel>(key, SaveStateOf(key));
        m_saves.emplace(ToKey(key), save);
        return save;
    }

    void SourcesViewModel::RefreshSaveStates()
    {
        for (auto const& [key, save] : m_saves)
        {
            winrt::get_self<SourceSaveViewModel>(save)->SetState(SaveStateOf(winrt::hstring{ key }));
        }
    }

    winrt::HaloDesktop::SourceDetailsViewModel SourcesViewModel::DetailsFor(
        Sources::SourceEntry const& entry,
        winrt::hstring const& warning,
        Sources::SourceDetails const& details,
        bool withSave)
    {
        return winrt::make<SourceDetailsViewModel>(
            entry.Key,
            entry.File,
            warning,
            details,
            withSave ? SaveFor(entry.Key) : winrt::HaloDesktop::SourceSaveViewModel{ nullptr });
    }

    void SourcesViewModel::Rebuild()
    {
        m_items.Clear();
        m_saves.clear();
        m_pickSave = nullptr;
        m_pickDetails = nullptr;
        if (m_loading)
        {
            m_listing = {};
            return;
        }

        m_listing = Sources::BuildListing(m_resolve, Sources::ListingOptions{
            .Filter = FilterAt(m_filterIndex),
            .Sort = SortAt(m_sortIndex),
            .ColdRevealed = m_coldRevealed,
            .Device = m_device,
        });
        if (m_listing.Best)
        {
            auto const& pick = *m_listing.Best;
            m_pickSave = SaveFor(pick.Entry.Key);
            // The pick has its own Save on the card, so its details carry none.
            m_pickDetails = DetailsFor(pick.Entry, pick.Warning, pick.Details, false);
        }
        for (auto const& item : m_listing.Items)
        {
            if (item.Type != Sources::ListItem::Kind::Row)
            {
                m_items.Append(winrt::make<SourceDisplayItemViewModel>(item, nullptr, nullptr));
                continue;
            }
            m_items.Append(winrt::make<SourceDisplayItemViewModel>(
                item,
                SaveFor(item.Entry.Key),
                DetailsFor(item.Entry, item.Warning, item.Details, true)));
        }
        ApplyExpansion();
        MarkCopied(m_copiedKey);
    }

    void SourcesViewModel::RebuildFooter()
    {
        m_providerItems.Clear();
        for (auto const& provider : m_resolve.Providers)
        {
            m_providerItems.Append(winrt::make<SourceProviderItemViewModel>(provider.Name, provider.Value, provider.Answered));
        }

        m_qualityItems.Clear();
        for (auto const& share : Sources::QualityMix(m_resolve.Pool))
        {
            m_qualityItems.Append(winrt::make<SourceQualityItemViewModel>(share.Tier, share.Count, share.Share));
        }

        std::optional<winrt::hstring> preferredSubtitles;
        try { preferredSubtitles = m_settings->PreferredSubtitleLanguage(); }
        catch (...) { preferredSubtitles.reset(); }
        m_pickerRules.Clear();
        m_pickerRules.Append(winrt::make<SourcePickerRuleViewModel>(L"Prefer sources that play instantly", L"On"));
        m_pickerRules.Append(winrt::make<SourcePickerRuleViewModel>(
            L"Preferred audio language",
            Sources::LanguageName(m_settings->PreferredAudioLanguage().value_or(L"Automatic"))));
        m_pickerRules.Append(winrt::make<SourcePickerRuleViewModel>(
            L"Preferred subtitles",
            preferredSubtitles && !preferredSubtitles->empty()
                ? Sources::LanguageName(*preferredSubtitles)
                : winrt::hstring{ L"Off" }));
        m_pickerRules.Append(winrt::make<SourcePickerRuleViewModel>(
            L"Autoplay next episode",
            m_settings->AutoplayNextEpisode() ? L"On" : L"Off"));

        Raise(L"ProviderItems");
        Raise(L"QualityItems");
        Raise(L"PickerRules");
    }

    SourceDisplayItemViewModel* SourcesViewModel::RowAt(std::uint32_t index) const
    {
        if (index >= m_items.Size()) return nullptr;
        auto const item = m_items.GetAt(index).try_as<winrt::HaloDesktop::SourceDisplayItemViewModel>();
        return item ? winrt::get_self<SourceDisplayItemViewModel>(item) : nullptr;
    }

    std::int32_t SourcesViewModel::IndexOfKey(winrt::hstring const& key) const
    {
        if (key.empty()) return -1;
        for (std::uint32_t index{}; index < m_items.Size(); ++index)
        {
            auto* const row = RowAt(index);
            if (row && row->IsRow() && row->Key() == key) return static_cast<std::int32_t>(index);
        }
        return -1;
    }

    void SourcesViewModel::ApplySelection()
    {
        for (std::uint32_t index{}; index < m_items.Size(); ++index)
        {
            auto* const row = RowAt(index);
            if (row && row->IsRow()) row->SetSelected(static_cast<std::int32_t>(index) == m_selectedIndex);
        }
        Raise(L"PickSelectionVisibility");
        Raise(L"SelectedIndex");
    }

    void SourcesViewModel::RaiseOffline()
    {
        for (auto const* property : {
                 L"OfflineVisibility", L"OfflineMedalVisibility", L"OfflinePlate", L"OfflineTag",
                 L"OfflineFinish", L"OfflineMeta", L"EmptyBody" })
        {
            Raise(property);
        }
    }

    void SourcesViewModel::RaisePick()
    {
        for (auto const* property : {
                 L"PickVisibility", L"PickPlate", L"PickTag", L"PickFinish", L"PickStatusLabel",
                 L"PickInstantVisibility", L"PickCachingVisibility", L"PickColdVisibility",
                 L"PickMetaLine", L"PickMetaVisibility", L"PickFileName", L"PickSave", L"PickDetails",
                 L"PickSelectionVisibility", L"PickExpandedVisibility", L"PickExpandGlyphVisibility",
                 L"PickCollapseGlyphVisibility", L"PickChevronName", L"PickChevronTip" })
        {
            Raise(property);
        }
    }

    void SourcesViewModel::RaiseState()
    {
        for (auto const* property : {
                 L"Items", L"Kicker", L"KickerVisibility", L"Heading", L"CountLine",
                 L"AllFilterCount", L"UltraHdFilterCount", L"FullHdFilterCount", L"HdFilterCount",
                 L"SortLabel", L"FilterIndex", L"SortIndex",
                 L"FilterRowVisibility", L"ListVisibility", L"ResolvingVisibility", L"EmptyVisibility",
                 L"EmptyTitle", L"EmptyBody",
                 L"BannerVisibility", L"BannerCautionVisibility", L"BannerInfoVisibility",
                 L"BannerTitle", L"BannerBody", L"BannerAction", L"SelectedIndex" })
        {
            Raise(property);
        }
        RaisePick();
    }

    void SourcesViewModel::Raise(wchar_t const* property)
    {
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, property);
    }
}
