#include "SearchInputTests.h"

#include "Services/SearchInputPolicy.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using HaloDesktop::Services::ClassifySearchInput;
    using HaloDesktop::Services::SearchInput;
    using HaloDesktop::Services::SearchInputKind;

    void Require(bool condition, std::string const& message)
    {
        if (!condition)
        {
            throw std::runtime_error{ message };
        }
    }

    std::string Narrow(std::wstring const& text)
    {
        std::string narrow;
        for (auto const value : text) narrow.push_back(value < 0x80 ? static_cast<char>(value) : '?');
        return narrow;
    }

    // One row: the input, the kind it must read as, and the one field of
    // that kind that must match (empty when only the kind is checked).
    struct Row final
    {
        std::wstring Input;
        SearchInputKind Kind;
        std::wstring Expected;
    };

    std::wstring KeyField(SearchInput const& input)
    {
        switch (input.Kind)
        {
        case SearchInputKind::ImdbTitle: return input.ImdbId;
        case SearchInputKind::ImdbPerson: return input.PersonId;
        case SearchInputKind::TvdbNumeric: return input.TvdbKind + L":" + input.TvdbId;
        case SearchInputKind::TvdbSlug: return input.TvdbKind + L":" + input.DisplayName;
        case SearchInputKind::Magnet: return input.InfoHashHex;
        case SearchInputKind::WebLink: return input.Host;
        case SearchInputKind::UnsupportedLink: return input.Reason;
        case SearchInputKind::Text: return {};
        }
        return {};
    }

    void TestClassificationTable()
    {
        constexpr wchar_t Hex[] = L"c12fe1c06bba254a9dc9f519b335aa7c1367a88a";
        std::vector<Row> const rows{
            // Words.
            { L"dune", SearchInputKind::Text, L"" },
            { L"Dune: Part Two", SearchInputKind::Text, L"" },
            { L"Re:Zero", SearchInputKind::Text, L"" },
            { L"blade runner 2049", SearchInputKind::Text, L"" },
            { Hex, SearchInputKind::Text, L"" },
            { L"tt123456", SearchInputKind::Text, L"" },
            { L"tt12345678901", SearchInputKind::Text, L"" },
            { L"www.imdb.com/title/tt0111161", SearchInputKind::Text, L"" },
            // IMDb titles: bare and linked, any case.
            { L"tt0111161", SearchInputKind::ImdbTitle, L"tt0111161" },
            { L"TT0111161", SearchInputKind::ImdbTitle, L"tt0111161" },
            { L"tt1234567890", SearchInputKind::ImdbTitle, L"tt1234567890" },
            { L"https://www.imdb.com/title/tt0111161/", SearchInputKind::ImdbTitle, L"tt0111161" },
            { L"http://imdb.com/title/tt0111161", SearchInputKind::ImdbTitle, L"tt0111161" },
            { L"https://m.imdb.com/title/tt0903747/episodes/?season=2#top", SearchInputKind::ImdbTitle, L"tt0903747" },
            { L"HTTPS://WWW.IMDB.COM/TITLE/TT0111161/?ref_=nv_sr_1", SearchInputKind::ImdbTitle, L"tt0111161" },
            { L"https://www.imdb.com/fr/title/tt0111161/", SearchInputKind::ImdbTitle, L"tt0111161" },
            // IMDb people and other IMDb pages.
            { L"https://www.imdb.com/name/nm0000138/", SearchInputKind::ImdbPerson, L"nm0000138" },
            { L"https://www.imdb.com/chart/top/", SearchInputKind::UnsupportedLink, L"This IMDb link isn't a title or a person." },
            // TVDB.
            { L"https://thetvdb.com/?tab=series&id=81189", SearchInputKind::TvdbNumeric, L"series:81189" },
            { L"https://www.thetvdb.com/?id=81189&tab=series", SearchInputKind::TvdbNumeric, L"series:81189" },
            { L"https://thetvdb.com/dereferrer/series/81189", SearchInputKind::TvdbNumeric, L"series:81189" },
            { L"https://thetvdb.com/dereferrer/movie/123", SearchInputKind::TvdbNumeric, L"movie:123" },
            { L"https://thetvdb.com/series/breaking-bad", SearchInputKind::TvdbSlug, L"series:breaking-bad" },
            { L"https://thetvdb.com/movies/dune-part-two/", SearchInputKind::TvdbSlug, L"movie:dune-part-two" },
            // Magnets: hex and base32 in either case; base32 becomes hex.
            { std::wstring{ L"magnet:?xt=urn:btih:" } + Hex + L"&dn=Some+Film", SearchInputKind::Magnet, Hex },
            { L"magnet:?xt=urn:btih:C12FE1C06BBA254A9DC9F519B335AA7C1367A88A", SearchInputKind::Magnet, Hex },
            { L"MAGNET:?XT=URN:BTIH:YEX6DQDLXISUVHOJ6UM3GNNKPQJWPKEK", SearchInputKind::Magnet, Hex },
            { L"magnet:?xt=urn:btih:yex6dqdlxisuvhoj6um3gnnkpqjwpkek", SearchInputKind::Magnet, Hex },
            { L"magnet:?xt=urn:btih:BCW2LJ5GDA5K4HQJ3AY56Z2I2VTASWQQ", SearchInputKind::Magnet, L"08ada5a7a6183aae1e09d831df6748d566095a10" },
            { L"magnet:?xt=urn:btih:77777777777777777777777777777777", SearchInputKind::Magnet, L"ffffffffffffffffffffffffffffffffffffffff" },
            { std::wstring{ L"magnet:?xt=urn:btih:" } + Hex + L"&xt=urn:btmh:1220abcd", SearchInputKind::Magnet, Hex },
            { L"magnet:?xt=urn:btmh:1220caf1e1c30e81cb361b9ee167c4aa64228a7fa4fa9f6105232b28ad099f3a302e", SearchInputKind::UnsupportedLink, L"This magnet link uses the newer v2 format, which isn't supported." },
            { L"magnet:?xt=urn:btih:12345", SearchInputKind::UnsupportedLink, L"This magnet link has no valid info hash." },
            { std::wstring{ L"magnet:?xt=urn:btih:" } + Hex + L"&xt=urn:btih:08ada5a7a6183aae1e09d831df6748d566095a10", SearchInputKind::UnsupportedLink, L"This magnet link has no valid info hash." },
            { L"magnet:?dn=nothing", SearchInputKind::UnsupportedLink, L"This magnet link has no valid info hash." },
            // Web links.
            { L"https://cdn.example.com/films/Some%20Film.mkv", SearchInputKind::WebLink, L"cdn.example.com" },
            { L"HTTP://Media.Example.org:8080/video.mp4?token=abc", SearchInputKind::WebLink, L"media.example.org" },
            { L"http://[::1]:8080/a.mp4", SearchInputKind::WebLink, L"[::1]" },
            { L"https://user:pass@example.com/a.mp4", SearchInputKind::UnsupportedLink, L"Links with a user name or password aren't supported." },
            { L"https://user@example.com/a.mp4", SearchInputKind::UnsupportedLink, L"Links with a user name or password aren't supported." },
            { L"https:///a.mp4", SearchInputKind::UnsupportedLink, L"This link isn't valid." },
            { L"https://example.com:99999/a.mp4", SearchInputKind::UnsupportedLink, L"This link isn't valid." },
            { L"https://example.com/a b.mp4", SearchInputKind::UnsupportedLink, L"This link isn't valid." },
            { L"https://example.com\\a.mp4", SearchInputKind::UnsupportedLink, L"This link isn't valid." },
            // Video sites and live TV.
            { L"https://www.youtube.com/watch?v=dQw4w9WgXcQ", SearchInputKind::UnsupportedLink, L"YouTube and live TV links aren't supported." },
            { L"https://youtu.be/dQw4w9WgXcQ", SearchInputKind::UnsupportedLink, L"YouTube and live TV links aren't supported." },
            { L"https://M.YOUTUBE.COM/watch?v=x", SearchInputKind::UnsupportedLink, L"YouTube and live TV links aren't supported." },
            { L"https://www.twitch.tv/somechannel", SearchInputKind::UnsupportedLink, L"YouTube and live TV links aren't supported." },
            { L"https://notyoutube.com/a.mp4", SearchInputKind::WebLink, L"notyoutube.com" },
            // Files on this PC and other schemes.
            { L"file:///C:/Videos/film.mkv", SearchInputKind::UnsupportedLink, L"Files on this PC can't be opened from Search." },
            { L"file:C:/Videos/film.mkv", SearchInputKind::UnsupportedLink, L"Files on this PC can't be opened from Search." },
            { L"C:\\Videos\\film.mkv", SearchInputKind::UnsupportedLink, L"Files on this PC can't be opened from Search." },
            { L"d:/film.mkv", SearchInputKind::UnsupportedLink, L"Files on this PC can't be opened from Search." },
            { L"\\\\server\\share\\film.mkv", SearchInputKind::UnsupportedLink, L"Files on this PC can't be opened from Search." },
            { L"//server/share/film.mkv", SearchInputKind::UnsupportedLink, L"Files on this PC can't be opened from Search." },
            { L"ftp://example.com/a.mp4", SearchInputKind::UnsupportedLink, L"This kind of link isn't supported." },
            { L"javascript:alert(1)", SearchInputKind::UnsupportedLink, L"This kind of link isn't supported." },
            { L"stremio://detail/movie/tt0111161", SearchInputKind::UnsupportedLink, L"This kind of link isn't supported." },
        };
        for (auto const& row : rows)
        {
            auto const input = ClassifySearchInput(row.Input);
            Require(input.Kind == row.Kind, "wrong kind for " + Narrow(row.Input));
            if (!row.Expected.empty())
            {
                Require(KeyField(input) == row.Expected,
                    "wrong value for " + Narrow(row.Input) + ": got " + Narrow(KeyField(input)));
            }
        }
    }

    void TestMagnetKeepsOnlyTheName()
    {
        auto const input = ClassifySearchInput(
            L"magnet:?xt=urn:btih:c12fe1c06bba254a9dc9f519b335aa7c1367a88a&dn=Some+Film%20%282024%29%0A%E2%80%AE"
            L"&tr=udp%3A%2F%2Ftracker.example%3A1337&tr=http://other.example/announce");
        Require(input.Kind == SearchInputKind::Magnet, "a magnet with trackers was not a magnet");
        Require(input.DisplayName == L"Some Film (2024)", "the magnet's name was not decoded and cleaned");
        Require(input.Url.empty() && input.Host.empty(), "a magnet kept something of its trackers");
        Require(input.DisplayName.find(L"tracker") == std::wstring::npos, "a tracker leaked into the name");

        std::wstring const longName(300, L'a');
        auto const capped = ClassifySearchInput(L"magnet:?xt=urn:btih:c12fe1c06bba254a9dc9f519b335aa7c1367a88a&dn=" + longName);
        Require(capped.DisplayName.size() == HaloDesktop::Services::SearchMagnetNameMaxLength, "the magnet's name was not capped at 200");

        auto const unicode = ClassifySearchInput(L"magnet:?xt=urn:btih:c12fe1c06bba254a9dc9f519b335aa7c1367a88a&dn=Am%C3%A9lie");
        Require(unicode.DisplayName == L"Am\u00E9lie", "a UTF-8 name was not decoded");
        auto const broken = ClassifySearchInput(L"magnet:?xt=urn:btih:c12fe1c06bba254a9dc9f519b335aa7c1367a88a&dn=%C0%AFx");
        Require(broken.DisplayName == L"\uFFFD\uFFFDx", "an overlong UTF-8 sequence was not refused");
    }

    void TestWebLinkLengthAndTitle()
    {
        std::wstring const prefix = L"https://example.com/";
        auto const atLimit = prefix + std::wstring(HaloDesktop::Services::SearchWebLinkMaxLength - prefix.size(), L'a');
        auto const accepted = ClassifySearchInput(atLimit);
        Require(accepted.Kind == SearchInputKind::WebLink, "a link of 2048 characters was refused");
        Require(accepted.Url == atLimit, "a link of 2048 characters was not kept whole");
        std::wstring sentence;
        while (sentence.size() < 500) sentence += L"a very long plain sentence about films ";
        sentence.resize(500);
        Require(ClassifySearchInput(sentence).Kind == SearchInputKind::Text, "a 500-character sentence was not words");
        auto const overLimit = atLimit + L"a";
        auto const refused = ClassifySearchInput(overLimit);
        Require(refused.Kind == SearchInputKind::UnsupportedLink && refused.Reason == L"This link is too long.",
            "a link of 2049 characters was accepted");

        auto const named = ClassifySearchInput(L"https://cdn.example.com/films/Some%20Film%20(2024).mkv?sig=1#t=5");
        Require(named.DisplayName == L"Some Film (2024).mkv", "the file name was not the link's title");
        Require(named.Url == L"https://cdn.example.com/films/Some%20Film%20(2024).mkv?sig=1#t=5", "the link was changed");
        auto const bare = ClassifySearchInput(L"https://cdn.example.com/");
        Require(bare.DisplayName == L"cdn.example.com", "a link without a file was not named by its host");
        auto const hidden = ClassifySearchInput(L"https://cdn.example.com/a%0Db%E2%80%AEc.mp4");
        Require(hidden.DisplayName == L"abc.mp4", "control and direction characters were kept in the title");
        auto const longFile = ClassifySearchInput(L"https://cdn.example.com/" + std::wstring(200, L'f'));
        Require(longFile.DisplayName.size() == HaloDesktop::Services::SearchWebLinkTitleMaxLength, "the title was not capped at 120");
    }
}

void RunSearchInputTests()
{
    TestClassificationTable();
    TestMagnetKeepsOnlyTheName();
    TestWebLinkLengthAndTitle();
}
