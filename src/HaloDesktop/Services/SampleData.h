#pragma once

#include <cstdint>
#include <vector>
#include <winrt/HaloDesktop.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace HaloDesktop::Services::SampleData
{
    namespace Copy
    {
        inline constexpr wchar_t HeroSynopsis[] = L"Six surveyors walk a border nobody has walked, and come back with two versions of the same winter.";
        inline constexpr wchar_t HeroRating[] = L"★ 8.4";
        inline constexpr wchar_t HeroMeta[] = L"2024 · Drama · Thriller · S02E04";
        inline constexpr wchar_t HomeCatalogStats[] = L"6 CATALOGS · 4 ADDONS";
        inline constexpr wchar_t SearchTopMatchMeta[] = L"SERIES · 2024 · 2 SEASONS · CINEMETA";
        inline constexpr wchar_t SearchSeedQuery[] = L"northwind";
        inline constexpr wchar_t DetailSynopsis[] = L"Six surveyors walk a border nobody has walked. The map they bring back does not agree with the map they were sent to confirm, and the second season is the argument about which one gets filed.";
        inline constexpr wchar_t ConnectHeadline[] = L"Point Halo at your server.";
        inline constexpr wchar_t ConnectBody[] = L"Your library, watch progress and addons live there. Nothing is stored by anyone else.";
        inline constexpr wchar_t ConnectPlaceholder[] = L"https://halo.mynas.dev";
        inline constexpr wchar_t ServerReached[] = L"REACHED · LOCAL AUTH · v0.9.4";
        inline constexpr wchar_t SourceTeachingTitle[] = L"Halo ranks sources for you";
        inline constexpr wchar_t SourceTeachingBody[] = L"Cached first, then the highest quality your connection holds, then audio in your language. Change the weighting in Settings · Playback.";
        inline constexpr wchar_t AddonInstallNotice[] = L"Addon install is wired to the server later.";
        inline constexpr wchar_t DownloadsInfoTitle[] = L"Downloads keep running in the tray";
        inline constexpr wchar_t DownloadsInfoBody[] = L"Closing the window minimises Halo. Transfers finish in the background and resume after a restart.";
        inline constexpr wchar_t PauseAllTitle[] = L"Pause all transfers?";
        inline constexpr wchar_t PauseAllBody[] = L"Two downloads are in flight. Paused transfers keep their partial files and resume from the same byte.";
        inline constexpr wchar_t PlayerSubtitle[] = L"The map does not agree with the ground.";
        inline constexpr wchar_t PlayerKeyHints[] = L"SPACE PLAY · ←/→ 10 S · F FULL SCREEN · ESC EXIT";
    }

    winrt::HaloDesktop::MediaSummary Hero();
    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::ContinueItem> ContinueWatching();
    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Shelf> Shelves();
    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary> LibraryItems();
    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::SearchGroup> SearchGroups();
    winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> RecentSearchTerms();
    winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> RecentSearchAges();

    winrt::HaloDesktop::MediaDetail Detail();
    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Episode> Episodes(std::int32_t season);
    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::SourceGroup> SourceGroups();

    std::vector<winrt::HaloDesktop::DownloadItem> TransferItems();
    std::vector<winrt::HaloDesktop::DownloadItem> ReadyItems();
    std::vector<double> ThroughputSamples();
    std::vector<winrt::HaloDesktop::Addon> Addons();
}
