#pragma once

#include "Services/Downloads/DownloadTypes.h"

#include <filesystem>
#include <string>

namespace HaloDesktop::Services::Downloads
{
    // Thread-safe current-user DPAPI vault. This is the only component allowed
    // to persist source URLs and proxy headers. Callers must not log exception
    // payloads or expose ProtectedRequest to XAML-bound state.
    class RequestVault final
    {
    public:
        explicit RequestVault(std::filesystem::path directory);

        void Write(std::wstring const& jobId, ProtectedRequest const& request) const;
        [[nodiscard]] ProtectedRequest Read(std::wstring const& jobId) const;
        void Remove(std::wstring const& jobId) const;

    private:
        [[nodiscard]] std::filesystem::path PathFor(std::wstring const& jobId) const;

        std::filesystem::path m_directory;
    };
}
