#include "pch.h"
#include "Views/TitleMenu.h"

#include "Models/Models.h"
#include "Services/NavigationService.h"
#include "Services/ServiceInterfaces.h"
#include "Services/TitleActions.h"
#include "Views/PageDialog.h"

#include <cstdint>
#include <stdexcept>
#include <utility>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

namespace
{
    using ::HaloDesktop::Services::IDownloadService;
    using ::HaloDesktop::Services::NavigationService;
    using ::HaloDesktop::Services::TitleActions;
    using ::HaloDesktop::Services::WatchMarkTarget;

    // Segoe Fluent Icons.
    constexpr wchar_t PlayGlyph[] = L"\xE768";
    constexpr wchar_t OfflineGlyph[] = L"\xE896";
    constexpr wchar_t DetailsGlyph[] = L"\xE946";
    constexpr wchar_t WatchedGlyph[] = L"\xE73E";
    constexpr wchar_t UnwatchedGlyph[] = L"\xE7A7";
    constexpr wchar_t EarlierGlyph[] = L"\xE762";
    constexpr wchar_t AddToLibraryGlyph[] = L"\xE734";
    constexpr wchar_t RemoveFromLibraryGlyph[] = L"\xE8D9";
    constexpr wchar_t ExplorerGlyph[] = L"\xE838";
    constexpr wchar_t DeleteGlyph[] = L"\xE74D";

    constexpr wchar_t DownloadFailedTitle[] = L"Download action failed";
    constexpr wchar_t DownloadMissingBody[] = L"This download is no longer on this device.";
    constexpr wchar_t RevealFailedBody[] = L"The downloaded file could not be shown.";
    constexpr wchar_t WatchFailedTitle[] = L"Watch history could not be updated";
    constexpr wchar_t LibraryFailedTitle[] = L"Library could not be updated";
    constexpr wchar_t ServerFailedBody[] = L"Check the connection to your Halo server and try again.";

    // Where a menu's follow-up dialog opens. Taken when the menu opens, because by
    // the time an action finishes the element it was opened from may be gone.
    struct DialogHost final
    {
        winrt::Microsoft::UI::Xaml::XamlRoot Root{ nullptr };
        winrt::Microsoft::UI::Xaml::ElementTheme Theme{ winrt::Microsoft::UI::Xaml::ElementTheme::Default };
    };

    winrt::Windows::Foundation::IAsyncAction ShowFailureAsync(
        DialogHost host,
        winrt::hstring title,
        winrt::hstring body)
    {
        if (!host.Root)
        {
            co_return;
        }
        try
        {
            auto dialog = ::HaloDesktop::Views::MakeDialog(host.Root, host.Theme, title, body);
            dialog.CloseButtonText(L"Close");
            co_await dialog.ShowAsync();
        }
        catch (...)
        {
            // Only one dialog can be open at a time, and whatever is already up
            // matters more than this report.
        }
    }

    winrt::fire_and_forget ReportFailure(DialogHost host, winrt::hstring title, winrt::hstring body)
    {
        co_await ShowFailureAsync(std::move(host), std::move(title), std::move(body));
    }

    // The coroutines below take their services by value, so a save in flight keeps
    // them alive after the menu and the page that opened it are gone.
    winrt::fire_and_forget SetWatched(
        std::shared_ptr<TitleActions> actions,
        WatchMarkTarget target,
        bool watched,
        DialogHost host)
    {
        auto const uiContext = winrt::apartment_context{};
        auto failed = false;
        try
        {
            co_await actions->SetWatchedAsync(std::move(target), watched);
        }
        catch (...)
        {
            failed = true;
        }
        if (failed)
        {
            co_await uiContext;
            co_await ShowFailureAsync(std::move(host), WatchFailedTitle, ServerFailedBody);
        }
    }

    winrt::fire_and_forget MarkWatchedInOrder(
        std::shared_ptr<TitleActions> actions,
        std::vector<WatchMarkTarget> targets,
        DialogHost host)
    {
        auto const uiContext = winrt::apartment_context{};
        auto failed = false;
        try
        {
            co_await actions->MarkWatchedInOrderAsync(std::move(targets));
        }
        catch (...)
        {
            failed = true;
        }
        if (failed)
        {
            co_await uiContext;
            co_await ShowFailureAsync(std::move(host), WatchFailedTitle, ServerFailedBody);
        }
    }

    winrt::fire_and_forget SetInLibrary(
        std::shared_ptr<TitleActions> actions,
        winrt::hstring type,
        winrt::hstring metaId,
        winrt::hstring name,
        winrt::hstring poster,
        bool wanted,
        DialogHost host)
    {
        auto const uiContext = winrt::apartment_context{};
        auto failed = false;
        try
        {
            co_await actions->SetInLibraryAsync(
                std::move(type),
                std::move(metaId),
                std::move(name),
                std::move(poster),
                wanted);
        }
        catch (...)
        {
            failed = true;
        }
        if (failed)
        {
            co_await uiContext;
            co_await ShowFailureAsync(std::move(host), LibraryFailedTitle, ServerFailedBody);
        }
    }

    winrt::fire_and_forget DeleteFromDevice(
        std::shared_ptr<IDownloadService> downloads,
        winrt::hstring jobId,
        DialogHost host)
    {
        if (!host.Root)
        {
            co_return;
        }
        auto confirmed = false;
        try
        {
            auto dialog = ::HaloDesktop::Views::MakeDeleteFromDeviceDialog(host.Root, host.Theme);
            confirmed = co_await dialog.ShowAsync() == winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary;
        }
        catch (...)
        {
            co_return;
        }
        if (!confirmed)
        {
            co_return;
        }
        // Re-checked by the service: the file may have gone while the question was up.
        if (!downloads->DeleteReady(jobId))
        {
            co_await ShowFailureAsync(std::move(host), DownloadFailedTitle, DownloadMissingBody);
        }
    }

    // The finished download of a video, if this device has one. Downloads are one
    // per video, so the first is the one.
    std::optional<winrt::hstring> ReadyJob(IDownloadService const& downloads, winrt::hstring const& videoId)
    {
        auto const completed = downloads.CompletedFor(videoId);
        if (completed.empty())
        {
            return std::nullopt;
        }
        return winrt::hstring{ completed.front().JobId };
    }

    // One menu being put together: the services its entries act through, where
    // their dialogs open, and what runs ahead of any navigation they make.
    class MenuComposer final
    {
    public:
        MenuComposer(
            std::shared_ptr<NavigationService> navigation,
            std::shared_ptr<IDownloadService> downloads,
            std::shared_ptr<TitleActions> actions,
            winrt::Microsoft::UI::Xaml::FrameworkElement const& anchor,
            std::function<void()> beforeOpen)
            : m_navigation(std::move(navigation)),
              m_downloads(std::move(downloads)),
              m_actions(std::move(actions)),
              m_host{ anchor.XamlRoot(), anchor.ActualTheme() },
              m_beforeOpen(std::move(beforeOpen))
        {
        }

        void Item(winrt::hstring const& label, wchar_t const* glyph, std::function<void()> action)
        {
            if (m_separatorPending && m_flyout.Items().Size() > 0)
            {
                m_flyout.Items().Append(winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutSeparator{});
            }
            m_separatorPending = false;
            winrt::Microsoft::UI::Xaml::Controls::FontIcon icon;
            icon.Glyph(glyph);
            winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem item;
            item.Text(label);
            item.Icon(icon);
            item.Click([action = std::move(action)](
                winrt::Windows::Foundation::IInspectable const&,
                winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
            {
                if (action)
                {
                    action();
                }
            });
            m_flyout.Items().Append(item);
        }

        // Separators only ever land between entries, so a group that turns out
        // empty leaves no stray line behind.
        void Separator() noexcept { m_separatorPending = true; }

        void AddDetails(
            winrt::hstring const& type,
            winrt::hstring const& metaId,
            winrt::hstring const& title,
            winrt::hstring const& poster)
        {
            if (type.empty() || metaId.empty())
            {
                return;
            }
            Item(L"Open details", DetailsGlyph, [navigation = m_navigation, beforeOpen = m_beforeOpen, type, metaId, title, poster]()
            {
                if (beforeOpen) beforeOpen();
                navigation->GoTo(
                    ::HaloDesktop::Services::Page::Detail,
                    winrt::make<winrt::HaloDesktop::implementation::DetailNavParams>(type, metaId, title, poster));
            });
        }

        // "Play offline" sits beside Play: it plays the saved file without asking
        // for a source. The download is looked up again when chosen, because it
        // may have been deleted while the menu was open.
        void AddPlayOffline(winrt::hstring const& videoId)
        {
            if (!ReadyJob(*m_downloads, videoId))
            {
                return;
            }
            Item(L"Play offline", OfflineGlyph, [navigation = m_navigation, downloads = m_downloads, beforeOpen = m_beforeOpen, host = m_host, videoId]()
            {
                auto const job = ReadyJob(*downloads, videoId);
                auto const request = job ? downloads->BuildPlaybackRequest(*job) : nullptr;
                if (!request)
                {
                    ReportFailure(host, DownloadFailedTitle, DownloadMissingBody);
                    return;
                }
                if (beforeOpen) beforeOpen();
                navigation->ShowOverlay(::HaloDesktop::Services::Page::Player, request);
            });
        }

        // `subject` names the video when the surface alone does not, as on a card
        // that stands for a whole show.
        void AddWatched(WatchMarkTarget target, winrt::hstring const& subject)
        {
            if (!m_actions->WatchStateKnown() || target.VideoId.empty() || target.ItemId.empty())
            {
                return;
            }
            auto const what = subject.empty() ? winrt::hstring{} : subject + L" ";
            auto const watched = m_actions->IsWatched(target.VideoId);
            Item(
                L"Mark " + what + (watched ? L"as unwatched" : L"as watched"),
                watched ? UnwatchedGlyph : WatchedGlyph,
                [actions = m_actions, host = m_host, target = std::move(target), watched]()
                {
                    SetWatched(actions, target, !watched, host);
                });
        }

        void AddEarlierEpisodes(::HaloDesktop::Views::EpisodeMenuTarget const& target)
        {
            if (!m_actions->WatchStateKnown() || !target.Episode)
            {
                return;
            }
            auto const count = m_actions->UnwatchedEarlier(target.Episodes, target.Episode).size();
            if (count == 0)
            {
                return;
            }
            auto const label = L"Mark " + winrt::to_hstring(static_cast<std::uint64_t>(count))
                + (count == 1 ? L" earlier episode as watched" : L" earlier episodes as watched");
            Item(label, EarlierGlyph, [actions = m_actions, host = m_host, target]()
            {
                // Counted again when chosen, so nothing marked since is written twice.
                std::vector<WatchMarkTarget> marks;
                auto const itemId = target.Type + L":" + target.MetaId;
                for (auto const& earlier : actions->UnwatchedEarlier(target.Episodes, target.Episode))
                {
                    marks.push_back({ earlier.VideoId(), itemId, target.ShowName, target.Poster });
                }
                if (!marks.empty())
                {
                    MarkWatchedInOrder(actions, std::move(marks), host);
                }
            });
        }

        void AddLibrary(
            winrt::hstring const& type,
            winrt::hstring const& metaId,
            winrt::hstring const& name,
            winrt::hstring const& poster)
        {
            if (!m_actions->LibraryKnown() || type.empty() || metaId.empty())
            {
                return;
            }
            auto const saved = m_actions->InLibrary(type, metaId);
            Item(
                saved ? L"Remove from library" : L"Add to library",
                saved ? RemoveFromLibraryGlyph : AddToLibraryGlyph,
                [actions = m_actions, host = m_host, type, metaId, name, poster, saved]()
                {
                    SetInLibrary(actions, type, metaId, name, poster, !saved, host);
                });
        }

        // What can be done with the file itself closes the menu, behind its own
        // separator.
        void AddSavedCopy(winrt::hstring const& videoId)
        {
            auto const job = ReadyJob(*m_downloads, videoId);
            if (!job)
            {
                return;
            }
            Separator();
            Item(L"Show in Explorer", ExplorerGlyph, [downloads = m_downloads, host = m_host, jobId = *job]()
            {
                if (!downloads->RevealInExplorer(jobId))
                {
                    ReportFailure(host, DownloadFailedTitle, RevealFailedBody);
                }
            });
            Item(L"Delete from device", DeleteGlyph, [downloads = m_downloads, host = m_host, jobId = *job]()
            {
                DeleteFromDevice(downloads, jobId, host);
            });
        }

        // At the pointer for a click or a press-and-hold, under the element for the
        // Menu key or Shift+F10, which report no position.
        void ShowAt(
            winrt::Microsoft::UI::Xaml::FrameworkElement const& anchor,
            winrt::Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args) const
        {
            if (m_flyout.Items().Size() == 0)
            {
                return;
            }
            winrt::Microsoft::UI::Xaml::Controls::Primitives::FlyoutShowOptions options;
            options.ShowMode(winrt::Microsoft::UI::Xaml::Controls::Primitives::FlyoutShowMode::Standard);
            winrt::Windows::Foundation::Point point{};
            if (args.TryGetPosition(anchor, point))
            {
                options.Position(point);
            }
            else
            {
                options.Placement(winrt::Microsoft::UI::Xaml::Controls::Primitives::FlyoutPlacementMode::BottomEdgeAlignedLeft);
            }
            m_flyout.ShowAt(anchor, options);
            args.Handled(true);
        }

        [[nodiscard]] std::function<void()> const& BeforeOpen() const noexcept { return m_beforeOpen; }
        [[nodiscard]] std::shared_ptr<NavigationService> const& Navigation() const noexcept { return m_navigation; }

    private:
        std::shared_ptr<NavigationService> m_navigation;
        std::shared_ptr<IDownloadService> m_downloads;
        std::shared_ptr<TitleActions> m_actions;
        DialogHost m_host;
        std::function<void()> m_beforeOpen;
        winrt::Microsoft::UI::Xaml::Controls::MenuFlyout m_flyout;
        bool m_separatorPending{};
    };

    // An episode tag such as S01E05, or nothing for the MOVIE and SERIES
    // placeholders the shelf uses when a video id names no episode.
    winrt::hstring EpisodeTagOf(winrt::HaloDesktop::ContinueItem const& item)
    {
        auto const tag = item.Tag();
        return tag == L"MOVIE" || tag == L"SERIES" ? winrt::hstring{} : tag;
    }
}

namespace HaloDesktop::Views
{
    TitleMenu::TitleMenu(::HaloDesktop::Services::AppServices const& services)
        : m_navigation(services.Navigation),
          m_downloads(services.Downloads),
          m_actions(std::make_shared<::HaloDesktop::Services::TitleActions>(
              services.WatchState,
              services.Library,
              services.Catalog,
              services.Metadata,
              services.Session))
    {
        if (!m_navigation || !m_downloads)
        {
            throw std::invalid_argument{ "TitleMenu requires navigation and downloads." };
        }
    }

    std::optional<TitleMenu::Subject> TitleMenu::SubjectOf(
        winrt::Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args)
    {
        // The templates bind with x:Bind, under which ItemsRepeater sets no data
        // context, so a card hands its item through Tag instead, the same way its
        // click handler reads it. The walk starts wherever the gesture landed,
        // which can be deep inside the card or a text run with no element of its
        // own, and stops at the first element carrying a title.
        auto node = args.OriginalSource().try_as<winrt::Microsoft::UI::Xaml::DependencyObject>();
        while (node)
        {
            if (auto const element = node.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>())
            {
                auto const item = element.Tag();
                if (item.try_as<winrt::HaloDesktop::MediaSummary>()
                    || item.try_as<winrt::HaloDesktop::FeaturedItem>()
                    || item.try_as<winrt::HaloDesktop::ContinueItem>()
                    || item.try_as<winrt::HaloDesktop::DetailEpisodeViewModel>())
                {
                    return Subject{ element, item };
                }
            }
            node = winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetParent(node);
        }
        return std::nullopt;
    }

    void TitleMenu::ShowForSubject(
        winrt::Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args,
        std::function<void()> beforeOpen) const
    {
        auto const subject = SubjectOf(args);
        if (!subject)
        {
            return;
        }
        if (auto const featured = subject->Item.try_as<winrt::HaloDesktop::FeaturedItem>())
        {
            ShowForPoster(subject->Anchor, args, featured.Media(), std::move(beforeOpen));
            return;
        }
        if (auto const media = subject->Item.try_as<winrt::HaloDesktop::MediaSummary>())
        {
            ShowForPoster(subject->Anchor, args, media, std::move(beforeOpen));
            return;
        }
        if (auto const item = subject->Item.try_as<winrt::HaloDesktop::ContinueItem>())
        {
            ShowForContinue(subject->Anchor, args, item);
        }
    }

    void TitleMenu::ShowForPoster(
        winrt::Microsoft::UI::Xaml::FrameworkElement const& anchor,
        winrt::Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args,
        winrt::HaloDesktop::MediaSummary const& media,
        std::function<void()> beforeOpen) const
    {
        if (!anchor || !media || media.Type().empty() || media.Id().empty())
        {
            return;
        }
        auto const type = media.Type();
        auto const id = media.Id();
        auto const title = media.Title();
        auto const poster = media.Poster();
        auto const itemId = type + L":" + id;
        // A series' own id is never a video, so it has no watch row and no
        // download of its own.
        auto const isSeries = media.Kind() == winrt::HaloDesktop::MediaKind::Series;

        MenuComposer menu{ m_navigation, m_downloads, m_actions, anchor, std::move(beforeOpen) };
        if (!isSeries)
        {
            menu.Item(L"Play", PlayGlyph, [navigation = menu.Navigation(), beforeOpen = menu.BeforeOpen(), type, id, itemId, title, poster]()
            {
                if (beforeOpen) beforeOpen();
                navigation->ShowSheet(
                    ::HaloDesktop::Services::Page::Sources,
                    winrt::make<winrt::HaloDesktop::implementation::SourcesNavParams>(
                        type, id, id, itemId, title, title, L"", poster));
            });
            menu.AddPlayOffline(id);
        }
        menu.AddDetails(type, id, title, poster);
        menu.Separator();
        if (!isSeries)
        {
            menu.AddWatched({ id, itemId, title, poster }, {});
        }
        menu.AddLibrary(type, id, title, poster);
        if (!isSeries)
        {
            menu.AddSavedCopy(id);
        }
        menu.ShowAt(anchor, args);
    }

    void TitleMenu::ShowForContinue(
        winrt::Microsoft::UI::Xaml::FrameworkElement const& anchor,
        winrt::Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args,
        winrt::HaloDesktop::ContinueItem const& item) const
    {
        if (!anchor || !item || item.VideoId().empty())
        {
            return;
        }
        auto const tag = EpisodeTagOf(item);
        // A promoted next episode has no row yet, so it plays rather than resumes.
        auto const resumable = m_actions->IsResumable(item.VideoId());
        auto const playLabel = resumable
            ? (tag.empty() ? winrt::hstring{ L"Resume" } : L"Resume " + tag)
            : (tag.empty() ? winrt::hstring{ L"Play" } : L"Play " + tag);

        MenuComposer menu{ m_navigation, m_downloads, m_actions, anchor, {} };
        // The card's own click, so this lands exactly where clicking it would.
        menu.Item(playLabel, PlayGlyph, [navigation = m_navigation, item]()
        {
            ::HaloDesktop::Services::OpenContinueItem(*navigation, item);
        });
        menu.AddPlayOffline(item.VideoId());
        menu.AddDetails(item.Type(), item.MetaId(), item.Name(), item.Poster());
        menu.Separator();
        menu.AddWatched({ item.VideoId(), item.ItemId(), item.Name(), item.Poster() }, tag);
        menu.AddLibrary(item.Type(), item.MetaId(), item.Name(), item.Poster());
        menu.AddSavedCopy(item.VideoId());
        menu.ShowAt(anchor, args);
    }

    void TitleMenu::ShowForEpisode(
        winrt::Microsoft::UI::Xaml::FrameworkElement const& anchor,
        winrt::Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args,
        EpisodeMenuTarget target) const
    {
        if (!anchor || !target.Episode || target.Episode.VideoId().empty()
            || target.Type.empty() || target.MetaId.empty())
        {
            return;
        }
        auto const videoId = target.Episode.VideoId();

        MenuComposer menu{ m_navigation, m_downloads, m_actions, anchor, {} };
        menu.Item(m_actions->IsResumable(videoId) ? L"Resume" : L"Play", PlayGlyph, target.Play);
        menu.AddPlayOffline(videoId);
        menu.Separator();
        menu.AddWatched({ videoId, target.Type + L":" + target.MetaId, target.ShowName, target.Poster }, {});
        menu.AddEarlierEpisodes(target);
        menu.AddSavedCopy(videoId);
        menu.ShowAt(anchor, args);
    }
}
