#include <winsock2.h>
#include <ws2tcpip.h>

#include "AddonResolveTests.h"

#include "AddonTestFakes.h"

#include "Api/Addons/AddonClientPolicy.h"
#include "Api/Addons/AddonNormalize.h"
#include "Api/Addons/AddonResolver.h"
#include "Api/Addons/HttpAddonFetcher.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
    namespace Addons = HaloDesktop::Api::Addons;
    namespace Json = winrt::Windows::Data::Json;
    using AddonTests::Require;
    using AddonTests::Route;

    /* Protocol rules */

    void TestUrlRules()
    {
        Require(Addons::TransportBase(L"https://a.test/manifest.json") == L"https://a.test", "manifest.json was not stripped");
        Require(Addons::TransportBase(L"https://a.test/") == L"https://a.test", "a trailing slash was not stripped");
        Require(Addons::TransportBase(L"https://a.test/x/manifest.json/") == L"https://a.test/x/manifest.json",
                "the transport base did not follow the reference's two replacements");
        Require(Addons::ManifestUrl(L"https://a.test/key/") == L"https://a.test/key/manifest.json", "the manifest URL is wrong");

        Require(Addons::ResourceUrl(L"https://a.test/manifest.json", Addons::AddonResource::Catalog, L"movie", L"top",
                    { { L"skip", L"100" }, { L"genre", L"Action" } })
                    == std::optional<std::wstring>{ L"https://a.test/catalog/movie/top/skip=100&genre=Action.json" },
                "a catalog URL with extras is wrong");
        Require(Addons::ResourceUrl(L"https://a.test", Addons::AddonResource::Stream, L"series", L"tt9:1:2")
                    == std::optional<std::wstring>{ L"https://a.test/stream/series/tt9%3A1%3A2.json" },
                "an id was not encoded in the path");
        Require(Addons::EncodeUriComponent(L"a bé!'()*~") == std::optional<std::wstring>{ L"a%20b%C3%A9!'()*~" },
                "encodeURIComponent was not followed");
        Require(Addons::EncodeUriComponent(L"\U0001F600") == std::optional<std::wstring>{ L"%F0%9F%98%80" }, "a surrogate pair was not encoded");
        std::wstring lone{ L"x" };
        lone.push_back(static_cast<wchar_t>(0xD800));
        Require(!Addons::EncodeUriComponent(lone), "a lone surrogate was encoded");

        Require(!Addons::IsUrl(L"not a url"), "text without a scheme read as a URL");
        Require(Addons::IsUrl(L"magnet:?xt=1") && !Addons::IsHttpUrl(L"magnet:?xt=1"), "a magnet link was misread");
        Require(Addons::IsUrl(L"javascript:alert(1)") && !Addons::IsHttpUrl(L"javascript:alert(1)"), "a script URL was misread");
        Require(!Addons::IsUrl(L"https://"), "an https URL without a host parsed");
        Require(Addons::IsHttpUrl(L"http:example.com"), "an http URL without slashes was refused, as WHATWG accepts it");
        Require(Addons::IsHttpUrl(L"HTTPS://A.TEST/x"), "an upper-case scheme was refused");
        Require(!Addons::IsUrl(L"https://a b.test/"), "a host with a space parsed");
        Require(!Addons::IsUrl(L"https://a.test:99999/"), "an out-of-range port parsed");
        Require(Addons::IsHttpUrl(L"https://user:pass@a.test:8080/x?y#z"), "a full http URL was refused");
        Require(Addons::IsHttpsUrl(L"https://img.test/a.jpg") && !Addons::IsHttpsUrl(L"http://img.test/a.jpg"), "https detection is wrong");
        Require(Addons::IsImdbId(L"tt28037987") && !Addons::IsImdbId(L"tmdb:5") && !Addons::IsImdbId(L"tt") && !Addons::IsImdbId(L"tt12345678901"),
                "the IMDb id rule is wrong");
    }

    void TestSupportRules()
    {
        Addons::ManifestFacts manifest;
        manifest.Types = { L"movie", L"series" };
        manifest.IdPrefixes = std::vector<std::wstring>{ L"tt" };
        manifest.Resources = {
            { L"meta", true, {}, std::nullopt },
            { L"stream", false, { L"series" }, std::vector<std::wstring>{ L"kitsu:" } },
            { L"subtitles", false, { L"movie" }, std::nullopt },
        };
        Require(Addons::AddonSupportsResource(manifest, Addons::AddonResource::Meta, L"movie", L"tt1"), "a bare resource did not match");
        Require(!Addons::AddonSupportsResource(manifest, Addons::AddonResource::Meta, L"movie", L"kitsu:1"), "the manifest prefixes were ignored");
        Require(Addons::AddonSupportsResource(manifest, Addons::AddonResource::Meta, L"movie"), "an absent id did not match every prefix");
        Require(!Addons::AddonSupportsResource(manifest, Addons::AddonResource::Meta, L"channel", L"tt1"), "a type the manifest lacks matched");
        Require(Addons::AddonSupportsResource(manifest, Addons::AddonResource::Stream, L"series", L"kitsu:1")
                    && !Addons::AddonSupportsResource(manifest, Addons::AddonResource::Stream, L"series", L"tt1")
                    && !Addons::AddonSupportsResource(manifest, Addons::AddonResource::Stream, L"movie", L"kitsu:1"),
                "a resource object's own types and prefixes were not used");
        Require(Addons::AddonSupportsResource(manifest, Addons::AddonResource::Subtitles, L"movie", L"tt1")
                    && !Addons::AddonSupportsResource(manifest, Addons::AddonResource::Subtitles, L"movie", L"kitsu:1"),
                "a resource object without prefixes did not fall back to the manifest's");
        Require(!Addons::AddonSupportsResource(manifest, Addons::AddonResource::Catalog, L"movie", L"tt1"), "an undeclared resource matched");

        auto const parsed = Addons::ReadManifestFacts(Json::JsonObject::Parse(
            LR"({"id":"x","types":["movie"],"idPrefixes":"tt","resources":["meta",{"name":"stream"},5]})"));
        Require(parsed.IdPrefixes && parsed.IdPrefixes->empty(), "a prefix list that is not a list matched anything");
        Require(parsed.Resources.size() == 2 && parsed.Resources[1].Types.empty(), "a resource object without types was misread");
    }

    void TestRequestLimits()
    {
        auto const extras = Addons::CatalogExtras({ { L"genre", L"A" }, { L"addon", L"x" }, { L"genre", L"B" }, { L"skip", L"10" } });
        Require(extras == Addons::ExtraArgs{ { L"genre", L"A" }, { L"skip", L"10" } }, "catalog extras were not read as the server reads them");
        Addons::ExtraArgs nine;
        for (int index = 0; index < 9; ++index) nine.emplace_back(L"k" + std::to_wstring(index), L"v");
        Require(Addons::CatalogExtraProblem(nine) == std::optional<std::wstring>{ L"too many extra params" }, "nine extras passed");
        Require(Addons::CatalogExtraProblem({ { std::wstring(65, L'k'), L"v" } }) == std::optional<std::wstring>{ L"extra param too long" },
                "an overlong extra name passed");
        Require(!Addons::CatalogExtraProblem({ { std::wstring(64, L'k'), std::wstring(256, L'v') } }), "extras at the bound were refused");

        Require(Addons::SubtitleQueryProblem({ std::wstring{ L"0123456789ABCDEF" }, std::nullopt, std::nullopt }) == std::nullopt,
                "an upper-case hash was refused");
        Require(Addons::SubtitleQueryProblem({ std::wstring{ L"012345" }, std::nullopt, std::nullopt })
                    == std::optional<std::wstring>{ L"videoHash must be 16 hex chars" },
                "a short hash passed");
        Require(Addons::SubtitleQueryProblem({ std::nullopt, std::nullopt, std::wstring(1025, L'f') })
                    == std::optional<std::wstring>{ L"filename too long" },
                "an overlong filename passed");
        auto const sent = Addons::SubtitleExtras({ std::wstring{ L"0123456789abcdef" }, std::uint64_t{ 42 }, std::wstring{ L"a b.mkv" } });
        Require(sent == Addons::ExtraArgs{ { L"videoHash", L"0123456789abcdef" }, { L"videoSize", L"42" }, { L"filename", L"a b.mkv" } },
                "subtitle extras are not in the reference's order");
        Require(Addons::SubtitleExtras({ std::wstring{}, std::uint64_t{ 0 }, std::wstring{} }).empty(), "empty subtitle values were sent");
    }

    void TestNextVideoOrder()
    {
        auto const key = [](std::wstring id, std::optional<double> season, std::optional<double> episode,
                             std::optional<std::int64_t> released = std::nullopt)
        {
            return Addons::VideoOrderKey{ std::move(id), season, episode, released };
        };
        // Specials (season 0) sort after every regular season.
        std::vector<Addons::VideoOrderKey> const show{
            key(L"s0e1", 0, 1), key(L"s2e1", 2, 1), key(L"s1e2", 1, 2), key(L"s1e1", 1, 1),
        };
        Require(Addons::NextVideoIndex(show, L"s1e1", 0) == std::optional<std::size_t>{ 2 }, "episodes were not in order");
        Require(Addons::NextVideoIndex(show, L"s1e2", 0) == std::optional<std::size_t>{ 1 }, "the next season did not follow");
        Require(Addons::NextVideoIndex(show, L"s2e1", 0) == std::optional<std::size_t>{ 0 }, "specials did not come last");
        Require(!Addons::NextVideoIndex(show, L"s0e1", 0), "the last video had a follower");
        Require(!Addons::NextVideoIndex(show, L"missing", 0), "an unknown video had a follower");

        std::vector<Addons::VideoOrderKey> const unaired{ key(L"a", 1, 1), key(L"b", 1, 2, 5000) };
        Require(!Addons::NextVideoIndex(unaired, L"a", 4999), "an unreleased episode was offered");
        Require(Addons::NextVideoIndex(unaired, L"a", 5000) == std::optional<std::size_t>{ 1 }, "a released episode was hidden");

        // Without numbers on every video the list order stands.
        std::vector<Addons::VideoOrderKey> const loose{ key(L"x", std::nullopt, std::nullopt), key(L"y", 1, 1) };
        Require(Addons::NextVideoIndex(loose, L"x", 0) == std::optional<std::size_t>{ 1 }, "an unnumbered list was reordered");

        Require(Addons::ParseIsoDateMs(L"1970-01-02") == std::optional<std::int64_t>{ 86400000 }, "a date-only release was misread");
        Require(Addons::ParseIsoDateMs(L"2024-05-01T00:00:00.000Z") == std::optional<std::int64_t>{ 1714521600000 }, "an ISO release was misread");
        Require(Addons::ParseIsoDateMs(L"2024-05-01T01:00:00+01:00") == std::optional<std::int64_t>{ 1714521600000 }, "an offset was ignored");
        Require(!Addons::ParseIsoDateMs(L"soon") && !Addons::ParseIsoDateMs(L"2024-02-30"), "an unreadable date was read");
    }

    void TestNormalizeRules()
    {
        Require(Addons::SafeAddonName(std::wstring_view{ L"  Torrentio \n\t v2 " }) == L"Torrentio v2", "an addon name was not cleaned");
        Require(Addons::SafeAddonName(std::wstring_view{ L"see HTTPS://x.test" }) == L"Addon", "a URL in a name was shown");
        Require(Addons::SafeAddonName(std::wstring_view{ L"a/b" }) == L"Addon", "a path in a name was shown");
        Require(Addons::SafeAddonName(std::wstring_view{ L"key " + std::wstring(32, L'a') }) == L"Addon", "a token in a name was shown");
        Require(Addons::SafeAddonName(std::wstring_view{ std::wstring(31, L'a') }) == std::wstring(31, L'a'), "a short run was taken for a token");
        std::wstring words;
        for (int index = 0; index < 20; ++index) words += L"word ";
        Require(Addons::SafeAddonName(std::wstring_view{ words }).size() == 80, "a long name was not cut");
        Require(Addons::SafeAddonName(std::nullopt) == L"Addon" && Addons::SafeAddonName(std::wstring_view{ L"   " }) == L"Addon",
                "a missing name did not read Addon");

        auto const streams = Addons::NormalizeStreamsResponse(Json::JsonValue::Parse(LR"({"streams":[
            {"url":"https://cdn.test/a.mkv","name":"A","title":"","extra":1,"behaviorHints":{"notWebReady":true,"bingeGroup":"g","videoSize":12.5,
             "proxyHeaders":{"request":{"Referer":"https://x.test","Bad\r\nName":"v","Empty":""},"response":{}},"filename":"a.mkv","videoHash":"h"}},
            {"url":"https://cdn.test/b.mkv","infoHash":"abc"},
            {"url":"ftp://cdn.test/c.mkv"},
            {"url":"https://cdn.test/delete_all_watchlist","name":"x"},
            {"url":"https://cdn.test/d.mkv","name":"Delete all files"},
            {"url":"https://cdn.test/e.mkv","behaviorHints":{"other":true},"subtitles":[{"id":"1","url":"https://s.test/1.srt","lang":"eng"},{"id":"","url":"https://s.test/2.srt","lang":"eng"}]}
        ]})"));
        Require(streams.Size() == 2, "unplayable or acting streams were kept");
        auto const first = streams.GetObjectAt(0);
        Require(!first.HasKey(L"extra") && first.GetNamedString(L"title").empty(), "fields were not kept as the reference keeps them");
        auto const hints = first.GetNamedObject(L"behaviorHints");
        Require(hints.GetNamedBoolean(L"notWebReady") && !hints.HasKey(L"videoSize") && hints.GetNamedString(L"videoHash") == L"h",
                "behaviour hints were not normalised");
        auto const request = hints.GetNamedObject(L"proxyHeaders").GetNamedObject(L"request");
        Require(request.Size() == 2 && request.HasKey(L"Referer") && request.HasKey(L"Empty"), "a header with a newline in its name was kept");
        Require(!hints.GetNamedObject(L"proxyHeaders").HasKey(L"response"), "an empty response header map was kept");
        auto const last = streams.GetObjectAt(1);
        Require(!last.HasKey(L"behaviorHints") && last.GetNamedArray(L"subtitles").Size() == 1, "empty hints or a subtitle without an id were kept");

        auto threw = false;
        try
        {
            static_cast<void>(Addons::NormalizeStreamsResponse(Json::JsonValue::Parse(LR"({"nope":true})")));
        }
        catch (Addons::AddonRequestFailure const& failure)
        {
            threw = failure.Failure().Kind == Addons::AddonFailureKind::InvalidResponse;
        }
        Require(threw, "a streams answer without streams was not an invalid response");

        Require(Addons::ParseManifest(Json::JsonValue::Parse(LR"({"id":"x","version":"","name":"X","types":[],"resources":[],"catalogs":[]})")).has_value(),
                "a manifest with an empty version was refused, as the reference accepts it");
        Require(!Addons::ParseManifest(Json::JsonValue::Parse(LR"({"id":"x","version":"1","name":"X","resources":[],"catalogs":[]})")),
                "a manifest without types was accepted");
        Require(!Addons::ParseManifest(Json::JsonValue::Parse(LR"({"id":"x","version":"1","name":"X","types":[],"resources":[{"types":[]}],"catalogs":[]})")),
                "a resource without a name was accepted");
        Require(!Addons::ParseManifest(Json::JsonValue::Parse(LR"({"id":"x","version":"1","name":"X","types":[],"resources":[],"catalogs":[{"type":"movie"}]})")),
                "a catalog without an id was accepted");

        auto const classified = Addons::ClassifyAddonFailure({ Addons::AddonFailureKind::UpstreamHttp, std::uint16_t{ 503 } });
        Require(classified.Code == L"upstream_http" && classified.Status == std::optional<std::uint16_t>{ std::uint16_t{ 503 } }
                    && classified.Message == L"Addon returned HTTP 503.",
                "an HTTP failure was not classified as the reference does");
        Require(Addons::ClassifyAddonFailure({ Addons::AddonFailureKind::UpstreamHttp, std::uint16_t{ 42 } }).Message == L"Addon returned an HTTP error.",
                "an impossible status was reported");

        auto const filtered = Addons::WithoutActionEntries(Json::JsonValue::Parse(
            LR"([{"id":"tt1","name":"One"},{"id":"dltorbox","name":"x"},{"id":"tt2","name":"Deleted Scenes"}])"));
        Require(filtered && filtered->Size() == 2, "an action entry was kept or a title dropped");
        Require(!Addons::WithoutActionEntries(Json::JsonValue::Parse(LR"([null])")), "a null meta did not fail the catalog");
    }

    /* resolveMetaWithCast.test.ts */

    Addons::ResolvableAddon Cinemeta()
    {
        return Addons::MakeResolvableAddon(L"a1", L"https://cinemeta.test/manifest.json", Json::JsonObject::Parse(
            LR"({"id":"com.linvo.cinemeta","version":"1.0.0","name":"Cinemeta","resources":["catalog","meta"],"types":["movie","series"],"idPrefixes":["tt"],"catalogs":[]})"));
    }

    Addons::ResolvableAddon Tmdb()
    {
        return Addons::MakeResolvableAddon(L"a2", L"https://tmdb.test/manifest.json", Json::JsonObject::Parse(
            LR"({"id":"tmdb-addon","version":"3.1.7","name":"The Movie Database Addon","resources":["catalog","meta"],"types":["movie","series"],"idPrefixes":["tmdb:"],"catalogs":[]})"));
    }

    constexpr wchar_t Pictured[] =
        LR"([{"name":"Ahaan Panday","character":"Krish Kapoor","photo":"https://image.tmdb.org/a.jpg"},)"
        LR"({"name":"Aneet Padda","character":"Vaani Batra","photo":"https://image.tmdb.org/b.jpg"}])";

    Addons::ResolveContext Context(std::shared_ptr<AddonTests::FakeFetcher> const& fetcher)
    {
        return Addons::ResolveContext{ fetcher, std::chrono::milliseconds{ 2000 } };
    }

    void TestNamesOnlyMetaGainsTheTmdbCast()
    {
        auto const fetcher = std::make_shared<AddonTests::FakeFetcher>(std::map<std::wstring, Route>{
            { L"https://cinemeta.test/meta/movie/tt28037987.json", AddonTests::Json(
                LR"({"meta":{"id":"tt28037987","type":"movie","name":"Saiyaara","cast":["Ahaan Panday"],"awards":"40 wins"}})") },
            { L"https://tmdb.test/meta/movie/tt28037987.json", AddonTests::Json(
                std::wstring{ LR"({"meta":{"id":"tmdb:1","type":"movie","name":"Saiyaara","app_extras":{"cast":)" } + Pictured + L"}}}") },
        });
        auto const answer = Addons::ResolveMetaWithCastAsync({ Cinemeta(), Tmdb() }, L"movie", L"tt28037987", Context(fetcher)).get();
        Require(answer.has_value(), "the meta was not resolved");
        auto const meta = answer->GetNamedObject(L"meta");
        Require(meta.GetNamedString(L"id") == L"tt28037987", "the primary addon did not answer the title");
        Require(meta.GetNamedString(L"awards") == L"40 wins", "the primary addon's fields were lost");
        Require(meta.GetNamedObject(L"app_extras").GetNamedArray(L"cast").Stringify() == Json::JsonArray::Parse(Pictured).Stringify(),
                "the pictured cast was not added");
    }

    void TestFailingOrMissingTmdbLeavesTheMeta()
    {
        constexpr wchar_t meta[] = LR"({"id":"tt1","type":"movie","name":"X","cast":["A"]})";
        auto const fetcher = std::make_shared<AddonTests::FakeFetcher>(std::map<std::wstring, Route>{
            { L"https://cinemeta.test/meta/movie/tt1.json", AddonTests::Json(std::wstring{ L"{\"meta\":" } + meta + L"}") },
        });
        auto const expected = Json::JsonObject::Parse(meta).Stringify();
        auto const withTmdb = Addons::ResolveMetaWithCastAsync({ Cinemeta(), Tmdb() }, L"movie", L"tt1", Context(fetcher)).get();
        Require(withTmdb && withTmdb->GetNamedObject(L"meta").Stringify() == expected, "a failing TMDB addon changed the meta");
        auto const without = Addons::ResolveMetaWithCastAsync({ Cinemeta() }, L"movie", L"tt1", Context(fetcher)).get();
        Require(without && without->GetNamedObject(L"meta").Stringify() == expected, "a missing TMDB addon changed the meta");
    }

    void TestOnlyImdbIdsGoToTmdbForACast()
    {
        auto const fetcher = std::make_shared<AddonTests::FakeFetcher>(std::map<std::wstring, Route>{
            { L"https://tmdb.test/meta/movie/tmdb%3A5.json", AddonTests::Json(LR"({"meta":{"id":"tmdb:5","type":"movie","name":"Y"}})") },
        });
        static_cast<void>(Addons::ResolveMetaWithCastAsync({ Cinemeta(), Tmdb() }, L"movie", L"tmdb:5", Context(fetcher)).get());
        auto const calls = fetcher->Calls();
        auto const toTmdb = std::count_if(calls.begin(), calls.end(), [](std::wstring const& url) { return url.starts_with(L"https://tmdb.test/"); });
        Require(toTmdb == 1, "a non-IMDb id was sent to TMDB for a cast");
    }

    void TestCastIsBounded()
    {
        auto const cast = Addons::SanitizeCast(Json::JsonValue::Parse(LR"([
            {"name":"  Ahaan Panday ","character":"Krish","photo":"https://img.test/a.jpg"},
            {"name":"No Photo","photo":"http://img.test/plain.jpg"},
            {"name":"","photo":"https://img.test/c.jpg"},
            {"name":")" + std::wstring(201, L'x') + LR"("},
            "not a person"])"));
        Require(cast.has_value(), "a pictured cast was dropped");
        Require(cast->Stringify() == Json::JsonArray::Parse(
                    LR"([{"name":"Ahaan Panday","character":"Krish","photo":"https://img.test/a.jpg"},{"name":"No Photo"}])").Stringify(),
                "the cast was not bounded as the reference bounds it");
        Require(!Addons::SanitizeCast(Json::JsonValue::Parse(LR"([{"name":"Nobody pictured"}])")), "a cast without pictures was kept");
        Require(!Addons::SanitizeCast(Json::JsonValue::CreateStringValue(L"nope")), "a cast that is not a list was kept");
    }

    void TestAnswerWithoutAMetaIsAMiss()
    {
        auto const second = Addons::MakeResolvableAddon(L"a3", L"https://second.test/manifest.json", Cinemeta().Manifest);
        for (auto const* empty : { LR"({"meta":null})", LR"({"meta":{}})", LR"({})" })
        {
            auto const fetcher = std::make_shared<AddonTests::FakeFetcher>(std::map<std::wstring, Route>{
                { L"https://cinemeta.test/meta/series/tt1.json", AddonTests::Json(empty) },
                { L"https://second.test/meta/series/tt1.json", AddonTests::Json(LR"({"meta":{"id":"tt1","type":"series","name":"Real"}})") },
            });
            auto const answer = Addons::ResolveMetaWithCastAsync({ Cinemeta(), second }, L"series", L"tt1", Context(fetcher)).get();
            Require(answer && answer->GetNamedObject(L"meta").GetNamedString(L"name") == L"Real", "an empty meta was taken as an answer");
            Require(fetcher->Calls().size() == 2, "the next addon was not asked after an empty meta");
        }
    }

    void TestNoAddonNamingTheTitleIsNothing()
    {
        auto const fetcher = std::make_shared<AddonTests::FakeFetcher>(std::map<std::wstring, Route>{
            { L"https://cinemeta.test/meta/series/tt1.json", AddonTests::Json(LR"({"meta":null})") },
        });
        Require(!Addons::ResolveMetaWithCastAsync({ Cinemeta() }, L"series", L"tt1", Context(fetcher)).get(),
                "an empty meta resolved to something");
    }

    /* Fan-out, failures and cancellation */

    Addons::ResolvableAddon StreamAddon(std::wstring const& id, std::wstring const& host)
    {
        return Addons::MakeResolvableAddon(id, L"https://" + host + L"/manifest.json", Json::JsonObject::Parse(
            L"{\"id\":\"" + host + L"\",\"version\":\"1\",\"name\":\"" + host
            + L"\",\"resources\":[\"stream\",\"subtitles\"],\"types\":[\"movie\"],\"catalogs\":[],\"idPrefixes\":[\"tt\"]}"));
    }

    void TestStreamFailuresCarryTheirStatus()
    {
        auto const fetcher = std::make_shared<AddonTests::FakeFetcher>(std::map<std::wstring, Route>{
            { L"https://down.test/stream/movie/tt1.json", AddonTests::Status(503, L"down") },
            { L"https://slow.test/stream/movie/tt1.json", AddonTests::Hang() },
            { L"https://empty.test/stream/movie/tt1.json", AddonTests::Json(LR"({"streams":[]})") },
        });
        auto const answer = Addons::ResolveStreamsAsync(
            { StreamAddon(L"d", L"down.test"), StreamAddon(L"s", L"slow.test"), StreamAddon(L"e", L"empty.test") },
            L"movie", L"tt1", Addons::ResolveContext{ fetcher, std::chrono::milliseconds{ 50 } }).get();
        Require(answer.GetNamedArray(L"results").Size() == 0, "an addon with no playable stream was listed");
        auto const errors = answer.GetNamedArray(L"errors");
        Require(errors.Size() == 2, "the failures were not both reported");
        auto const down = errors.GetObjectAt(0);
        Require(down.GetNamedString(L"id") == L"d" && down.GetNamedString(L"code") == L"upstream_http"
                    && down.GetNamedNumber(L"status") == 503 && down.GetNamedString(L"message") == L"Addon returned HTTP 503.",
                "an HTTP failure lost its status");
        auto const slow = errors.GetObjectAt(1);
        Require(slow.GetNamedString(L"code") == L"timeout" && !slow.HasKey(L"status"), "a timeout was not reported as one");

        auto const subtitles = Addons::ResolveSubtitlesAsync(
            { StreamAddon(L"e", L"empty.test") }, L"movie", L"tt1", {}, Addons::ResolveContext{ fetcher, std::chrono::milliseconds{ 50 } }).get();
        Require(subtitles.GetNamedArray(L"results").Size() == 0 && subtitles.GetNamedArray(L"errors").Size() == 1
                    && !subtitles.GetNamedBoolean(L"hashMatched"),
                "a subtitle addon answering a 404 was not reported");
    }

    void TestCancelledResolutionRejects()
    {
        auto const fetcher = std::make_shared<AddonTests::FakeFetcher>(std::map<std::wstring, Route>{
            { L"https://slow.test/stream/movie/tt1.json", AddonTests::Hang() },
        });
        concurrency::cancellation_token_source source;
        Addons::ResolveContext context{ fetcher, std::chrono::milliseconds{ 10000 } };
        context.Cancellation = source.get_token();
        auto pending = Addons::ResolveStreamsAsync({ StreamAddon(L"s", L"slow.test") }, L"movie", L"tt1", context);
        Sleep(20);
        auto const started = std::chrono::steady_clock::now();
        source.cancel();
        auto cancelled = false;
        try
        {
            static_cast<void>(pending.get());
        }
        catch (winrt::hresult_canceled const&)
        {
            cancelled = true;
        }
        Require(cancelled, "a cancelled resolution did not reject as cancelled");
        Require(std::chrono::steady_clock::now() - started < std::chrono::seconds{ 2 }, "a cancelled resolution kept waiting for its budget");
    }

    void TestManifestsAreAllOrNothing()
    {
        auto const fetcher = std::make_shared<AddonTests::FakeFetcher>(std::map<std::wstring, Route>{
            { L"https://a.test/manifest.json", AddonTests::Json(LR"({"id":"a","version":"1","name":"A","types":[],"resources":[],"catalogs":[]})") },
            { L"https://b.test/manifest.json", AddonTests::Json(LR"({"id":"b","version":"1","name":"B","types":[],"resources":[],"catalogs":[]})") },
            { L"https://bad.test/manifest.json", AddonTests::Json(L"not json") },
        });
        auto const ok = Addons::FetchManifestsAsync({ L"https://a.test/manifest.json", L"https://b.test" }, fetcher, std::chrono::milliseconds{ 500 }).get();
        Require(!ok.Error && ok.Entries.size() == 2 && ok.Entries[1].first == L"https://b.test"
                    && ok.Entries[1].second.GetNamedString(L"id") == L"b",
                "valid manifests were not all returned in order");
        auto const failed = Addons::FetchManifestsAsync(
            { L"https://a.test/manifest.json", L"https://missing.test/manifest.json", L"https://bad.test/manifest.json" },
            fetcher, std::chrono::milliseconds{ 500 }).get();
        Require(failed.Entries.empty() && failed.Error == std::optional<std::wstring>{ L"could not fetch a valid manifest for https://missing.test/manifest.json" },
                "a failed manifest did not fail the whole set with the first failure");
    }

    /* The network fetcher against a loopback server */

    struct Reply final
    {
        int Status{ 200 };
        std::string Body;
        // Extra header lines, each ending in CRLF.
        std::string Headers;
        int StallBeforeHeadersMs{};
        int StallBeforeBodyMs{};
        std::optional<std::size_t> DeclaredLength;
    };

    // One request per connection. A stalled reply waits until its time is up
    // or the client closes the connection, so a cancelled request frees the
    // server at once.
    class LoopbackAddonServer final
    {
    public:
        using Responder = std::function<Reply(std::string const& path)>;

        explicit LoopbackAddonServer(Responder responder)
            : m_responder(std::move(responder))
        {
            WSADATA data{};
            if (WSAStartup(MAKEWORD(2, 2), &data) != 0) throw std::runtime_error{ "Winsock could not start for the addon test." };
            m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            int addressSize = sizeof(address);
            if (m_socket == INVALID_SOCKET
                || bind(m_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR
                || listen(m_socket, SOMAXCONN) == SOCKET_ERROR
                || getsockname(m_socket, reinterpret_cast<sockaddr*>(&address), &addressSize) == SOCKET_ERROR)
            {
                if (m_socket != INVALID_SOCKET) closesocket(m_socket);
                WSACleanup();
                throw std::runtime_error{ "The addon test server could not listen." };
            }
            m_port = ntohs(address.sin_port);
            m_thread = std::jthread([this]() { Run(); });
        }

        ~LoopbackAddonServer()
        {
            m_stopping.store(true);
            auto const wake = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (wake != INVALID_SOCKET)
            {
                sockaddr_in address{};
                address.sin_family = AF_INET;
                address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                address.sin_port = htons(m_port);
                static_cast<void>(connect(wake, reinterpret_cast<sockaddr*>(&address), sizeof(address)));
                closesocket(wake);
            }
            if (m_thread.joinable()) m_thread.join();
            closesocket(m_socket);
            WSACleanup();
        }

        LoopbackAddonServer(LoopbackAddonServer const&) = delete;
        LoopbackAddonServer& operator=(LoopbackAddonServer const&) = delete;

        [[nodiscard]] std::wstring Url(std::wstring_view path) const
        {
            return L"http://127.0.0.1:" + std::to_wstring(m_port) + std::wstring{ path };
        }

        [[nodiscard]] std::vector<std::string> Requests() const
        {
            std::scoped_lock const lock{ m_mutex };
            return m_requests;
        }

    private:
        // True when the client closed the connection before the time was up.
        bool WaitOrClosed(SOCKET client, int milliseconds) const
        {
            auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{ milliseconds };
            while (!m_stopping.load() && std::chrono::steady_clock::now() < deadline)
            {
                fd_set readable;
                FD_ZERO(&readable);
                FD_SET(client, &readable);
                timeval wait{ 0, 20000 };
                auto const ready = select(0, &readable, nullptr, nullptr, &wait);
                if (ready == SOCKET_ERROR) return true;
                if (ready > 0)
                {
                    char probe{};
                    if (recv(client, &probe, 1, MSG_PEEK) <= 0) return true;
                }
            }
            return m_stopping.load();
        }

        static bool SendAll(SOCKET client, std::string const& text)
        {
            std::size_t sent{};
            while (sent < text.size())
            {
                auto const count = send(client, text.data() + sent, static_cast<int>(text.size() - sent), 0);
                if (count <= 0) return false;
                sent += static_cast<std::size_t>(count);
            }
            return true;
        }

        void Run() noexcept
        {
            while (!m_stopping.load())
            {
                auto const client = accept(m_socket, nullptr, nullptr);
                if (client == INVALID_SOCKET) continue;
                try
                {
                    Serve(client);
                }
                catch (...)
                {
                }
                shutdown(client, SD_BOTH);
                closesocket(client);
            }
        }

        void Serve(SOCKET client)
        {
            std::string request;
            std::array<char, 2048> buffer{};
            while (request.find("\r\n\r\n") == std::string::npos && request.size() < 64u * 1024u)
            {
                auto const count = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
                if (count <= 0) return;
                request.append(buffer.data(), static_cast<std::size_t>(count));
            }
            auto const pathStart = request.find(' ');
            auto const pathEnd = request.find(' ', pathStart + 1);
            if (pathStart == std::string::npos || pathEnd == std::string::npos) return;
            auto const path = request.substr(pathStart + 1, pathEnd - pathStart - 1);
            Reply reply;
            {
                std::scoped_lock const lock{ m_mutex };
                m_requests.push_back(request);
                reply = m_responder(path);
            }
            if (reply.StallBeforeHeadersMs > 0 && WaitOrClosed(client, reply.StallBeforeHeadersMs)) return;
            auto const length = reply.DeclaredLength.value_or(reply.Body.size());
            auto const head = "HTTP/1.1 " + std::to_string(reply.Status) + " Test\r\nContent-Type: application/json\r\nContent-Length: "
                + std::to_string(length) + "\r\n" + reply.Headers + "Connection: close\r\n\r\n";
            if (!SendAll(client, head)) return;
            if (reply.StallBeforeBodyMs > 0 && WaitOrClosed(client, reply.StallBeforeBodyMs)) return;
            static_cast<void>(SendAll(client, reply.Body));
        }

        mutable std::mutex m_mutex;
        Responder m_responder;
        std::vector<std::string> m_requests;
        SOCKET m_socket{ INVALID_SOCKET };
        std::uint16_t m_port{};
        std::atomic_bool m_stopping{};
        std::jthread m_thread;
    };

    std::optional<Addons::AddonFailureKind> FailureKindOf(std::function<void()> const& call)
    {
        try
        {
            call();
        }
        catch (Addons::AddonRequestFailure const& failure)
        {
            return failure.Failure().Kind;
        }
        return std::nullopt;
    }

    std::string Lowered(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
        return text;
    }

    void TestFetcherSafety()
    {
        LoopbackAddonServer server{ [](std::string const& path) -> Reply
        {
            if (path == "/ok.json") return { 200, R"({"a":1})", "Set-Cookie: halo=1; Path=/\r\n" };
            if (path == "/again.json") return { 200, R"({"b":2})" };
            if (path == "/slow-headers.json") return { 200, "{}", {}, 5000 };
            if (path == "/slow-body.json") return { 200, std::string(100, ' '), {}, 0, 5000 };
            if (path == "/huge.json") return { 200, {}, {}, 0, 3000, std::size_t{ 17u * 1024u * 1024u } };
            if (path == "/start") return { 302, {}, "Location: /final.json\r\n" };
            if (path == "/final.json") return { 200, R"({"ok":true})" };
            if (path == "/to-ftp") return { 302, {}, "Location: ftp://127.0.0.1/x.json\r\n" };
            if (path == "/loop") return { 302, {}, "Location: /loop\r\n" };
            return { 404, R"({"error":"missing"})" };
        } };
        auto const fetcher = std::make_shared<Addons::HttpAddonFetcher>();
        auto const none = concurrency::cancellation_token::none();
        constexpr std::chrono::milliseconds budget{ 300 };

        auto const ok = fetcher->GetAsync(server.Url(L"/ok.json"), std::chrono::seconds{ 5 }, none).get();
        Require(ok.Status == 200 && ok.Body == LR"({"a":1})", "a plain answer was not returned");
        auto const again = fetcher->GetAsync(server.Url(L"/again.json"), std::chrono::seconds{ 5 }, none).get();
        Require(again.Status == 200, "a second request failed");
        for (auto const& request : server.Requests())
        {
            auto const lowered = Lowered(request);
            Require(lowered.find("\r\ncookie:") == std::string::npos, "a cookie an addon set was sent back");
            Require(lowered.find("\r\nauthorization:") == std::string::npos, "an addon request carried an Authorization header");
            Require(lowered.starts_with("get "), "an addon request was not a GET");
        }

        auto started = std::chrono::steady_clock::now();
        Require(FailureKindOf([&fetcher, &server, &none, budget]()
                {
                    static_cast<void>(fetcher->GetAsync(server.Url(L"/slow-headers.json"), budget, none).get());
                }) == Addons::AddonFailureKind::Timeout,
                "an addon that never answered did not time out");
        Require(std::chrono::steady_clock::now() - started < std::chrono::milliseconds{ 2500 }, "the budget did not cancel a request waiting for headers");

        started = std::chrono::steady_clock::now();
        Require(FailureKindOf([&fetcher, &server, &none, budget]()
                {
                    static_cast<void>(fetcher->GetAsync(server.Url(L"/slow-body.json"), budget, none).get());
                }) == Addons::AddonFailureKind::Timeout,
                "an addon that stalled its body did not time out");
        Require(std::chrono::steady_clock::now() - started < std::chrono::milliseconds{ 2500 }, "the budget did not cancel a stalled body");

        Require(FailureKindOf([&fetcher, &server, &none]()
                {
                    static_cast<void>(fetcher->GetAsync(server.Url(L"/huge.json"), std::chrono::seconds{ 5 }, none).get());
                }) == Addons::AddonFailureKind::InvalidResponse,
                "an answer past the size bound was read");

        auto const redirected = fetcher->GetAsync(server.Url(L"/start"), std::chrono::seconds{ 5 }, none).get();
        Require(redirected.Status == 200 && redirected.Body == LR"({"ok":true})", "a relative redirect was not followed");

        Require(FailureKindOf([&fetcher, &server, &none]()
                {
                    static_cast<void>(fetcher->GetAsync(server.Url(L"/to-ftp"), std::chrono::seconds{ 5 }, none).get());
                }) == Addons::AddonFailureKind::BlockedTarget,
                "a redirect away from http was followed");

        auto const before = server.Requests().size();
        Require(FailureKindOf([&fetcher, &server, &none]()
                {
                    static_cast<void>(fetcher->GetAsync(server.Url(L"/loop"), std::chrono::seconds{ 5 }, none).get());
                }) == Addons::AddonFailureKind::Unavailable,
                "a redirect loop did not fail");
        Require(server.Requests().size() - before == Addons::HttpAddonFetcher::MaximumRedirects + 1, "redirects were not bounded");

        auto const missing = fetcher->GetAsync(server.Url(L"/missing.json"), std::chrono::seconds{ 5 }, none).get();
        Require(missing.Status == 404 && missing.Body.empty(), "an HTTP failure's body was read");

        Require(FailureKindOf([&fetcher, &none]()
                {
                    static_cast<void>(fetcher->GetAsync(L"ftp://127.0.0.1/manifest.json", std::chrono::seconds{ 5 }, none).get());
                }) == Addons::AddonFailureKind::BlockedTarget,
                "a non-http addon URL was fetched");

        concurrency::cancellation_token_source source;
        auto pending = fetcher->GetAsync(server.Url(L"/slow-headers.json"), std::chrono::seconds{ 10 }, source.get_token());
        Sleep(100);
        started = std::chrono::steady_clock::now();
        source.cancel();
        auto cancelled = false;
        try
        {
            static_cast<void>(pending.get());
        }
        catch (winrt::hresult_canceled const&)
        {
            cancelled = true;
        }
        Require(cancelled, "a cancelled addon request did not reject as cancelled");
        Require(std::chrono::steady_clock::now() - started < std::chrono::milliseconds{ 2500 }, "cancelling did not stop the request");
    }
}

void RunAddonResolveTests()
{
    TestUrlRules();
    TestSupportRules();
    TestRequestLimits();
    TestNextVideoOrder();
    TestNormalizeRules();
    TestNamesOnlyMetaGainsTheTmdbCast();
    TestFailingOrMissingTmdbLeavesTheMeta();
    TestOnlyImdbIdsGoToTmdbForACast();
    TestCastIsBounded();
    TestAnswerWithoutAMetaIsAMiss();
    TestNoAddonNamingTheTitleIsNothing();
    TestStreamFailuresCarryTheirStatus();
    TestCancelledResolutionRejects();
    TestManifestsAreAllOrNothing();
    TestFetcherSafety();
}
