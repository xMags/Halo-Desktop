#pragma once

#include "Services/WatchMarkPolicy.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <pplawait.h>
#include <ppltasks.h>
#include <vector>
#include <winrt/HaloDesktop.h>

namespace HaloDesktop::Services
{
    class ICatalogService;
    class IMetadataService;
    class ISessionService;
    class LibraryService;
    class WatchStateService;

    // What a title's right-click menu records about it: watched or not, in the
    // library or not. UI-thread-only, and holds nothing but its services, so a
    // menu can build one per opening. The caller keeps it alive until a task it
    // returned has finished.
    //
    // Every write that lands is published through the catalog, so the pages and
    // the pane that show this state re-read it. A write that fails throws and
    // changes nothing; one that outlives the account it began under is dropped
    // rather than written into the account signed in since.
    class TitleActions final
    {
    public:
        TitleActions(
            std::shared_ptr<WatchStateService> watchState,
            std::shared_ptr<LibraryService> library,
            std::shared_ptr<ICatalogService> catalog,
            std::shared_ptr<IMetadataService> metadata,
            std::shared_ptr<ISessionService> session);

        // False until the account's rows have arrived. A menu leaves the entries
        // that depend on them out rather than guessing.
        [[nodiscard]] bool WatchStateKnown() const noexcept;
        [[nodiscard]] bool LibraryKnown() const noexcept;

        [[nodiscard]] bool IsWatched(winrt::hstring const& videoId) const;
        // Started and not finished, which is what makes the action "Resume".
        [[nodiscard]] bool IsResumable(winrt::hstring const& videoId) const;
        [[nodiscard]] bool InLibrary(winrt::hstring const& type, winrt::hstring const& metaId) const;

        // The episodes before `episode` that are not marked watched yet, first to
        // last. `episodes` is every season of the show.
        [[nodiscard]] std::vector<winrt::HaloDesktop::Episode> UnwatchedEarlier(
            std::vector<winrt::HaloDesktop::Episode> const& episodes,
            winrt::HaloDesktop::Episode const& episode) const;

        // How many videos of the title the watch history holds, which is how
        // many "Remove from Continue watching" forgets.
        [[nodiscard]] std::size_t HistoryRowCount(winrt::hstring const& itemId) const;

        [[nodiscard]] concurrency::task<void> SetWatchedAsync(WatchMarkTarget target, bool watched);
        // Forgets every video of the title in the watch history, read when this
        // runs, so a row removed since the menu opened is not written again. The
        // pages and the pane re-read the history afterwards whether it worked or
        // not. Throws HistoryRemovalUnsupported for a server too old to remove.
        [[nodiscard]] concurrency::task<void> RemoveFromHistoryAsync(winrt::hstring itemId);
        // Forgets exactly these videos, read the same way and published the same
        // way: "Remove from history" passes one, "Clear history" every video the
        // page held when the viewer confirmed.
        [[nodiscard]] concurrency::task<void> RemoveVideosFromHistoryAsync(std::vector<winrt::hstring> videoIds);
        // Marks the targets watched in one write, stamped in the order given. What
        // is watched is read again when this runs, so an episode marked since the
        // menu opened is not written a second time.
        [[nodiscard]] concurrency::task<void> MarkWatchedInOrderAsync(std::vector<WatchMarkTarget> targets);
        // Adds or removes, whichever `wanted` asks for, against the library as it
        // is when this runs. False when the save did not land for this account.
        [[nodiscard]] concurrency::task<bool> SetInLibraryAsync(
            winrt::hstring type,
            winrt::hstring metaId,
            winrt::hstring name,
            winrt::hstring poster,
            bool wanted);

    private:
        // The addon's runtime for the title an item id names, or 0 when it gives
        // none or cannot be reached: a mark is still worth writing without one.
        [[nodiscard]] concurrency::task<double> RuntimeSecondsAsync(winrt::hstring itemId);
        // Waits for a removal already under way, then tells the pages, whether it
        // worked or not, if the account it began under (`generation`) is still
        // signed in; rethrows its failure after.
        [[nodiscard]] concurrency::task<void> PublishAfterRemovalAsync(
            concurrency::task<void> removal,
            std::uint64_t generation);

        std::shared_ptr<WatchStateService> m_watchState;
        std::shared_ptr<LibraryService> m_library;
        std::shared_ptr<ICatalogService> m_catalog;
        std::shared_ptr<IMetadataService> m_metadata;
        std::shared_ptr<ISessionService> m_session;
    };
}
