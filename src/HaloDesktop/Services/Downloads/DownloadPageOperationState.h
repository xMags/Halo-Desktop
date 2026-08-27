#pragma once

#include <cstdint>
#include <optional>

namespace HaloDesktop::Services::Downloads
{
    class DownloadPageOperationState final
    {
    public:
        struct Ticket final
        {
            std::uint64_t PageGeneration{};
            std::uint64_t OperationGeneration{};
        };

        void NavigatedTo() noexcept;
        void Loaded() noexcept;
        void Unloaded() noexcept;
        [[nodiscard]] std::optional<Ticket> TryBegin() noexcept;
        [[nodiscard]] bool CanApply(Ticket const& ticket) const noexcept;
        void Complete(Ticket const& ticket) noexcept;
        [[nodiscard]] bool InFlight() const noexcept;

    private:
        std::uint64_t m_pageGeneration{};
        std::uint64_t m_operationGeneration{};
        bool m_loaded{};
        bool m_inFlight{};
    };
}
