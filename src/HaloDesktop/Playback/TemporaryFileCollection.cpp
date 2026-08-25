#include "Playback/TemporaryFileCollection.h"

#include <system_error>
#include <utility>
#include <vector>

namespace HaloDesktop::Playback
{
    TemporaryFileCollection::~TemporaryFileCollection()
    {
        Cleanup();
    }

    void TemporaryFileCollection::Add(std::filesystem::path path)
    {
        if(!path.empty())m_paths.insert(std::move(path));
    }

    void TemporaryFileCollection::Remove(std::filesystem::path const&path)noexcept
    {
        std::error_code error;
        auto const removed=std::filesystem::remove(path,error);
        if(removed||(!error&&!std::filesystem::exists(path,error)))m_paths.erase(path);
    }

    void TemporaryFileCollection::Cleanup()noexcept
    {
        auto const paths=std::vector<std::filesystem::path>{m_paths.begin(),m_paths.end()};
        for(auto const&path:paths)Remove(path);
    }

    std::size_t TemporaryFileCollection::Size()const noexcept
    {
        return m_paths.size();
    }
}
