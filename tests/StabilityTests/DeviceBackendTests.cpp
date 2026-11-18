#include "DeviceBackendTests.h"

#include "AddonTestFakes.h"

#include "Api/ApiClient.h"
#include "Api/ApiError.h"
#include "Api/BackendSelector.h"
#include "Api/HaloBackend.h"
#include "Api/HttpExecutor.h"
#include "Services/Auth/ITokenProvider.h"
#include "Services/Device/DeviceBackend.h"
#include "Services/Device/DeviceDocuments.h"
#include "Services/Device/DeviceRecordsPolicy.h"
#include "Services/Device/DeviceStore.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include <wil/resource.h>

namespace
{
    namespace Device = HaloDesktop::Services::Device;
    namespace Dto = HaloDesktop::Api::Dto;
    namespace Json = winrt::Windows::Data::Json;
    using AddonTests::Hang;
    using AddonTests::Require;
    using AddonTests::Route;
    using AddonTests::Status;

    constexpr wchar_t CatalogAddon[] = L"https://catalog.test/manifest.json";
    constexpr wchar_t StreamA[] = L"https://stream-a.test/manifest.json";
    constexpr wchar_t StreamB[] = L"https://stream-b.test/manifest.json";

    std::wstring CatalogManifest()
    {
        return LR"({"id":"catalog","version":"1.0.0","name":"catalog","resources":["catalog","meta"],"types":["movie","series"],)"
               LR"("catalogs":[{"type":"movie","id":"top","name":"Top","extra":[{"name":"search"}]}],"idPrefixes":["tt"]})";
    }

    std::wstring StreamManifest(std::wstring const& id)
    {
        return L"{\"id\":\"" + id + L"\",\"version\":\"1.0.0\",\"name\":\"" + id
            + L"\",\"resources\":[\"stream\",\"subtitles\"],\"types\":[\"movie\",\"series\"],\"catalogs\":[],\"idPrefixes\":[\"tt\"]}";
    }

    struct Harness final
    {
        std::shared_ptr<AddonTests::MemoryStore> Store;
        std::shared_ptr<AddonTests::FakeFetcher> Fetcher;
        std::shared_ptr<Device::DeviceBackend> Backend;
    };

    Harness MakeBackend(
        std::map<std::wstring, Route> const& routes,
        std::chrono::milliseconds timeout = HaloDesktop::Api::Addons::ResolveTimeout)
    {
        std::map<std::wstring, Route> all{
            { CatalogAddon, AddonTests::Json(CatalogManifest()) },
            { StreamA, AddonTests::Json(StreamManifest(L"stream-a")) },
            { StreamB, AddonTests::Json(StreamManifest(L"stream-b")) },
        };
        for (auto const& [url, route] : routes) all.insert_or_assign(url, route);

        Harness harness;
        harness.Store = std::make_shared<AddonTests::MemoryStore>();
        harness.Fetcher = std::make_shared<AddonTests::FakeFetcher>(std::move(all));
        auto const clock = std::make_shared<std::int64_t>(1000);
        auto const ids = std::make_shared<int>(0);
        harness.Backend = std::make_shared<Device::DeviceBackend>(Device::DeviceBackendOptions{
            .Store = harness.Store,
            .Fetcher = harness.Fetcher,
            .Now = [clock]() { return ++*clock; },
            .NewId = [ids]() { return L"id-" + std::to_wstring(++*ids); },
            .Timeout = timeout,
        });
        return harness;
    }

    std::vector<winrt::hstring> Urls(std::initializer_list<std::wstring> urls)
    {
        std::vector<winrt::hstring> result;
        for (auto const& url : urls) result.emplace_back(url);
        return result;
    }

    // The refusal's message, after checking it carries the expected HTTP status.
    std::wstring RejectsWithStatus(std::function<void()> const& call, std::uint16_t status, char const* message)
    {
        try
        {
            call();
        }
        catch (winrt::hresult_error const& error)
        {
            auto const actual = HaloDesktop::Api::ApiError::HttpStatus(error.code());
            Require(actual.has_value() && *actual == status, message);
            return std::wstring{ error.message() };
        }
        Require(false, message);
        return {};
    }

    bool IsUnreadable(std::function<void()> const& call)
    {
        try
        {
            call();
        }
        catch (winrt::hresult_error const& error)
        {
            return HaloDesktop::Api::ApiError::IsDeviceDataUnreadable(error.code());
        }
        return false;
    }

    Dto::LibraryRow Library(std::wstring const& id, std::int64_t updatedAt)
    {
        return { winrt::hstring{ id }, L"movie", winrt::hstring{ id }, std::nullopt, 1, std::nullopt, updatedAt };
    }

    Dto::WatchEntry Watch(std::wstring const& videoId, std::int64_t updatedAt)
    {
        return {
            winrt::hstring{ videoId }, winrt::hstring{ L"movie:" + videoId }, 10, 100, false,
            std::nullopt, std::nullopt, updatedAt, std::nullopt,
        };
    }

    bool ContainsText(std::wstring const& text, std::wstring_view part)
    {
        return text.find(part) != std::wstring::npos;
    }

    /* Addons */

    void TestInstallKeepsIdsAndFetchesOnce()
    {
        auto const harness = MakeBackend({});
        harness.Backend->PutAddonsAsync(Urls({ CatalogAddon, StreamA })).get();
        auto const listed = harness.Backend->GetAddonsAsync().get();
        Require(listed.Global.empty(), "a device listed global addons");
        Require(listed.User.size() == 2
                    && listed.User[0].Id == L"id-1" && listed.User[0].TransportUrl == winrt::hstring{ CatalogAddon }
                    && listed.User[0].Position == 0 && listed.User[0].ManifestId == L"catalog"
                    && listed.User[1].Id == L"id-2" && listed.User[1].TransportUrl == winrt::hstring{ StreamA }
                    && listed.User[1].Position == 1 && listed.User[1].ManifestId == L"stream-a",
                "installed addons were not listed as the user's own, in order");

        // Kept URLs keep their id and are not fetched again; dropped ones go;
        // order follows the list.
        harness.Fetcher->ClearCalls();
        harness.Backend->PutAddonsAsync(Urls({ StreamB, CatalogAddon })).get();
        auto const next = harness.Backend->GetAddonsAsync().get();
        Require(next.User.size() == 2
                    && next.User[0].Id == L"id-3" && next.User[0].TransportUrl == winrt::hstring{ StreamB } && next.User[0].Position == 0
                    && next.User[1].Id == L"id-1" && next.User[1].TransportUrl == winrt::hstring{ CatalogAddon } && next.User[1].Position == 1,
                "a kept addon lost its id or the order did not follow the list");
        auto const calls = harness.Fetcher->Calls();
        Require(calls.size() == 1 && calls[0] == StreamB, "a kept addon's manifest was fetched again");
    }

    void TestOneBadManifestFailsTheInstall()
    {
        auto const harness = MakeBackend({ { L"https://broken.test/manifest.json", AddonTests::Json(LR"({"id":"","name":"x"})") } });
        harness.Backend->PutAddonsAsync(Urls({ CatalogAddon })).get();
        auto const message = RejectsWithStatus([&harness]()
        {
            harness.Backend->PutAddonsAsync(Urls({ CatalogAddon, StreamA, L"https://broken.test/manifest.json" })).get();
        }, 400, "a bad manifest did not refuse the install with a 400");
        Require(ContainsText(message, L"could not fetch a valid manifest for https://broken.test"),
                "the refusal did not name the addon whose manifest failed");
        auto const listed = harness.Backend->GetAddonsAsync().get();
        Require(listed.User.size() == 1 && listed.User[0].TransportUrl == winrt::hstring{ CatalogAddon },
                "a failed install changed the list");
    }

    void TestAddonListsAreRefused()
    {
        auto const harness = MakeBackend({});
        RejectsWithStatus([&harness]() { harness.Backend->PutAddonsAsync(Urls({ StreamA, StreamA })).get(); },
                          400, "a duplicate addon was accepted");
        RejectsWithStatus([&harness]() { harness.Backend->PutAddonsAsync(Urls({ L"ftp://addon.test/manifest.json" })).get(); },
                          400, "a non-http addon was accepted");
        RejectsWithStatus([&harness]() { harness.Backend->PutAddonsAsync(Urls({ L"not a url" })).get(); },
                          400, "a non-URL addon was accepted");
        std::vector<winrt::hstring> many;
        for (int index = 0; index < 51; ++index) many.emplace_back(L"https://a" + std::to_wstring(index) + L".test/manifest.json");
        RejectsWithStatus([&harness, &many]() { harness.Backend->PutAddonsAsync(many).get(); },
                          400, "more than fifty addons were accepted");
    }

    void TestHiddenCatalogs()
    {
        auto const harness = MakeBackend({
            { L"https://catalog.test/catalog/movie/top.json", AddonTests::Json(LR"({"metas":[{"id":"tt1","type":"movie","name":"One"}]})") },
        });
        harness.Backend->PutAddonsAsync(Urls({ CatalogAddon })).get();
        auto const id = harness.Backend->GetAddonsAsync().get().User.at(0).Id;
        harness.Backend->PatchAddonAsync(id, true).get();
        auto const listed = harness.Backend->GetAddonsAsync().get().User.at(0);
        Require(listed.HideCatalogs && listed.Catalogs.empty(), "a hidden addon still listed its catalogs");
        auto const page = harness.Backend->GetCatalogAsync(id, L"movie", L"top").get();
        Require(page.size() == 1 && page[0].Id == L"tt1", "a hidden addon stopped answering by id");

        harness.Backend->PatchAddonAsync(id, false).get();
        Require(harness.Backend->GetAddonsAsync().get().User.at(0).Catalogs.size() == 1, "showing the catalogs again did not list them");
        RejectsWithStatus([&harness]() { harness.Backend->PatchAddonAsync(L"missing", true).get(); },
                          404, "patching an unknown addon was not a 404");
    }

    // An addon stored before installs required a shown name (blank after
    // trimming) lists under its manifest id and can be removed again; a stored
    // row that still cannot be read costs only itself.
    void TestStoredBlankNamedAddonStaysUsable()
    {
        auto const harness = MakeBackend({});
        auto const blank = Json::JsonObject::Parse(
            LR"({"id":"blank","version":"1","name":"   ","resources":["stream"],"types":["movie"],"catalogs":[]})");
        auto const nameless = Json::JsonObject::Parse(
            LR"({"id":"","version":"1","name":"\t","resources":["stream"],"types":["movie"],"catalogs":[]})");
        auto const unreadable = Json::JsonObject::Parse(
            LR"({"id":"odd","version":"1","name":"Odd","resources":"stream","types":["movie"],"catalogs":[]})");
        harness.Store->WriteAsync(Device::DeviceCollection::Addons, Device::WriteAddonsDocument({
            { L"id-a", L"https://blank.test/manifest.json", blank, false, 1 },
            { L"id-b", L"https://nameless.test/manifest.json", nameless, false, 2 },
            { L"id-c", L"https://odd.test/manifest.json", unreadable, false, 3 },
            { L"id-d", StreamA, Json::JsonObject::Parse(StreamManifest(L"stream-a")), false, 4 },
        })).get();

        auto const listed = harness.Backend->GetAddonsAsync().get();
        Require(listed.User.size() == 3
                    && listed.User[0].Name == L"blank" && listed.User[1].Name == L"Addon"
                    && listed.User[2].Name == L"stream-a",
                "a stored addon with a blank name failed the list or kept no shown name");

        harness.Backend->PutAddonsAsync(Urls({ StreamA })).get();
        auto const after = harness.Backend->GetAddonsAsync().get();
        Require(after.User.size() == 1 && after.User[0].TransportUrl == winrt::hstring{ StreamA },
                "a stored addon with a blank name could not be removed");

        // New installs refuse such a name instead of storing it.
        auto const strict = MakeBackend({ { L"https://spaces.test/manifest.json",
            AddonTests::Json(LR"({"id":"spaces","version":"1","name":"  ","resources":[],"types":[],"catalogs":[]})") } });
        RejectsWithStatus([&strict]() { strict.Backend->PutAddonsAsync(Urls({ L"https://spaces.test/manifest.json" })).get(); },
                          400, "an addon whose name is only spaces was installed");
    }

    /* Resolution */

    void TestCatalogs()
    {
        auto const harness = MakeBackend({
            { L"https://catalog.test/catalog/movie/top/search=dune.json", AddonTests::Json(LR"({"metas":[{"id":"tt2","type":"movie","name":"Dune"}]})") },
        });
        harness.Backend->PutAddonsAsync(Urls({ CatalogAddon })).get();
        auto const id = harness.Backend->GetAddonsAsync().get().User.at(0).Id;
        auto const page = harness.Backend->GetCatalogAsync(id, L"movie", L"top", { { L"search", L"dune" } }).get();
        Require(page.size() == 1 && page[0].Name == L"Dune", "a catalog with extras was not asked with them");
        RejectsWithStatus([&harness]() { harness.Backend->GetCatalogAsync(L"nope", L"movie", L"top").get(); },
                          403, "an unknown addon's catalog was not a 403");
        RejectsWithStatus([&harness, &id]()
        {
            harness.Backend->GetCatalogAsync(id, L"movie", L"top", { { L"search", winrt::hstring{ std::wstring(257, L'x') } } }).get();
        }, 400, "an overlong extra was accepted");
        RejectsWithStatus([&harness, &id]()
        {
            std::vector<std::pair<winrt::hstring, winrt::hstring>> tooMany;
            for (int index = 0; index < 9; ++index) tooMany.emplace_back(L"k" + std::to_wstring(index), L"v");
            harness.Backend->GetCatalogAsync(id, L"movie", L"top", tooMany).get();
        }, 400, "nine extras were accepted");
        RejectsWithStatus([&harness, &id]() { harness.Backend->GetCatalogAsync(id, L"movie", L"missing").get(); },
                          502, "a failing addon catalog was not a 502");
    }

    void TestMetaFirstAddonThatDescribes()
    {
        auto const second = std::wstring{ L"https://meta-two.test/manifest.json" };
        auto const harness = MakeBackend({
            { second, AddonTests::Json(LR"({"id":"meta-two","version":"1.0.0","name":"meta-two","resources":["meta"],"types":["movie","series"],"catalogs":[],"idPrefixes":["tt"]})") },
            { L"https://catalog.test/meta/movie/tt1.json", Status(500, L"boom") },
            { L"https://meta-two.test/meta/movie/tt1.json", AddonTests::Json(LR"({"meta":{"id":"tt1","type":"movie","name":"From two"}})") },
        });
        harness.Backend->PutAddonsAsync(Urls({ StreamA, CatalogAddon, second })).get();
        Require(harness.Backend->GetMetaAsync(L"movie", L"tt1").get().Preview.Name == L"From two",
                "a failing addon was not skipped for the next one that describes the id");
        RejectsWithStatus([&harness]() { harness.Backend->GetMetaAsync(L"movie", L"kitsu:1").get(); },
                          404, "an id no addon describes was not a 404");
    }

    void TestStreamsAskEveryCapableAddon()
    {
        auto const hanging = std::wstring{ L"https://hanging.test/manifest.json" };
        auto const invalid = std::wstring{ L"https://invalid.test/manifest.json" };
        auto const harness = MakeBackend({
            { hanging, AddonTests::Json(StreamManifest(L"hanging")) },
            { invalid, AddonTests::Json(StreamManifest(L"invalid")) },
            { L"https://stream-a.test/stream/movie/tt1.json", AddonTests::Json(
                LR"({"streams":[{"url":"https://cdn.test/a.mkv","name":"A","behaviorHints":{"bingeGroup":"a"}},{"infoHash":"abc"},{"url":"magnet:?xt=1"}]})") },
            { L"https://stream-b.test/stream/movie/tt1.json", Status(503, L"down") },
            { L"https://hanging.test/stream/movie/tt1.json", Hang() },
            { L"https://invalid.test/stream/movie/tt1.json", AddonTests::Json(LR"({"nope":true})") },
        }, std::chrono::milliseconds{ 50 });
        harness.Backend->PutAddonsAsync(Urls({ StreamA, StreamB, hanging, invalid, CatalogAddon })).get();
        auto const result = harness.Backend->GetStreamsAsync(L"movie", L"tt1").get();
        Require(result.Results.size() == 1 && result.Results[0].AddonId == L"id-1" && result.Results[0].AddonName == L"stream-a"
                    && result.Results[0].Streams.size() == 1 && result.Results[0].Streams[0].Url == L"https://cdn.test/a.mkv"
                    && result.Results[0].Streams[0].Name == winrt::hstring{ L"A" }
                    && result.Results[0].Streams[0].BingeGroup == winrt::hstring{ L"a" },
                "unplayable streams were kept or the playable one was lost");
        Require(result.Errors.size() == 3
                    && result.Errors[0].Name == winrt::hstring{ L"stream-b" } && result.Errors[0].Code == winrt::hstring{ L"upstream_http" }
                    && result.Errors[1].Name == winrt::hstring{ L"hanging" } && result.Errors[1].Code == winrt::hstring{ L"timeout" }
                    && result.Errors[2].Name == winrt::hstring{ L"invalid" } && result.Errors[2].Code == winrt::hstring{ L"invalid_response" },
                "failing stream addons were not reported by code, in order");
    }

    void TestSubtitles()
    {
        auto const harness = MakeBackend({
            { L"https://stream-a.test/subtitles/movie/tt1/videoHash=0123456789abcdef&videoSize=42.json", AddonTests::Json(
                LR"json({"subtitles":[{"id":"s1","url":"https://subs.test/1.srt","lang":"eng"},{"id":"s2","url":"javascript:alert(1)","lang":"eng"}]})json") },
            { L"https://stream-a.test/subtitles/movie/tt1.json", AddonTests::Json(LR"({"subtitles":[]})") },
        });
        harness.Backend->PutAddonsAsync(Urls({ StreamA })).get();
        RejectsWithStatus([&harness]() { harness.Backend->GetSubtitlesAsync(L"movie", L"tt1", winrt::hstring{ L"xyz" }, std::nullopt, std::nullopt).get(); },
                          400, "a malformed video hash was accepted");
        RejectsWithStatus([&harness]() { harness.Backend->GetSubtitlesAsync(L"movie", L"tt1", std::nullopt, std::uint64_t{ 0 }, std::nullopt).get(); },
                          400, "a zero video size was accepted");

        auto const matched = harness.Backend->GetSubtitlesAsync(
            L"movie", L"tt1", winrt::hstring{ L"0123456789abcdef" }, std::uint64_t{ 42 }, std::nullopt).get();
        Require(matched.HashMatched, "a hashed subtitle request did not report hash matching");
        Require(matched.Results.size() == 1 && matched.Results[0].Subtitles.size() == 1
                    && matched.Results[0].Subtitles[0].Id == L"s1" && matched.Results[0].Subtitles[0].Url == L"https://subs.test/1.srt",
                "a subtitle with a non-http URL was kept");

        harness.Fetcher->ClearCalls();
        auto const bare = harness.Backend->GetSubtitlesAsync(
            L"movie", L"tt1", winrt::hstring{ L"" }, std::nullopt, winrt::hstring{ L"" }).get();
        Require(!bare.HashMatched, "an empty hash was reported as matched");
        auto const calls = harness.Fetcher->Calls();
        Require(calls.size() == 1 && calls[0] == L"https://stream-a.test/subtitles/movie/tt1.json",
                "empty subtitle values reached the addon");
    }

    void TestNextEpisode()
    {
        auto const harness = MakeBackend({
            { L"https://catalog.test/meta/series/tt9.json", AddonTests::Json(
                LR"({"meta":{"id":"tt9","type":"series","name":"Show","videos":[)"
                LR"({"id":"tt9:1:1","title":"One","season":1,"episode":1},{"id":"tt9:1:2","title":"Two","season":1,"episode":2}]}})") },
            // Ids are URL-encoded in the path.
            { L"https://stream-a.test/stream/series/tt9%3A1%3A2.json", AddonTests::Json(
                LR"({"streams":[{"url":"https://cdn.test/other.mkv","behaviorHints":{"bingeGroup":"other"}},)"
                LR"({"url":"https://cdn.test/two.mkv","behaviorHints":{"bingeGroup":"grp"}}]})") },
        });
        harness.Backend->PutAddonsAsync(Urls({ CatalogAddon, StreamA })).get();
        auto const streamA = harness.Backend->GetAddonsAsync().get().User.at(1).Id;
        auto const matched = harness.Backend->GetNextEpisodeAsync(L"series", L"tt9", L"tt9:1:1", streamA, L"grp").get();
        Require(matched.Video && matched.Video->Id == L"tt9:1:2", "the next episode was not found");
        Require(matched.Stream && matched.Stream->Url == L"https://cdn.test/two.mkv", "the same binge group's stream was not matched");

        auto const unmatched = harness.Backend->GetNextEpisodeAsync(L"series", L"tt9", L"tt9:1:1", streamA, L"none").get();
        Require(unmatched.Video && unmatched.Video->Id == L"tt9:1:2" && !unmatched.Stream,
                "a different binge group matched a stream");

        auto const last = harness.Backend->GetNextEpisodeAsync(L"series", L"tt9", L"tt9:1:2", L"", L"").get();
        Require(!last.Video && !last.Stream, "the last episode had a next one");
        RejectsWithStatus([&harness]() { harness.Backend->GetNextEpisodeAsync(L"series", L"tt404", L"tt404:1:1", L"", L"").get(); },
                          404, "an unknown title's next episode was not a 404");
    }

    /* Library, watch history, settings */

    void TestLibraryLastWriteWins()
    {
        auto const harness = MakeBackend({});
        auto original = Library(L"movie:tt1", 10);
        original.Name = L"Original";
        harness.Backend->PutLibraryAsync({ original }).get();

        auto tie = Library(L"movie:tt1", 10);
        tie.Name = L"Tie";
        auto rows = harness.Backend->PutLibraryAsync({ tie }).get();
        Require(rows.at(0).Name == L"Original", "a tie replaced the stored library row");

        auto renamed = Library(L"movie:tt1", 11);
        renamed.Name = L"Renamed";
        renamed.Type = L"series";
        rows = harness.Backend->PutLibraryAsync({ renamed }).get();
        Require(rows.at(0).Name == L"Renamed" && rows.at(0).Type == L"movie", "a newer row was ignored or changed the type");

        auto removed = Library(L"movie:tt1", 20);
        removed.RemovedAt = 20;
        harness.Backend->PutLibraryAsync({ removed }).get();
        rows = harness.Backend->PutLibraryAsync({ Library(L"movie:tt1", 15) }).get();
        Require(rows.at(0).RemovedAt == std::optional<std::int64_t>{ 20 }, "a stale re-add undid a removal");
        auto const read = harness.Backend->GetLibraryAsync().get();
        Require(read.size() == rows.size() && read.at(0).Id == rows.at(0).Id && read.at(0).RemovedAt == rows.at(0).RemovedAt
                    && read.at(0).UpdatedAt == rows.at(0).UpdatedAt,
                "the library read did not answer what the write answered");
    }

    void TestRefusedRowsStoreNothing()
    {
        auto const harness = MakeBackend({});
        RejectsWithStatus([&harness]() { harness.Backend->PutLibraryAsync({ Library(L"movie:tt1", 1), Library(L"", 1) }).get(); },
                          400, "a library row without an id was accepted");
        RejectsWithStatus([&harness]()
        {
            auto row = Library(L"movie:tt1", 1);
            row.Poster = winrt::hstring{ L"not a url" };
            harness.Backend->PutLibraryAsync({ row }).get();
        }, 400, "a library poster that is no URL was accepted");
        // The reference's fractional timestamp cannot be expressed in a typed
        // row; a zero one is refused by the same rule.
        RejectsWithStatus([&harness]() { harness.Backend->PutLibraryAsync({ Library(L"movie:tt1", 0) }).get(); },
                          400, "a library row with no timestamp was accepted");
        RejectsWithStatus([&harness]()
        {
            auto row = Watch(L"tt1", 1);
            row.PositionSec = -1;
            harness.Backend->PutWatchStateAsync({ row }).get();
        }, 400, "a negative position was accepted");
        RejectsWithStatus([&harness]()
        {
            auto row = Watch(L"tt1", 1);
            row.PositionSec = std::numeric_limits<double>::infinity();
            harness.Backend->PutWatchStateAsync({ row }).get();
        }, 400, "an infinite position was accepted");
        RejectsWithStatus([&harness]()
        {
            auto row = Watch(L"tt1", 1);
            row.Name = winrt::hstring{ std::wstring(513, L'x') };
            harness.Backend->PutWatchStateAsync({ row }).get();
        }, 400, "an overlong watch state name was accepted");
        Require(harness.Store->Size() == 0, "a refused batch stored something");
    }

    void TestOverlappingProgressReportsBothLand()
    {
        auto const harness = MakeBackend({});
        harness.Store->ReadDelayMs = 5;
        auto first = harness.Backend->PutWatchStateAsync({ Watch(L"tt1", 5) });
        auto second = harness.Backend->PutWatchStateAsync({ Watch(L"tt2", 6) });
        static_cast<void>(first.get());
        static_cast<void>(second.get());
        auto const rows = harness.Backend->GetWatchStateAsync().get();
        std::vector<std::wstring> ids;
        for (auto const& row : rows) ids.emplace_back(row.VideoId);
        std::sort(ids.begin(), ids.end());
        Require(ids == std::vector<std::wstring>{ L"tt1", L"tt2" }, "an overlapping progress report was lost");
    }

    void TestWatchNewerReplacesWhole()
    {
        auto const harness = MakeBackend({});
        auto named = Watch(L"tt1", 10);
        named.Name = L"Named";
        named.PositionSec = 30;
        harness.Backend->PutWatchStateAsync({ named }).get();
        auto older = Watch(L"tt1", 9);
        older.PositionSec = 99;
        auto rows = harness.Backend->PutWatchStateAsync({ older }).get();
        Require(rows.at(0).PositionSec == 30, "an older report replaced a newer one");
        auto newer = Watch(L"tt1", 11);
        newer.PositionSec = 40;
        rows = harness.Backend->PutWatchStateAsync({ newer }).get();
        Require(rows.at(0).PositionSec == 40 && !rows.at(0).Name, "a newer report did not replace the row whole");
    }

    void TestWatchRemovals()
    {
        auto const harness = MakeBackend({});
        auto gone = Watch(L"tt1", 10);
        gone.Name = L"Gone";
        harness.Backend->PutWatchStateAsync({ gone, Watch(L"tt2", 10) }).get();
        auto tombstone = Watch(L"tt1", 20);
        tombstone.PositionSec = 0;
        tombstone.DurationSec = 0;
        tombstone.RemovedAt = 20;
        auto rows = harness.Backend->PutWatchStateAsync({ tombstone }).get();
        Require(rows.size() == 1 && rows[0].VideoId == L"tt2", "a removal was returned");

        auto stale = Watch(L"tt1", 15);
        stale.Name = L"Stale copy";
        rows = harness.Backend->PutWatchStateAsync({ stale }).get();
        Require(rows.size() == 1 && rows[0].VideoId == L"tt2", "a stale copy outlasted a removal");
        auto const read = harness.Backend->GetWatchStateAsync().get();
        Require(read.size() == 1 && read[0].VideoId == L"tt2", "the history read did not answer what the write answered");
        // Moving into an account reads the stored rows, so the removal goes along.
        auto const stored = harness.Backend->GetStoredWatchStatesAsync().get();
        auto const kept = std::find_if(stored.begin(), stored.end(), [](Dto::WatchEntry const& row) { return row.VideoId == L"tt1"; });
        Require(kept != stored.end() && kept->RemovedAt == std::optional<std::int64_t>{ 20 }, "the removal tombstone was not kept");

        auto replay = Watch(L"tt1", 30);
        replay.PositionSec = 12;
        rows = harness.Backend->PutWatchStateAsync({ replay }).get();
        auto const back = std::find_if(rows.begin(), rows.end(), [](Dto::WatchEntry const& row) { return row.VideoId == L"tt1"; });
        Require(back != rows.end() && back->PositionSec == 12 && !back->RemovedAt, "a newer play did not bring the video back");

        // The reference refuses a fractional removal; a typed row refuses a zero one.
        RejectsWithStatus([&harness]()
        {
            auto row = Watch(L"tt3", 1);
            row.RemovedAt = 0;
            harness.Backend->PutWatchStateAsync({ row }).get();
        }, 400, "a removal without a timestamp was accepted");
    }

    void TestSettings()
    {
        auto const harness = MakeBackend({});
        auto const initial = harness.Backend->GetSettingsAsync().get();
        Require(initial.UpdatedAt == 0 && initial.Value && initial.Value.Size() == 0, "settings did not start empty");

        auto const value = Json::JsonObject::Parse(LR"({"preferredSubtitleLang":"eng","futureSetting":{"nested":true}})");
        auto const checkValue = [](Dto::SettingsPayload const& payload, char const* message)
        {
            Require(payload.UpdatedAt == 10 && payload.Value.GetNamedString(L"preferredSubtitleLang") == L"eng"
                        && payload.Value.GetNamedObject(L"futureSetting").GetNamedBoolean(L"nested"),
                    message);
        };
        checkValue(harness.Backend->PutSettingsAsync(value, 10).get(), "newer settings were not stored with unknown keys");
        checkValue(harness.Backend->PutSettingsAsync(Json::JsonObject::Parse(LR"({"preferredSubtitleLang":"fre"})"), 9).get(),
                   "older settings replaced newer ones");
        checkValue(harness.Backend->GetSettingsAsync().get(), "the settings read did not answer what was stored");
        RejectsWithStatus([&harness]() { harness.Backend->PutSettingsAsync(Json::JsonObject::Parse(LR"({"playbackRate":9})"), 11).get(); },
                          400, "an out-of-range playback rate was accepted");
        RejectsWithStatus([&harness]() { harness.Backend->PutSettingsAsync(Json::JsonObject::Parse(LR"({"videoFitMode":"stretch"})"), 11).get(); },
                          400, "an unknown fit mode was accepted");
        RejectsWithStatus([&harness]() { harness.Backend->PutSettingsAsync(Json::JsonObject{}, 0).get(); },
                          400, "settings without a timestamp were accepted");
    }

    void TestUnreadableDocumentIsReportedNotReplaced()
    {
        auto const harness = MakeBackend({});
        harness.Store->Set(Device::DeviceCollection::Library, "{ not json");
        Require(IsUnreadable([&harness]() { static_cast<void>(harness.Backend->GetLibraryAsync().get()); }),
                "an unreadable library was not reported as unreadable");
        Require(IsUnreadable([&harness]() { static_cast<void>(harness.Backend->PutLibraryAsync({ Library(L"movie:tt1", 1) }).get()); }),
                "a write over an unreadable library was not refused as unreadable");
        Require(harness.Store->Get(Device::DeviceCollection::Library) == std::optional<std::string>{ "{ not json" },
                "an unreadable library was replaced");

        harness.Store->Set(Device::DeviceCollection::WatchStates, R"({"version":2,"states":[]})");
        Require(IsUnreadable([&harness]() { static_cast<void>(harness.Backend->GetWatchStateAsync().get()); }),
                "a document of another version was read");
    }

    /* The store on disk, in a temporary folder */

    class TemporaryFolder final
    {
    public:
        explicit TemporaryFolder(wchar_t const* name)
            : m_path(std::filesystem::temp_directory_path()
                  / (std::wstring{ L"halo-device-store-" } + name + L"-" + std::to_wstring(GetCurrentProcessId())))
        {
            std::error_code ignored;
            std::filesystem::remove_all(m_path, ignored);
        }

        ~TemporaryFolder()
        {
            std::error_code ignored;
            std::filesystem::remove_all(m_path, ignored);
        }

        TemporaryFolder(TemporaryFolder const&) = delete;
        TemporaryFolder& operator=(TemporaryFolder const&) = delete;

        [[nodiscard]] std::filesystem::path const& Path() const noexcept
        {
            return m_path;
        }

    private:
        std::filesystem::path m_path;
    };

    std::string FileBytes(std::filesystem::path const& path)
    {
        std::ifstream input{ path, std::ios::binary };
        return std::string{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
    }

    void WriteFileBytes(std::filesystem::path const& path, std::string const& bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output{ path, std::ios::binary | std::ios::trunc };
        output << bytes;
    }

    bool HasTemporaryFiles(std::filesystem::path const& folder)
    {
        std::error_code error;
        for (auto const& entry : std::filesystem::directory_iterator{ folder, error })
        {
            if (entry.path().extension() == L".tmp") return true;
        }
        return false;
    }

    void TestStoreRoundTrip()
    {
        TemporaryFolder const folder{ L"round-trip" };
        auto const store = std::make_shared<Device::DeviceStore>(folder.Path());
        Require(!store->ReadAsync(Device::DeviceCollection::Library).get(), "a document never written read as something");
        Require(!store->ReadProfileAsync().get(), "a profile never written read as something");

        store->WriteAsync(Device::DeviceCollection::Library, R"({"version":1,"items":[]})").get();
        Require(store->ReadAsync(Device::DeviceCollection::Library).get() == std::optional<std::string>{ R"({"version":1,"items":[]})" },
                "a document did not read back as written");
        Require(std::filesystem::exists(folder.Path() / L"library.json"), "the library was not kept in library.json");
        Require(!HasTemporaryFiles(folder.Path()), "a write left a temporary file");

        // The addon list is protected: its URLs can carry debrid keys.
        std::string const addons = R"({"version":1,"addons":[{"transportUrl":"https://addon.test/secret-key/manifest.json"}]})";
        store->WriteAsync(Device::DeviceCollection::Addons, addons).get();
        Require(FileBytes(folder.Path() / L"addons.bin").find("secret-key") == std::string::npos,
                "the addon list was stored in the clear");
        Require(store->ReadAsync(Device::DeviceCollection::Addons).get() == std::optional<std::string>{ addons },
                "the protected addon list did not read back");

        // The whole backend over the real store.
        auto const fetcher = std::make_shared<AddonTests::FakeFetcher>(std::map<std::wstring, Route>{});
        auto const backend = std::make_shared<Device::DeviceBackend>(Device::DeviceBackendOptions{ .Store = store, .Fetcher = fetcher });
        backend->PutWatchStateAsync({ Watch(L"tt1", 5) }).get();
        Require(backend->GetWatchStateAsync().get().size() == 1, "a watch state did not survive the disk");
        Require(std::filesystem::exists(folder.Path() / L"watch-states.json"), "watch states were not kept in watch-states.json");
        backend->PutSettingsAsync(Json::JsonObject::Parse(LR"({"autoplayNextEpisode":true})"), 7).get();
        Require(backend->GetSettingsAsync().get().UpdatedAt == 7, "settings did not survive the disk");

        auto const profile = store->CreateProfileAsync(1234).get();
        Require(profile.Id.size() == 36 && profile.Id[14] == L'4' && profile.CreatedAt == 1234, "the profile id is not a v4 UUID");
        auto const read = store->ReadProfileAsync().get();
        Require(read && read->Id == profile.Id && read->CreatedAt == 1234, "the profile did not read back");
        auto const text = store->ReadAsync(Device::DeviceCollection::Profile).get().value_or("");
        Require(text.find(R"("version":1)") != std::string::npos, "the profile is not a version 1 document");
        auto refused = false;
        try
        {
            static_cast<void>(store->CreateProfileAsync(99).get());
        }
        catch (...)
        {
            refused = true;
        }
        Require(refused && store->ReadProfileAsync().get()->Id == profile.Id, "an existing profile was replaced");

        store->ClearAsync().get();
        for (auto const collection : { Device::DeviceCollection::Profile, Device::DeviceCollection::Addons,
                                       Device::DeviceCollection::Library, Device::DeviceCollection::WatchStates,
                                       Device::DeviceCollection::Settings })
        {
            Require(!store->ReadAsync(collection).get(), "clearing left a document");
        }
        // Clearing again finds nothing to remove and still succeeds.
        store->ClearAsync().get();
    }

    void TestStoreUnreadableDocumentsAreLeftUntouched()
    {
        TemporaryFolder const folder{ L"unreadable" };
        auto const store = std::make_shared<Device::DeviceStore>(folder.Path());
        auto const fetcher = std::make_shared<AddonTests::FakeFetcher>(std::map<std::wstring, Route>{});
        auto const backend = std::make_shared<Device::DeviceBackend>(Device::DeviceBackendOptions{ .Store = store, .Fetcher = fetcher });

        auto const library = folder.Path() / L"library.json";
        WriteFileBytes(library, "{ not json");
        Require(IsUnreadable([&backend]() { static_cast<void>(backend->GetLibraryAsync().get()); }),
                "an unreadable library file was not reported");
        Require(IsUnreadable([&backend]() { static_cast<void>(backend->PutLibraryAsync({ Library(L"movie:tt1", 1) }).get()); }),
                "a write over an unreadable library file was not refused");
        Require(FileBytes(library) == "{ not json", "an unreadable library file was changed");

        // Not UTF-8.
        auto const settings = folder.Path() / L"settings.json";
        WriteFileBytes(settings, std::string{ "\xff\xfe\x00junk", 7 });
        Require(IsUnreadable([&backend]() { static_cast<void>(backend->PutSettingsAsync(Json::JsonObject{}, 5).get()); }),
                "a settings file that is not text was not reported");
        Require(FileBytes(settings) == std::string{ "\xff\xfe\x00junk", 7 }, "an unreadable settings file was changed");

        // A protected file Windows cannot decrypt.
        auto const addons = folder.Path() / L"addons.bin";
        WriteFileBytes(addons, "not protected");
        Require(IsUnreadable([&backend]() { static_cast<void>(backend->GetAddonsAsync().get()); }),
                "an undecryptable addon list was not reported");
        Require(FileBytes(addons) == "not protected", "an undecryptable addon list was changed");

        auto const profile = folder.Path() / L"profile.json";
        WriteFileBytes(profile, R"({"version":1,"id":""})");
        Require(IsUnreadable([&store]() { static_cast<void>(store->ReadProfileAsync().get()); }),
                "a profile in the wrong shape was not reported");
        auto created = true;
        try
        {
            static_cast<void>(store->CreateProfileAsync(1).get());
        }
        catch (...)
        {
            created = false;
        }
        Require(!created && FileBytes(profile) == R"({"version":1,"id":""})", "an unreadable profile was replaced");
        Require(!HasTemporaryFiles(folder.Path()), "a refused write left a temporary file");
    }

    void TestStoreFailedWriteKeepsTheOldFile()
    {
        TemporaryFolder const folder{ L"failed-write" };
        auto const store = std::make_shared<Device::DeviceStore>(folder.Path());
        store->WriteAsync(Device::DeviceCollection::Settings, "old").get();
        auto const path = folder.Path() / L"settings.json";

        // An open handle without delete sharing makes the final replace fail.
        auto failed = false;
        {
            wil::unique_hfile holder{ CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) };
            Require(static_cast<bool>(holder), "the settings file could not be held open");
            try
            {
                store->WriteAsync(Device::DeviceCollection::Settings, "new").get();
            }
            catch (...)
            {
                failed = true;
            }
        }
        Require(failed, "a write whose replace failed reported success");
        Require(store->ReadAsync(Device::DeviceCollection::Settings).get() == std::optional<std::string>{ "old" },
                "a failed write did not leave the old file");
        Require(!HasTemporaryFiles(folder.Path()), "a failed write left its temporary file");

        // Past the size bound nothing is touched.
        auto refused = false;
        try
        {
            store->WriteAsync(Device::DeviceCollection::Settings, std::string(Device::DeviceStore::MaximumDocumentBytes + 1, 'x')).get();
        }
        catch (std::length_error const&)
        {
            refused = true;
        }
        Require(refused && store->ReadAsync(Device::DeviceCollection::Settings).get() == std::optional<std::string>{ "old" },
                "an oversized write was not refused or replaced the old document");
    }

    void TestDownloadPartitionVector()
    {
        // Computed by running the reference's own opaqueDownloadOwner('device', id)
        // (apps/desktop/src/downloads/downloadsLogic.ts) under Node.
        Device::DeviceProfile const profile{ L"3f8a1c2e-9b4d-4e6f-a1b2-c3d4e5f60718", 1 };
        Require(Device::DeviceDownloadOwner(profile) == L"5460177c7b1e94f8385f52c3bfcf6eba5d8f5c0ec9e3d490b298876776f7ec66",
                "the device downloads partition differs from the reference's");
        Require(Device::ParseDeviceProfile(Device::SerializeDeviceProfile(profile))->Id == profile.Id,
                "a profile did not survive its own document");
        Require(!Device::ParseDeviceProfile(R"({"version":2,"id":"x","createdAt":1})"), "a profile of another version was read");
        Require(!Device::ParseDeviceProfile(R"({"version":1,"id":"x"})"), "a profile without createdAt was read");
    }

    /* The selector */

    class StillTokenProvider final : public HaloDesktop::Services::Auth::ITokenProvider
    {
    public:
        [[nodiscard]] concurrency::task<std::optional<winrt::hstring>> AccessTokenAsync() override
        {
            return concurrency::task_from_result(std::optional<winrt::hstring>{});
        }

        [[nodiscard]] concurrency::task<std::optional<winrt::hstring>> RefreshAccessTokenAsync() override
        {
            return concurrency::task_from_result(std::optional<winrt::hstring>{});
        }

        [[nodiscard]] concurrency::task<void> RejectSessionAsync(std::uint64_t) override
        {
            return concurrency::task_from_result();
        }

        [[nodiscard]] std::uint64_t SessionGeneration() const noexcept override
        {
            return 1;
        }

        [[nodiscard]] HaloDesktop::Services::Auth::SessionTarget CurrentTarget() const override
        {
            return { .Generation = 1, .ServerUrl = std::nullopt };
        }
    };

    void TestSelectorAnswersTheAccount()
    {
        auto const api = std::make_shared<HaloDesktop::Api::ApiClient>(
            L"http://127.0.0.1:9", std::make_shared<HaloDesktop::Api::HttpExecutor>(), std::make_shared<StillTokenProvider>());
        HaloDesktop::Api::BackendSelector const selector{ api };
        auto const current = selector.Current();
        Require(current.get() == static_cast<HaloDesktop::Api::IHaloBackend*>(api.get()), "the selector did not answer the account's client");
        Require(selector.Current() == current, "the selector changed its answer on its own");
    }

    /* Records policy */

    void TestRecordsPolicy()
    {
        Require(!Device::AddonUrlsProblem({ L"https://a.test/manifest.json", L"http://b.test" }), "valid addon URLs were refused");
        Require(Device::AddonUrlsProblem({ L"https://a.test/x", L"https://a.test/x" }) == std::optional<std::wstring>{ L"duplicate transportUrl: https://a.test/x" },
                "a duplicate did not give the server's sentence");
        Require(Device::AddonUrlsProblem({ L"https://" }) == std::optional<std::wstring>{ L"not an addon URL: https://" },
                "an http URL without a host was accepted");

        auto value = Json::JsonObject::Parse(LR"({"subtitleScalePercent":120,"subtitleOutline":"thin","catalogLanguage":"en"})");
        Require(!Device::SettingsProblem(value, 1), "valid settings were refused");
        Require(Device::SettingsProblem(Json::JsonObject::Parse(LR"({"subtitleScalePercent":120.5})"), 1)
                    == std::optional<std::wstring>{ L"invalid setting: subtitleScalePercent" },
                "a fractional scale was accepted");
        Require(Device::SettingsProblem(Json::JsonObject::Parse(LR"({"preferredAudioLang":null})"), 1).has_value(),
                "a null setting was read as absent");
        Require(Device::SettingsProblem(Json::JsonObject::Parse(LR"({"catalogLanguage":""})"), 1).has_value(),
                "an empty catalog language was accepted");

        auto withPoster = Library(L"movie:tt1", 1);
        withPoster.Poster = winrt::hstring{ L"mailto:x" };
        Require(!Device::LibraryItemProblem(withPoster), "a poster with any URL scheme was refused, as the server accepts it");
    }
}

void RunDeviceBackendTests()
{
    TestInstallKeepsIdsAndFetchesOnce();
    TestOneBadManifestFailsTheInstall();
    TestAddonListsAreRefused();
    TestHiddenCatalogs();
    TestStoredBlankNamedAddonStaysUsable();
    TestCatalogs();
    TestMetaFirstAddonThatDescribes();
    TestStreamsAskEveryCapableAddon();
    TestSubtitles();
    TestNextEpisode();
    TestLibraryLastWriteWins();
    TestRefusedRowsStoreNothing();
    TestOverlappingProgressReportsBothLand();
    TestWatchNewerReplacesWhole();
    TestWatchRemovals();
    TestSettings();
    TestUnreadableDocumentIsReportedNotReplaced();
    TestStoreRoundTrip();
    TestStoreUnreadableDocumentsAreLeftUntouched();
    TestStoreFailedWriteKeepsTheOldFile();
    TestDownloadPartitionVector();
    TestSelectorAnswersTheAccount();
    TestRecordsPolicy();
}
