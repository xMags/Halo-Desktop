#include "pch.h"
#include "Views/HistoryPage.xaml.h"
#include "Controls/SmoothWheelScroll.h"
#if __has_include("HistoryPage.g.cpp")
#include "HistoryPage.g.cpp"
#endif

#include "App.xaml.h"
#include "Services/HistoryRowsPolicy.h"
#include "ViewModels/HistoryViewModel.h"
#include "Views/PageDialog.h"
#include "Controls/ContentEntrance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string_view>
#include <utility>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

namespace
{
    // The page's margin at either side, and the widest its content grows:
    // past that the margins take the rest, so the content stays centred.
    constexpr double PageMargin = 32.0;
    constexpr double ContentMaxWidth = 1100.0;

    double SideMargin(double pageWidth) noexcept
    {
        if (!(pageWidth > 0.0)) return PageMargin;
        return (std::max)(PageMargin, std::floor((pageWidth - ContentMaxWidth) / 2.0));
    }

    struct EntrySubject final
    {
        winrt::Microsoft::UI::Xaml::FrameworkElement Anchor{ nullptr };
        winrt::HaloDesktop::HistoryEntryViewModel Item{ nullptr };
    };

    // The card a gesture or a button belongs to: the first element up from
    // where it landed that carries a History row in its Tag.
    std::optional<EntrySubject> SubjectOf(winrt::Windows::Foundation::IInspectable const& source)
    {
        auto node = source.try_as<winrt::Microsoft::UI::Xaml::DependencyObject>();
        while (node)
        {
            if (auto const element = node.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>())
            {
                if (auto const item = element.Tag().try_as<winrt::HaloDesktop::HistoryEntryViewModel>())
                {
                    return EntrySubject{ element, item };
                }
            }
            node = winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetParent(node);
        }
        return std::nullopt;
    }

    winrt::HaloDesktop::HistoryEntryViewModel ItemOf(winrt::Windows::Foundation::IInspectable const& sender)
    {
        auto const element = sender.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>();
        return element ? element.Tag().try_as<winrt::HaloDesktop::HistoryEntryViewModel>() : nullptr;
    }

    winrt::Microsoft::UI::Xaml::DependencyObject FindNamedDescendant(
        winrt::Microsoft::UI::Xaml::DependencyObject const& root,
        std::wstring_view name)
    {
        auto const count = winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetChildrenCount(root);
        for (std::int32_t index = 0; index < count; ++index)
        {
            auto const child = winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetChild(root, index);
            if (auto const element = child.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>();
                element && std::wstring_view{ element.Name() } == name)
            {
                return child;
            }
            if (auto found = FindNamedDescendant(child, name))
            {
                return found;
            }
        }
        return nullptr;
    }

    // `:focus-within`.
    bool HoldsFocus(winrt::Microsoft::UI::Xaml::FrameworkElement const& card)
    {
        auto const root = card.XamlRoot();
        if (!root) return false;
        auto node = winrt::Microsoft::UI::Xaml::Input::FocusManager::GetFocusedElement(root)
            .try_as<winrt::Microsoft::UI::Xaml::DependencyObject>();
        while (node)
        {
            if (node == card) return true;
            node = winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetParent(node);
        }
        return false;
    }

    winrt::Windows::Foundation::IAsyncAction ShowMessageAsync(
        winrt::Microsoft::UI::Xaml::XamlRoot root,
        winrt::Microsoft::UI::Xaml::ElementTheme theme,
        winrt::hstring title,
        winrt::hstring body)
    {
        if (!root) co_return;
        try
        {
            auto dialog = ::HaloDesktop::Views::MakeDialog(root, theme, title, body);
            dialog.CloseButtonText(L"Close");
            co_await dialog.ShowAsync();
        }
        catch (...)
        {
            // Only one dialog can be open at a time, and whatever is already up
            // matters more than this report.
        }
    }
}

namespace winrt::HaloDesktop::implementation
{
    HistoryPage::HistoryPage()
        : m_viewModel(winrt::make<HistoryViewModel>(App::Services())),
          m_titleMenu(App::Services())
    {
    }

    winrt::HaloDesktop::HistoryViewModel HistoryPage::ViewModel() const { return m_viewModel; }

    void HistoryPage::OnLoaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        ::HaloDesktop::Controls::AttachSmoothWheelScroll(HistoryScroller());
        auto const viewModel = winrt::get_self<HistoryViewModel>(m_viewModel);
        // Bound once for the life of the page: handing the list the same
        // collection again resets it, and this runs on every return.
        if (!m_listBound)
        {
            m_listBound = true;
            GroupsList().ItemsSource(viewModel->GroupsView());
        }
        viewModel->Shown();
    }

    void HistoryPage::OnUnloaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        winrt::get_self<HistoryViewModel>(m_viewModel)->Hidden();
        m_hoveredCard = nullptr;
    }

    void HistoryPage::OnQueryBoxLoaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        // WinUI's box shows its own clear button only while it has the caret; the
        // page draws one that stays whenever there is text, as Search's does.
        if (auto const builtIn = FindNamedDescendant(QueryBox(), L"DeleteButton").try_as<Microsoft::UI::Xaml::FrameworkElement>())
        {
            builtIn.Width(0.0);
            builtIn.MinWidth(0.0);
            builtIn.Margin({});
            builtIn.IsHitTestVisible(false);
            if (auto const control = builtIn.try_as<Microsoft::UI::Xaml::Controls::Control>()) control.IsTabStop(false);
        }
    }

    void HistoryPage::OnClearQueryClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.Query(L"");
        QueryBox().Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
    }

    // The header and the list take the same side margins from the page's
    // width, so their edges stay one line whatever the window does.
    void HistoryPage::OnPageSizeChanged(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        auto const side = SideMargin(args.NewSize().Width);
        HeaderRoot().Padding({ side, 28.0, side, 4.0 });
        HistoryBody().Padding({ side, 16.0, side, 40.0 });
    }

    // The tabs in the view model's order: All, Movies, Series, In progress.
    void HistoryPage::OnFilterSelectionChanged(
        Microsoft::UI::Xaml::Controls::SelectorBar const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::SelectorBarSelectionChangedEventArgs const&)
    {
        auto const selected = sender.SelectedItem();
        std::array const items{ AllFilterItem(), MoviesFilterItem(), SeriesFilterItem(), InProgressFilterItem() };
        for (std::size_t position = 0; position < items.size(); ++position)
        {
            if (items[position] == selected)
            {
                m_viewModel.SetFilter(static_cast<std::int32_t>(position));
                ::HaloDesktop::Controls::PlayContentEntrance(GroupsList());
                return;
            }
        }
    }
    void HistoryPage::OnRetryClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.Retry(); }

    void HistoryPage::OnClearHistoryClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        ClearHistory();
    }

    void HistoryPage::OnEntryClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (auto const item = ItemOf(sender)) winrt::get_self<HistoryViewModel>(m_viewModel)->Open(item);
    }

    void HistoryPage::OnPlayClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (auto const item = ItemOf(sender)) winrt::get_self<HistoryViewModel>(m_viewModel)->Play(item);
    }

    void HistoryPage::OnMoreClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        auto const button = sender.try_as<Microsoft::UI::Xaml::FrameworkElement>();
        auto const item = ItemOf(sender);
        if (!button || !item) return;
        ShowMenu(button, nullptr, item);
    }

    // Right-click, press-and-hold, Shift+F10 or the Menu key on a card. Anywhere
    // else (the search box) keeps its own menu.
    void HistoryPage::OnContextRequested(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args)
    {
        auto const subject = SubjectOf(args.OriginalSource());
        if (!subject) return;
        ShowMenu(subject->Anchor, args, subject->Item);
    }

    void HistoryPage::ShowMenu(
        Microsoft::UI::Xaml::FrameworkElement const& anchor,
        Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args,
        winrt::HaloDesktop::HistoryEntryViewModel const& item)
    {
        auto const viewModel = winrt::get_self<HistoryViewModel>(m_viewModel);
        auto const& entry = winrt::get_self<HistoryEntryViewModel>(item)->Entry();
        ::HaloDesktop::Views::HistoryMenuTarget target;
        target.Type = winrt::hstring{ entry.Type };
        target.MetaId = winrt::hstring{ entry.MetaId };
        target.ItemId = winrt::hstring{ entry.ItemId };
        target.VideoId = winrt::hstring{ entry.VideoId };
        target.Name = winrt::hstring{ entry.Name };
        target.Poster = winrt::hstring{ entry.Poster.value_or(std::wstring{}) };
        auto const tag = ::HaloDesktop::Services::VideoIdTag(entry.VideoId, entry.MetaId);
        target.Tag = tag == L"MOVIE" || tag == L"SERIES" ? winrt::hstring{} : winrt::hstring{ tag };
        target.Finished = entry.Finished;
        if (auto show = viewModel->CachedShow(entry))
        {
            target.Episode = show->Episode;
            target.Episodes = std::move(show->Episodes);
            target.ShowName = show->Name;
            target.ShowPoster = show->Poster;
        }
        // The menu can outlive the page, so its actions come back through it weakly.
        target.Play = [weak = get_weak(), item]()
        {
            if (auto const self = weak.get()) winrt::get_self<HistoryViewModel>(self->m_viewModel)->Play(item);
        };
        target.Remove = [weak = get_weak(), videoId = target.VideoId]()
        {
            if (auto const self = weak.get()) self->RemoveVideo(videoId);
        };
        m_titleMenu.ShowForHistory(anchor, args, std::move(target));
    }

    winrt::fire_and_forget HistoryPage::RemoveVideo(winrt::hstring videoId)
    {
        auto const uiContext = winrt::apartment_context{};
        auto const root = XamlRoot();
        auto const theme = ActualTheme();
        auto removal = winrt::get_self<HistoryViewModel>(m_viewModel)->RemoveVideoAsync(std::move(videoId));
        std::optional<winrt::hstring> failure;
        try
        {
            failure = co_await removal;
        }
        catch (...)
        {
            failure = winrt::hstring{ L"Check the connection to your Halo server and try again." };
        }
        co_await uiContext;
        if (failure)
        {
            co_await ShowMessageAsync(root, theme, L"Could not remove from history", *failure);
        }
    }

    // Every row, whatever the filter and the search show: the dialog says so.
    // Only what was live when the question was asked, and still is once it is
    // answered, is forgotten.
    winrt::fire_and_forget HistoryPage::ClearHistory()
    {
        auto const uiContext = winrt::apartment_context{};
        auto const weak = get_weak();
        auto const root = XamlRoot();
        auto const theme = ActualTheme();
        auto videoIds = winrt::get_self<HistoryViewModel>(m_viewModel)->ClearSnapshot();
        if (videoIds.empty() || !root) co_return;

        auto confirmed = false;
        try
        {
            auto dialog = ::HaloDesktop::Views::MakeDialog(
                root,
                theme,
                L"Clear your watch history?",
                winrt::hstring{ ::HaloDesktop::Services::ClearHistoryPrompt(videoIds.size()) },
                ::HaloDesktop::Views::DialogTone::Destructive);
            dialog.PrimaryButtonText(L"Clear history");
            dialog.CloseButtonText(L"Cancel");
            confirmed = co_await dialog.ShowAsync() == Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary;
        }
        catch (...)
        {
            co_return;
        }
        if (!confirmed) co_return;
        auto const self = weak.get();
        if (!self) co_return;

        auto removal = winrt::get_self<HistoryViewModel>(self->m_viewModel)->ClearAsync(std::move(videoIds));
        std::optional<winrt::hstring> failure;
        try
        {
            failure = co_await removal;
        }
        catch (...)
        {
            failure = winrt::hstring{ L"Check the connection to your Halo server and try again." };
        }
        co_await uiContext;
        if (failure)
        {
            co_await ShowMessageAsync(root, theme, L"Could not clear your history", *failure);
        }
    }

    // A recycled row starts at rest: the one it was may have been hovered.
    void HistoryPage::OnCardPrepared(
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::ItemsRepeater const&,
        Microsoft::UI::Xaml::Controls::ItemsRepeaterElementPreparedEventArgs const& args)
    {
        if (auto const card = args.Element().try_as<Microsoft::UI::Xaml::FrameworkElement>())
        {
            if (m_hoveredCard.get() == card) m_hoveredCard = nullptr;
            UpdateCard(card);
        }
    }

    void HistoryPage::OnCardPointerEntered(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
    {
        auto const card = sender.try_as<Microsoft::UI::Xaml::FrameworkElement>();
        if (!card) return;
        auto const previous = m_hoveredCard.get();
        m_hoveredCard = winrt::make_weak(card);
        if (previous && previous != card) UpdateCard(previous);
        UpdateCard(card);
    }

    void HistoryPage::OnCardPointerExited(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
    {
        auto const card = sender.try_as<Microsoft::UI::Xaml::FrameworkElement>();
        if (!card) return;
        if (m_hoveredCard.get() == card) m_hoveredCard = nullptr;
        UpdateCard(card);
    }

    void HistoryPage::OnCardGotFocus(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (auto const card = sender.try_as<Microsoft::UI::Xaml::FrameworkElement>()) UpdateCard(card);
    }

    // Judged once focus has settled: while it moves between the card's own
    // buttons it is still inside.
    void HistoryPage::OnCardLostFocus(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        auto const card = sender.try_as<Microsoft::UI::Xaml::FrameworkElement>();
        if (!card) return;
        auto const queue = DispatcherQueue();
        if (!queue) return;
        queue.TryEnqueue(
            Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
            [weak = get_weak(), weakCard = winrt::make_weak(card)]()
            {
                auto const self = weak.get();
                auto const target = weakCard.get();
                if (self && target) self->UpdateCard(target);
            });
    }

    void HistoryPage::UpdateCard(Microsoft::UI::Xaml::FrameworkElement const& card)
    {
        try
        {
            auto const hovered = m_hoveredCard.get() == card;
            auto const revealed = hovered || HoldsFocus(card);
            if (auto const actions = card.FindName(L"Actions").try_as<Microsoft::UI::Xaml::UIElement>())
            {
                actions.Opacity(revealed ? 1.0 : 0.0);
            }
            if (auto const time = card.FindName(L"TimeText").try_as<Microsoft::UI::Xaml::UIElement>())
            {
                time.Opacity(revealed ? 0.0 : 1.0);
            }
            if (auto const hover = card.FindName(L"HoverPlate").try_as<Microsoft::UI::Xaml::UIElement>())
            {
                hover.Opacity(hovered ? 1.0 : 0.0);
            }
        }
        catch (winrt::hresult_error const&)
        {
            // Decorative only; the buttons work whether or not they are shown.
        }
    }
}
