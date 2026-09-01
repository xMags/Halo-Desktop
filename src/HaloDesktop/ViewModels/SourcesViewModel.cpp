#include "pch.h"
#include "ViewModels/SourcesViewModel.h"
#if __has_include("SourceDetailChipViewModel.g.cpp")
#include "SourceDetailChipViewModel.g.cpp"
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
#if __has_include("SourceSpecItemViewModel.g.cpp")
#include "SourceSpecItemViewModel.g.cpp"
#endif
#if __has_include("SourcesViewModel.g.cpp")
#include "SourcesViewModel.g.cpp"
#endif

#include "Models/Models.h"
#include "Services/DevicePreferencesStore.h"
#include "Services/NavigationService.h"
#include "Services/SettingsSyncService.h"
#include "Services/WatchStateService.h"
#include "Shell/LayoutMetricsService.h"
#include "ViewModels/ObservableHelper.h"

#include <algorithm>
#include <utility>

namespace
{
    namespace Sources = ::HaloDesktop::Sources;

    auto const Visible = winrt::Microsoft::UI::Xaml::Visibility::Visible;
    auto const Collapsed = winrt::Microsoft::UI::Xaml::Visibility::Collapsed;

    constexpr std::int32_t FilterAll = 0;
    constexpr std::int32_t FilterPlaysNow = 1;
    constexpr std::int32_t FilterUltraHd = 2;
    constexpr std::int32_t FilterFullHd = 3;
    constexpr std::int32_t FilterHd = 4;
    constexpr std::int32_t FilterCount = 5;

    constexpr std::int32_t SortRecommended = 0;
    constexpr std::int32_t SortBestPicture = 1;
    constexpr std::int32_t SortSmallestFile = 2;
    constexpr std::int32_t SortFastestStart = 3;
    constexpr std::int32_t SortCount = 4;

    // Named the same way in the list heading and in the footer's provider column,
    // where it sits beside the addons that answered even though it is not one.
    constexpr wchar_t const* LocalProviderName = L"On this device";

    bool IsOnDisk(Sources::SourceEntry const& entry) noexcept
    {
        return entry.Source && entry.Source.Status() == winrt::HaloDesktop::StreamStatus::OnDisk;
    }

    // 700 px at the shipped window size, 880 px once the content area is wide
    // enough that the sheet would otherwise leave the list looking stranded.
    constexpr double NarrowSheetWidth = 700.0;
    constexpr double WideSheetWidth = 880.0;

    wchar_t const* SortName(std::int32_t index)
    {
        switch (index)
        {
        case SortBestPicture: return L"Best picture";
        case SortSmallestFile: return L"Smallest file";
        case SortFastestStart: return L"Fastest start";
        default: break;
        }
        return L"Recommended";
    }

    wchar_t const* FilterName(std::int32_t index)
    {
        switch (index)
        {
        case FilterPlaysNow: return L"plays now";
        case FilterUltraHd: return L"4K";
        case FilterFullHd: return L"Full HD";
        case FilterHd: return L"HD";
        default: break;
        }
        return L"all";
    }

    void SortEntries(std::vector<Sources::SourceEntry>& entries, std::int32_t sortIndex)
    {
        switch (sortIndex)
        {
        case SortBestPicture:
            std::stable_sort(entries.begin(), entries.end(), [](auto const& left, auto const& right)
            {
                if (left.Tier != right.Tier) return left.Tier < right.Tier;
                return left.SizeBytes > right.SizeBytes;
            });
            return;
        case SortSmallestFile:
            std::stable_sort(entries.begin(), entries.end(), [](auto const& left, auto const& right)
            {
                // A source that never reported a size cannot claim to be the
                // smallest, so the unknowns sink instead of leading the list.
                if ((left.SizeBytes == 0) != (right.SizeBytes == 0)) return right.SizeBytes == 0;
                return left.SizeBytes < right.SizeBytes;
            });
            return;
        case SortFastestStart:
            std::stable_sort(entries.begin(), entries.end(), [](auto const& left, auto const& right)
            {
                if (left.Speed != right.Speed) return left.Speed < right.Speed;
                return left.Rank < right.Rank;
            });
            return;
        default:
            std::stable_sort(entries.begin(), entries.end(), [](auto const& left, auto const& right)
            {
                return left.Rank < right.Rank;
            });
            return;
        }
    }
}

namespace winrt::HaloDesktop::implementation
{
    SourceSpecItemViewModel::SourceSpecItemViewModel(winrt::hstring key, winrt::hstring value)
        : m_key(std::move(key)), m_value(std::move(value)) {}
    winrt::hstring SourceSpecItemViewModel::Key() const { return m_key; }
    winrt::hstring SourceSpecItemViewModel::Value() const { return m_value; }

    SourceDetailChipViewModel::SourceDetailChipViewModel(winrt::hstring label, bool muted)
        : m_label(std::move(label)), m_muted(muted) {}
    winrt::hstring SourceDetailChipViewModel::Label() const { return m_label; }
    Microsoft::UI::Xaml::Visibility SourceDetailChipViewModel::NormalVisibility() const noexcept { return m_muted ? Collapsed : Visible; }
    Microsoft::UI::Xaml::Visibility SourceDetailChipViewModel::MutedVisibility() const noexcept { return m_muted ? Visible : Collapsed; }

    SourceDetailsViewModel::SourceDetailsViewModel(
        winrt::hstring key,
        winrt::hstring resolution,
        winrt::hstring picture,
        winrt::hstring codec,
        winrt::hstring sound,
        winrt::hstring channels,
        std::vector<Sources::DetailChipData> audioLanguages,
        std::vector<Sources::DetailChipData> subtitles,
        winrt::hstring provider,
        winrt::hstring cacheLabel,
        bool cacheGood,
        winrt::hstring lineLabel,
        winrt::hstring mbpsLabel,
        winrt::hstring headroom,
        double meterFraction,
        winrt::hstring fileName)
        : m_key(std::move(key)),
          m_resolution(std::move(resolution)),
          m_picture(std::move(picture)),
          m_codec(std::move(codec)),
          m_sound(std::move(sound)),
          m_channels(std::move(channels)),
          m_audioLanguages(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_subtitles(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_provider(std::move(provider)),
          m_cacheLabel(std::move(cacheLabel)),
          m_cacheGood(cacheGood),
          m_lineLabel(std::move(lineLabel)),
          m_mbpsLabel(std::move(mbpsLabel)),
          m_headroom(std::move(headroom)),
          m_meterFraction(std::clamp(meterFraction, 0.0, 1.0)),
          m_fileName(std::move(fileName))
    {
        for (auto& chip : audioLanguages)
        {
            m_audioLanguages.Append(winrt::make<SourceDetailChipViewModel>(std::move(chip.Label), chip.Muted));
        }
        if (m_audioLanguages.Size() == 0)
        {
            m_audioLanguages.Append(winrt::make<SourceDetailChipViewModel>(L"NOT LISTED", true));
        }
        for (auto& chip : subtitles)
        {
            m_subtitles.Append(winrt::make<SourceDetailChipViewModel>(std::move(chip.Label), chip.Muted));
        }
        if (m_subtitles.Size() == 0)
        {
            m_subtitles.Append(winrt::make<SourceDetailChipViewModel>(L"NONE INCLUDED", true));
        }
    }
    winrt::hstring SourceDetailsViewModel::Key() const { return m_key; }
    winrt::hstring SourceDetailsViewModel::Resolution() const { return m_resolution; }
    winrt::hstring SourceDetailsViewModel::Picture() const { return m_picture; }
    winrt::hstring SourceDetailsViewModel::Codec() const { return m_codec; }
    winrt::hstring SourceDetailsViewModel::Sound() const { return m_sound; }
    winrt::hstring SourceDetailsViewModel::Channels() const { return m_channels; }
    winrt::Windows::Foundation::IInspectable SourceDetailsViewModel::AudioLanguageChips() const { return m_audioLanguages; }
    winrt::Windows::Foundation::IInspectable SourceDetailsViewModel::SubtitleChips() const { return m_subtitles; }
    winrt::hstring SourceDetailsViewModel::Provider() const { return m_provider; }
    winrt::hstring SourceDetailsViewModel::CacheLabel() const { return m_cacheLabel; }
    Microsoft::UI::Xaml::Visibility SourceDetailsViewModel::CacheGoodVisibility() const noexcept { return m_cacheGood ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourceDetailsViewModel::CacheNeutralVisibility() const noexcept { return m_cacheGood ? Collapsed : Visible; }
    winrt::hstring SourceDetailsViewModel::LineLabel() const { return m_lineLabel; }
    winrt::hstring SourceDetailsViewModel::MbpsLabel() const { return m_mbpsLabel; }
    winrt::hstring SourceDetailsViewModel::Headroom() const { return m_headroom; }
    double SourceDetailsViewModel::MeterFraction() const noexcept { return m_meterFraction; }
    winrt::hstring SourceDetailsViewModel::FileName() const { return m_fileName; }
    winrt::hstring SourceDetailsViewModel::CopyLabel() const { return m_copyLabel; }
    void SourceDetailsViewModel::SetCopyLabel(winrt::hstring value)
    {
        if (m_copyLabel == value) return;
        m_copyLabel = std::move(value);
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, L"CopyLabel");
    }
    winrt::event_token SourceDetailsViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler) { return m_propertyChanged.add(handler); }
    void SourceDetailsViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }

    SourceProviderItemViewModel::SourceProviderItemViewModel(winrt::hstring name, winrt::hstring value, bool answered)
        : m_name(std::move(name)), m_value(std::move(value)), m_answered(answered) {}
    winrt::hstring SourceProviderItemViewModel::Name() const { return m_name; }
    winrt::hstring SourceProviderItemViewModel::Value() const { return m_value; }
    Microsoft::UI::Xaml::Visibility SourceProviderItemViewModel::AnsweredVisibility() const noexcept { return m_answered ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourceProviderItemViewModel::FailedVisibility() const noexcept { return m_answered ? Collapsed : Visible; }

    SourceQualityItemViewModel::SourceQualityItemViewModel(Sources::QualityTier tier, std::int32_t count, std::int32_t total)
        : m_tier(tier), m_count(count), m_total(total) {}
    winrt::hstring SourceQualityItemViewModel::Label() const { return Sources::TierLabel(m_tier); }
    winrt::hstring SourceQualityItemViewModel::Count() const { return winrt::to_hstring(m_count); }
    double SourceQualityItemViewModel::Share() const noexcept
    {
        return m_total > 0 ? static_cast<double>(m_count) / static_cast<double>(m_total) : 0.0;
    }
    Microsoft::UI::Xaml::Visibility SourceQualityItemViewModel::TopTierVisibility() const noexcept { return m_tier == Sources::QualityTier::UltraHd ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourceQualityItemViewModel::MidTierVisibility() const noexcept { return m_tier == Sources::QualityTier::FullHd ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourceQualityItemViewModel::LowTierVisibility() const noexcept
    {
        return m_tier != Sources::QualityTier::UltraHd && m_tier != Sources::QualityTier::FullHd ? Visible : Collapsed;
    }

    SourcePickerRuleViewModel::SourcePickerRuleViewModel(winrt::hstring name, winrt::hstring value)
        : m_name(std::move(name)), m_value(std::move(value)) {}
    winrt::hstring SourcePickerRuleViewModel::Name() const { return m_name; }
    winrt::hstring SourcePickerRuleViewModel::Value() const { return m_value; }

    SourceDisplayItemViewModel::SourceDisplayItemViewModel(winrt::hstring name, winrt::hstring note, winrt::hstring count)
        : m_kind(Kind::GroupHeader), m_groupName(std::move(name)), m_groupNote(std::move(note)), m_groupCount(std::move(count)) {}

    SourceDisplayItemViewModel::SourceDisplayItemViewModel(winrt::hstring showMoreLabel)
        : m_kind(Kind::ShowMore), m_showMoreLabel(std::move(showMoreLabel)) {}

    SourceDisplayItemViewModel::SourceDisplayItemViewModel(
        Sources::SourceEntry entry,
        winrt::hstring statusLabel,
        Sources::MetaLineData meta,
        winrt::hstring warning,
        winrt::hstring reason,
        std::vector<Sources::SpecRow> specs,
        winrt::HaloDesktop::SourceDetailsViewModel details)
        : m_kind(Kind::Row),
          m_entry(std::move(entry)),
          m_statusLabel(std::move(statusLabel)),
          m_meta(std::move(meta)),
          m_warning(std::move(warning)),
          m_reason(std::move(reason)),
          m_specs(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_details(std::move(details))
    {
        for (auto& [key, value] : specs)
        {
            m_specs.Append(winrt::make<SourceSpecItemViewModel>(std::move(key), std::move(value)));
        }
    }

    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::HeaderVisibility() const noexcept { return m_kind == Kind::GroupHeader ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::RowVisibility() const noexcept { return m_kind == Kind::Row ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::ShowMoreVisibility() const noexcept { return m_kind == Kind::ShowMore ? Visible : Collapsed; }

    winrt::hstring SourceDisplayItemViewModel::GroupName() const { return m_groupName; }
    winrt::hstring SourceDisplayItemViewModel::GroupNote() const { return m_groupNote; }
    winrt::hstring SourceDisplayItemViewModel::GroupCount() const { return m_groupCount; }
    winrt::hstring SourceDisplayItemViewModel::ShowMoreLabel() const { return m_showMoreLabel; }

    winrt::hstring SourceDisplayItemViewModel::Key() const { return m_entry.Source ? m_entry.Source.Key() : L""; }
    winrt::hstring SourceDisplayItemViewModel::QualityBadgeTier() const { return Sources::BadgeTierLabel(m_entry.Tier); }
    // Only high dynamic range earns the qualifier. Standard range is the norm and
    // stamping every other row with it would say nothing; the details panel still
    // spells it out for anyone who opens a source.
    winrt::hstring SourceDisplayItemViewModel::QualityBadgeDetail() const { return m_entry.Hdr ? winrt::hstring{ L"HDR" } : winrt::hstring{}; }
    winrt::HaloDesktop::QualityBadgeTone SourceDisplayItemViewModel::QualityTone() const noexcept
    {
        return Sources::IsPremiumTier(m_entry.Tier) ? winrt::HaloDesktop::QualityBadgeTone::Gold
                                                    : winrt::HaloDesktop::QualityBadgeTone::Muted;
    }
    winrt::hstring SourceDisplayItemViewModel::StatusLabel() const { return m_statusLabel; }

    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::InstantVisibility() const noexcept
    {
        return m_entry.Source && m_entry.Source.Status() == winrt::HaloDesktop::StreamStatus::Instant ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::OnDiskVisibility() const noexcept
    {
        return m_entry.Source && m_entry.Source.Status() == winrt::HaloDesktop::StreamStatus::OnDisk ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::CachingVisibility() const noexcept
    {
        return m_entry.Source && m_entry.Source.Status() == winrt::HaloDesktop::StreamStatus::Caching ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::ColdVisibility() const noexcept
    {
        if (!m_entry.Source) return Collapsed;
        auto const status = m_entry.Source.Status();
        return status == winrt::HaloDesktop::StreamStatus::Uncached
                || status == winrt::HaloDesktop::StreamStatus::Unknown
            ? Visible
            : Collapsed;
    }

    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::SaveVisibility() const noexcept
    {
        return OnDiskVisibility() == Visible ? Collapsed : Visible;
    }

    winrt::hstring SourceDisplayItemViewModel::MetaLine() const { return m_meta.Line; }
    winrt::hstring SourceDisplayItemViewModel::SubsChip() const { return m_meta.Subtitles; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::SubsOnVisibility() const noexcept
    {
        return m_meta.HasSubtitles ? Microsoft::UI::Xaml::Visibility::Visible : Microsoft::UI::Xaml::Visibility::Collapsed;
    }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::SubsOffVisibility() const noexcept
    {
        return m_meta.HasSubtitles ? Microsoft::UI::Xaml::Visibility::Collapsed : Microsoft::UI::Xaml::Visibility::Visible;
    }
    winrt::hstring SourceDisplayItemViewModel::Warning() const { return m_warning; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::WarningVisibility() const noexcept { return m_warning.empty() ? Collapsed : Visible; }
    winrt::hstring SourceDisplayItemViewModel::FileName() const { return m_entry.Source ? m_entry.Source.File() : L""; }
    winrt::hstring SourceDisplayItemViewModel::Reason() const { return m_reason; }
    winrt::Windows::Foundation::IInspectable SourceDisplayItemViewModel::Specs() const { return m_specs; }
    winrt::HaloDesktop::SourceDetailsViewModel SourceDisplayItemViewModel::Details() const { return m_details; }

    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::ExpandedVisibility() const noexcept { return m_expanded ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::ExpandGlyphVisibility() const noexcept { return m_expanded ? Collapsed : Visible; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::CollapseGlyphVisibility() const noexcept { return m_expanded ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::SelectionVisibility() const noexcept { return m_selected ? Visible : Collapsed; }

    bool SourceDisplayItemViewModel::IsRow() const noexcept { return m_kind == Kind::Row; }
    Sources::SourceEntry const& SourceDisplayItemViewModel::Entry() const noexcept { return m_entry; }

    void SourceDisplayItemViewModel::SetExpanded(bool value)
    {
        if (m_expanded == value) return;
        m_expanded = value;
        Raise(L"ExpandedVisibility");
        Raise(L"ExpandGlyphVisibility");
        Raise(L"CollapseGlyphVisibility");
    }

    void SourceDisplayItemViewModel::SetSelected(bool value)
    {
        if (m_selected == value) return;
        m_selected = value;
        Raise(L"SelectionVisibility");
    }

    winrt::event_token SourceDisplayItemViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }
    void SourceDisplayItemViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }
    void SourceDisplayItemViewModel::Raise(wchar_t const* property)
    {
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, property);
    }

    SourcesViewModel::SourcesViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_sources(services.Sources),
          m_metadata(services.Metadata),
          m_navigation(services.Navigation),
          m_settings(services.SettingsSync),
          m_downloads(services.Downloads),
          m_devicePreferences(services.DevicePreferences),
          m_watch(services.WatchState),
          m_layout(services.LayoutMetrics),
          m_sourceGroups(services.Sources->Groups()),
          m_items(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_providerItems(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_qualityItems(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_pickerRules(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>())
    {
        if (!m_devicePreferences || !m_watch || !m_metadata)
        {
            throw std::invalid_argument{ "SourcesViewModel requires its dependencies." };
        }
        ApplyLayoutMetrics();
    }

    SourcesViewModel::~SourcesViewModel() { Deactivate(); }

    void SourcesViewModel::Activate()
    {
        if (m_downloadToken == 0)
        {
            m_downloadToken = m_downloads->AddChangedHandler([weak = get_weak()]()
            {
                auto const self = weak.get();
                if (!self) return;
                self->m_sources->RefreshDownloadStates();
                self->AdoptResolve();
                self->Rebuild();
                self->RebuildFooter();
                self->RaiseState();
            });
        }
        if (m_layoutToken == 0 && m_layout)
        {
            m_layoutToken = m_layout->AddChangedHandler([weak = get_weak()]()
            {
                if (auto const self = weak.get()) self->ApplyLayoutMetrics();
            });
            ApplyLayoutMetrics();
        }
    }

    void SourcesViewModel::Deactivate() noexcept
    {
        if (m_downloadToken != 0)
        {
            m_downloads->RemoveChangedHandler(m_downloadToken);
            m_downloadToken = 0;
        }
        if (m_layoutToken != 0 && m_layout)
        {
            m_layout->RemoveChangedHandler(m_layoutToken);
            m_layoutToken = 0;
        }
    }

    winrt::Windows::Foundation::IInspectable SourcesViewModel::Items() const { return m_items; }
    winrt::Windows::Foundation::IInspectable SourcesViewModel::ProviderItems() const { return m_providerItems; }
    winrt::Windows::Foundation::IInspectable SourcesViewModel::QualityItems() const { return m_qualityItems; }
    winrt::Windows::Foundation::IInspectable SourcesViewModel::PickerRules() const { return m_pickerRules; }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> SourcesViewModel::ItemsView() const { return m_items; }

    double SourcesViewModel::SheetWidth() const noexcept { return m_sheetWidth; }

    winrt::hstring SourcesViewModel::Kicker() const
    {
        if (!m_parameters) return L"";
        // The kicker only carries what the heading below does not already say. A
        // movie's show name is its own title, and an entry resumed from Continue
        // watching names the show rather than the episode, so in both cases
        // repeating the show name would print the same words twice.
        auto const show = m_parameters.ShowName();
        auto const episode = m_parameters.EpisodeLabel();
        auto const redundant = show.empty() || show == m_parameters.Title();
        if (episode.empty()) return redundant ? winrt::hstring{} : show;
        return redundant ? episode : episode + L" \x00B7 " + show;
    }

    Microsoft::UI::Xaml::Visibility SourcesViewModel::KickerVisibility() const noexcept
    {
        return Kicker().empty() ? Collapsed : Visible;
    }

    winrt::hstring SourcesViewModel::Heading() const
    {
        auto const title = m_parameters ? m_parameters.Title() : L"";
        return title.empty() ? winrt::hstring{ L"Where to play this" } : L"Where to play " + title;
    }

    winrt::hstring SourcesViewModel::CountLine() const
    {
        if (m_loading) return L"Asking your providers\x2026";
        if (m_error) return L"No provider could be reached";
        return m_sources->ResolveSummary();
    }

    winrt::hstring SourcesViewModel::AllFilterCount() const { return winrt::to_hstring(Filtered(FilterAll).size()); }
    winrt::hstring SourcesViewModel::PlaysNowFilterCount() const { return winrt::to_hstring(Filtered(FilterPlaysNow).size()); }
    winrt::hstring SourcesViewModel::UltraHdFilterCount() const { return winrt::to_hstring(Filtered(FilterUltraHd).size()); }
    winrt::hstring SourcesViewModel::FullHdFilterCount() const { return winrt::to_hstring(Filtered(FilterFullHd).size()); }
    winrt::hstring SourcesViewModel::HdFilterCount() const { return winrt::to_hstring(Filtered(FilterHd).size()); }
    winrt::hstring SourcesViewModel::SortLabel() const { return SortName(m_sortIndex); }
    std::int32_t SourcesViewModel::FilterIndex() const noexcept { return m_filterIndex; }
    std::int32_t SourcesViewModel::SortIndex() const noexcept { return m_sortIndex; }

    Microsoft::UI::Xaml::Visibility SourcesViewModel::ListVisibility() const noexcept
    {
        return !m_loading && !m_pool.empty() && !Filtered(m_filterIndex).empty() ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::ResolvingVisibility() const noexcept { return m_loading ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::EmptyVisibility() const noexcept
    {
        return !m_loading && (m_pool.empty() || Filtered(m_filterIndex).empty()) ? Visible : Collapsed;
    }
    // The filters are only meaningful over a list; with nothing to narrow they
    // would be four buttons that all lead to the same empty block.
    Microsoft::UI::Xaml::Visibility SourcesViewModel::FilterRowVisibility() const noexcept
    {
        return !m_loading && !m_pool.empty() ? Visible : Collapsed;
    }

    winrt::hstring SourcesViewModel::EmptyTitle() const
    {
        if (m_error) return L"Nothing could be reached";
        if (m_pool.empty()) return L"Nothing playable came back";
        return L"No source matches that filter";
    }

    winrt::hstring SourcesViewModel::EmptyBody() const
    {
        if (m_error)
        {
            return L"Halo could not reach any provider, and nothing for this is saved on this device. "
                   L"Anything you download appears here and plays without a connection.";
        }
        if (m_pool.empty())
        {
            return L"Every provider answered, and none of them had a file for this. "
                   L"This usually clears up within a day of release.";
        }
        return L"Try All, or widen the quality you are willing to accept.";
    }

    Microsoft::UI::Xaml::Visibility SourcesViewModel::BannerVisibility() const noexcept
    {
        return !m_loading && (m_error || m_failedProviders > 0) ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::BannerCautionVisibility() const noexcept
    {
        return !m_loading && !m_error && m_failedProviders > 0 ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::BannerInfoVisibility() const noexcept
    {
        return !m_loading && m_error ? Visible : Collapsed;
    }

    winrt::hstring SourcesViewModel::BannerTitle() const
    {
        if (m_error) return L"You are offline";
        if (m_failedProviders == 1) return m_firstFailure;
        return winrt::to_hstring(m_failedProviders) + L" providers did not answer";
    }

    winrt::hstring SourcesViewModel::BannerBody() const
    {
        if (m_error) return L"Only files already downloaded to this device can play right now.";
        if (m_answeredProviders == 0) return L"Nothing was returned, so some sources may be missing.";
        return L"Showing what the other "
            + Sources::CountLabel(static_cast<std::size_t>(m_answeredProviders), L"provider", L"providers")
            + L" returned. Some sources may be missing.";
    }

    winrt::hstring SourcesViewModel::BannerAction() const
    {
        return m_error ? winrt::hstring{ L"Retry connection" } : winrt::hstring{ L"Ask them again" };
    }

    bool SourcesViewModel::HasPick() const noexcept
    {
        // The error case is not excluded: when every provider failed, a file already
        // on the device is still a real pick, and it is the only one left to make.
        if (m_loading || m_pool.empty() || m_sortIndex != SortRecommended) return false;
        return Matches(m_pool.front(), m_filterIndex);
    }

    Microsoft::UI::Xaml::Visibility SourcesViewModel::PickVisibility() const noexcept { return HasPick() ? Visible : Collapsed; }
    winrt::hstring SourcesViewModel::PickQualityBadgeTier() const { return HasPick() ? Sources::BadgeTierLabel(m_pool.front().Tier) : winrt::hstring{}; }
    winrt::hstring SourcesViewModel::PickQualityBadgeDetail() const { return HasPick() && m_pool.front().Hdr ? winrt::hstring{ L"HDR" } : winrt::hstring{}; }
    winrt::HaloDesktop::QualityBadgeTone SourcesViewModel::PickQualityTone() const noexcept
    {
        return HasPick() && Sources::IsPremiumTier(m_pool.front().Tier) ? winrt::HaloDesktop::QualityBadgeTone::Gold
                                                                       : winrt::HaloDesktop::QualityBadgeTone::Muted;
    }
    winrt::hstring SourcesViewModel::PickStatusLabel() const { return HasPick() ? Sources::StatusLabel(m_pool.front().Source.Status()) : L""; }

    Microsoft::UI::Xaml::Visibility SourcesViewModel::PickInstantVisibility() const noexcept
    {
        return HasPick() && m_pool.front().Source.Status() == winrt::HaloDesktop::StreamStatus::Instant ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::PickOnDiskVisibility() const noexcept
    {
        return HasPick() && m_pool.front().Source.Status() == winrt::HaloDesktop::StreamStatus::OnDisk ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::PickCachingVisibility() const noexcept
    {
        return HasPick() && m_pool.front().Source.Status() == winrt::HaloDesktop::StreamStatus::Caching ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::PickColdVisibility() const noexcept
    {
        if (!HasPick()) return Collapsed;
        auto const status = m_pool.front().Source.Status();
        return status == winrt::HaloDesktop::StreamStatus::Uncached
                || status == winrt::HaloDesktop::StreamStatus::Unknown
            ? Visible
            : Collapsed;
    }

    Microsoft::UI::Xaml::Visibility SourcesViewModel::PickSaveVisibility() const noexcept
    {
        return PickOnDiskVisibility() == Visible ? Collapsed : Visible;
    }

    winrt::hstring SourcesViewModel::PickWhy() const
    {
        return HasPick() ? Sources::PickHeadline(m_pool.front(), m_pool.size() == 1) : L"";
    }
    winrt::hstring SourcesViewModel::PickMetaLine() const { return m_pickMeta.Line; }
    winrt::hstring SourcesViewModel::PickSubsChip() const { return m_pickMeta.Subtitles; }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::PickSubsOnVisibility() const noexcept
    {
        return m_pickMeta.HasSubtitles ? Microsoft::UI::Xaml::Visibility::Visible : Microsoft::UI::Xaml::Visibility::Collapsed;
    }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::PickSubsOffVisibility() const noexcept
    {
        return m_pickMeta.HasSubtitles ? Microsoft::UI::Xaml::Visibility::Collapsed : Microsoft::UI::Xaml::Visibility::Visible;
    }
    winrt::hstring SourcesViewModel::PickFileName() const { return HasPick() ? m_pool.front().Source.File() : L""; }
    winrt::hstring SourcesViewModel::PickWatchNote() const { return HasPick() ? Sources::WatchNote(m_device) : L""; }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::PickWatchNoteVisibility() const noexcept
    {
        return HasPick() && !Sources::WatchNote(m_device).empty() ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::PickSelectionVisibility() const noexcept
    {
        return HasPick() && m_selectedIndex < 0 ? Visible : Collapsed;
    }
    winrt::HaloDesktop::SourceDetailsViewModel SourcesViewModel::PickDetails() const
    {
        return HasPick() ? m_pickDetails : nullptr;
    }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::PickExpandedVisibility() const noexcept
    {
        return HasPick() && m_pickExpanded ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::PickExpandGlyphVisibility() const noexcept
    {
        return PickExpandedVisibility() == Visible ? Collapsed : Visible;
    }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::PickCollapseGlyphVisibility() const noexcept
    {
        return PickExpandedVisibility() == Visible ? Visible : Collapsed;
    }

    Microsoft::UI::Xaml::Visibility SourcesViewModel::InfoVisibility() const noexcept { return m_infoOpen ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::InfoExpandGlyphVisibility() const noexcept { return m_infoOpen ? Collapsed : Visible; }
    Microsoft::UI::Xaml::Visibility SourcesViewModel::InfoCollapseGlyphVisibility() const noexcept { return m_infoOpen ? Visible : Collapsed; }

    std::int32_t SourcesViewModel::SelectedIndex() const noexcept { return m_selectedIndex; }

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
            m_loading = false;
            m_error = true;
            RaiseState();
            return;
        }
        static_cast<void>(LoadAsync());
    }

    void SourcesViewModel::Retry() { if (m_parameters) static_cast<void>(LoadAsync()); }

    void SourcesViewModel::SetFilter(std::int32_t index)
    {
        if (index < 0 || index >= FilterCount || index == m_filterIndex) return;
        m_filterIndex = index;
        m_expandedKey = L"";
        m_pickExpanded = false;
        m_selectedIndex = -1;
        ResetCopyState();
        Rebuild();
        RaiseState();
    }

    void SourcesViewModel::SetSort(std::int32_t index)
    {
        if (index < 0 || index >= SortCount || index == m_sortIndex) return;
        m_sortIndex = index;
        m_expandedKey = L"";
        m_pickExpanded = false;
        m_selectedIndex = -1;
        ResetCopyState();
        Rebuild();
        RaiseState();
    }

    void SourcesViewModel::ToggleInfo()
    {
        m_infoOpen = !m_infoOpen;
        Raise(L"InfoVisibility");
        Raise(L"InfoExpandGlyphVisibility");
        Raise(L"InfoCollapseGlyphVisibility");
    }

    void SourcesViewModel::ToggleExpanded(winrt::hstring const& key)
    {
        if (key.empty()) return;
        ResetCopyState();
        m_pickExpanded = false;
        m_expandedKey = m_expandedKey == key ? winrt::hstring{} : key;
        for (auto const& item : m_items)
        {
            auto const row = winrt::get_self<SourceDisplayItemViewModel>(item.as<winrt::HaloDesktop::SourceDisplayItemViewModel>());
            if (row->IsRow()) row->SetExpanded(row->Key() == m_expandedKey);
        }
        Raise(L"PickExpandedVisibility");
        Raise(L"PickExpandGlyphVisibility");
        Raise(L"PickCollapseGlyphVisibility");
        SelectKey(key);
    }

    void SourcesViewModel::TogglePickExpanded()
    {
        if (!HasPick()) return;
        ResetCopyState();
        m_pickExpanded = !m_pickExpanded;
        m_expandedKey = L"";
        for (auto const& item : m_items)
        {
            auto const row = winrt::get_self<SourceDisplayItemViewModel>(item.as<winrt::HaloDesktop::SourceDisplayItemViewModel>());
            if (row->IsRow()) row->SetExpanded(false);
        }
        SelectPick();
        Raise(L"PickExpandedVisibility");
        Raise(L"PickExpandGlyphVisibility");
        Raise(L"PickCollapseGlyphVisibility");
    }

    void SourcesViewModel::SelectKey(winrt::hstring const& key)
    {
        auto const index = IndexOfKey(key);
        if (index < 0) return;
        m_selectedIndex = index;
        ApplySelection();
    }

    void SourcesViewModel::SelectPick()
    {
        if (!HasPick()) return;
        m_selectedIndex = -1;
        ApplySelection();
    }

    void SourcesViewModel::MoveSelection(std::int32_t delta)
    {
        if (delta == 0) return;
        std::vector<std::int32_t> stops;
        if (HasPick()) stops.push_back(-1);
        for (std::uint32_t index{}; index < m_items.Size(); ++index)
        {
            auto const row = winrt::get_self<SourceDisplayItemViewModel>(
                m_items.GetAt(index).as<winrt::HaloDesktop::SourceDisplayItemViewModel>());
            if (row->IsRow()) stops.push_back(static_cast<std::int32_t>(index));
        }
        if (stops.empty()) return;

        auto const found = std::find(stops.begin(), stops.end(), m_selectedIndex);
        auto position = found == stops.end() ? 0 : static_cast<std::int32_t>(std::distance(stops.begin(), found));
        position = std::clamp(position + delta, 0, static_cast<std::int32_t>(stops.size()) - 1);
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
        if (m_expandedKey == key) return;
        ToggleExpanded(key);
    }

    void SourcesViewModel::CollapseSelected()
    {
        if (m_pickExpanded)
        {
            TogglePickExpanded();
            return;
        }
        if (m_expandedKey.empty()) return;
        ToggleExpanded(m_expandedKey);
    }

    void SourcesViewModel::RevealCold()
    {
        if (m_coldRevealed) return;
        m_coldRevealed = true;
        Rebuild();
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

    void SourcesViewModel::OpenSettings()
    {
        m_navigation->CloseSheet();
        m_navigation->GoTo(::HaloDesktop::Services::Page::Settings);
    }

    void SourcesViewModel::Close() { m_navigation->CloseSheet(); }

    winrt::hstring SourcesViewModel::FileNameFor(winrt::hstring const& key) const
    {
        auto const found = std::find_if(m_pool.begin(), m_pool.end(), [&key](auto const& entry)
        {
            return entry.Source && entry.Source.Key() == key;
        });
        return found == m_pool.end() ? winrt::hstring{} : found->Source.File();
    }

    void SourcesViewModel::ResetCopyState()
    {
        m_copiedKey.clear();
        if (m_pickDetails) m_pickDetails.SetCopyLabel(L"Copy file name");
        for (auto const& item : m_items)
        {
            auto const row = winrt::get_self<SourceDisplayItemViewModel>(item.as<winrt::HaloDesktop::SourceDisplayItemViewModel>());
            if (row->IsRow() && row->Details()) row->Details().SetCopyLabel(L"Copy file name");
        }
    }

    void SourcesViewModel::MarkCopied(winrt::hstring const& key)
    {
        if (key.empty()) return;
        ResetCopyState();
        m_copiedKey = key;
        if (m_pickDetails && key == PickKey())
        {
            m_pickDetails.SetCopyLabel(L"File name copied");
        }
        for (auto const& item : m_items)
        {
            auto const row = winrt::get_self<SourceDisplayItemViewModel>(item.as<winrt::HaloDesktop::SourceDisplayItemViewModel>());
            if (row->IsRow() && row->Key() == key && row->Details())
            {
                row->Details().SetCopyLabel(L"File name copied");
            }
        }
    }

    concurrency::task<::HaloDesktop::Services::DownloadStartOutcome> SourcesViewModel::StartDownloadAsync(
        winrt::hstring key,
        bool replaceExisting)
    {
        co_return co_await m_sources->StartDownloadAsync(std::move(key), replaceExisting);
    }

    winrt::hstring SourcesViewModel::SelectedKey() const
    {
        if (m_selectedIndex < 0 || static_cast<std::uint32_t>(m_selectedIndex) >= m_items.Size()) return {};
        auto const row = winrt::get_self<SourceDisplayItemViewModel>(
            m_items.GetAt(static_cast<std::uint32_t>(m_selectedIndex)).as<winrt::HaloDesktop::SourceDisplayItemViewModel>());
        return row->IsRow() ? row->Key() : winrt::hstring{};
    }

    winrt::hstring SourcesViewModel::PickKey() const
    {
        return m_pool.empty() || !m_pool.front().Source ? winrt::hstring{} : m_pool.front().Source.Key();
    }

    winrt::event_token SourcesViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }
    void SourcesViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }

    winrt::Windows::Foundation::IAsyncAction SourcesViewModel::LoadAsync()
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        auto const version = ++m_loadVersion;
        m_loading = true;
        m_error = false;
        m_filterIndex = FilterAll;
        m_sortIndex = SortRecommended;
        m_selectedIndex = -1;
        m_expandedKey = L"";
        m_pickExpanded = false;
        ResetCopyState();
        m_coldRevealed = false;
        m_items.Clear();
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
        AdoptResolve();
        Rebuild();
        RebuildFooter();
        RaiseState();
    }

    // Rebuilds the decorated pool from whatever the service currently holds. Also
    // runs when a download completes, because that changes a source's status.
    void SourcesViewModel::AdoptResolve()
    {
        m_answeredProviders = 0;
        m_failedProviders = 0;
        m_firstFailure = L"";
        m_smallestBytes = 0;
        m_maximumNeededMbps = 0.0;
        m_pickDetails = nullptr;
        m_localCount = 0;
        m_pool.clear();

        // A throw leaves the service holding whatever it resolved last, which may
        // belong to a different title entirely. Nothing of its groups may be shown
        // here. Local sources are the exception: the service rebuilds those for the
        // parameters of this load before it reports the failure, so they belong to
        // this title and are exactly what is still playable.
        m_sourceGroups = m_error
            ? winrt::single_threaded_vector<winrt::HaloDesktop::SourceGroup>().GetView()
            : m_sources->Groups();

        m_device = {};
        try { m_device.PreferredSubtitleLanguage = m_settings->PreferredSubtitleLanguage(); }
        catch (...) { m_device.PreferredSubtitleLanguage.reset(); }
        m_device.LineMbps = m_devicePreferences->MeasuredLineMbps();

        if (m_parameters)
        {
            if (auto const row = m_watch->Find(m_parameters.VideoId()))
            {
                if (row->DurationSec > 0.0)
                {
                    m_device.DurationSeconds = row->DurationSec;
                    m_device.WatchedDurationSeconds = row->DurationSec;
                    m_device.WatchedSeconds = row->Watched ? 0.0 : row->PositionSec;
                }
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

        for (auto const& source : m_sources->LocalSources())
        {
            m_pool.push_back(Sources::MakeEntry(source, L"local", LocalProviderName, m_device.DurationSeconds));
        }
        m_localCount = m_pool.size();

        for (auto const& group : m_sourceGroups)
        {
            if (group.Answered()) ++m_answeredProviders;
            else
            {
                ++m_failedProviders;
                if (m_firstFailure.empty()) m_firstFailure = group.Name() + L" " + group.Note();
            }
            for (auto const& source : group.Sources())
            {
                auto const providerId = group.AddonId().empty() ? group.Name() : group.AddonId();
                m_pool.push_back(Sources::MakeEntry(source, providerId, group.Name(), m_device.DurationSeconds));
            }
        }
        std::stable_sort(m_pool.begin(), m_pool.end(), [](auto const& left, auto const& right)
        {
            return left.Rank < right.Rank;
        });

        for (auto const& entry : m_pool)
        {
            if (entry.SizeBytes != 0 && (m_smallestBytes == 0 || entry.SizeBytes < m_smallestBytes))
            {
                m_smallestBytes = entry.SizeBytes;
            }
            m_maximumNeededMbps = (std::max)(m_maximumNeededMbps, entry.NeededMbps);
        }
        if (!m_pool.empty()) m_pickDetails = DetailsFor(m_pool.front(), m_maximumNeededMbps);
    }

    bool SourcesViewModel::Matches(Sources::SourceEntry const& entry, std::int32_t filterIndex) const noexcept
    {
        switch (filterIndex)
        {
        case FilterPlaysNow: return entry.Speed == Sources::StartSpeed::Immediate;
        case FilterUltraHd: return entry.Tier == Sources::QualityTier::UltraHd;
        case FilterFullHd: return entry.Tier == Sources::QualityTier::FullHd;
        case FilterHd: return entry.Tier == Sources::QualityTier::Hd;
        default: break;
        }
        return true;
    }

    std::vector<Sources::SourceEntry> SourcesViewModel::Filtered(std::int32_t filterIndex) const
    {
        std::vector<Sources::SourceEntry> result;
        result.reserve(m_pool.size());
        for (auto const& entry : m_pool)
        {
            if (Matches(entry, filterIndex)) result.push_back(entry);
        }
        return result;
    }

    winrt::HaloDesktop::SourceDetailsViewModel SourcesViewModel::DetailsFor(
        Sources::SourceEntry const& entry,
        double maximumNeededMbps) const
    {
        auto data = Sources::DetailsFor(entry, m_device, maximumNeededMbps);
        return winrt::make<SourceDetailsViewModel>(
            entry.Source ? entry.Source.Key() : winrt::hstring{},
            std::move(data.Resolution),
            std::move(data.Picture),
            std::move(data.Codec),
            std::move(data.Sound),
            std::move(data.Channels),
            std::move(data.AudioLanguages),
            std::move(data.Subtitles),
            std::move(data.Provider),
            std::move(data.CacheLabel),
            data.CacheGood,
            std::move(data.LineLabel),
            std::move(data.MbpsLabel),
            std::move(data.Headroom),
            data.MeterFraction,
            entry.Source ? entry.Source.File() : winrt::hstring{});
    }

    double SourcesViewModel::MaximumNeededMbps() const noexcept { return m_maximumNeededMbps; }

    winrt::HaloDesktop::SourceDisplayItemViewModel SourcesViewModel::RowFor(
        Sources::SourceEntry const& entry,
        Sources::SourceEntry const* pick,
        double maximumNeededMbps) const
    {
        auto const& source = entry.Source;
        // Rows leave the range to the quality plate beside them; only the pick
        // spells it out.
        return winrt::make<SourceDisplayItemViewModel>(
            entry,
            Sources::StatusLabel(source.Status()),
            Sources::MetaLineFor(entry, false, m_device.PreferredSubtitleLanguage),
            Sources::BitrateWarning(entry, m_device.LineMbps),
            Sources::ReasonFor(entry, pick, entry.SizeBytes != 0 && entry.SizeBytes == m_smallestBytes),
            Sources::SpecsFor(entry, m_device),
            DetailsFor(entry, maximumNeededMbps));
    }

    void SourcesViewModel::Rebuild()
    {
        m_items.Clear();
        if (m_loading) return;

        auto listed = Filtered(m_filterIndex);
        SortEntries(listed, m_sortIndex);
        auto const maximumNeededMbps = [&listed]()
        {
            auto maximum = 0.0;
            for (auto const& entry : listed) maximum = (std::max)(maximum, entry.NeededMbps);
            return maximum;
        }();

        Sources::SourceEntry const* pick = HasPick() ? &m_pool.front() : nullptr;
        m_pickDetails = pick ? DetailsFor(*pick, maximumNeededMbps) : nullptr;
        // The pick is the one card arguing for itself, so it names its range.
        m_pickMeta = pick
            ? Sources::MetaLineFor(*pick, true, m_device.PreferredSubtitleLanguage)
            : Sources::MetaLineData{};
        if (pick)
        {
            // The pick has its own block above the list, so it is not repeated in it.
            listed.erase(
                std::remove_if(listed.begin(), listed.end(), [pick](auto const& entry)
                {
                    return entry.Source.Key() == pick->Source.Key();
                }),
                listed.end());
        }

        struct ProviderBucket final
        {
            winrt::hstring Id;
            winrt::hstring Name;
            std::vector<Sources::SourceEntry> Entries;
        };
        std::vector<ProviderBucket> providers;
        std::size_t hiddenCold{};
        for (auto const& entry : listed)
        {
            if (m_sortIndex == SortRecommended
                && !m_coldRevealed
                && entry.Speed == Sources::StartSpeed::NeedsDownload)
            {
                ++hiddenCold;
                continue;
            }
            auto const found = std::find_if(providers.begin(), providers.end(), [&entry](auto const& provider)
            {
                return provider.Id == entry.ProviderId;
            });
            if (found == providers.end())
            {
                providers.push_back(ProviderBucket{ entry.ProviderId, entry.Provider, {} });
                providers.back().Entries.push_back(entry);
            }
            else
            {
                found->Entries.push_back(entry);
            }
        }

        for (auto const& provider : providers)
        {
            m_items.Append(winrt::make<SourceDisplayItemViewModel>(
                provider.Name,
                provider.Name == LocalProviderName ? winrt::hstring{ L"Saved on this device" } : winrt::hstring{},
                Sources::CountLabel(provider.Entries.size(), L"source", L"sources")));
            for (auto const& entry : provider.Entries)
            {
                m_items.Append(RowFor(entry, pick, maximumNeededMbps));
            }
        }
        if (hiddenCold != 0)
        {
            m_items.Append(winrt::make<SourceDisplayItemViewModel>(
                L"Show " + Sources::CountLabel(hiddenCold, L"source", L"sources")
                    + L" that need downloading first"));
        }

        for (auto const& item : m_items)
        {
            auto const row = winrt::get_self<SourceDisplayItemViewModel>(item.as<winrt::HaloDesktop::SourceDisplayItemViewModel>());
            if (row->IsRow() && row->Key() == m_expandedKey) row->SetExpanded(true);
        }
    }

    void SourcesViewModel::RebuildFooter()
    {
        m_providerItems.Clear();
        if (m_localCount != 0)
        {
            m_providerItems.Append(winrt::make<SourceProviderItemViewModel>(
                winrt::hstring{ LocalProviderName },
                Sources::CountLabel(m_localCount, L"file", L"files"),
                true));
        }
        for (auto const& group : m_sourceGroups)
        {
            auto const value = group.Answered()
                ? Sources::CountLabel(static_cast<std::size_t>(group.Count()), L"source", L"sources")
                : group.Note();
            m_providerItems.Append(winrt::make<SourceProviderItemViewModel>(group.Name(), value, group.Answered()));
        }

        m_qualityItems.Clear();
        auto const total = static_cast<std::int32_t>(m_pool.size());
        for (auto const tier : {
                 Sources::QualityTier::UltraHd,
                 Sources::QualityTier::FullHd,
                 Sources::QualityTier::Hd })
        {
            auto const count = static_cast<std::int32_t>(std::count_if(
                m_pool.begin(),
                m_pool.end(),
                [tier](auto const& entry) { return entry.Tier == tier; }));
            m_qualityItems.Append(winrt::make<SourceQualityItemViewModel>(tier, count, total));
        }

        m_pickerRules.Clear();
        m_pickerRules.Append(winrt::make<SourcePickerRuleViewModel>(L"Prefer sources that play instantly", L"On"));
        m_pickerRules.Append(winrt::make<SourcePickerRuleViewModel>(
            L"Preferred audio language",
            Sources::LanguageName(m_settings->PreferredAudioLanguage().value_or(L"Automatic"))));
        m_pickerRules.Append(winrt::make<SourcePickerRuleViewModel>(
            L"Preferred subtitles",
            m_device.PreferredSubtitleLanguage
                ? Sources::LanguageName(*m_device.PreferredSubtitleLanguage)
                : winrt::hstring{ L"Off" }));
        m_pickerRules.Append(winrt::make<SourcePickerRuleViewModel>(
            L"Autoplay next episode",
            m_settings->AutoplayNextEpisode() ? L"On" : L"Off"));

        Raise(L"ProviderItems");
        Raise(L"QualityItems");
        Raise(L"PickerRules");
    }

    std::int32_t SourcesViewModel::IndexOfKey(winrt::hstring const& key) const
    {
        for (std::uint32_t index{}; index < m_items.Size(); ++index)
        {
            auto const row = winrt::get_self<SourceDisplayItemViewModel>(
                m_items.GetAt(index).as<winrt::HaloDesktop::SourceDisplayItemViewModel>());
            if (row->IsRow() && row->Key() == key) return static_cast<std::int32_t>(index);
        }
        return -1;
    }

    void SourcesViewModel::ApplySelection()
    {
        for (std::uint32_t index{}; index < m_items.Size(); ++index)
        {
            auto const row = winrt::get_self<SourceDisplayItemViewModel>(
                m_items.GetAt(index).as<winrt::HaloDesktop::SourceDisplayItemViewModel>());
            if (row->IsRow()) row->SetSelected(static_cast<std::int32_t>(index) == m_selectedIndex);
        }
        Raise(L"PickSelectionVisibility");
        Raise(L"SelectedIndex");
    }

    void SourcesViewModel::ApplyLayoutMetrics()
    {
        if (!m_layout) return;
        auto const wide = m_layout->Current().Step == ::HaloDesktop::Shell::LayoutStep::Wide;
        auto const width = wide ? WideSheetWidth : NarrowSheetWidth;
        if (width == m_sheetWidth) return;
        m_sheetWidth = width;
        Raise(L"SheetWidth");
    }

    void SourcesViewModel::RaiseState()
    {
        for (auto const* property : {
                 L"Items", L"Kicker", L"KickerVisibility", L"Heading", L"CountLine",
                 L"AllFilterCount", L"PlaysNowFilterCount", L"UltraHdFilterCount", L"FullHdFilterCount", L"HdFilterCount",
                 L"SortLabel", L"FilterIndex", L"SortIndex",
                 L"FilterRowVisibility", L"ListVisibility", L"ResolvingVisibility", L"EmptyVisibility",
                 L"EmptyTitle", L"EmptyBody",
                 L"BannerVisibility", L"BannerCautionVisibility", L"BannerInfoVisibility",
                 L"BannerTitle", L"BannerBody", L"BannerAction",
                 L"PickVisibility", L"PickQualityBadgeTier", L"PickQualityBadgeDetail", L"PickQualityTone", L"PickStatusLabel",
                 L"PickInstantVisibility", L"PickOnDiskVisibility", L"PickCachingVisibility", L"PickColdVisibility",
                 L"PickWhy", L"PickMetaLine", L"PickSubsChip", L"PickSubsOnVisibility",
                 L"PickSubsOffVisibility", L"PickFileName", L"PickWatchNote", L"PickWatchNoteVisibility",
                 L"PickSelectionVisibility", L"PickSaveVisibility", L"PickDetails", L"PickExpandedVisibility",
                 L"PickExpandGlyphVisibility", L"PickCollapseGlyphVisibility", L"SelectedIndex" })
        {
            Raise(property);
        }
    }

    void SourcesViewModel::Raise(wchar_t const* property)
    {
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, property);
    }
}
