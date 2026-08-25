#pragma once

#include <cstddef>
#include <filesystem>
#include <set>

namespace HaloDesktop::Playback
{
    class TemporaryFileCollection final
    {
    public:
        TemporaryFileCollection()=default;
        ~TemporaryFileCollection();

        TemporaryFileCollection(TemporaryFileCollection const&)=delete;
        TemporaryFileCollection&operator=(TemporaryFileCollection const&)=delete;

        void Add(std::filesystem::path path);
        void Remove(std::filesystem::path const&path)noexcept;
        void Cleanup()noexcept;
        [[nodiscard]] std::size_t Size()const noexcept;

    private:
        std::set<std::filesystem::path>m_paths;
    };
}
