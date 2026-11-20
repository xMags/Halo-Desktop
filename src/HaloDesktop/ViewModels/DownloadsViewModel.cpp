#include "pch.h"
#include "ViewModels/DownloadsViewModel.h"
#if __has_include("DownloadRowViewModel.g.cpp")
#include "DownloadRowViewModel.g.cpp"
#endif
#if __has_include("DownloadsViewModel.g.cpp")
#include "DownloadsViewModel.g.cpp"
#endif

#include "Models/Models.h"
#include "Services/DownloadStreamFacts.h"
#include "Services/DownloadsLogicPolicy.h"
#include "Services/NavigationService.h"
#include "ViewModels/ObservableHelper.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <iterator>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace
{
    namespace Presentation = ::HaloDesktop::ViewModels::DownloadsPresentation;
    using Presentation::DownloadRow;
    using Presentation::RowState;
    using Presentation::RowTone;

    auto const Visible = winrt::Microsoft::UI::Xaml::Visibility::Visible;
    auto const Collapsed = winrt::Microsoft::UI::Xaml::Visibility::Collapsed;

    // The space between the queue line's parts (`gap: 14px` at 12px mono).
    constexpr std::wstring_view MetaGap = L"    ";

    winrt::Microsoft::UI::Xaml::Visibility VisibleWhen(bool condition) noexcept
    {
        return condition ? Visible : Collapsed;
    }

    winrt::Microsoft::UI::Xaml::GridLength Star(double value) noexcept
    {
        return winrt::Microsoft::UI::Xaml::GridLengthHelper::FromValueAndType(
            std::isfinite(value) ? (std::max)(0.0, value) : 0.0,
            winrt::Microsoft::UI::Xaml::GridUnitType::Star);
    }

    std::wstring MetaMiddleOf(std::vector<std::wstring> const& parts)
    {
        std::wstring result;
        for (std::size_t index = 1; index + 1 < parts.size(); ++index)
        {
            result += MetaGap;
            result += parts[index];
        }
        return result;
    }

    std::wstring MetaLastOf(std::vector<std::wstring> const& parts)
    {
        if (parts.size() < 2) return {};
        return std::wstring{ MetaGap } + parts.back();
    }

    constexpr std::array<Presentation::Filter, 4> Filters{
        Presentation::Filter::All,
        Presentation::Filter::Active,
        Presentation::Filter::Ready,
        Presentation::Filter::Failed,
    };
}

namespace winrt::HaloDesktop::implementation
{
    DownloadRowViewModel::DownloadRowViewModel(DownloadRow row)
        : m_row(std::move(row))
    {
    }

    void DownloadRowViewModel::Update(DownloadRow row)
    {
        auto const previous = std::exchange(m_row, std::move(row));
        auto const& next = m_row;
        if (previous.Tag != next.Tag) Raise(L"Tag");
        if (previous.EpisodeLabel != next.EpisodeLabel)
        {
            Raise(L"EpisodeLabel");
            Raise(L"EpisodeLabelVisibility");
        }
        if (previous.Name != next.Name) Raise(L"Name");
        if (previous.Sub != next.Sub) Raise(L"Sub");
        if (previous.CardTitle != next.CardTitle || previous.CardSub != next.CardSub || previous.Name != next.Name || previous.Sub != next.Sub)
        {
            Raise(L"AccessibleName");
        }
        if (previous.MetaId != next.MetaId) Raise(L"MetaId");
        if (previous.RowArtwork != next.RowArtwork) Raise(L"RowArtwork");
        if (previous.SavedArtwork != next.SavedArtwork) Raise(L"SavedArtwork");
        if (previous.SavedLogo != next.SavedLogo || previous.OnlineLogo != next.OnlineLogo)
        {
            Raise(L"SavedLogo");
            Raise(L"OnlineLogo");
        }
        if (previous.Progress != next.Progress)
        {
            for (auto const property : { L"Progress", L"ProgressDone", L"ProgressRest", L"PercentText" }) Raise(property);
        }
        if (previous.Lead != next.Lead) Raise(L"Lead");
        if (previous.Tone != next.Tone)
        {
            for (auto const property : { L"ToneNormalVisibility", L"ToneCautionVisibility", L"ToneCriticalVisibility" }) Raise(property);
        }
        if (previous.DownloadedLine != next.DownloadedLine) Raise(L"DownloadedLine");
        if (previous.TransferMeta != next.TransferMeta)
        {
            for (auto const property : { L"MetaLead", L"MetaMiddle", L"MetaLast" }) Raise(property);
        }
        if (previous.State != next.State)
        {
            for (auto const property : {
                     L"UpNextVisibility", L"TransferBodyVisibility", L"ThumbOpacity", L"ResumeVisibility",
                     L"PanePauseVisibility", L"StatePill", L"StateAccentVisibility", L"StateCautionVisibility",
                     L"StateCriticalVisibility", L"StateNeutralVisibility", L"FailedNoteVisibility", L"SizeFactLabel",
                     L"IsPaused" })
            {
                Raise(property);
            }
        }
        if (previous.RequiresNewSource != next.RequiresNewSource) Raise(L"ChooseSourceVisibility");
        if (previous.CanPause != next.CanPause) Raise(L"PauseVisibility");
        if (previous.CanRetry != next.CanRetry) Raise(L"RetryVisibility");
        if (previous.CanResume != next.CanResume) Raise(L"PaneResumeVisibility");
        if (previous.PaneMeta != next.PaneMeta) Raise(L"PaneMeta");
        if (previous.QualityLine != next.QualityLine) Raise(L"QualityLine");
        if (previous.Subs != next.Subs) Raise(L"Subs");
        if (previous.AddedLabel != next.AddedLabel) Raise(L"AddedFact");
        if (previous.FileName != next.FileName) Raise(L"FileName");
        if (previous.CardTitle != next.CardTitle) Raise(L"CardTitle");
        if (previous.CardSub != next.CardSub)
        {
            Raise(L"CardSub");
            Raise(L"CardSubVisibility");
        }
        if (previous.CardMeta != next.CardMeta) Raise(L"CardMeta");
        if (previous.Badge != next.Badge)
        {
            for (auto const property : { L"QualityBadge", L"QualityBadgeGoldVisibility", L"QualityBadgePlainVisibility" }) Raise(property);
        }
    }

    void DownloadRowViewModel::SetSelected(bool selected)
    {
        if (m_selected == selected) return;
        m_selected = selected;
        Raise(L"IsSelected");
        Raise(L"SelectedVisibility");
    }

    void DownloadRowViewModel::SetTopRule(bool visible)
    {
        if (m_topRule == visible) return;
        m_topRule = visible;
        Raise(L"TopRuleVisibility");
    }

    DownloadRow const& DownloadRowViewModel::Row() const noexcept { return m_row; }
    winrt::hstring DownloadRowViewModel::Id() const { return winrt::hstring{ m_row.Id }; }
    winrt::hstring DownloadRowViewModel::Tag() const { return winrt::hstring{ m_row.Tag }; }
    winrt::hstring DownloadRowViewModel::EpisodeLabel() const { return winrt::hstring{ m_row.EpisodeLabel }; }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::EpisodeLabelVisibility() const noexcept { return VisibleWhen(!m_row.EpisodeLabel.empty()); }
    winrt::hstring DownloadRowViewModel::Name() const { return winrt::hstring{ m_row.Name }; }
    winrt::hstring DownloadRowViewModel::Sub() const { return winrt::hstring{ m_row.Sub }; }
    winrt::hstring DownloadRowViewModel::AccessibleName() const
    {
        auto const& sub = m_row.State == RowState::OnDisk ? m_row.CardSub : m_row.Sub;
        auto const& title = m_row.State == RowState::OnDisk ? m_row.CardTitle : m_row.Name;
        return winrt::hstring{ sub.empty() ? title : title + L" · " + sub };
    }
    winrt::hstring DownloadRowViewModel::MetaId() const { return winrt::hstring{ m_row.MetaId }; }
    winrt::hstring DownloadRowViewModel::RowArtwork() const { return winrt::hstring{ m_row.RowArtwork }; }
    winrt::hstring DownloadRowViewModel::SavedArtwork() const { return winrt::hstring{ m_row.SavedArtwork }; }
    winrt::hstring DownloadRowViewModel::SavedLogo() const { return winrt::hstring{ m_row.SavedLogo }; }
    winrt::hstring DownloadRowViewModel::OnlineLogo() const { return winrt::hstring{ m_row.OnlineLogo }; }
    double DownloadRowViewModel::Progress() const noexcept { return m_row.Progress; }
    Microsoft::UI::Xaml::GridLength DownloadRowViewModel::ProgressDone() const noexcept { return Star(m_row.Progress); }
    Microsoft::UI::Xaml::GridLength DownloadRowViewModel::ProgressRest() const noexcept { return Star(1.0 - m_row.Progress); }
    winrt::hstring DownloadRowViewModel::PercentText() const { return winrt::hstring{ Presentation::PercentText(m_row.Progress) }; }
    winrt::hstring DownloadRowViewModel::Lead() const { return winrt::hstring{ m_row.Lead }; }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::ToneNormalVisibility() const noexcept { return VisibleWhen(m_row.Tone == RowTone::Normal); }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::ToneCautionVisibility() const noexcept { return VisibleWhen(m_row.Tone == RowTone::Caution); }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::ToneCriticalVisibility() const noexcept { return VisibleWhen(m_row.Tone == RowTone::Critical); }
    winrt::hstring DownloadRowViewModel::DownloadedLine() const { return winrt::hstring{ m_row.DownloadedLine }; }
    winrt::hstring DownloadRowViewModel::MetaLead() const { return m_row.TransferMeta.empty() ? winrt::hstring{} : winrt::hstring{ m_row.TransferMeta.front() }; }
    winrt::hstring DownloadRowViewModel::MetaMiddle() const { return winrt::hstring{ MetaMiddleOf(m_row.TransferMeta) }; }
    winrt::hstring DownloadRowViewModel::MetaLast() const { return winrt::hstring{ MetaLastOf(m_row.TransferMeta) }; }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::UpNextVisibility() const noexcept { return VisibleWhen(m_row.State == RowState::Queued); }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::TransferBodyVisibility() const noexcept { return VisibleWhen(m_row.State != RowState::Queued); }
    double DownloadRowViewModel::ThumbOpacity() const noexcept { return m_row.State == RowState::Queued ? 0.55 : 1.0; }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::ChooseSourceVisibility() const noexcept { return VisibleWhen(m_row.RequiresNewSource); }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::PauseVisibility() const noexcept { return VisibleWhen(m_row.CanPause); }
    // The row resumes only what the viewer paused; a retryable failure has its own retry button.
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::ResumeVisibility() const noexcept { return VisibleWhen(m_row.State == RowState::Paused); }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::RetryVisibility() const noexcept { return VisibleWhen(m_row.CanRetry); }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::PanePauseVisibility() const noexcept { return VisibleWhen(m_row.State == RowState::Downloading); }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::PaneResumeVisibility() const noexcept { return VisibleWhen(m_row.CanResume); }
    winrt::hstring DownloadRowViewModel::StatePill() const { return winrt::hstring{ Presentation::StatePillText(m_row.State) }; }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::StateAccentVisibility() const noexcept { return VisibleWhen(m_row.State == RowState::Downloading); }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::StateCautionVisibility() const noexcept { return VisibleWhen(m_row.State == RowState::Paused); }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::StateCriticalVisibility() const noexcept { return VisibleWhen(m_row.State == RowState::Failed); }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::StateNeutralVisibility() const noexcept { return VisibleWhen(m_row.State == RowState::Queued); }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::FailedNoteVisibility() const noexcept { return VisibleWhen(m_row.State == RowState::Failed); }
    // `.dl-pane-meta { text-transform: uppercase }`.
    winrt::hstring DownloadRowViewModel::PaneMeta() const
    {
        auto value = m_row.PaneMeta;
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(std::towupper(character));
        });
        return winrt::hstring{ value };
    }
    winrt::hstring DownloadRowViewModel::QualityLine() const { return winrt::hstring{ m_row.QualityLine }; }
    winrt::hstring DownloadRowViewModel::Subs() const { return winrt::hstring{ m_row.Subs }; }
    winrt::hstring DownloadRowViewModel::AddedFact() const { return winrt::hstring{ Presentation::AddedFact(m_row.AddedLabel) }; }
    winrt::hstring DownloadRowViewModel::SizeFactLabel() const { return m_row.State == RowState::OnDisk ? L"Size" : L"Downloaded"; }
    winrt::hstring DownloadRowViewModel::FileName() const { return winrt::hstring{ m_row.FileName }; }
    winrt::hstring DownloadRowViewModel::CardTitle() const { return winrt::hstring{ m_row.CardTitle }; }
    winrt::hstring DownloadRowViewModel::CardSub() const { return winrt::hstring{ m_row.CardSub }; }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::CardSubVisibility() const noexcept { return VisibleWhen(!m_row.CardSub.empty()); }
    winrt::hstring DownloadRowViewModel::CardMeta() const { return winrt::hstring{ m_row.CardMeta }; }
    winrt::hstring DownloadRowViewModel::QualityBadge() const { return m_row.Badge ? winrt::hstring{ m_row.Badge->Label } : winrt::hstring{}; }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::QualityBadgeGoldVisibility() const noexcept { return VisibleWhen(m_row.Badge && m_row.Badge->Gold); }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::QualityBadgePlainVisibility() const noexcept { return VisibleWhen(m_row.Badge && !m_row.Badge->Gold); }
    bool DownloadRowViewModel::IsSelected() const noexcept { return m_selected; }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::SelectedVisibility() const noexcept { return VisibleWhen(m_selected); }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::TopRuleVisibility() const noexcept { return VisibleWhen(m_topRule); }
    bool DownloadRowViewModel::IsPaused() const noexcept { return m_row.State == RowState::Paused; }
    bool DownloadRowViewModel::IsExpanded() const noexcept { return m_expanded; }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::DetailsVisibility() const noexcept { return VisibleWhen(m_expanded); }
    float DownloadRowViewModel::ChevronRotation() const noexcept { return m_expanded ? 180.0F : 0.0F; }
    void DownloadRowViewModel::SetExpanded(bool expanded)
    {
        if (m_expanded == expanded) return;
        m_expanded = expanded;
        for (auto const property : { L"IsExpanded", L"DetailsVisibility", L"ChevronRotation" }) Raise(property);
    }
    winrt::event_token DownloadRowViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler) { return m_propertyChanged.add(handler); }
    void DownloadRowViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }
    void DownloadRowViewModel::Raise(wchar_t const* property)
    {
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, property);
    }

    DownloadsViewModel::DownloadsViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_downloads(services.Downloads),
          m_navigation(services.Navigation),
          m_runningView(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_waitingView(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_attentionView(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_filteredReady(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>())
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

    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> DownloadsViewModel::RunningView() const { return m_runningView; }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> DownloadsViewModel::WaitingView() const { return m_waitingView; }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> DownloadsViewModel::AttentionView() const { return m_attentionView; }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::RunningVisibility() const noexcept { return VisibleWhen(m_runningView.Size() > 0); }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::WaitingVisibility() const noexcept { return VisibleWhen(m_waitingView.Size() > 0); }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::AttentionVisibility() const noexcept { return VisibleWhen(m_attentionView.Size() > 0); }
    winrt::hstring DownloadsViewModel::WaitingLine() const
    {
        return winrt::to_hstring(m_waitingView.Size()) + L" waiting";
    }
    winrt::hstring DownloadsViewModel::StatusLine() const
    {
        auto const rate = Presentation::RateFigureOf(m_rate, m_pausedAll);
        auto const speed = m_pausedAll ? std::wstring{ L"All paused" } : rate.Value + (rate.Unit.empty() ? std::wstring{} : L" " + rate.Unit);
        return winrt::hstring{ speed + L" \x00B7 " + Presentation::FreeLine(m_storage.FreeBytes) };
    }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> DownloadsViewModel::ReadyView() const { return m_filteredReady; }
    std::vector<Presentation::ChartBar> const& DownloadsViewModel::ChartBars() const noexcept { return m_chartBars; }

    winrt::hstring DownloadsViewModel::ActionErrorText() const { return m_downloads->ActionError(); }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::ActionErrorVisibility() const noexcept
    {
        return m_downloads->ActionError().empty() ? Collapsed : Visible;
    }
    winrt::hstring DownloadsViewModel::SummaryLine() const
    {
        std::vector<DownloadRow> rows{ m_transferRows };
        rows.insert(rows.end(), m_readyRows.begin(), m_readyRows.end());
        return winrt::hstring{ Presentation::SummaryLine(rows) };
    }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::SummaryVisibility() const noexcept { return VisibleWhen(!m_transferRows.empty() || !m_readyRows.empty()); }
    winrt::hstring DownloadsViewModel::FilterAllCount() const { return winrt::to_hstring(Presentation::CountFilters(m_transferRows, m_readyRows).All); }
    bool DownloadsViewModel::CanDeleteAll() const { return Presentation::CountFilters(m_transferRows, m_readyRows).All > 0; }
    winrt::hstring DownloadsViewModel::FilterActiveCount() const { return winrt::to_hstring(Presentation::CountFilters(m_transferRows, m_readyRows).Active); }
    winrt::hstring DownloadsViewModel::FilterReadyCount() const { return winrt::to_hstring(Presentation::CountFilters(m_transferRows, m_readyRows).Ready); }
    winrt::hstring DownloadsViewModel::FilterFailedCount() const { return winrt::to_hstring(Presentation::CountFilters(m_transferRows, m_readyRows).Failed); }
    std::int32_t DownloadsViewModel::FilterIndex() const noexcept
    {
        auto const found = std::find(Filters.begin(), Filters.end(), m_filter);
        return static_cast<std::int32_t>(found - Filters.begin());
    }
    winrt::hstring DownloadsViewModel::PauseAllLabel() const { return m_pausedAll ? L"Resume all" : L"Pause all"; }
    winrt::hstring DownloadsViewModel::PauseAllGlyph() const { return m_pausedAll ? L"" : L""; }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::PauseAllVisibility() const noexcept { return VisibleWhen(m_activeCount > 0 || m_pausedAll); }
    bool DownloadsViewModel::IsPausedAll() const noexcept { return m_pausedAll; }
    winrt::hstring DownloadsViewModel::RateValue() const { return winrt::hstring{ Presentation::RateFigureOf(m_rate, m_pausedAll).Value }; }
    winrt::hstring DownloadsViewModel::RateUnit() const { return winrt::hstring{ Presentation::RateFigureOf(m_rate, m_pausedAll).Unit }; }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::RateUnitVisibility() const noexcept { return VisibleWhen(!m_pausedAll); }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::RateNormalVisibility() const noexcept { return VisibleWhen(Presentation::RateToneOf(m_rate, m_pausedAll) == Presentation::RateTone::Normal); }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::RateIdleVisibility() const noexcept { return VisibleWhen(Presentation::RateToneOf(m_rate, m_pausedAll) == Presentation::RateTone::Idle); }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::RatePausedVisibility() const noexcept { return VisibleWhen(Presentation::RateToneOf(m_rate, m_pausedAll) == Presentation::RateTone::Paused); }
    winrt::hstring DownloadsViewModel::ConnectionsText() const { return m_rateNote.Kind == Presentation::RateNoteKind::Connections ? winrt::hstring{ m_rateNote.Text } : winrt::hstring{}; }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::ConnectionsVisibility() const noexcept { return VisibleWhen(m_rateNote.Kind == Presentation::RateNoteKind::Connections); }
    winrt::hstring DownloadsViewModel::RateNoteText() const { return m_rateNote.Kind == Presentation::RateNoteKind::Note ? winrt::hstring{ m_rateNote.Text } : winrt::hstring{}; }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::RateNoteVisibility() const noexcept { return VisibleWhen(m_rateNote.Kind == Presentation::RateNoteKind::Note); }
    winrt::hstring DownloadsViewModel::PeakText() const { return winrt::hstring{ Presentation::PeakText(m_chartValues) }; }
    winrt::hstring DownloadsViewModel::StorageValue() const { return winrt::hstring{ Presentation::SplitBytes(m_storage.StoredBytes + m_storage.InFlightBytes).Value }; }
    winrt::hstring DownloadsViewModel::StorageUnitLine() const
    {
        auto const used = Presentation::SplitBytes(m_storage.StoredBytes + m_storage.InFlightBytes);
        return winrt::hstring{ used.Unit + L" · " + Presentation::FreeLine(m_storage.FreeBytes) };
    }
    winrt::hstring DownloadsViewModel::StoredLine() const { return winrt::hstring{ ::HaloDesktop::Services::DownloadsLogic::FormatDownloadBytes(static_cast<double>(m_storage.StoredBytes)) }; }
    winrt::hstring DownloadsViewModel::InFlightLine() const { return winrt::hstring{ ::HaloDesktop::Services::DownloadsLogic::FormatDownloadBytes(static_cast<double>(m_storage.InFlightBytes)) }; }
    // The track's three parts as star columns: stored, in flight, and the rest.
    Microsoft::UI::Xaml::GridLength DownloadsViewModel::StoredWidth() const noexcept { return Star(m_storage.StoredFraction); }
    Microsoft::UI::Xaml::GridLength DownloadsViewModel::FlightWidth() const noexcept { return Star(m_storage.UsedFraction - m_storage.StoredFraction); }
    Microsoft::UI::Xaml::GridLength DownloadsViewModel::FreeWidth() const noexcept
    {
        return Star(m_storage.UsedFraction > 0.0 ? 1.0 - m_storage.UsedFraction : 1.0);
    }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::StoredVisibility() const noexcept { return VisibleWhen(m_storage.StoredFraction > 0.0); }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::FlightVisibility() const noexcept { return VisibleWhen(m_storage.UsedFraction - m_storage.StoredFraction > 0.0); }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::QueueVisibility() const noexcept { return VisibleWhen(!m_shownTransfers.empty()); }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::ReadySectionVisibility() const noexcept { return VisibleWhen(!m_shownReady.empty()); }
    winrt::hstring DownloadsViewModel::ReadyHeadline() const { return winrt::hstring{ Presentation::ReadyHeadline(m_readyRows.size(), m_storage.StoredBytes) }; }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::EmptyVisibility() const noexcept { return VisibleWhen(m_transferRows.empty() && m_readyRows.empty()); }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::NoMatchesVisibility() const noexcept
    {
        return VisibleWhen((!m_transferRows.empty() || !m_readyRows.empty()) && m_shownTransfers.empty() && m_shownReady.empty());
    }
    winrt::hstring DownloadsViewModel::NoMatchesLine() const { return winrt::hstring{ Presentation::NoMatchesLine(m_filter) }; }
    winrt::HaloDesktop::DownloadRowViewModel DownloadsViewModel::SelectedRow() const { return m_selected; }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::DetailVisibility() const noexcept { return VisibleWhen(m_selected != nullptr); }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::FolderVisibility() const noexcept { return VisibleWhen(m_selected == nullptr); }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::SelectedReadyVisibility() const noexcept
    {
        auto const row = SelectedProjection();
        return VisibleWhen(row && row->State == RowState::OnDisk);
    }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::SelectedTransferVisibility() const noexcept
    {
        auto const row = SelectedProjection();
        return VisibleWhen(row && row->State != RowState::OnDisk);
    }

    void DownloadsViewModel::Select(winrt::hstring const& id)
    {
        m_selectedId = std::wstring{ id.c_str() };
        ApplySelection();
        RaiseState();
    }
    void DownloadsViewModel::ToggleDetails(winrt::hstring const& id)
    {
        auto const wanted = std::wstring{ id.c_str() };
        m_expandedId = m_expandedId == wanted ? std::wstring{} : wanted;
        for (auto const& [rowId, row] : m_rows)
        {
            winrt::get_self<DownloadRowViewModel>(row)->SetExpanded(!m_expandedId.empty() && rowId == m_expandedId);
        }
    }
    void DownloadsViewModel::SetFilter(std::int32_t index)
    {
        if (index < 0 || index >= static_cast<std::int32_t>(Filters.size())) return;
        auto const filter = Filters[static_cast<std::size_t>(index)];
        if (filter == m_filter) return;
        m_filter = filter;
        ApplyFilteredViews();
        ApplySelection();
        RaiseState();
    }
    void DownloadsViewModel::Pause(winrt::hstring const& id) { static_cast<void>(m_downloads->PauseTransfer(id)); }
    void DownloadsViewModel::Resume(winrt::hstring const& id) { static_cast<void>(m_downloads->ResumeTransfer(id)); }
    void DownloadsViewModel::Retry(winrt::hstring const& id) { static_cast<void>(m_downloads->ResumeTransfer(id)); }
    void DownloadsViewModel::Cancel(winrt::hstring const& id) { static_cast<void>(m_downloads->CancelTransfer(id)); }
    void DownloadsViewModel::Delete(winrt::hstring const& id) { static_cast<void>(m_downloads->DeleteReady(id)); }
    void DownloadsViewModel::ChooseSourceFor(winrt::hstring const& id)
    {
        if (auto const parameters = m_downloads->BuildSourcesNavigation(id))
        {
            m_navigation->ShowSheet(::HaloDesktop::Services::Page::Sources, parameters);
        }
    }
    void DownloadsViewModel::Play(winrt::hstring const& id)
    {
        if (auto const request = m_downloads->BuildPlaybackRequest(id))
        {
            m_navigation->ShowOverlay(::HaloDesktop::Services::Page::Player, request);
        }
    }
    void DownloadsViewModel::OpenDownloadFolder() { static_cast<void>(m_downloads->OpenDownloadDirectory()); }
    // The pane's "Open folder" shows the file itself; a file the engine will not
    // stand behind any more falls back to the folder, which reports its own failure.
    void DownloadsViewModel::RevealSelected()
    {
        if (!m_selected) return;
        if (!m_downloads->RevealInExplorer(winrt::hstring{ m_selectedId }))
        {
            static_cast<void>(m_downloads->OpenDownloadDirectory());
        }
    }
    // The page the download came from: a film's own, an episode's show. A
    // download without the title's id (an addon that gave none) has no page to open.
    void DownloadsViewModel::OpenDetails(winrt::hstring const& id)
    {
        auto const& records = m_downloads->Records();
        auto const found = std::find_if(records.begin(), records.end(), [&id](auto const& record)
        {
            return record.JobId == std::wstring_view{ id };
        });
        if (found == records.end()) return;
        auto const& media = found->Media;
        if (!media.MetaId || media.MetaId->empty() || media.MediaType.empty()) return;
        auto const& title = media.ShowName && !media.ShowName->empty() ? *media.ShowName : media.Title;
        m_navigation->GoTo(
            ::HaloDesktop::Services::Page::Detail,
            winrt::make<winrt::HaloDesktop::implementation::DetailNavParams>(
                winrt::hstring{ media.MediaType },
                winrt::hstring{ *media.MetaId },
                winrt::hstring{ title },
                winrt::hstring{ media.Poster.value_or(L"") }));
    }
    void DownloadsViewModel::RetryFailed() { m_downloads->RetryFailedTransfers(); }
    void DownloadsViewModel::PauseAll() { m_downloads->PauseAll(); }
    void DownloadsViewModel::ResumeAll() { m_downloads->ResumeAll(); }
    void DownloadsViewModel::PauseSelected()
    {
        if (m_selected) static_cast<void>(m_downloads->PauseTransfer(winrt::hstring{ m_selectedId }));
    }
    void DownloadsViewModel::ResumeSelected()
    {
        if (m_selected) static_cast<void>(m_downloads->ResumeTransfer(winrt::hstring{ m_selectedId }));
    }
    void DownloadsViewModel::CancelSelected()
    {
        if (m_selected) static_cast<void>(m_downloads->CancelTransfer(winrt::hstring{ m_selectedId }));
    }
    void DownloadsViewModel::DeleteAll() { static_cast<void>(m_downloads->DeleteAllDownloads()); }
    void DownloadsViewModel::OpenPlayer()
    {
        if (m_selected) Play(winrt::hstring{ m_selectedId });
    }
    void DownloadsViewModel::ChooseSource()
    {
        if (m_selected) ChooseSourceFor(winrt::hstring{ m_selectedId });
    }
    void DownloadsViewModel::BrowseLibrary()
    {
        static_cast<void>(m_navigation->GoTo(::HaloDesktop::Services::Page::Library));
    }
    std::filesystem::path DownloadsViewModel::CurrentDownloadDirectory() const
    {
        return m_downloads->DownloadDirectory();
    }
    winrt::Windows::Foundation::IAsyncAction DownloadsViewModel::SetDownloadDirectoryAsync(
        std::filesystem::path directory)
    {
        auto lifetime = get_strong();
        co_await m_downloads->SetDownloadDirectoryAsync(std::move(directory));
    }
    winrt::event_token DownloadsViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler) { return m_propertyChanged.add(handler); }
    void DownloadsViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }

    Presentation::StreamFacts DownloadsViewModel::FactsFor(::HaloDesktop::Services::Downloads::DownloadRecord const& record)
    {
        auto key = std::make_tuple(
            record.Media.StreamName,
            record.Media.StreamTitle,
            record.FileName,
            record.TotalBytes,
            record.Media.VideoSize,
            record.Media.DynamicRange);
        if (auto const found = m_facts.find(record.JobId); found != m_facts.end() && found->second.Key == key)
        {
            return found->second.Facts;
        }
        auto facts = ::HaloDesktop::Services::DownloadStreamFactsOf(record);
        m_facts.insert_or_assign(record.JobId, CachedFacts{ .Key = std::move(key), .Facts = facts });
        return facts;
    }

    DownloadRowViewModel* DownloadsViewModel::RowFor(std::wstring const& id) const
    {
        auto const found = m_rows.find(id);
        return found == m_rows.end() ? nullptr : winrt::get_self<DownloadRowViewModel>(found->second);
    }

    DownloadRow const* DownloadsViewModel::SelectedProjection() const
    {
        return m_selected ? &winrt::get_self<DownloadRowViewModel>(m_selected)->Row() : nullptr;
    }

    // Every engine change lands here, progress included, so rows are updated in
    // place: a row keeps its view model while its download exists, and the
    // bound lists change only when what they show does.
    void DownloadsViewModel::Synchronize()
    {
        auto const& records = m_downloads->Records();
        m_transferRows.clear();
        m_readyRows.clear();
        std::unordered_set<std::wstring> present;
        for (auto const& record : records)
        {
            auto row = Presentation::RowOf(record, FactsFor(record));
            present.insert(row.Id);
            if (auto const existing = RowFor(row.Id))
            {
                existing->Update(row);
            }
            else
            {
                m_rows.emplace(row.Id, winrt::make<DownloadRowViewModel>(row));
            }
            (row.State == RowState::OnDisk ? m_readyRows : m_transferRows).push_back(std::move(row));
        }
        std::erase_if(m_rows, [&present](auto const& entry) { return !present.contains(entry.first); });
        std::erase_if(m_facts, [&present](auto const& entry) { return !present.contains(entry.first); });

        m_pausedAll = m_downloads->IsPausedAll();
        m_rate = Presentation::AggregateRate(records);
        m_rateNote = Presentation::RateNoteOf(records);
        m_activeCount = Presentation::ActiveCount(records);
        m_storage = Presentation::StorageFiguresOf(records, m_downloads->FreeBytes());
        ApplyFilteredViews();
        ApplySelection();
        RebuildChart();
        RaiseState();
    }

    void DownloadsViewModel::ApplyFilteredViews()
    {
        m_shownTransfers.clear();
        m_shownReady.clear();
        std::copy_if(m_transferRows.begin(), m_transferRows.end(), std::back_inserter(m_shownTransfers), [this](DownloadRow const& row)
        {
            return Presentation::MatchesFilter(row, m_filter);
        });
        std::copy_if(m_readyRows.begin(), m_readyRows.end(), std::back_inserter(m_shownReady), [this](DownloadRow const& row)
        {
            return Presentation::MatchesFilter(row, m_filter);
        });
        auto const apply = [this](
            std::vector<DownloadRow> const& rows,
            winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> const& target)
        {
            std::vector<winrt::HaloDesktop::DownloadRowViewModel> wanted;
            wanted.reserve(rows.size());
            for (auto const& row : rows)
            {
                if (auto const found = m_rows.find(row.Id); found != m_rows.end()) wanted.push_back(found->second);
            }
            auto unchanged = target.Size() == wanted.size();
            for (std::uint32_t index = 0; unchanged && index < target.Size(); ++index)
            {
                unchanged = target.GetAt(index) == wanted[index];
            }
            if (unchanged) return;
            target.Clear();
            for (auto const& row : wanted) target.Append(row);
        };
        std::vector<DownloadRow> running;
        std::vector<DownloadRow> waiting;
        std::vector<DownloadRow> attention;
        for (auto const& row : m_shownTransfers)
        {
            switch (Presentation::SectionOf(row.State))
            {
            case Presentation::TransferSection::Running: running.push_back(row); break;
            case Presentation::TransferSection::Waiting: waiting.push_back(row); break;
            case Presentation::TransferSection::Attention: attention.push_back(row); break;
            }
        }
        apply(running, m_runningView);
        apply(waiting, m_waitingView);
        apply(attention, m_attentionView);
        apply(m_shownReady, m_filteredReady);
    }

    // The pane follows a download the filter still shows, else the first one
    // it does, and keeps that as the selection.
    void DownloadsViewModel::ApplySelection()
    {
        auto const resolved = Presentation::ResolveSelection(m_selectedId, m_shownReady, m_shownTransfers);
        if (resolved) m_selectedId = resolved->Id;
        winrt::HaloDesktop::DownloadRowViewModel selected{ nullptr };
        if (resolved)
        {
            if (auto const found = m_rows.find(resolved->Id); found != m_rows.end()) selected = found->second;
        }
        auto const changed = selected != m_selected;
        m_selected = selected;
        for (auto const& [id, row] : m_rows)
        {
            winrt::get_self<DownloadRowViewModel>(row)->SetSelected(resolved && id == resolved->Id);
        }
        for (std::size_t index = 0; index < m_shownTransfers.size(); ++index)
        {
            auto const isSelected = [this](std::size_t position)
            {
                return m_selected && m_shownTransfers[position].Id == m_selectedId;
            };
            if (auto const row = RowFor(m_shownTransfers[index].Id))
            {
                row->SetTopRule(index > 0 && !isSelected(index) && !isSelected(index - 1));
            }
        }
        if (changed)
        {
            for (auto const property : { L"SelectedRow", L"DetailVisibility", L"FolderVisibility" })
            {
                ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, property);
            }
        }
    }

    // The samples move once per bar; rebuilding identical bars would restart
    // their eased heights on every unrelated change.
    void DownloadsViewModel::RebuildChart()
    {
        auto const& values = m_downloads->Throughput();
        if (m_chartBuilt && values == m_chartValues) return;
        m_chartValues = values;
        m_chartBars = Presentation::ChartBars(m_chartValues);
        m_chartBuilt = true;
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, L"ChartBars");
    }

    void DownloadsViewModel::RaiseState()
    {
        for (auto const property : {
                 L"ActionErrorText", L"ActionErrorVisibility", L"SummaryLine", L"SummaryVisibility",
                 L"FilterAllCount", L"CanDeleteAll", L"FilterActiveCount", L"FilterReadyCount", L"FilterFailedCount", L"FilterIndex",
                 L"PauseAllLabel", L"PauseAllGlyph", L"PauseAllVisibility", L"IsPausedAll",
                 L"RateValue", L"RateUnit", L"RateUnitVisibility", L"RateNormalVisibility", L"RateIdleVisibility",
                 L"RatePausedVisibility", L"ConnectionsText", L"ConnectionsVisibility", L"RateNoteText",
                 L"RateNoteVisibility", L"PeakText", L"StorageValue", L"StorageUnitLine", L"StoredLine",
                 L"InFlightLine", L"StoredWidth", L"FlightWidth", L"FreeWidth", L"StoredVisibility",
                 L"FlightVisibility", L"QueueVisibility", L"ReadySectionVisibility", L"ReadyHeadline",
                 L"EmptyVisibility", L"NoMatchesVisibility", L"NoMatchesLine", L"SelectedReadyVisibility",
                 L"SelectedTransferVisibility", L"RunningVisibility", L"WaitingVisibility", L"AttentionVisibility",
                 L"WaitingLine", L"StatusLine" })
        {
            ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, property);
        }
    }
}
