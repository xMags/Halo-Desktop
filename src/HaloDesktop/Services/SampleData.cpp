#include "pch.h"
#include "Services/SampleData.h"

#include "Models/Models.h"

#include <utility>

namespace
{
    winrt::HaloDesktop::MediaSummary Media(
        wchar_t const* id,
        wchar_t const* title,
        wchar_t const* meta,
        winrt::HaloDesktop::MediaKind kind)
    {
        return winrt::make<winrt::HaloDesktop::implementation::MediaSummary>(id, title, meta, kind);
    }

    winrt::HaloDesktop::Episode Episode(
        wchar_t const* tag,
        wchar_t const* title,
        wchar_t const* blurb,
        wchar_t const* runtime,
        wchar_t const* aired,
        double progress,
        bool downloaded)
    {
        return winrt::make<winrt::HaloDesktop::implementation::Episode>(
            tag, title, blurb, runtime, aired, progress, downloaded);
    }

    winrt::HaloDesktop::StreamSource Source(
        wchar_t const* quality,
        wchar_t const* range,
        wchar_t const* file,
        wchar_t const* codec,
        wchar_t const* audio,
        wchar_t const* languages,
        winrt::HaloDesktop::StreamStatus status,
        wchar_t const* size)
    {
        return winrt::make<winrt::HaloDesktop::implementation::StreamSource>(
            quality, range, file, codec, audio, languages, status, size);
    }

    winrt::HaloDesktop::DownloadItem Download(
        wchar_t const* id,
        wchar_t const* tag,
        wchar_t const* name,
        wchar_t const* sub,
        winrt::HaloDesktop::DownloadState state,
        double progress,
        wchar_t const* detail,
        wchar_t const* quality,
        wchar_t const* codec,
        wchar_t const* size,
        wchar_t const* subtitles)
    {
        return winrt::make<winrt::HaloDesktop::implementation::DownloadItem>(
            id, tag, name, sub, state, progress, detail, quality, codec, size, subtitles);
    }

    template <typename T>
    winrt::Windows::Foundation::Collections::IVectorView<T> View(std::vector<T> values)
    {
        return winrt::single_threaded_vector<T>(std::move(values)).GetView();
    }
}

namespace HaloDesktop::Services::SampleData
{
    winrt::HaloDesktop::MediaSummary Hero()
    {
        return Media(L"northwind-divide", L"Northwind Divide", L"2024 · SERIES", winrt::HaloDesktop::MediaKind::Series);
    }

    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::ContinueItem> ContinueWatching()
    {
        std::vector<winrt::HaloDesktop::ContinueItem> items;
        items.push_back(winrt::make<winrt::HaloDesktop::implementation::ContinueItem>(L"Northwind Divide", L"The Cut Line", L"S02E04", L"18:24 LEFT", 0.62));
        items.push_back(winrt::make<winrt::HaloDesktop::implementation::ContinueItem>(L"The Long Sunday", L"Feature · 2h 08m", L"MOVIE", L"52:10 LEFT", 0.31));
        items.push_back(winrt::make<winrt::HaloDesktop::implementation::ContinueItem>(L"Harbour Lights", L"Signals", L"S01E07", L"06:02 LEFT", 0.88));
        items.push_back(winrt::make<winrt::HaloDesktop::implementation::ContinueItem>(L"Copper Basin", L"Feature · 1h 44m", L"MOVIE", L"1:12:40 LEFT", 0.14));
        return View(std::move(items));
    }

    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Shelf> Shelves()
    {
        std::vector<winrt::HaloDesktop::Shelf> shelves;

        std::vector<winrt::HaloDesktop::MediaSummary> library{
            Media(L"northwind-divide", L"Northwind Divide", L"2024 · SERIES", winrt::HaloDesktop::MediaKind::Series),
            Media(L"the-long-sunday", L"The Long Sunday", L"2023 · MOVIE", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"harbour-lights", L"Harbour Lights", L"2022 · SERIES", winrt::HaloDesktop::MediaKind::Series),
            Media(L"copper-basin", L"Copper Basin", L"2021 · MOVIE", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"slow-tide", L"Slow Tide", L"2024 · MOVIE", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"fieldwork", L"Fieldwork", L"2020 · SERIES", winrt::HaloDesktop::MediaKind::Series),
            Media(L"anvil-road", L"Anvil Road", L"2023 · MOVIE", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"quiet-hours", L"Quiet Hours", L"2019 · SERIES", winrt::HaloDesktop::MediaKind::Series),
        };
        shelves.push_back(winrt::make<winrt::HaloDesktop::implementation::Shelf>(L"My library", L"SYNCED · 42", View(std::move(library))));

        std::vector<winrt::HaloDesktop::MediaSummary> trending{
            Media(L"grand-meridian", L"Grand Meridian", L"2025 · MOVIE", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"salt-static", L"Salt & Static", L"2024 · SERIES", winrt::HaloDesktop::MediaKind::Series),
            Media(L"ninth-winter", L"Ninth Winter", L"2024 · MOVIE", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"paper-cities", L"Paper Cities", L"2023 · SERIES", winrt::HaloDesktop::MediaKind::Series),
            Media(L"undertow", L"Undertow", L"2025 · MOVIE", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"blue-hour", L"Blue Hour", L"2022 · MOVIE", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"the-remainder", L"The Remainder", L"2024 · SERIES", winrt::HaloDesktop::MediaKind::Series),
            Media(L"longitude", L"Longitude", L"2021 · MOVIE", winrt::HaloDesktop::MediaKind::Movie),
        };
        shelves.push_back(winrt::make<winrt::HaloDesktop::implementation::Shelf>(L"Trending now", L"TOP", View(std::move(trending))));

        std::vector<winrt::HaloDesktop::MediaSummary> releases{
            Media(L"silt", L"Silt", L"2025 · MOVIE", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"twelve-fathoms", L"Twelve Fathoms", L"2025 · MOVIE", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"hollow-season", L"Hollow Season", L"2025 · SERIES", winrt::HaloDesktop::MediaKind::Series),
            Media(L"marrow", L"Marrow", L"2025 · MOVIE", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"kestrel", L"Kestrel", L"2025 · MOVIE", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"nightshift", L"Nightshift", L"2025 · SERIES", winrt::HaloDesktop::MediaKind::Series),
            Media(L"frostline", L"Frostline", L"2025 · MOVIE", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"ash-harbour", L"Ash Harbour", L"2025 · MOVIE", winrt::HaloDesktop::MediaKind::Movie),
        };
        shelves.push_back(winrt::make<winrt::HaloDesktop::implementation::Shelf>(L"New releases", L"2025", View(std::move(releases))));
        return View(std::move(shelves));
    }

    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary> LibraryItems()
    {
        std::vector<winrt::HaloDesktop::MediaSummary> items{
            Media(L"northwind-divide", L"Northwind Divide", L"2024 · S02", winrt::HaloDesktop::MediaKind::Series),
            Media(L"the-long-sunday", L"The Long Sunday", L"2023 · 2h 08m", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"harbour-lights", L"Harbour Lights", L"2022 · S01", winrt::HaloDesktop::MediaKind::Series),
            Media(L"copper-basin", L"Copper Basin", L"2021 · 1h 44m", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"slow-tide", L"Slow Tide", L"2024 · 1h 58m", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"fieldwork", L"Fieldwork", L"2020 · S03", winrt::HaloDesktop::MediaKind::Series),
            Media(L"anvil-road", L"Anvil Road", L"2023 · 2h 12m", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"quiet-hours", L"Quiet Hours", L"2019 · S02", winrt::HaloDesktop::MediaKind::Series),
            Media(L"grand-meridian", L"Grand Meridian", L"2025 · 1h 51m", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"salt-static", L"Salt & Static", L"2024 · S01", winrt::HaloDesktop::MediaKind::Series),
            Media(L"ninth-winter", L"Ninth Winter", L"2024 · 2h 02m", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"paper-cities", L"Paper Cities", L"2023 · S02", winrt::HaloDesktop::MediaKind::Series),
            Media(L"undertow", L"Undertow", L"2025 · 1h 39m", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"blue-hour", L"Blue Hour", L"2022 · 2h 20m", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"longitude", L"Longitude", L"2021 · 1h 47m", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"silt", L"Silt", L"2025 · 1h 36m", winrt::HaloDesktop::MediaKind::Movie),
        };
        return View(std::move(items));
    }

    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::SearchGroup> SearchGroups()
    {
        std::vector<winrt::HaloDesktop::MediaSummary> series{
            Media(L"northwind-divide", L"Northwind Divide", L"2024 · S02", winrt::HaloDesktop::MediaKind::Series),
            Media(L"northwind-field-notes", L"Northwind: Field Notes", L"2025 · S01", winrt::HaloDesktop::MediaKind::Series),
            Media(L"north-by-nine", L"North by Nine", L"2019 · S03", winrt::HaloDesktop::MediaKind::Series),
            Media(L"windward", L"Windward", L"2022 · S01", winrt::HaloDesktop::MediaKind::Series),
            Media(L"divide-measure", L"Divide & Measure", L"2020 · S02", winrt::HaloDesktop::MediaKind::Series),
            Media(L"cold-survey", L"Cold Survey", L"2023 · S01", winrt::HaloDesktop::MediaKind::Series),
            Media(L"treeline", L"Treeline", L"2021 · S04", winrt::HaloDesktop::MediaKind::Series),
            Media(L"border-crew", L"Border Crew", L"2018 · S02", winrt::HaloDesktop::MediaKind::Series),
        };
        std::vector<winrt::HaloDesktop::MediaSummary> movies{
            Media(L"northwind-1997", L"Northwind", L"1997 · MOVIE", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"the-divide", L"The Divide", L"2011 · MOVIE", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"winter-survey", L"Winter Survey", L"2016 · MOVIE", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"above-the-treeline", L"Above the Treeline", L"2024 · MOVIE", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"snowline", L"Snowline", L"2015 · MOVIE", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"the-crew", L"The Crew", L"2013 · MOVIE", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"meridian-pass", L"Meridian Pass", L"2022 · MOVIE", winrt::HaloDesktop::MediaKind::Movie),
            Media(L"whiteout", L"Whiteout", L"2009 · MOVIE", winrt::HaloDesktop::MediaKind::Movie),
        };
        std::vector<winrt::HaloDesktop::SearchGroup> groups;
        groups.push_back(winrt::make<winrt::HaloDesktop::implementation::SearchGroup>(L"Series", L"8 RESULTS", View(std::move(series))));
        groups.push_back(winrt::make<winrt::HaloDesktop::implementation::SearchGroup>(L"Movies", L"8 RESULTS · OPENSUBTITLES", View(std::move(movies))));
        return View(std::move(groups));
    }

    winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> RecentSearchTerms()
    {
        return View<winrt::hstring>({ L"northwind", L"the long sunday", L"documentary 2024", L"harbour lights s01" });
    }

    winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> RecentSearchAges()
    {
        return View<winrt::hstring>({ L"2M AGO", L"TODAY", L"YESTERDAY", L"MON" });
    }

    winrt::HaloDesktop::MediaDetail Detail()
    {
        return winrt::make<winrt::HaloDesktop::implementation::MediaDetail>(
            L"northwind-divide",
            L"Northwind Divide",
            L"SERIES",
            L"★ 8.4 · 2024 · 2 seasons · 18 episodes · TV-MA",
            Copy::DetailSynopsis,
            View<winrt::hstring>({ L"CREATED BY · A. Rennick", L"CAST · M. Oyelaran, J. Fisk, S. Toth", L"GENRE · Drama · Thriller", L"NETWORK · Meridian" }),
            View<winrt::hstring>({ L"Stream sources · 18 SOURCES", L"OpenSubtitles · 42 SUBS", L"Local files · 4 EPISODES" }),
            View<std::int32_t>({ 1, 2 }));
    }

    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Episode> Episodes(std::int32_t season)
    {
        std::vector<winrt::HaloDesktop::Episode> items;
        if (season == 1)
        {
            items.push_back(Episode(L"S01E01", L"Above the Treeline", L"Six people, one winter, a border nobody has walked.", L"52 min", L"04 MAR", 1.0, false));
            items.push_back(Episode(L"S01E02", L"Baseline", L"The first survey disagrees with the map.", L"47 min", L"11 MAR", 1.0, false));
            items.push_back(Episode(L"S01E03", L"Whiteout", L"A storm costs them four days and one instrument.", L"45 min", L"18 MAR", 1.0, false));
            items.push_back(Episode(L"S01E04", L"Signals", L"The radio starts answering itself.", L"48 min", L"25 MAR", 1.0, true));
        }
        else
        {
            items.push_back(Episode(L"S02E01", L"First Thaw", L"The crew returns to a camp that has been moved twenty metres north.", L"46 min", L"12 JAN", 1.0, true));
            items.push_back(Episode(L"S02E02", L"Chain of Custody", L"Mara files the survey and someone files a different one.", L"44 min", L"19 JAN", 1.0, true));
            items.push_back(Episode(L"S02E03", L"The Inquiry", L"Two versions of the same winter go on record.", L"49 min", L"26 JAN", 1.0, true));
            items.push_back(Episode(L"S02E04", L"The Cut Line", L"A stand of trees decides the argument.", L"48 min", L"02 FEB", 0.62, true));
            items.push_back(Episode(L"S02E05", L"Thaw Season", L"The ground gives up what the ground was holding.", L"47 min", L"09 FEB", 0.0, false));
            items.push_back(Episode(L"S02E06", L"Reference Point", L"Everything depends on where the first stake went in.", L"45 min", L"16 FEB", 0.0, false));
        }
        return View(std::move(items));
    }

    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::SourceGroup> SourceGroups()
    {
        std::vector<winrt::HaloDesktop::StreamSource> remoteSources{
            Source(L"2160p", L"HDR10", L"Northwind.Divide.S02E04.2160p.WEB-DL.DDP5.1.HEVC-NTb.mkv", L"HEVC 10-bit", L"DDP 5.1", L"ENG · JPN", winrt::HaloDesktop::StreamStatus::Instant, L"6.2 GB"),
            Source(L"2160p", L"HDR10", L"Northwind.Divide.S02E04.2160p.HDR.WEB.H265-FLUX.mkv", L"HEVC 10-bit", L"AAC 2.0", L"ENG", winrt::HaloDesktop::StreamStatus::Instant, L"5.1 GB"),
            Source(L"1080p", L"SDR", L"Northwind.Divide.S02E04.1080p.WEB-DL.H264-GROUP.mkv", L"H.264", L"AC3 5.1", L"ENG · DEU", winrt::HaloDesktop::StreamStatus::Instant, L"3.4 GB"),
            Source(L"1080p", L"SDR", L"Northwind.Divide.S02E04.1080p.AMZN.WEBRip.DDP5.1-KiNGS.mkv", L"H.264", L"DDP 5.1", L"ENG", winrt::HaloDesktop::StreamStatus::Caching, L"2.8 GB"),
            Source(L"720p", L"SDR", L"Northwind.Divide.S02E04.720p.WEB.x264-BATCH.mkv", L"H.264", L"AAC 2.0", L"ENG", winrt::HaloDesktop::StreamStatus::Uncached, L"1.4 GB"),
        };
        std::vector<winrt::HaloDesktop::StreamSource> local{
            Source(L"1080p", L"SDR", L"Northwind Divide - S02E04 - The Cut Line.mkv", L"H.264", L"DDP 5.1", L"ENG · 4 SUBS", winrt::HaloDesktop::StreamStatus::OnDisk, L"3.4 GB"),
            Source(L"1080p", L"SDR", L"Northwind Divide - S02E03 - The Inquiry.mkv", L"H.264", L"DDP 5.1", L"ENG · 4 SUBS", winrt::HaloDesktop::StreamStatus::OnDisk, L"3.2 GB"),
        };
        std::vector<winrt::HaloDesktop::SourceGroup> groups;
        groups.push_back(winrt::make<winrt::HaloDesktop::implementation::SourceGroup>(L"STREAM SOURCES", L"DEBRID · RESOLVED IN 0.8 S", 11, View(std::move(remoteSources))));
        groups.push_back(winrt::make<winrt::HaloDesktop::implementation::SourceGroup>(L"LOCAL LIBRARY", L"FROM YOUR DISK · 0.1 S", 4, View(std::move(local))));
        return View(std::move(groups));
    }

    std::vector<winrt::HaloDesktop::DownloadItem> TransferItems()
    {
        return {
            Download(L"t1", L"S02E05", L"Northwind Divide", L"Thaw Season", winrt::HaloDesktop::DownloadState::Downloading, 0.68, L"2.3 GB OF 3.4 GB · 28.4 MB/S · 39 S LEFT", L"1080p", L"H.264", L"3.4 GB", L"English (SRT)"),
            Download(L"t2", L"S02E06", L"Northwind Divide", L"Reference Point", winrt::HaloDesktop::DownloadState::Queued, 0.0, L"WAITING FOR A SLOT · 3.4 GB", L"1080p", L"H.264", L"3.4 GB", L"English (SRT)"),
            Download(L"t3", L"MOVIE", L"The Long Sunday", L"Feature · 2h 08m", winrt::HaloDesktop::DownloadState::Paused, 0.41, L"1.8 GB OF 4.4 GB · PAUSED BY YOU", L"1080p", L"H.264", L"4.4 GB", L"English (SRT)"),
        };
    }

    std::vector<winrt::HaloDesktop::DownloadItem> ReadyItems()
    {
        return {
            Download(L"r1", L"S02E04", L"Northwind Divide", L"The Cut Line", winrt::HaloDesktop::DownloadState::OnDisk, 0.62, L"3.4 GB · 1080p · 18 MIN LEFT TO WATCH", L"1080p", L"H.264", L"3.4 GB", L"English (ASS)"),
            Download(L"r2", L"S02E03", L"Northwind Divide", L"The Inquiry", winrt::HaloDesktop::DownloadState::OnDisk, 0.0, L"3.2 GB · 1080p · ADDED 4 DAYS AGO", L"1080p", L"H.264", L"3.2 GB", L"English (ASS)"),
            Download(L"r3", L"MOVIE", L"Copper Basin", L"Feature · 1h 44m", winrt::HaloDesktop::DownloadState::OnDisk, 0.0, L"5.8 GB · 2160p · ADDED LAST WEEK", L"2160p", L"HEVC", L"5.8 GB", L"English (SRT)"),
        };
    }

    std::vector<double> ThroughputSamples()
    {
        return { 18.0, 21.0, 20.0, 24.0, 27.0, 25.0, 30.0, 29.0, 33.0, 31.0, 35.0, 34.0, 36.0, 34.0, 38.0, 36.0, 40.0, 38.0, 41.2, 39.0, 36.0, 39.0, 40.5, 38.0, 35.0, 37.0, 39.0, 34.0, 31.0, 28.4 };
    }

    std::vector<winrt::HaloDesktop::Addon> Addons()
    {
        return {
            winrt::make<winrt::HaloDesktop::implementation::Addon>(L"sample-cm", L"", L"CM", L"Cinemeta", L"v3.0.13", L"GLOBAL", L"Catalogs · Metadata · movie, series", false, true, true),
            winrt::make<winrt::HaloDesktop::implementation::Addon>(L"sample-to", L"", L"TO", L"Torrentio", L"v0.0.15", L"YOURS", L"Streams · debrid resolution", false, false, true),
            winrt::make<winrt::HaloDesktop::implementation::Addon>(L"sample-os", L"", L"OS", L"OpenSubtitles v3", L"v1.2.0", L"YOURS", L"Subtitles · 62 languages", false, false, true),
            winrt::make<winrt::HaloDesktop::implementation::Addon>(L"sample-lb", L"", L"LB", L"Local Files", L"v0.4.1", L"YOURS", L"Catalogs · Streams · from your disk", false, false, false),
        };
    }
}
