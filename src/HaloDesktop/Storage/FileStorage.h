#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

namespace HaloDesktop::Storage
{
    // Process-local object backed by a per-user named Windows mutex. Different
    // Halo processes that name the same target serialize their file mutation.
    class FileMutationLock final
    {
    public:
        explicit FileMutationLock(
            std::filesystem::path const& target,
            std::chrono::milliseconds timeout = std::chrono::seconds{ 30 });
        ~FileMutationLock();

        FileMutationLock(FileMutationLock const&) = delete;
        FileMutationLock& operator=(FileMutationLock const&) = delete;
        FileMutationLock(FileMutationLock&&) = delete;
        FileMutationLock& operator=(FileMutationLock&&) = delete;

    private:
        void* m_mutex{};
        bool m_owned{};
    };

    [[nodiscard]] std::string ReadUtf8File(
        std::filesystem::path const& path,
        std::uint64_t maximumBytes);

    void WriteUtf8FileAtomic(
        std::filesystem::path const& target,
        std::string_view bytes);
}
