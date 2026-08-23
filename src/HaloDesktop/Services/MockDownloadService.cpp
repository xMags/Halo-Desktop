#include "pch.h"
#include "Services/MockDownloadService.h"

#include "Models/Models.h"
#include "Services/SampleData.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

namespace
{
    double TotalGigabytes(winrt::HaloDesktop::DownloadItem const& item)
    {
        if (item.Id() == L"t3")
        {
            return 4.4;
        }
        return 3.4;
    }
}

namespace HaloDesktop::Services
{
    MockDownloadService::MockDownloadService()
        : m_transfers(winrt::single_threaded_observable_vector<winrt::HaloDesktop::DownloadItem>(SampleData::TransferItems())),
          m_ready(winrt::single_threaded_observable_vector<winrt::HaloDesktop::DownloadItem>(SampleData::ReadyItems())),
          m_throughput(winrt::single_threaded_observable_vector<double>(SampleData::ThroughputSamples()))
    {
    }

    MockDownloadService::~MockDownloadService()
    {
        Stop();
    }

    void MockDownloadService::Start()
    {
        if (m_running)
        {
            return;
        }

        auto const dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        if (!dispatcher)
        {
            throw winrt::hresult_wrong_thread();
        }

        m_timer = dispatcher.CreateTimer();
        m_timer.Interval(std::chrono::seconds(1));
        m_timer.IsRepeating(true);
        m_tickRevoker = m_timer.Tick(
            winrt::auto_revoke,
            [weak = weak_from_this()](
                [[maybe_unused]] winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer const& timer,
                [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& args)
            {
                if (auto const self = weak.lock())
                {
                    self->Tick();
                }
            });
        m_timer.Start();
        m_running = true;
    }

    void MockDownloadService::Stop() noexcept
    {
        if (m_timer)
        {
            m_timer.Stop();
        }
        m_tickRevoker.revoke();
        m_timer = nullptr;
        m_running = false;
    }

    void MockDownloadService::PauseAll()
    {
        if (m_pausedAll)
        {
            return;
        }

        for (auto const& item : m_transfers)
        {
            if (item.State() == winrt::HaloDesktop::DownloadState::Downloading)
            {
                auto const implementation = winrt::get_self<winrt::HaloDesktop::implementation::DownloadItem>(item);
                implementation->UpdateState(winrt::HaloDesktop::DownloadState::Paused, L"PAUSED BY YOU");
            }
        }
        m_pausedAll = true;
        m_aggregateRate = 0.0;
        NotifyChanged();
    }

    void MockDownloadService::ResumeAll()
    {
        if (!m_pausedAll)
        {
            return;
        }

        for (auto const& item : m_transfers)
        {
            if (item.Id() == L"t1" && item.State() == winrt::HaloDesktop::DownloadState::Paused)
            {
                auto const implementation = winrt::get_self<winrt::HaloDesktop::implementation::DownloadItem>(item);
                implementation->UpdateState(
                    winrt::HaloDesktop::DownloadState::Downloading,
                    FormatTransferDetail(item, item.Progress()));
            }
        }
        m_pausedAll = false;
        m_aggregateRate = 28.4;
        NotifyChanged();
    }

    bool MockDownloadService::IsRunning() const noexcept { return m_running; }

    std::int32_t MockDownloadService::ActiveCount() const noexcept
    {
        std::int32_t count{};
        for (auto const& item : m_transfers)
        {
            if (item.State() == winrt::HaloDesktop::DownloadState::Downloading
                || item.State() == winrt::HaloDesktop::DownloadState::Queued)
            {
                ++count;
            }
        }
        return count;
    }

    double MockDownloadService::AggregateRate() const noexcept { return m_aggregateRate; }

    winrt::hstring MockDownloadService::QueueLine() const
    {
        std::int32_t transferring{};
        std::int32_t queued{};
        for (auto const& item : m_transfers)
        {
            if (item.State() == winrt::HaloDesktop::DownloadState::Downloading)
            {
                ++transferring;
            }
            else if (item.State() != winrt::HaloDesktop::DownloadState::OnDisk)
            {
                ++queued;
            }
        }

        std::wostringstream line;
        line << transferring << L" of " << m_transfers.Size() << L" transferring · " << queued << L" queued";
        return winrt::hstring(line.str());
    }

    winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::DownloadItem> MockDownloadService::Transfers() const { return m_transfers; }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::DownloadItem> MockDownloadService::Ready() const { return m_ready; }
    winrt::Windows::Foundation::Collections::IObservableVector<double> MockDownloadService::Throughput() const { return m_throughput; }

    DownloadChangedToken MockDownloadService::AddChangedHandler(DownloadChangedHandler handler)
    {
        if (!handler)
        {
            return 0;
        }
        auto const token = ++m_nextToken;
        m_handlers.emplace(token, std::move(handler));
        return token;
    }

    void MockDownloadService::RemoveChangedHandler(DownloadChangedToken token) noexcept
    {
        m_handlers.erase(token);
    }

    void MockDownloadService::Tick()
    {
        if (m_pausedAll)
        {
            if (m_throughput.Size() >= 30)
            {
                m_throughput.RemoveAt(0);
            }
            m_throughput.Append(0.0);
            NotifyChanged();
            return;
        }

        std::uniform_real_distribution<double> delta(-2.4, 2.4);
        m_aggregateRate = std::clamp(m_aggregateRate + delta(m_random), 18.0, 41.2);

        for (std::uint32_t index = 0; index < m_transfers.Size(); ++index)
        {
            auto const item = m_transfers.GetAt(index);
            if (item.State() != winrt::HaloDesktop::DownloadState::Downloading)
            {
                continue;
            }

            auto const implementation = winrt::get_self<winrt::HaloDesktop::implementation::DownloadItem>(item);
            auto const nextProgress = (std::min)(1.0, item.Progress() + 0.003);
            implementation->UpdateProgress(nextProgress, FormatTransferDetail(item, nextProgress));
            if (nextProgress >= 1.0)
            {
                implementation->UpdateState(winrt::HaloDesktop::DownloadState::OnDisk, L"READY TO WATCH · 1080p");
                m_transfers.RemoveAt(index);
                m_ready.Append(item);

                for (auto const& queued : m_transfers)
                {
                    if (queued.State() == winrt::HaloDesktop::DownloadState::Queued)
                    {
                        auto const queuedImplementation = winrt::get_self<winrt::HaloDesktop::implementation::DownloadItem>(queued);
                        queuedImplementation->UpdateState(winrt::HaloDesktop::DownloadState::Downloading, L"STARTING · 3.4 GB");
                        break;
                    }
                }
            }
            break;
        }

        if (m_throughput.Size() >= 30)
        {
            m_throughput.RemoveAt(0);
        }
        m_throughput.Append(m_aggregateRate);
        NotifyChanged();
    }

    void MockDownloadService::NotifyChanged()
    {
        std::vector<DownloadChangedHandler> handlers;
        handlers.reserve(m_handlers.size());
        for (auto const& [token, handler] : m_handlers)
        {
            static_cast<void>(token);
            handlers.push_back(handler);
        }
        for (auto const& handler : handlers)
        {
            handler();
        }
    }

    winrt::hstring MockDownloadService::FormatTransferDetail(
        winrt::HaloDesktop::DownloadItem const& item,
        double progress) const
    {
        auto const total = TotalGigabytes(item);
        auto const transferred = progress * total;
        auto const remainingMegabytes = (total - transferred) * 1024.0;
        auto const seconds = m_aggregateRate > 0.0
            ? static_cast<std::int32_t>(std::ceil(remainingMegabytes / m_aggregateRate))
            : 0;

        std::wostringstream detail;
        detail << std::fixed << std::setprecision(1)
               << transferred << L" GB OF " << total << L" GB · "
               << m_aggregateRate << L" MB/S · " << seconds << L" S LEFT";
        return winrt::hstring(detail.str());
    }
}
