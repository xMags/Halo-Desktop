#pragma once

#include "Services/AppServices.h"

#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include <winrt/HaloDesktop.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Windows.Foundation.h>

namespace HaloDesktop::Services
{
    class IDownloadService;
    class NavigationService;
    class TitleActions;
}

namespace HaloDesktop::Views
{
    // An episode row on the title page, which already names the episode, so its
    // menu does not repeat the name the way a continue card's does.
    struct EpisodeMenuTarget final
    {
        winrt::hstring Type;
        winrt::hstring MetaId;
        winrt::hstring ShowName;
        winrt::hstring Poster;
        winrt::HaloDesktop::Episode Episode{ nullptr };
        // Every season's episodes, which is what "earlier" is counted against.
        std::vector<winrt::HaloDesktop::Episode> Episodes;
        // The row's own click, so Play here does exactly what clicking it does.
        std::function<void()> Play;
    };

    // The right-click menus for everything that stands for a title: a poster or
    // the featured banner, a continue card or a Jump back in row, an episode row.
    // Every menu reads the same way: what a click would do and where the title
    // lives, then what can be recorded about it (watched, in the library), then
    // what can be done with a copy saved on this device.
    //
    // Menus are built when they open, from what the services already hold, so a
    // card subscribes to nothing and opening one costs no request. An entry whose
    // state has not arrived yet is left out rather than guessed. Shift+F10 and the
    // Menu key open the same menu under the focused element.
    //
    // UI-thread-only. A page owns one; the menus it opens hold their own
    // references, so a menu or a save in flight outlives a page that goes away.
    class TitleMenu final
    {
    public:
        explicit TitleMenu(::HaloDesktop::Services::AppServices const& services);

        // What a context request landed on: the title a card hands through its
        // Tag, and that card, to place the menu against.
        struct Subject final
        {
            winrt::Microsoft::UI::Xaml::FrameworkElement Anchor{ nullptr };
            winrt::Windows::Foundation::IInspectable Item{ nullptr };
        };
        [[nodiscard]] static std::optional<Subject> SubjectOf(
            winrt::Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args);

        // Opens the matching menu when the request landed on a poster, the
        // featured banner or a continue card, and leaves it unhandled anywhere
        // else, so a text field keeps its own menu. `beforeOpen` runs ahead of any
        // navigation the menu makes; Search records its term with it.
        void ShowForSubject(
            winrt::Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args,
            std::function<void()> beforeOpen = {}) const;

        // A film plays from here; a series needs an episode chosen, which is what
        // its title page is for, so its menu offers that instead.
        void ShowForPoster(
            winrt::Microsoft::UI::Xaml::FrameworkElement const& anchor,
            winrt::Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args,
            winrt::HaloDesktop::MediaSummary const& media,
            std::function<void()> beforeOpen = {}) const;
        // Marking the card's episode watched moves the shelf on to the next one,
        // as finishing it would.
        void ShowForContinue(
            winrt::Microsoft::UI::Xaml::FrameworkElement const& anchor,
            winrt::Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args,
            winrt::HaloDesktop::ContinueItem const& item) const;
        void ShowForEpisode(
            winrt::Microsoft::UI::Xaml::FrameworkElement const& anchor,
            winrt::Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args,
            EpisodeMenuTarget target) const;

    private:
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        std::shared_ptr<::HaloDesktop::Services::IDownloadService> m_downloads;
        std::shared_ptr<::HaloDesktop::Services::TitleActions> m_actions;
    };
}
