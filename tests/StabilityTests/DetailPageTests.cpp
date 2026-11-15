#include "DetailPageTests.h"

#include "Api/Dto.h"
#include "Controls/ShelfScrollPolicy.h"
#include "Services/HistoryRowsPolicy.h"
#include "Services/SimilarTitlesPolicy.h"
#include "ViewModels/DetailFactsPolicy.h"
#include "ViewModels/EpisodePagesPolicy.h"
#include "Views/DetailLayoutPolicy.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    void Require(bool condition, char const* message)
    {
        if (!condition)
        {
            throw std::runtime_error{ message };
        }
    }

    using HaloDesktop::Api::Dto::AddonRecord;
    using HaloDesktop::Api::Dto::MetaDetail;
    using HaloDesktop::Api::Dto::MetaPreview;
    using HaloDesktop::Api::Dto::StreamRecord;
    using HaloDesktop::Api::Dto::WatchEntry;
    using HaloDesktop::ViewModels::Credit;
    using HaloDesktop::ViewModels::EpisodeRef;
    using HaloDesktop::ViewModels::Medal;
    using HaloDesktop::ViewModels::MedalFinish;

    // The reference test's `stream(title, langs)`.
    StreamRecord Stream(wchar_t const* title, std::vector<wchar_t const*> const& languages = {})
    {
        StreamRecord stream;
        stream.Url = L"https://example.test/v";
        stream.Title = winrt::hstring{ title };
        std::size_t index{};
        for (auto const language : languages)
        {
            stream.Subtitles.push_back({ winrt::hstring{ std::wstring{ title } + L"-" + std::to_wstring(index++) },
                L"https://example.test/s", winrt::hstring{ language } });
        }
        return stream;
    }

    void TestAvailability()
    {
        using HaloDesktop::ViewModels::SummarizeAvailability;
        std::vector<StreamRecord> const streams{
            Stream(L"Movie.2025.1080p.WEB-DL.mkv", { L"eng" }),
            Stream(L"Movie.2025.2160p.WEB-DL.mkv", { L"eng", L"hin" }),
            Stream(L"Movie.2025.1080p.BluRay.mkv"),
            Stream(L"Movie.2025.480p.mkv"),
            Stream(L"Movie.2025.mkv"),
        };
        auto const summary = SummarizeAvailability(streams);
        Require(summary.Groups.size() == 4, "availability did not group by the picker tier");
        struct Expected final
        {
            wchar_t const* Label;
            std::size_t Count;
            bool Gold;
            Medal Medal;
        };
        // The reference's unknown plate is an em dash; this repository stamps
        // an en dash in its place (QualityMedalPolicy).
        Expected const expected[]{
            { L"4K", 1, true, { L"4K", L"UHD", MedalFinish::Gold } },
            { L"FULL HD", 2, false, { L"1080p", L"FHD", MedalFinish::Silver } },
            { L"SD", 1, false, { L"480p", L"", MedalFinish::Plain } },
            { L"OTHER", 1, false, { L"\x2013", L"", MedalFinish::Plain } },
        };
        for (std::size_t index{}; index < 4; ++index)
        {
            auto const& group = summary.Groups[index];
            Require(group.Label == expected[index].Label, "an availability group had the wrong label or order");
            Require(group.Count == expected[index].Count, "an availability group had the wrong count");
            Require(group.Gold == expected[index].Gold, "gold was not kept for 4K alone");
            Require(group.Medal == expected[index].Medal, "an availability group had the wrong medal");
        }
        Require(summary.Total == 5, "availability did not count every source");
        Require(summary.SubtitleLanguages == 2, "availability did not count the distinct subtitle languages");

        auto const empty = SummarizeAvailability({});
        Require(empty.Groups.empty() && empty.Total == 0 && empty.SubtitleLanguages == 0,
            "availability of nothing was not empty");

        // 480p and SD share a label and count together, at the better rank.
        std::vector<StreamRecord> const lower{ Stream(L"Old.Film.SD.avi"), Stream(L"Old.Film.480p.mkv") };
        auto const merged = SummarizeAvailability(lower);
        Require(merged.Groups.size() == 1 && merged.Groups[0].Count == 2 && merged.Groups[0].Medal.Plate == L"480p",
            "tiers sharing a label did not count together at the better rank");
    }

    void TestRuntime()
    {
        using HaloDesktop::ViewModels::FormatRuntime;
        using HaloDesktop::ViewModels::RuntimeMinutes;
        Require(FormatRuntime(std::wstring_view{ L"150 min" }).value_or(L"") == L"2 h 30 min", "150 min did not read as hours and minutes");
        Require(FormatRuntime(std::wstring_view{ L"120 min" }).value_or(L"") == L"2 h", "120 min did not read as whole hours");
        Require(FormatRuntime(std::wstring_view{ L"45 min" }).value_or(L"") == L"45 min", "45 min did not stay minutes");
        Require(!FormatRuntime(std::nullopt), "a missing runtime was formatted");
        Require(FormatRuntime(std::wstring_view{ L"Ongoing" }).value_or(L"") == L"Ongoing", "odd runtime text was not passed through");

        Require(RuntimeMinutes(L"2 h 46 min") == 166, "an hour and minute runtime was misread");
        Require(RuntimeMinutes(L"2h46") == 120, "the reference reads 2h46 as its hours alone");
        Require(RuntimeMinutes(L"90") == 90 && RuntimeMinutes(L" 120 ") == 120, "a bare number of minutes was misread");
        Require(!RuntimeMinutes(L"1500") && !RuntimeMinutes(L"5"), "a bare number outside two or three digits was read");
        Require(!RuntimeMinutes(L"5 seasons"), "a season count was read as minutes");
        Require(!RuntimeMinutes(L"0 min"), "a zero runtime was read");
    }

    void TestCredits()
    {
        using HaloDesktop::ViewModels::CreditsOf;
        MetaDetail meta;
        meta.Cast = { L"Plain Name" };
        meta.Director = { L"Mohit Suri", L"aneet padda" };
        meta.PicturedCast = {
            { L"Ahaan Panday", winrt::hstring{ L"Krish Kapoor" }, winrt::hstring{ L"https://img.test/a.jpg" } },
            { L"Aneet Padda", winrt::hstring{ L"Vaani Batra" }, std::nullopt },
        };
        auto const credits = CreditsOf(meta);
        std::vector<Credit> const expected{
            { L"Ahaan Panday", std::wstring{ L"Krish Kapoor" }, std::wstring{ L"https://img.test/a.jpg" } },
            { L"Aneet Padda", std::wstring{ L"Vaani Batra" }, std::nullopt },
            { L"Mohit Suri", std::wstring{ L"Director" }, std::nullopt },
        };
        Require(credits == expected, "credits did not prefer the pictured cast, close with directors and drop repeats");

        MetaDetail plain;
        plain.Cast = { L"Tim Robbins" };
        Require(CreditsOf(plain) == std::vector<Credit>{ { L"Tim Robbins", std::nullopt, std::nullopt } },
            "credits did not fall back to the plain cast names");

        MetaDetail blank;
        blank.Cast = { L"  ", L"Same", L" same " };
        Require(CreditsOf(blank).size() == 1, "a blank name was kept or a padded repeat was not dropped");
    }

    void TestLinks()
    {
        using HaloDesktop::ViewModels::EncodeUriComponent;
        using HaloDesktop::ViewModels::PersonSearchUrl;
        using HaloDesktop::ViewModels::TrailerUrl;
        Require(PersonSearchUrl(L" Aneet Padda ") == L"https://www.imdb.com/find/?q=Aneet%20Padda&s=nm",
            "a cast card did not search IMDb people for the name");
        Require(EncodeUriComponent(L"a&b=c/d?e#f") == L"a%26b%3Dc%2Fd%3Fe%23f", "reserved characters were not encoded");
        Require(EncodeUriComponent(L"-_.!~*'()") == L"-_.!~*'()", "unreserved marks were encoded");
        Require(EncodeUriComponent(L"Am\x00E9lie \x4E2D") == L"Am%C3%A9lie%20%E4%B8%AD", "non-ASCII was not encoded as UTF-8");
        Require(EncodeUriComponent(L"\xD83D\xDE00") == L"%F0%9F%98%80", "a surrogate pair was not encoded as one code point");
        Require(EncodeUriComponent(std::wstring_view{ L"\xD83D", 1 }) == L"%EF%BF%BD", "a lone surrogate was not replaced");

        MetaDetail streams;
        streams.TrailerStreamIds = { L"PLl99DlL6b4" };
        Require(TrailerUrl(streams).value_or(L"") == L"https://www.youtube.com/watch?v=PLl99DlL6b4",
            "the trailer stream did not link to YouTube");
        MetaDetail list;
        list.Trailers = { { L"clip123456", winrt::hstring{ L"Clip" } }, { L"xyXX8LXiNJ4", winrt::hstring{ L"Trailer" } } };
        Require(TrailerUrl(list).value_or(L"") == L"https://www.youtube.com/watch?v=xyXX8LXiNJ4",
            "the older trailer list was not read, or a clip was taken for the trailer");
        MetaDetail evil;
        evil.TrailerStreamIds = { L"x&list=evil" };
        Require(!TrailerUrl(evil), "an id that is not YouTube's shape became a link");
        Require(!TrailerUrl(MetaDetail{}), "a title without trailers got one");
        MetaDetail untyped;
        untyped.Trailers = { { L"abcdef", std::nullopt } };
        Require(TrailerUrl(untyped).value_or(L"") == L"https://www.youtube.com/watch?v=abcdef", "an untyped trailer was skipped");
    }

    void TestAboutAndDates()
    {
        using HaloDesktop::ViewModels::AboutRows;
        using HaloDesktop::ViewModels::AirYear;
        using HaloDesktop::ViewModels::CalendarDate;
        using HaloDesktop::ViewModels::FormatAirDate;
        using HaloDesktop::ViewModels::FormatReleaseDate;
        using HaloDesktop::ViewModels::ParseIsoDate;

        MetaDetail meta;
        meta.Preview.Id = L"tt1";
        meta.Preview.Type = L"movie";
        meta.Preview.Name = L"X";
        meta.Director = { L"A" };
        meta.Released = winrt::hstring{ L"2025-07-18T00:00:00.000Z" };
        meta.Country = winrt::hstring{ L"India" };
        meta.Awards = winrt::hstring{ L"  " };
        std::vector<std::pair<std::wstring, std::wstring>> const expected{
            { L"Director", L"A" },
            { L"Released", L"18 July 2025" },
            { L"Country", L"India" },
        };
        Require(AboutRows(meta) == expected, "about rows did not keep only the facts the addon sent");
        Require(!FormatReleaseDate(std::wstring_view{ L"not a date" }), "a non-date was formatted");

        meta.Writer = { L"W1", L"W2" };
        Require(AboutRows(meta)[1] == std::pair<std::wstring, std::wstring>{ L"Writers", L"W1, W2" }, "writers were not joined");

        Require(ParseIsoDate(L"2025-07-18") == CalendarDate{ 2025, 7, 18 }, "a date-only ISO string did not parse");
        Require(ParseIsoDate(L"2025") == CalendarDate{ 2025, 1, 1 }, "a bare year did not parse as its first day");
        Require(ParseIsoDate(L"2025-07-18T23:30:00-05:00") == CalendarDate{ 2025, 7, 19 }, "a western offset did not move the UTC day");
        Require(ParseIsoDate(L"2025-07-18T01:00:00+05:30") == CalendarDate{ 2025, 7, 17 }, "an eastern offset did not move the UTC day");
        Require(ParseIsoDate(L"2024-12-31T23:59:59.999-01:00") == CalendarDate{ 2025, 1, 1 }, "an offset did not carry into the next year");
        Require(!ParseIsoDate(L"2025-02-30") && !ParseIsoDate(L"2025-13-01") && !ParseIsoDate(L"18/07/2025"),
            "an impossible or non-ISO date parsed");

        Require(FormatAirDate(std::wstring_view{ L"2026-02-07T00:00:00.000Z" }) == L"FEB 07 2026", "the air date was not FEB 07 2026");
        Require(FormatAirDate(std::wstring_view{ L"garbage" }).empty() && FormatAirDate(std::nullopt).empty(), "a bad air date printed");
        Require(AirYear(std::wstring_view{ L"2026-02-07" }) == L"2026" && AirYear(std::nullopt).empty(), "the air year was wrong");
        Require(FormatReleaseDate(std::wstring_view{ L"2025-01-05" }).value_or(L"") == L"5 January 2025", "the day was zero-padded");
    }

    void TestHeroFacts()
    {
        using HaloDesktop::ViewModels::CountOf;
        using HaloDesktop::ViewModels::HeroFacts;
        using HaloDesktop::ViewModels::Initials;
        MetaDetail film;
        film.Preview.ReleaseInfo = winrt::hstring{ L"2025" };
        film.Runtime = winrt::hstring{ L"150 min" };
        film.Preview.Genres = { L"Drama", L"Action", L"Crime", L"Thriller" };
        Require(HeroFacts(film, 0) == std::vector<std::wstring>{ L"2025", L"2 h 30 min", L"Drama, Action, Crime" },
            "a film's facts were not release, runtime and three genres");
        Require(HeroFacts(film, 3) == std::vector<std::wstring>{ L"2025", L"3 seasons", L"Drama, Action, Crime" },
            "a show's facts did not count its seasons");
        Require(HeroFacts(film, 1)[1] == L"1 season", "one season was plural");
        Require(HeroFacts(MetaDetail{}, 0).empty(), "a bare title had facts");
        Require(CountOf(1, L"source", L"sources") == L"1 source" && CountOf(0, L"source", L"sources") == L"0 sources",
            "counts were not singular for one alone");

        Require(Initials(L"Tim Robbins") == L"TR" && Initials(L"cher") == L"CH" && Initials(L"  ") == L"?"
            && Initials(L"jean-luc picard") == L"JL", "initials were wrong");
    }

    std::vector<EpisodeRef> Season(std::size_t count, std::int32_t firstNumber = 1)
    {
        std::vector<EpisodeRef> episodes;
        for (std::size_t index{}; index < count; ++index)
        {
            auto const number = firstNumber + static_cast<std::int32_t>(index);
            episodes.push_back({ L"show:1:" + std::to_wstring(number), number, 1 });
        }
        return episodes;
    }

    void TestEpisodePages()
    {
        using HaloDesktop::ViewModels::EpisodePages;
        using HaloDesktop::ViewModels::PageContaining;
        Require(EpisodePages(Season(25)).size() == 1, "a season of one page was split");
        auto const long_ = Season(2000);
        auto const pages = EpisodePages(long_);
        Require(pages.size() == 80, "a long season did not split into pages of 25");
        Require(pages[0].Label == L"Episodes 1\x2013" L"25" && pages[1].Label == L"Episodes 26\x2013" L"50",
            "page labels did not follow the episode numbers");
        Require(pages[79].Label == L"Episodes 1976\x2013" L"2000", "the last page's label was wrong");
        std::size_t total{};
        for (auto const& page : pages) total += page.Count;
        Require(total == 2000, "paging lost or repeated episodes");

        Require(EpisodePages(Season(60, 101), 50)[1].Label == L"Episodes 151\x2013" L"160", "labels did not use the episodes' own numbers");
        auto unnumbered = Season(3);
        for (auto& episode : unnumbered) episode.Number.reset();
        Require(EpisodePages(unnumbered, 2)[1].Label == L"Episode 3", "an unnumbered page did not fall back to positions");

        auto const twoHundred = Season(200);
        auto const pagesOf200 = EpisodePages(twoHundred);
        Require(PageContaining(pagesOf200, twoHundred, std::wstring_view{ L"show:1:120" }) == 4, "the panel did not open on the resume page");
        Require(PageContaining(pagesOf200, twoHundred, std::wstring_view{ L"elsewhere" }) == 0, "an unknown episode did not open the first page");
        Require(PageContaining(pagesOf200, twoHundred, std::nullopt) == 0, "no resume episode did not open the first page");
        Require(EpisodePages({}).empty(), "an empty season had pages");
    }

    WatchEntry Row(wchar_t const* videoId, double position, double duration, bool watched, std::int64_t updatedAt)
    {
        WatchEntry row;
        row.VideoId = videoId;
        row.ItemId = L"series:tt1";
        row.PositionSec = position;
        row.DurationSec = duration;
        row.Watched = watched;
        row.UpdatedAt = updatedAt;
        return row;
    }

    void TestSeasonsAndResume()
    {
        using HaloDesktop::ViewModels::LastWatchedSeason;
        using HaloDesktop::ViewModels::NumberedSeasonCount;
        using HaloDesktop::ViewModels::ResumeTarget;
        using HaloDesktop::ViewModels::SeasonOrder;

        std::vector<EpisodeRef> const videos{
            { L"tt1:0:1", 1, 0 },
            { L"tt1:2:1", 1, 2 },
            { L"tt1:1:1", 1, 1 },
            { L"tt1:1:2", 2, 1 },
            { L"tt1:extra", std::nullopt, std::nullopt },
        };
        auto const seasons = SeasonOrder(videos);
        Require(seasons == std::vector<std::int32_t>{ 1, 2, 0 }, "seasons were not ascending with Specials last");
        Require(NumberedSeasonCount(seasons) == 2, "Specials counted as a numbered season");

        // No thresholds: a row a second in still resumes; the newest wins.
        std::vector<WatchEntry> const rows{
            Row(L"tt1:1:1", 1.0, 3000.0, false, 100),
            Row(L"tt1:1:2", 2900.0, 3000.0, false, 200),
            Row(L"tt1:2:1", 0.0, 3000.0, true, 300),
            Row(L"elsewhere", 10.0, 3000.0, false, 400),
            Row(L"tt1:0:1", 0.0, 0.0, false, 500),
        };
        auto const resume = ResumeTarget(rows, videos);
        Require(resume && resume->VideoId == L"tt1:1:2", "resume did not take the newest unwatched row with a length in the title");
        Require(!ResumeTarget(std::vector<WatchEntry>{ Row(L"tt1:1:1", 0.0, 3000.0, true, 1) }, videos), "a watched row resumed");

        // A film lists no videos: any of its rows counts.
        auto const film = ResumeTarget(std::vector<WatchEntry>{ Row(L"tt9", 30.0, 6000.0, false, 7) }, {});
        Require(film && film->VideoId == L"tt9", "a film's row did not resume");

        // The newest row with progress is tt1:2:1 (watched), so season 2 opens.
        Require(LastWatchedSeason(rows, videos) == 2, "the page did not open on the most recently watched season");
        std::vector<WatchEntry> const unwatched{ Row(L"tt1:2:1", 0.0, 3000.0, false, 900), Row(L"tt1:1:1", 5.0, 3000.0, false, 100) };
        Require(LastWatchedSeason(unwatched, videos) == 1, "an episode marked unwatched counted as viewing");
        Require(!LastWatchedSeason(std::vector<WatchEntry>{ Row(L"tt1:extra", 5.0, 3000.0, false, 1) }, videos),
            "an episode without a season named one");
        Require(!LastWatchedSeason({}, videos), "nothing watched named a season");
    }

    AddonRecord Addon(wchar_t const* id, std::vector<AddonRecord::Catalog> catalogs)
    {
        AddonRecord addon;
        addon.Id = id;
        addon.Name = L"Addon " + winrt::hstring{ id };
        addon.Catalogs = std::move(catalogs);
        return addon;
    }

    AddonRecord::Catalog Catalog(wchar_t const* type, wchar_t const* id, std::vector<AddonRecord::CatalogExtra> extras = {})
    {
        AddonRecord::Catalog catalog;
        catalog.Type = type;
        catalog.Id = id;
        catalog.Extras = std::move(extras);
        return catalog;
    }

    void TestGenreCatalog()
    {
        using HaloDesktop::Services::GenreCatalog;
        using HaloDesktop::Services::PickGenreCatalog;
        std::vector<winrt::hstring> const drama{ L"Drama" };

        std::vector<AddonRecord> const first{
            Addon(L"a", { Catalog(L"series", L"top", { { L"genre", false, {} } }) }),
            Addon(L"b", { Catalog(L"movie", L"top", { { L"genre", false, { L"Drama", L"Comedy" } } }) }),
            Addon(L"c", { Catalog(L"movie", L"popular", { { L"genre", false, {} } }) }),
        };
        Require(PickGenreCatalog(first, L"movie", drama) == GenreCatalog{ L"b", L"Addon b", L"top", L"Drama" },
            "the first addon filtering this type by the leading genre did not win");

        std::vector<AddonRecord> const listed{ Addon(L"a", { Catalog(L"movie", L"top", { { L"genre", false, { L"Comedy" } } }) }) };
        Require(!PickGenreCatalog(listed, L"movie", std::vector<winrt::hstring>{ L"Musical" }), "a catalog was picked for a genre it does not list");
        auto const second = PickGenreCatalog(listed, L"movie", std::vector<winrt::hstring>{ L"Musical", L"Comedy" });
        Require(second && second->Genre == L"Comedy", "the next genre was not tried");

        auto legacyRequired = Catalog(L"movie", L"legacy");
        legacyRequired.ExtraSupported = { L"genre" };
        legacyRequired.ExtraRequired = { L"search" };
        std::vector<AddonRecord> const needy{
            Addon(L"a", { Catalog(L"movie", L"search", { { L"genre", false, {} }, { L"search", true, {} } }) }),
            Addon(L"b", { Catalog(L"movie", L"top", { { L"skip", false, {} } }) }),
            Addon(L"c", { legacyRequired }),
        };
        Require(!PickGenreCatalog(needy, L"movie", drama), "a catalog needing another extra, or without a genre filter, was picked");

        auto legacy = Catalog(L"movie", L"legacy");
        legacy.ExtraSupported = { L"genre" };
        std::vector<AddonRecord> const old{ Addon(L"a", { legacy }) };
        auto const picked = PickGenreCatalog(old, L"movie", drama);
        Require(picked && picked->CatalogId == L"legacy", "the legacy extraSupported list did not count as a genre filter");
    }

    MetaPreview Preview(wchar_t const* type, wchar_t const* id)
    {
        MetaPreview preview;
        preview.Type = type;
        preview.Id = id;
        preview.Name = id;
        return preview;
    }

    void TestSimilarShelf()
    {
        using HaloDesktop::Services::SimilarTitles;
        std::vector<MetaPreview> items{
            Preview(L"movie", L"tt1"),
            Preview(L"movie", L"tt2"),
            Preview(L"movie", L"tt2"),
            Preview(L"series", L"tt2"),
            Preview(L"movie", L"tt3"),
        };
        auto const shelf = SimilarTitles(items, L"movie", L"tt1");
        Require(shelf.size() == 3 && shelf[0].Id == L"tt2" && shelf[1].Type == L"series" && shelf[2].Id == L"tt3",
            "the shelf did not drop this title and repeats by type and id");
        std::vector<MetaPreview> many;
        for (int index = 0; index < 30; ++index) many.push_back(Preview(L"movie", (L"tt" + std::to_wstring(100 + index)).c_str()));
        Require(SimilarTitles(many, L"movie", L"tt0").size() == HaloDesktop::Services::SimilarLimit, "the shelf was not held to 20");
    }

    void TestVideoIdTags()
    {
        // format.test.ts cases not already mirrored in HistoryRowsTests.
        using HaloDesktop::Services::VideoIdTag;
        Require(VideoIdTag(L"tt0111161", L"tt0111161") == L"MOVIE", "a video id equal to its meta id was not a movie");
        Require(VideoIdTag(L"tt0944947:1:2", L"tt0944947") == L"S01E02" && VideoIdTag(L"tt2861424:0:4", L"tt2861424") == L"S00E04"
            && VideoIdTag(L"tt0903747:5:16", L"tt0903747") == L"S05E16", "a season and episode did not become a padded tag");
        Require(VideoIdTag(L"kitsu:1376:3:7", L"kitsu:1376") == L"S03E07", "a meta id with colons was split instead of stripped");
        Require(VideoIdTag(L"kitsu:1376:5", L"kitsu:1376") == L"SERIES", "a kitsu episode without a season was not SERIES");
        Require(VideoIdTag(L"tt0944947:x:2", L"tt0944947") == L"SERIES" && VideoIdTag(L"tt0944947:1:2:3", L"tt0944947") == L"SERIES"
            && VideoIdTag(L"tt0944947:", L"tt0944947") == L"SERIES", "an episode id that does not parse was not SERIES");
    }

    void TestLayout()
    {
        using HaloDesktop::Views::CompactBarShown;
        using HaloDesktop::Views::DetailHeroHeight;
        using HaloDesktop::Views::DetailLayout;
        using HaloDesktop::Views::DetailLayoutFor;
        Require(DetailLayoutFor(800.0) == DetailLayout{ 32.0, false, false }, "a narrow page kept the aside or the split");
        Require(DetailLayoutFor(940.0).ShowAside == false && DetailLayoutFor(941.0).ShowAside, "the aside did not hide at 940px");
        Require(!DetailLayoutFor(1040.0).SplitSideBySide && DetailLayoutFor(1041.0).SplitSideBySide, "the split did not fold at 1040px");
        Require(DetailLayoutFor(1099.0).Gutter == 32.0 && DetailLayoutFor(1100.0).Gutter == 48.0
            && DetailLayoutFor(1499.0).Gutter == 48.0 && DetailLayoutFor(1500.0).Gutter == 56.0, "the gutter steps were wrong");
        Require(DetailHeroHeight(500.0) == 480.0 && std::abs(DetailHeroHeight(800.0) - 592.0) < 1e-9 && DetailHeroHeight(2000.0) == 780.0,
            "the hero height was not 74% held between 480 and 780");
        Require(!CompactBarShown(100.0, 46.0) && !CompactBarShown(18.0, 46.0) && CompactBarShown(17.0, 46.0),
            "the compact bar did not follow the main button leaving the band under it");
        Require(!CompactBarShown(900.0, 46.0), "a button below the view showed the compact bar");
    }

    void TestScrollRow()
    {
        using HaloDesktop::Controls::ScrollRowPageTarget;
        using HaloDesktop::Controls::ShelfOverflows;
        Require(!ShelfOverflows(500.0, 500.0) && !ShelfOverflows(500.0, 501.0) && ShelfOverflows(500.0, 502.0),
            "a strip within the slack counted as overflowing");
        Require(ScrollRowPageTarget(0.0, 800.0, 3000.0, 1) == 640.0, "a page did not move the viewport less 160");
        Require(ScrollRowPageTarget(0.0, 200.0, 3000.0, 1) == 160.0, "a narrow page moved less than 160");
        Require(ScrollRowPageTarget(2000.0, 800.0, 3000.0, 1) == 2200.0, "a page past the end was not held at the end");
        Require(ScrollRowPageTarget(300.0, 800.0, 3000.0, -1) == 0.0, "a page before the start was not held at the start");
    }
}

void RunDetailPageTests()
{
    TestAvailability();
    TestRuntime();
    TestCredits();
    TestLinks();
    TestAboutAndDates();
    TestHeroFacts();
    TestEpisodePages();
    TestSeasonsAndResume();
    TestGenreCatalog();
    TestSimilarShelf();
    TestVideoIdTags();
    TestLayout();
    TestScrollRow();
}
