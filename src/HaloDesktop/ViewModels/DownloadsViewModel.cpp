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

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace
{
    auto const Visible = winrt::Microsoft::UI::Xaml::Visibility::Visible;
    auto const Collapsed = winrt::Microsoft::UI::Xaml::Visibility::Collapsed;

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
    void DownloadRowViewModel::Update(winrt::HaloDesktop::DownloadItem item)
    {
        m_item = std::move(item);
        RaiseState();
    }
    winrt::HaloDesktop::DownloadItem DownloadRowViewModel::Item() const { return m_item; }
    winrt::hstring DownloadRowViewModel::Id() const { return m_item.Id(); }
    winrt::hstring DownloadRowViewModel::Tag() const { return m_item.Tag(); }
    winrt::hstring DownloadRowViewModel::Name() const { return m_item.Name(); }
    winrt::hstring DownloadRowViewModel::Sub() const { return m_item.Sub(); }
    winrt::hstring DownloadRowViewModel::StateLabel() const { return ::StateLabel(m_item.State()); }
    double DownloadRowViewModel::Progress() const noexcept { return m_item.Progress(); }
    winrt::hstring DownloadRowViewModel::Detail() const { return m_item.Detail(); }
    winrt::hstring DownloadRowViewModel::QualityLine() const { return winrt::hstring(std::wstring(m_item.Quality()) + L" · " + std::wstring(m_item.Codec())); }
    winrt::hstring DownloadRowViewModel::Size() const { return m_item.Size(); }
    winrt::hstring DownloadRowViewModel::Subs() const { return m_item.Subs(); }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::DownloadingVisibility() const noexcept { return m_item.State() == winrt::HaloDesktop::DownloadState::Downloading ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::QueuedVisibility() const noexcept { return m_item.State() == winrt::HaloDesktop::DownloadState::Queued ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::PausedVisibility() const noexcept { return m_item.State() == winrt::HaloDesktop::DownloadState::Paused ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility DownloadRowViewModel::FailedVisibility() const noexcept { return m_item.State() == winrt::HaloDesktop::DownloadState::Failed ? Visible : Collapsed; }
    winrt::event_token DownloadRowViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler) { return m_propertyChanged.add(handler); }
    void DownloadRowViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }
    void DownloadRowViewModel::RaiseState()
    {
        for (auto const property : { L"Tag", L"Name", L"Sub", L"StateLabel", L"Progress", L"Detail", L"QualityLine", L"Size", L"Subs", L"DownloadingVisibility", L"QueuedVisibility", L"PausedVisibility", L"FailedVisibility" })
            ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, property);
    }

    ChartBarViewModel::ChartBarViewModel(double value, bool recent)
        : m_height((std::max)(4.0, (std::min)(72.0, value))), m_recent(recent)
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
    winrt::hstring DownloadsViewModel::InfoTitle() const { return L"Downloads are stored on this device"; }
    winrt::hstring DownloadsViewModel::InfoMessage() const { return L"Keep Halo open while a transfer is running. Partial transfers resume after Halo is reopened."; }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> DownloadsViewModel::TransfersView() const { return m_transfers; }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> DownloadsViewModel::ReadyView() const { return m_ready; }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> DownloadsViewModel::ChartBarsView() const { return m_chartBars; }
    winrt::Windows::Foundation::IInspectable DownloadsViewModel::SelectedRow() const { return m_selected; }
    winrt::hstring DownloadsViewModel::RateText() const
    {
        std::wostringstream value;
        value << std::fixed << std::setprecision(1) << m_downloads->AggregateRate() << L" MB/s";
        return winrt::hstring(value.str());
    }
    winrt::hstring DownloadsViewModel::QueueLine() const { return m_downloads->QueueLine(); }
    winrt::hstring DownloadsViewModel::TransferCountLabel() const
    {
        std::wostringstream value;
        value << m_transfers.Size() << L" ITEMS";
        return winrt::hstring(value.str());
    }
    winrt::hstring DownloadsViewModel::ReadyCountLabel() const
    {
        std::wostringstream value;
        value << m_ready.Size() << L" ITEMS";
        return winrt::hstring(value.str());
    }
    winrt::hstring DownloadsViewModel::PauseAllLabel() const { return m_downloads->IsPausedAll() ? L"Resume all" : L"Pause all"; }
    bool DownloadsViewModel::IsPausedAll() const noexcept { return m_downloads->IsPausedAll(); }
    winrt::hstring DownloadsViewModel::SelectedTag() const { return m_selected ? m_selected.Tag() : L""; }
    winrt::hstring DownloadsViewModel::SelectedTitle() const { return m_selected ? m_selected.Name() : L""; }
    winrt::hstring DownloadsViewModel::SelectedSub() const { return m_selected ? m_selected.Sub() : L""; }
    double DownloadsViewModel::SelectedProgress() const noexcept { return m_selected ? m_selected.Progress() : 0.0; }
    winrt::hstring DownloadsViewModel::SelectedDetail() const { return m_selected ? m_selected.Detail() : L""; }
    winrt::hstring DownloadsViewModel::SelectedQualityLine() const { return m_selected ? m_selected.QualityLine() : L""; }
    winrt::hstring DownloadsViewModel::SelectedSize() const { return m_selected ? m_selected.Size() : L""; }
    winrt::hstring DownloadsViewModel::SelectedSubs() const { return m_selected ? m_selected.Subs() : L""; }
    winrt::hstring DownloadsViewModel::ReadyActionLabel() const { return L"Play offline"; }
    winrt::hstring DownloadsViewModel::StorageLine() const
    {
        return FormatBytes(m_downloads->StoredBytes()) + L" stored on this device";
    }
    winrt::hstring DownloadsViewModel::FreeLine() const
    {
        auto const free = m_downloads->FreeBytes();
        return free ? FormatBytes(*free) + L" free" : winrt::hstring{ L"Free space unavailable" };
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
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::DetailVisibility() const noexcept { return m_selected ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::SelectedTransferVisibility() const noexcept { return m_selected && !SelectedIsReady() ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::SelectedReadyVisibility() const noexcept { return m_selected && SelectedIsReady() ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::PauseVisibility() const noexcept { return SelectedItem() && SelectedItem().State() == winrt::HaloDesktop::DownloadState::Downloading ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::ResumeVisibility() const noexcept { auto const item=SelectedItem();return item&&(item.State()==winrt::HaloDesktop::DownloadState::Paused||(item.State()==winrt::HaloDesktop::DownloadState::Failed&&!item.RequiresNewSource()))?Visible:Collapsed; }
    Microsoft::UI::Xaml::Visibility DownloadsViewModel::ChooseSourceVisibility() const noexcept { return SelectedItem() && SelectedItem().State() == winrt::HaloDesktop::DownloadState::Failed && SelectedItem().RequiresNewSource() ? Visible : Collapsed; }
    void DownloadsViewModel::Select(winrt::hstring const& id) { m_selectedId = id; ResolveSelection(); RaiseState(); }
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
            m_navigation->GoTo(::HaloDesktop::Services::Page::Sources, parameters);
        }
    }
    void DownloadsViewModel::SetDownloadDirectory(std::filesystem::path directory) { m_downloads->SetDownloadDirectory(std::move(directory)); }
    winrt::event_token DownloadsViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler) { return m_propertyChanged.add(handler); }
    void DownloadsViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }

    void DownloadsViewModel::Synchronize()
    {
        SynchronizeRows(m_downloads->Transfers(), m_transfers);
        SynchronizeRows(m_downloads->Ready(), m_ready);
        RebuildChart();
        ResolveSelection();
        RaiseState();
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
    void DownloadsViewModel::RebuildChart()
    {
        m_chartBars.Clear();
        auto const values = m_downloads->Throughput();
        auto peak = 0.0;
        for (auto const value : values) peak = (std::max)(peak, value);
        for (std::uint32_t index = 0; index < values.Size(); ++index)
        {
            auto const height = peak > 0.0 ? values.GetAt(index) / peak * 72.0 : 0.0;
            m_chartBars.Append(winrt::make<ChartBarViewModel>(height, index + 6 >= values.Size()));
        }
    }
    void DownloadsViewModel::ResolveSelection()
    {
        m_selected = nullptr;
        for (auto const& row : m_ready)
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
            for (auto const& row : m_transfers)
            {
                auto const candidate = row.as<winrt::HaloDesktop::DownloadRowViewModel>();
                if (candidate.Id() == m_selectedId)
                {
                    m_selected = candidate;
                    break;
                }
            }
        }
        if (!m_selected && m_ready.Size() > 0)
        {
            m_selected = m_ready.GetAt(0).as<winrt::HaloDesktop::DownloadRowViewModel>();
        }
        if (!m_selected && m_transfers.Size() > 0)
        {
            m_selected = m_transfers.GetAt(0).as<winrt::HaloDesktop::DownloadRowViewModel>();
        }
        if (m_selected)
        {
            m_selectedId = m_selected.Id();
        }
    }
    void DownloadsViewModel::RaiseState()
    {
        for (auto const property : { L"RateText", L"QueueLine", L"TransferCountLabel", L"ReadyCountLabel", L"PauseAllLabel", L"IsPausedAll", L"SelectedTag", L"SelectedTitle", L"SelectedSub", L"SelectedProgress", L"SelectedDetail", L"SelectedQualityLine", L"SelectedSize", L"SelectedSubs", L"ReadyActionLabel", L"StorageLine", L"FreeLine", L"StoredLine", L"InFlightLine", L"PeakText", L"StorageFraction", L"DetailVisibility", L"SelectedTransferVisibility", L"SelectedReadyVisibility", L"PauseVisibility", L"ResumeVisibility", L"ChooseSourceVisibility" })
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
