#pragma once

#include "Services/Tmdb/TmdbCredential.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <ppltasks.h>
#include <string>
#include <unordered_map>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Windows.Web.Http.h>

namespace HaloDesktop::Services::Tmdb
{
    using TmdbKeyChangedToken = std::uint64_t;

    // How saving a pasted key went.
    enum class TmdbKeySave
    {
        Saved,
        // It has neither shape of a TMDB credential.
        NotAKey,
        // TMDB refused it (401).
        Refused,
        // TMDB could not be asked (offline, a timeout, or another answer).
        Unreachable,
        // TMDB took it but this PC could not store it.
        NotStored,
    };

    // The viewer's own TMDB credential, for people search and IMDb/TVDB
    // lookups without an account. Kept in one file under this PC's app data,
    // encrypted for the Windows user (DPAPI), and only ever sent to TMDB. A
    // key is stored only after TMDB has accepted it; one TMDB refuses later
    // (MarkRefused) stays stored but unused until it is replaced or removed.
    //
    // Current and the predicates are safe on any thread. Load, Save and Remove
    // are called on the UI thread, one at a time; change handlers run there.
    class TmdbKeyStore final : public std::enable_shared_from_this<TmdbKeyStore>
    {
    public:
        using ChangedHandler = std::function<void()>;

        TmdbKeyStore(std::filesystem::path file, winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher);

        TmdbKeyStore(TmdbKeyStore const&) = delete;
        TmdbKeyStore& operator=(TmdbKeyStore const&) = delete;

        // Reads the stored key; an unreadable or malformed file reads as none.
        [[nodiscard]] concurrency::task<void> LoadAsync();
        // Checks the pasted key with TMDB, and stores it only when TMDB accepts it.
        [[nodiscard]] concurrency::task<TmdbKeySave> SaveAsync(std::wstring pasted);
        [[nodiscard]] concurrency::task<void> RemoveAsync();

        // The key to ask TMDB with: empty when there is none or TMDB has refused it.
        [[nodiscard]] std::optional<TmdbCredential> Current() const;
        [[nodiscard]] bool HasKey() const;
        // A stored key TMDB stopped accepting.
        [[nodiscard]] bool IsRefused() const;
        // TMDB answered 401 to a request made with `credential`. Any thread;
        // ignored when the key has been replaced since.
        void MarkRefused(TmdbCredential const& credential) noexcept;

        [[nodiscard]] TmdbKeyChangedToken AddChangedHandler(ChangedHandler handler);
        void RemoveChangedHandler(TmdbKeyChangedToken token) noexcept;

    private:
        void NotifyChanged();

        std::filesystem::path m_file;
        winrt::Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        winrt::Windows::Web::Http::HttpClient m_client;
        mutable std::mutex m_mutex;
        std::optional<TmdbCredential> m_credential;
        bool m_refused{};
        // UI thread only.
        std::unordered_map<TmdbKeyChangedToken, ChangedHandler> m_handlers;
        TmdbKeyChangedToken m_nextToken{ 1 };
    };
}
