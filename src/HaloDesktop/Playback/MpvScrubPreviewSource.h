#pragma once

#include "Playback/IScrubPreviewSource.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <winrt/Microsoft.UI.Dispatching.h>

struct mpv_handle;

namespace HaloDesktop::Playback
{
    // A second, headless libmpv instance whose only job is to decode single frames for
    // the seek bar. It is deliberately separate from the playing engine: asking the
    // engine to seek for a preview is what made scrubbing disturb playback, and on a
    // remote source every such seek cost a fresh range request and a cache flush.
    //
    // The cost of this design is a second connection to the origin, so the instance is
    // opened lazily on the first preview request and gives up permanently once the
    // source refuses to open. Hovering a seek bar must never hammer a host.
    //
    // Threading: one worker thread owns the mpv handle end to end. The UI thread only
    // publishes into a single-slot mailbox, so there is no shared libmpv access at all.
    class MpvScrubPreviewSource final : public IScrubPreviewSource
    {
    public:
        MpvScrubPreviewSource() = default;
        ~MpvScrubPreviewSource() override;

        MpvScrubPreviewSource(MpvScrubPreviewSource const&) = delete;
        MpvScrubPreviewSource& operator=(MpvScrubPreviewSource const&) = delete;
        MpvScrubPreviewSource(MpvScrubPreviewSource&&) = delete;
        MpvScrubPreviewSource& operator=(MpvScrubPreviewSource&&) = delete;

        void Open(PlaybackSource source) override;
        void Close() noexcept override;
        std::uint64_t Request(double seconds) override;
        void SetFrameHandler(ScrubPreviewFrameHandler handler) override;
        void ClearFrameHandler() noexcept override;

    private:
        struct PendingRequest final
        {
            double Seconds{};
            std::uint64_t Id{};
        };

        void WorkerLoop() noexcept;
        void Disable() noexcept;
        [[nodiscard]] std::optional<PendingRequest> WaitForRequest();
        [[nodiscard]] bool Stopping() const noexcept;
        [[nodiscard]] bool Superseded(std::uint64_t requestId) const noexcept;
        [[nodiscard]] bool AwaitFirstFrame(mpv_handle* handle) noexcept;
        [[nodiscard]] bool AwaitSeekCompletion(mpv_handle* handle, std::uint64_t requestId) noexcept;
        void DecodeAndDeliver(mpv_handle* handle, PendingRequest const& request) noexcept;
        void Deliver(ScrubPreviewFrame frame) noexcept;

        mutable std::mutex m_mutex;
        std::condition_variable m_wake;
        PlaybackSource m_source;
        std::optional<PendingRequest> m_pending;
        ScrubPreviewFrameHandler m_frameHandler;
        winrt::Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        std::shared_ptr<std::atomic_bool> m_dispatchAlive;
        std::uint64_t m_nextRequestId{};
        std::uint64_t m_currentRequestId{};
        double m_lastIssuedSeconds{};
        bool m_hasIssued{};
        bool m_stopping{};
        // Set by the worker once the source proves unopenable. Every later request is
        // answered from the UI thread without waking the worker again.
        bool m_disabled{};
        bool m_workerStarted{};
        std::jthread m_worker;
    };
}
