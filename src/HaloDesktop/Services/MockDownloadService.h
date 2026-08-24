#pragma once

#include "Services/ServiceInterfaces.h"

#include <memory>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <winrt/Microsoft.UI.Dispatching.h>

namespace HaloDesktop::Services
{
    // UI-thread-only. The DispatcherQueue timer, collections, and callbacks all
    // run on the creating UI thread. Stop must be called before XAML teardown.
    class MockDownloadService final
        : public IDownloadService,
          public std::enable_shared_from_this<MockDownloadService>
    {
    public:
        MockDownloadService();
        ~MockDownloadService() override;

        MockDownloadService(MockDownloadService const&) = delete;
        MockDownloadService& operator=(MockDownloadService const&) = delete;
        MockDownloadService(MockDownloadService&&) = delete;
        MockDownloadService& operator=(MockDownloadService&&) = delete;

        void Start() override;
        void Stop() noexcept override;
        void PauseAll() override;
        void ResumeAll() override;
        bool PauseTransfer(winrt::hstring const& id) override;
        bool ResumeTransfer(winrt::hstring const& id) override;
        bool StartNow(winrt::hstring const& id) override;
        bool CancelTransfer(winrt::hstring const& id) override;
        bool DeleteReady(winrt::hstring const& id) override;
        [[nodiscard]] bool IsRunning() const noexcept override;
        [[nodiscard]] bool IsPausedAll() const noexcept override;
        [[nodiscard]] bool HasCompleted(winrt::hstring const& videoId) const noexcept override;
        [[nodiscard]] std::int32_t ActiveCount() const noexcept override;
        [[nodiscard]] double AggregateRate() const noexcept override;
        [[nodiscard]] winrt::hstring QueueLine() const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::DownloadItem> Transfers() const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::DownloadItem> Ready() const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<double> Throughput() const override;
        DownloadChangedToken AddChangedHandler(DownloadChangedHandler handler) override;
        void RemoveChangedHandler(DownloadChangedToken token) noexcept override;

    private:
        void Tick();
        void NotifyChanged();
        void StartNextQueued();
        [[nodiscard]] bool HasDownloading() const noexcept;
        [[nodiscard]] std::optional<std::uint32_t> FindTransfer(winrt::hstring const& id) const noexcept;
        [[nodiscard]] std::optional<std::uint32_t> FindReady(winrt::hstring const& id) const noexcept;
        [[nodiscard]] winrt::hstring FormatTransferDetail(
            winrt::HaloDesktop::DownloadItem const& item,
            double progress) const;

        winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::DownloadItem> m_transfers{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::DownloadItem> m_ready{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<double> m_throughput{ nullptr };
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_timer{ nullptr };
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer::Tick_revoker m_tickRevoker{};
        std::unordered_map<DownloadChangedToken, DownloadChangedHandler> m_handlers;
        std::unordered_map<std::wstring, winrt::HaloDesktop::DownloadState> m_pauseAllStates;
        DownloadChangedToken m_nextToken{};
        std::mt19937 m_random{ 0x48414C4F };
        double m_aggregateRate{ 28.4 };
        bool m_running{};
        bool m_pausedAll{};
    };
}
