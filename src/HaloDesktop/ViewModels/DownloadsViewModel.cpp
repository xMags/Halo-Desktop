#include "pch.h"
#include "ViewModels/DownloadsViewModel.h"
#if __has_include("ChartBarViewModel.g.cpp")
#include "ChartBarViewModel.g.cpp"
#endif
#if __has_include("DownloadRowViewModel.g.cpp")
#include "DownloadRowViewModel.g.cpp"
#endif
#if __has_include("DownloadsViewModel.g.cpp")
#include "DownloadsViewModel.g.cpp"
#endif

#include "Services/NavigationService.h"
#include "ViewModels/ObservableHelper.h"
#include "ViewModels/SourcePresentation.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    namespace Sources = ::HaloDesktop::Sources;

    auto const Visible = winrt::Microsoft::UI::Xaml::Visibility::Visible;
    auto const Collapsed = winrt::Microsoft::UI::Xaml::Visibility::Collapsed;

    // DownloadService writes this exact sentence when a transfer has no sidecar.
    constexpr wchar_t const* NoSubtitleSidecar = L"No subtitle sidecar";

    // The throughput strip is drawn at this height in DownloadsPage.xaml; the bars
    // are projected in the same space so nothing has to be scaled at bind time.
    constexpr double ChartHeight = 38.0;

    // DownloadService keeps at most this many throughput samples.
    constexpr std::uint32_t ChartSlots = 30;

    constexpr std::int32_t FilterAll = 0;
    constexpr std::int32_t FilterActive = 1;
    constexpr std::int32_t FilterReady = 2;
    constexpr std::int32_t FilterFailed = 3;
    constexpr std::int32_t FilterCount = 4;

    bool MatchesFilter(winrt::HaloDesktop::DownloadRowViewModel const& row, std::int32_t filter)
    {
        switch (filter)
        {
        case FilterActive:
            return row.StateLabel() != L"ON DISK";
        case FilterReady:
            return row.StateLabel() == L"ON DISK";
        case FilterFailed:
            return row.StateLabel() == L"FAILED";
        default:
            return true;
        }
    }

    bool IsFailed(winrt::HaloDesktop::DownloadRowViewModel const& row)
    {
        return row.StateLabel() == L"FAILED";
    }

    winrt::hstring StateLabel(winrt::HaloDesktop::DownloadState state)
    {
        switch (state)
        {
        case winrt::HaloDesktop::DownloadState::Downloading: return L"DOWNLOADING";
        case winrt::HaloDesktop::DownloadState::Queued: return L"QUEUED";
        case winrt::HaloDesktop::DownloadState::Paused: return L"PAUSED";
        case winrt::HaloDesktop::DownloadState::OnDisk: return L"ON DISK";
        case winrt::HaloDesktop::DownloadState::Failed: return L"FAILED";
        }
        return L"";
    }

    winrt::hstring FormatBytes(std::uint64_t value)
    {
        constexpr std::array<wchar_t const*, 5> units{ L"B", L"KB", L"MB", L"GB", L"TB" };
        auto amount = static_cast<double>(value);
        std::size_t unit{};
        while (amount >= 1024.0 && unit + 1 < units.size())
        {
            amount /= 1024.0;
            ++unit;
        }
        std::wostringstream output;
        output << std::fixed << std::setprecision(unit == 0 ? 0 : 1) << amount << L" " << units[unit];
        return winrt::hstring{ output.str() };
    }
}

namespace winrt::HaloDesktop::implementation
{
    DownloadRowViewModel::DownloadRowViewModel(winrt::HaloDesktop::DownloadItem item)
        : m_item(std::move(item))
    {
    }
    // A row is handed a rebuilt item on every progress sample, roughly once a
    // second per transfer. Raising every property each time re-runs every
    // binding on the row, and the poster binding turns a string into a fresh
    // BitmapImage each pass: the artwork blinks out and decodes again while the
    // progress bar moves. Only what actually changed is announced.
    void DownloadRowViewModel::Update(winrt::HaloDesktop::DownloadItem item)
    {
        auto const previous = std::exchange(m_item, std::move(item));
        if (!previous)
        {
            RaiseState();
            return;
        }
        auto const raise = [this](wchar_t const* property)
        {
            ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, property);
        };
        if (previous.Tag() != m_item.Tag()) raise(L"Tag");
        if (previous.Name() != m_item.Name()) raise(L"Name");
        if (previous.Sub() != m_item.Sub()) raise(L"Sub");
        if (previous.Progress() != m_item.Progress()) raise(L"Progress");
        if (previous.Detail() != m_item.Detail()) raise(L"Detail");
        if (previous.Quality() != m_item.Quality() || previous.Codec() != m_item.Codec()) raise(L"QualityLine");
        if (previous.Size() != m_item.Size()) raise(L"Size");
        if (previous.Subs() != m_item.Subs())
        {
            raise(L"Subs");
            raise(L"SubsChip");
            raise(L"SubsNormalVisibility");
            raise(L"SubsMutedVisibility");
        }
        if (previous.Poster() != m_item.Poster()) raise(L"Poster");
        if (previous.RowArtwork() != m_item.RowArtwork()) raise(L"RowArtwork");
        if (previous.DownloadedBytes() != m_item.DownloadedBytes()
            || previous.TotalBytes() != m_item.TotalBytes()) raise(L"DownloadedLine");
        if (previous.FileName() != m_item.FileName()) raise(L"FileName");
        if (previous.AddedLabel() != m_item.AddedLabel()) raise(L"AddedLabel");
        if (previous.Hdr() != m_item.Hdr())
        {
            raise(L"QualityBadgeDetail");
            raise(L"QualityTone");
        }
        if (previous.State() != m_item.State())
        {
            // The badges and row actions are views of the one state field, so they move together.
            for (auto const property : { L"StateLabel", L"DownloadingVisibility", L"QueuedVisibility", L"PausedVisibility", L"FailedVisibility", L"OnDiskVisibility", L"RetryVisibility", L"ChooseSourceVisibility", L"PauseVisibility", L"ResumeVisibility", L"LeadNormalVisibility", L"LeadCautionVisibility", L"LeadCriticalVisibility", L"ProgressAccentVisibility", L"ProgressCautionVisibility", L"ProgressCriticalVisibility", L"PauseGlyph", L"PauseLabel", L"DownloadedLine" })
                raise(property);
        }
    }
    winrt::HaloDesktop::DownloadItem DownloadRowViewModel::Item() const { return m_item; }
    winrt::hstring DownloadRowViewModel::Id() const { return m_item.Id(); }
    winrt::hstring DownloadRowViewModel::Tag() const { return m_item.Tag(); }
    winrt::hstring DownloadRowViewModel::Name() const { return m_item.Name(); }
    // A movie's secondary line repeats its own title, and the reference drops the
    // line rather than printing the same words twice on one row.
    winrt::hstring DownloadRowViewModel::Sub() const
    {
        return m_item.Sub() == m_item.Name() ? winrt::hstring{} : m_item.Sub();
    }
    winrt::hstring DownloadRowViewModel::StateLabel() const { return ::StateLabel(m_item.State()); }
    double DownloadRowViewModel::Progress() const noexcept { return m_item.Progress(); }
    winrt::hstring DownloadRowViewModel::Detail() const { return m_item.Detail(); }
    winrt::hstring DownloadRowViewModel::QualityLine() const { return winrt::hstring(std::wstring(m_item.Quality()) + L" · " + std::wstring(m_item.Codec())); }
    winrt::hstring DownloadRowViewModel::Size() const { return m_item.Size(); }
    winrt::hstring DownloadRowViewModel::Subs() const { return m_item.Subs(); }
    // The row's chip is a mono token beside four other tokens, so it carries the
    // language rather than the sentence. The sentence still reads out in the
    // detail pane's facts table, where there is room for it.
    winrt::hstring DownloadRowViewModel::SubsChip() const
    {
        auto const value = std::wstring{ m_item.Subs() };
        constexpr std::wstring_view prefix{ L"Subtitle: " };
        if (!value.starts_with(prefix)) return L"NO SUBS";
        auto language = value.substr(prefix.size());
        std::transform(language.begin(), language.end(), language.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(std::towupper(character));
        });
        return winrt::hstring{ L"SUB " + language };
    }
    winrt::hstring DownloadRowViewModel::Poster() const { return m_item.Poster(); }
    winrt::hstring DownloadRowViewModel::RowArtwork() const { return m_item.RowArtwork(); }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::DownloadingVisibility() const noexcept { return m_item.State() == winrt::HaloDesktop::DownloadState::Downloading ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::QueuedVisibility() const noexcept { return m_item.State() == winrt::HaloDesktop::DownloadState::Queued ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::PausedVisibility() const noexcept { return m_item.State() == winrt::HaloDesktop::DownloadState::Paused ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::FailedVisibility() const noexcept { return m_item.State() == winrt::HaloDesktop::DownloadState::Failed ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::OnDiskVisibility() const noexcept { return m_item.State() == winrt::HaloDesktop::DownloadState::OnDisk ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::RetryVisibility() const noexcept { return m_item.State() == winrt::HaloDesktop::DownloadState::Failed && !m_item.RequiresNewSource() ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::ChooseSourceVisibility() const noexcept { return m_item.State() == winrt::HaloDesktop::DownloadState::Failed && m_item.RequiresNewSource() ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::PauseVisibility() const noexcept
    {
        return m_item.State() == winrt::HaloDesktop::DownloadState::Downloading || m_item.State() == winrt::HaloDesktop::DownloadState::Queued ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::ResumeVisibility() const noexcept
    {
        return m_item.State() == winrt::HaloDesktop::DownloadState::Paused
                || (m_item.State() == winrt::HaloDesktop::DownloadState::Failed && !m_item.RequiresNewSource())
            ? Visible
            : Collapsed;
    }
    // The lead line, the progress fill and the subtitle chip are three views of
    // the same state, so they are projected as visibility pairs rather than as
    // brushes: the palette stays in the theme dictionaries where it can follow
    // light and dark.
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::LeadNormalVisibility() const noexcept
    {
        auto const state = m_item.State();
        return state != winrt::HaloDesktop::DownloadState::Paused && state != winrt::HaloDesktop::DownloadState::Failed ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::LeadCautionVisibility() const noexcept { return m_item.State() == winrt::HaloDesktop::DownloadState::Paused ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::LeadCriticalVisibility() const noexcept { return m_item.State() == winrt::HaloDesktop::DownloadState::Failed ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::ProgressAccentVisibility() const noexcept
    {
        auto const state = m_item.State();
        return state != winrt::HaloDesktop::DownloadState::Paused && state != winrt::HaloDesktop::DownloadState::Failed ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::ProgressCautionVisibility() const noexcept { return m_item.State() == winrt::HaloDesktop::DownloadState::Paused ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::ProgressCriticalVisibility() const noexcept { return m_item.State() == winrt::HaloDesktop::DownloadState::Failed ? Visible : Collapsed; }
    // Subs() carries the sidecar sentence, so the muted tone is the "no sidecar" case.
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::SubsNormalVisibility() const noexcept { return m_item.Subs() == NoSubtitleSidecar ? Collapsed : Visible; }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::SubsMutedVisibility() const noexcept { return m_item.Subs() == NoSubtitleSidecar ? Visible : Collapsed; }
    winrt::hstring DownloadRowViewModel::PauseGlyph() const { return ResumeVisibility() == Visible ? L"" : L""; }
    winrt::hstring DownloadRowViewModel::PauseLabel() const { return ResumeVisibility() == Visible ? L"Resume transfer" : L"Pause transfer"; }
    winrt::hstring DownloadRowViewModel::DownloadedLine() const
    {
        if (m_item.TotalBytes() == 0) return m_item.State() == winrt::HaloDesktop::DownloadState::OnDisk ? L"SIZE UNKNOWN" : L"SIZE UNKNOWN · " + FormatBytes(m_item.DownloadedBytes());
        if (m_item.State() == winrt::HaloDesktop::DownloadState::OnDisk) return FormatBytes(m_item.TotalBytes());
        return FormatBytes(m_item.DownloadedBytes()) + L" / " + FormatBytes(m_item.TotalBytes());
    }
    winrt::hstring DownloadRowViewModel::FileName() const { return m_item.FileName(); }
    winrt::hstring DownloadRowViewModel::AddedLabel() const { return m_item.AddedLabel(); }
    winrt::hstring DownloadRowViewModel::QualityBadgeTier() const { return Sources::BadgeTierLabel(Sources::TierOf(m_item.Quality())); }
    winrt::hstring DownloadRowViewModel::QualityBadgeDetail() const { return m_item.Hdr() ? winrt::hstring{ L"HDR" } : winrt::hstring{}; }
    winrt::HaloDesktop::QualityBadgeTone DownloadRowViewModel::QualityTone() const noexcept
    {
        return Sources::IsPremiumTier(Sources::TierOf(m_item.Quality()))
            ? winrt::HaloDesktop::QualityBadgeTone::Gold
            : winrt::HaloDesktop::QualityBadgeTone::Muted;
    }
    winrt::event_token DownloadRowViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler) { return m_propertyChanged.add(handler); }
    void DownloadRowViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }
    void DownloadRowViewModel::RaiseState()
    {
        for (auto const property : { L"Tag", L"Name", L"Sub", L"StateLabel", L"Progress", L"Detail", L"QualityLine", L"Size", L"Subs", L"SubsChip", L"Poster", L"DownloadingVisibility", L"QueuedVisibility", L"PausedVisibility", L"FailedVisibility", L"OnDiskVisibility", L"RetryVisibility", L"ChooseSourceVisibility", L"PauseVisibility", L"ResumeVisibility", L"LeadNormalVisibility", L"LeadCautionVisibility", L"LeadCriticalVisibility", L"ProgressAccentVisibility", L"ProgressCautionVisibility", L"ProgressCriticalVisibility", L"SubsNormalVisibility", L"SubsMutedVisibility", L"PauseGlyph", L"PauseLabel", L"DownloadedLine", L"FileName", L"AddedLabel", L"QualityBadgeTier", L"QualityBadgeDetail", L"QualityTone" })
            ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, property);
    }

    ChartBarViewModel::ChartBarViewModel(double value, bool recent)
        : m_height((std::max)(2.0, (std::min)(ChartHeight, value))), m_recent(recent)
    {
    }
    double ChartBarViewModel::Height() const noexcept { return m_height; }
    Microsoft::UI::Xaml::Visibility ChartBarViewModel::RecentVisibility() const noexcept { return m_recent ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility ChartBarViewModel::HistoricalVisibility() const noexcept { return m_recent ? Collapsed : Visible; }

    DownloadsViewModel::DownloadsViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_downloads(services.Downloads),
          m_navigation(services.Navigation),
          m_transfers(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_ready(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_filteredTransfers(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_filteredReady(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_chartBars(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>())
    {
        Synchronize();
    }
    DownloadsViewModel::~DownloadsViewModel() { Deactivate(); }
    void DownloadsViewModel::Activate()
    {
        if (m_changedToken != 0)
        {
            return;
        }
        m_changedToken = m_downloads->AddChangedHandler([weak = get_weak()]()
        {
            if (auto const self = weak.get())
            {
                self->Synchronize();
            }
        });
        Synchronize();
    }
    void DownloadsViewModel::Deactivate() noexcept
    {
        if (m_changedToken == 0)
        {
            return;
        }
        m_downloads->RemoveChangedHandler(m_changedToken);
        m_changedToken = 0;
    }
    winrt::Windows::Foundation::IInspectable DownloadsViewModel::Transfers() const { return m_transfers; }
    winrt::Windows::Foundation::IInspectable DownloadsViewModel::Ready() const { return m_ready; }
    winrt::Windows::Foundation::IInspectable DownloadsViewModel::ChartBars() const { return m_chartBars; }
    winrt::hstring DownloadsViewModel::ActionErrorText() const { return m_downloads->ActionError(); }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::ActionErrorVisibility() const noexcept
    {
        return m_downloads->ActionError().empty() ? Collapsed : Visible;
    }
    winrt::hstring DownloadsViewModel::FilterAllCount() const { return winrt::to_hstring(m_transfers.Size() + m_ready.Size()); }
    winrt::hstring DownloadsViewModel::FilterActiveCount() const { return winrt::to_hstring(m_transfers.Size()); }
    winrt::hstring DownloadsViewModel::FilterReadyCount() const { return winrt::to_hstring(m_ready.Size()); }
    winrt::hstring DownloadsViewModel::FilterFailedCount() const
    {
        std::uint32_t count{};
        for (auto const& item : m_transfers)
        {
            if (item.as<winrt::HaloDesktop::DownloadRowViewModel>().StateLabel() == L"FAILED") ++count;
        }
        return winrt::to_hstring(count);
    }
    std::int32_t DownloadsViewModel::FilterIndex() const noexcept { return m_filterIndex; }
    winrt::hstring DownloadsViewModel::NoMatchesLine() const
    {
        switch (m_filterIndex)
        {
        case FilterActive: return L"No active transfers right now.";
        case FilterReady: return L"No downloads are ready to watch yet.";
        case FilterFailed: return L"No failed transfers need attention.";
        default: return L"No downloads match this filter right now.";
        }
    }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> DownloadsViewModel::TransfersView() const { return m_filteredTransfers; }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> DownloadsViewModel::ReadyView() const { return m_filteredReady; }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> DownloadsViewModel::ChartBarsView() const { return m_chartBars; }
    winrt::HaloDesktop::DownloadRowViewModel DownloadsViewModel::SelectedRow() const { return m_selected; }
    winrt::hstring DownloadsViewModel::RateText() const
    {
        if (m_downloads->IsPausedAll()) return L"PAUSED";
        std::wostringstream value;
        value << std::fixed << std::setprecision(1) << m_downloads->AggregateRate() << L" MB/s";
        return winrt::hstring(value.str());
    }
    // Three tones for one number: moving, idle at zero, and held by the user.
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::RateNormalVisibility() const noexcept
    {
        return !m_downloads->IsPausedAll() && m_downloads->AggregateRate() > 0.0 ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::RateIdleVisibility() const noexcept
    {
        return !m_downloads->IsPausedAll() && m_downloads->AggregateRate() <= 0.0 ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::RatePausedVisibility() const noexcept
    {
        return m_downloads->IsPausedAll() ? Visible : Collapsed;
    }
    winrt::hstring DownloadsViewModel::QueueLine() const { return m_downloads->QueueLine(); }
    winrt::hstring DownloadsViewModel::TransferCountLabel() const
    {
        std::wostringstream value;
        value << m_filteredTransfers.Size() << L" ITEMS";
        return winrt::hstring(value.str());
    }
    winrt::hstring DownloadsViewModel::ReadyCountLabel() const
    {
        std::wostringstream value;
        value << m_filteredReady.Size() << L" ITEMS";
        return winrt::hstring(value.str());
    }
    winrt::hstring DownloadsViewModel::PauseAllLabel() const { return m_downloads->IsPausedAll() ? L"Resume all" : L"Pause all"; }
    winrt::hstring DownloadsViewModel::PauseAllGlyph() const { return m_downloads->IsPausedAll() ? L"\uE768" : L"\uE769"; }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::PauseAllVisibility() const noexcept
    {
        return m_downloads->ActiveCount() > 0 || m_downloads->IsPausedAll() ? Visible : Collapsed;
    }
    bool DownloadsViewModel::IsPausedAll() const noexcept { return m_downloads->IsPausedAll(); }
    winrt::hstring DownloadsViewModel::SelectedTag() const { return m_selected ? m_selected.Tag() : L""; }
    winrt::hstring DownloadsViewModel::SelectedTitle() const { return m_selected ? m_selected.Name() : L""; }
    winrt::hstring DownloadsViewModel::SelectedSub() const { return m_selected ? m_selected.Sub() : L""; }
    double DownloadsViewModel::SelectedProgress() const noexcept { return m_selected ? m_selected.Progress() : 0.0; }
    winrt::hstring DownloadsViewModel::SelectedDetail() const { return m_selected ? m_selected.Detail() : L""; }
    winrt::hstring DownloadsViewModel::SelectedPercentText() const
    {
        if (!m_selected) return {};
        std::wostringstream value;
        value << static_cast<int>(m_selected.Progress() * 100.0 + 0.5) << L"%";
        return winrt::hstring{ value.str() };
    }
    winrt::hstring DownloadsViewModel::SelectedQualityLine() const { return m_selected ? m_selected.QualityLine() : L""; }
    winrt::hstring DownloadsViewModel::SelectedSize() const { return m_selected ? m_selected.DownloadedLine() : L""; }
    // A finished file has one size; a running transfer has two, so the label says which.
    winrt::hstring DownloadsViewModel::SelectedSizeFactLabel() const { return SelectedIsReady() ? L"File size" : L"Downloaded"; }
    winrt::hstring DownloadsViewModel::SelectedSubs() const { return m_selected ? m_selected.Subs() : L""; }
    winrt::hstring DownloadsViewModel::SelectedAdded() const
    {
        if (!m_selected) return {};
        auto const label = std::wstring{ m_selected.AddedLabel() };
        constexpr std::wstring_view prefix{ L"ADDED " };
        return winrt::hstring{ label.starts_with(prefix) ? label.substr(prefix.size()) : label };
    }
    winrt::hstring DownloadsViewModel::SelectedFileName() const { return m_selected ? m_selected.FileName() : L""; }
    winrt::hstring DownloadsViewModel::SelectedPoster() const { return m_selected ? m_selected.Poster() : L""; }
    winrt::hstring DownloadsViewModel::ReadyActionLabel() const { return L"Play offline"; }
    // The failure sentence already sits on the row's lead line; the pane adds what
    // happens to the bytes already on disk, which is the part a viewer acts on.
    winrt::hstring DownloadsViewModel::PaneNote() const
    {
        return PaneNoteVisibility() == Visible
            ? winrt::hstring{ L"THE PARTIAL FILE IS KEPT UNTIL A NEW SOURCE FINISHES." }
            : winrt::hstring{};
    }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::PaneNoteVisibility() const noexcept
    {
        auto const item = SelectedItem();
        return item && item.State() == winrt::HaloDesktop::DownloadState::Failed ? Visible : Collapsed;
    }
    winrt::hstring DownloadsViewModel::StorageLine() const
    {
        return FormatBytes(m_downloads->StoredBytes()) + L" on this device";
    }
    winrt::hstring DownloadsViewModel::FreeLine() const
    {
        auto const free = m_downloads->FreeBytes();
        return free ? FormatBytes(*free) + L" FREE" : winrt::hstring{ L"FREE SPACE UNKNOWN" };
    }
    winrt::hstring DownloadsViewModel::StoredLine() const { return FormatBytes(m_downloads->StoredBytes()); }
    winrt::hstring DownloadsViewModel::InFlightLine() const { return FormatBytes(m_downloads->InFlightBytes()); }
    winrt::hstring DownloadsViewModel::PeakText() const
    {
        auto const values = m_downloads->Throughput();
        auto peak = 0.0;
        for (auto const value : values) peak = (std::max)(peak, value);
        std::wostringstream output;
        output << L"PEAK " << std::fixed << std::setprecision(1) << peak << L" MB/S";
        return winrt::hstring{ output.str() };
    }
    double DownloadsViewModel::StorageFraction() const noexcept
    {
        auto const used = m_downloads->StoredBytes() + m_downloads->InFlightBytes();
        auto const free = m_downloads->FreeBytes().value_or(0);
        auto const total = used + free;
        return total > 0 ? static_cast<double>(used) / static_cast<double>(total) : 0.0;
    }
    double DownloadsViewModel::StoredFraction() const noexcept
    {
        auto const total = m_downloads->StoredBytes() + m_downloads->InFlightBytes() + m_downloads->FreeBytes().value_or(0);
        return total > 0 ? static_cast<double>(m_downloads->StoredBytes()) / static_cast<double>(total) : 0.0;
    }
    double DownloadsViewModel::InFlightFraction() const noexcept
    {
        auto const total = m_downloads->StoredBytes() + m_downloads->InFlightBytes() + m_downloads->FreeBytes().value_or(0);
        return total > 0 ? static_cast<double>(m_downloads->InFlightBytes()) / static_cast<double>(total) : 0.0;
    }
    winrt::hstring DownloadsViewModel::DownloadDirectory() const
    {
        auto const directory = m_downloads->DownloadDirectory().wstring();
        return directory.empty() ? winrt::hstring{ L"Folder unavailable" } : winrt::hstring{ directory };
    }
    winrt::hstring DownloadsViewModel::FolderLine() const
    {
        auto const free = m_downloads->FreeBytes();
        auto line = std::wstring{ DownloadDirectory() };
        std::transform(line.begin(), line.end(), line.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(std::towupper(character));
        });
        return winrt::hstring{ free ? L"FOLDER · " + line + L" · " + std::wstring{ FormatBytes(*free) } + L" FREE" : L"FOLDER · " + line };
    }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::DetailVisibility() const noexcept { return m_selected ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::FolderVisibility() const noexcept { return m_selected ? Collapsed : Visible; }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::SelectedTransferVisibility() const noexcept { return m_selected && !SelectedIsReady() ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::SelectedReadyVisibility() const noexcept { return m_selected && SelectedIsReady() ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::PauseVisibility() const noexcept { return SelectedItem() && SelectedItem().State() == winrt::HaloDesktop::DownloadState::Downloading ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::ResumeVisibility() const noexcept { auto const item=SelectedItem();return item&&(item.State()==winrt::HaloDesktop::DownloadState::Paused||(item.State()==winrt::HaloDesktop::DownloadState::Failed&&!item.RequiresNewSource()))?Visible:Collapsed; }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::ChooseSourceVisibility() const noexcept { return SelectedItem() && SelectedItem().State() == winrt::HaloDesktop::DownloadState::Failed && SelectedItem().RequiresNewSource() ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::TransferSectionVisibility() const noexcept { return m_filteredTransfers.Size() > 0 ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::ReadySectionVisibility() const noexcept { return m_filteredReady.Size() > 0 ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::EmptyVisibility() const noexcept { return m_transfers.Size() == 0 && m_ready.Size() == 0 ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::NoMatchesVisibility() const noexcept
    {
        return EmptyVisibility() == Collapsed && m_filteredTransfers.Size() == 0 && m_filteredReady.Size() == 0 ? Visible : Collapsed;
    }
    void DownloadsViewModel::Select(winrt::hstring const& id) { m_selectedId = id; ResolveSelection(); RaiseState(); }
    void DownloadsViewModel::SetFilter(std::int32_t index)
    {
        if (index < 0 || index >= FilterCount || index == m_filterIndex) return;
        m_filterIndex = index;
        RebuildFilteredViews();
        ResolveSelection();
        RaiseState();
    }
    void DownloadsViewModel::Pause(winrt::hstring const& id) { static_cast<void>(m_downloads->PauseTransfer(id)); }
    void DownloadsViewModel::Resume(winrt::hstring const& id) { static_cast<void>(m_downloads->ResumeTransfer(id)); }
    void DownloadsViewModel::Retry(winrt::hstring const& id) { static_cast<void>(m_downloads->ResumeTransfer(id)); }
    void DownloadsViewModel::Cancel(winrt::hstring const& id) { static_cast<void>(m_downloads->CancelTransfer(id)); }
    void DownloadsViewModel::ChooseSourceFor(winrt::hstring const& id)
    {
        if (auto const parameters = m_downloads->BuildSourcesNavigation(id))
        {
            m_navigation->ShowSheet(::HaloDesktop::Services::Page::Sources, parameters);
        }
    }
    void DownloadsViewModel::OpenDownloadFolder() { static_cast<void>(m_downloads->OpenDownloadDirectory()); }
    void DownloadsViewModel::RetryFailed() { m_downloads->RetryFailedTransfers(); }
    void DownloadsViewModel::PauseAll() { m_downloads->PauseAll(); }
    void DownloadsViewModel::ResumeAll() { m_downloads->ResumeAll(); }
    void DownloadsViewModel::PauseSelected()
    {
        if (m_selected)
        {
            m_downloads->PauseTransfer(m_selectedId);
        }
    }
    void DownloadsViewModel::ResumeSelected()
    {
        if (m_selected)
        {
            m_downloads->ResumeTransfer(m_selectedId);
        }
    }
    void DownloadsViewModel::CancelSelected()
    {
        if (m_selected)
        {
            m_downloads->CancelTransfer(m_selectedId);
        }
    }
    void DownloadsViewModel::DeleteSelected()
    {
        if (m_selected)
        {
            m_downloads->DeleteReady(m_selectedId);
        }
    }
    void DownloadsViewModel::OpenPlayer()
    {
        if (auto const request = m_downloads->BuildPlaybackRequest(m_selectedId))
        {
            m_navigation->ShowOverlay(::HaloDesktop::Services::Page::Player, request);
        }
    }
    void DownloadsViewModel::ChooseSource()
    {
        if (auto const parameters = m_downloads->BuildSourcesNavigation(m_selectedId))
        {
            m_navigation->ShowSheet(::HaloDesktop::Services::Page::Sources, parameters);
        }
    }
    void DownloadsViewModel::BrowseLibrary()
    {
        static_cast<void>(m_navigation->GoTo(::HaloDesktop::Services::Page::Library));
    }
    winrt::Windows::Foundation::IAsyncAction DownloadsViewModel::SetDownloadDirectoryAsync(
        std::filesystem::path directory)
    {
        auto lifetime = get_strong();
        co_await m_downloads->SetDownloadDirectoryAsync(std::move(directory));
    }
    winrt::event_token DownloadsViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler) { return m_propertyChanged.add(handler); }
    void DownloadsViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }

    void DownloadsViewModel::Synchronize()
    {
        SynchronizeRows(m_downloads->Transfers(), m_transfers);
        SynchronizeRows(m_downloads->Ready(), m_ready);
        RebuildFilteredViews();
        RebuildChart();
        ResolveSelection();
        RaiseState();
    }
    // A transfer publishes a fresh sample about once a second. Clearing and
    // refilling the bound vector on every one of those tears the ListView's
    // containers down and back up: the poster is decoded again and the selection
    // visual blinks. The membership is compared first so a moving progress bar
    // costs no collection change at all.
    void DownloadsViewModel::RebuildFilteredViews()
    {
        auto const apply = [this](
            winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> const& source,
            winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> const& target)
        {
            std::vector<winrt::HaloDesktop::DownloadRowViewModel> wanted;
            for (auto const& item : source)
            {
                auto const row = item.as<winrt::HaloDesktop::DownloadRowViewModel>();
                if (MatchesFilter(row, m_filterIndex)) wanted.push_back(row);
            }
            auto unchanged = target.Size() == wanted.size();
            if (unchanged)
            {
                for (std::uint32_t index = 0; index < target.Size(); ++index)
                {
                    if (target.GetAt(index) != wanted[index])
                    {
                        unchanged = false;
                        break;
                    }
                }
            }
            if (unchanged) return;
            target.Clear();
            for (auto const& row : wanted) target.Append(row);
        };
        apply(m_transfers, m_filteredTransfers);
        apply(m_ready, m_filteredReady);
    }
    void DownloadsViewModel::SynchronizeRows(
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::DownloadItem> const& source,
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> const& target)
    {
        auto sameMembership = source.Size() == target.Size();
        if (sameMembership)
        {
            for (std::uint32_t index = 0; index < source.Size(); ++index)
            {
                if (target.GetAt(index).as<winrt::HaloDesktop::DownloadRowViewModel>().Id() != source.GetAt(index).Id())
                {
                    sameMembership = false;
                    break;
                }
            }
        }
        if (!sameMembership)
        {
            target.Clear();
            for (auto const& item : source)
            {
                target.Append(winrt::make<DownloadRowViewModel>(item));
            }
            return;
        }
        for (std::uint32_t index = 0; index < source.Size(); ++index)
        {
            winrt::get_self<DownloadRowViewModel>(target.GetAt(index).as<winrt::HaloDesktop::DownloadRowViewModel>())->Update(source.GetAt(index));
        }
    }
    // The strip is a fixed window of samples. A freshly opened app has only a
    // couple, and without the empty leading slots those two would be stretched
    // across the whole cell and read as a chart of two enormous readings.
    void DownloadsViewModel::RebuildChart()
    {
        m_chartBars.Clear();
        auto const values = m_downloads->Throughput();
        auto peak = 0.0;
        for (auto const value : values) peak = (std::max)(peak, value);
        auto const taken = (std::min)(values.Size(), ChartSlots);
        for (auto slot = taken; slot < ChartSlots; ++slot)
        {
            m_chartBars.Append(winrt::make<ChartBarViewModel>(0.0, false));
        }
        for (std::uint32_t index = values.Size() - taken; index < values.Size(); ++index)
        {
            auto const height = peak > 0.0 ? values.GetAt(index) / peak * ChartHeight : 0.0;
            m_chartBars.Append(winrt::make<ChartBarViewModel>(height, index + 6 >= values.Size()));
        }
    }
    void DownloadsViewModel::ResolveSelection()
    {
        m_selected = nullptr;
        for (auto const& row : m_filteredReady)
        {
            auto const candidate = row.as<winrt::HaloDesktop::DownloadRowViewModel>();
            if (candidate.Id() == m_selectedId)
            {
                m_selected = candidate;
                break;
            }
        }
        if (!m_selected)
        {
            for (auto const& row : m_filteredTransfers)
            {
                auto const candidate = row.as<winrt::HaloDesktop::DownloadRowViewModel>();
                if (candidate.Id() == m_selectedId)
                {
                    m_selected = candidate;
                    break;
                }
            }
        }
        if (!m_selected && m_filteredReady.Size() > 0)
        {
            m_selected = m_filteredReady.GetAt(0).as<winrt::HaloDesktop::DownloadRowViewModel>();
        }
        if (!m_selected && m_filteredTransfers.Size() > 0)
        {
            m_selected = m_filteredTransfers.GetAt(0).as<winrt::HaloDesktop::DownloadRowViewModel>();
        }
        if (m_selected)
        {
            m_selectedId = m_selected.Id();
        }
    }
    void DownloadsViewModel::RaiseState()
    {
        for (auto const property : { L"ActionErrorText", L"ActionErrorVisibility", L"RateText", L"RateNormalVisibility", L"RateIdleVisibility", L"RatePausedVisibility", L"SelectedRow", L"QueueLine", L"TransferCountLabel", L"ReadyCountLabel", L"PauseAllLabel", L"PauseAllGlyph", L"PauseAllVisibility", L"IsPausedAll", L"SelectedTag", L"SelectedTitle", L"SelectedSub", L"SelectedProgress", L"SelectedDetail", L"SelectedPercentText", L"SelectedQualityLine", L"SelectedSize", L"SelectedSizeFactLabel", L"SelectedSubs", L"SelectedAdded", L"SelectedFileName", L"SelectedPoster", L"ReadyActionLabel", L"PaneNote", L"PaneNoteVisibility", L"StorageLine", L"FreeLine", L"StoredLine", L"InFlightLine", L"PeakText", L"StorageFraction", L"StoredFraction", L"InFlightFraction", L"DownloadDirectory", L"FolderLine", L"DetailVisibility", L"FolderVisibility", L"SelectedTransferVisibility", L"SelectedReadyVisibility", L"PauseVisibility", L"ResumeVisibility", L"ChooseSourceVisibility", L"TransferSectionVisibility", L"ReadySectionVisibility", L"EmptyVisibility", L"NoMatchesVisibility", L"NoMatchesLine", L"FilterAllCount", L"FilterActiveCount", L"FilterReadyCount", L"FilterFailedCount", L"FilterIndex" })
            ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, property);
    }
    winrt::HaloDesktop::DownloadItem DownloadsViewModel::SelectedItem() const
    {
        return m_selected ? winrt::get_self<DownloadRowViewModel>(m_selected)->Item() : nullptr;
    }
    bool DownloadsViewModel::SelectedIsReady() const noexcept
    {
        if (!m_selected)
        {
            return false;
        }
        for (auto const& row : m_ready)
        {
            if (row.as<winrt::HaloDesktop::DownloadRowViewModel>().Id() == m_selectedId)
            {
                return true;
            }
        }
        return false;
    }
}
