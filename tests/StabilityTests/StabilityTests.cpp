#include <winsock2.h>
#include <ws2tcpip.h>

#include "Services/CatalogRefreshPolicy.h"
#include "Api/ApiError.h"
#include "Api/CatalogExtrasPolicy.h"
#include "Api/Dto.h"
#include "Api/JsonNumberPolicy.h"
#include "Diagnostics/CrashLog.h"
#include "Services/SettingsSyncPolicy.h"
#include "Services/DiscordPresence.h"
#include "Services/Downloads/DownloadPageOperationState.h"
#include "Services/Downloads/DownloadTypes.h"
#include "Services/Downloads/DownloadPreparation.h"
#include "Services/LandscapeArtworkPolicy.h"
#include "Services/Auth/LoopbackListener.h"
#include "Services/StreamInfo.h"
#include "Services/ContinueShelfPolicy.h"
#include "Services/WatchMarkPolicy.h"
#include "ViewModels/HomeStatePolicy.h"
#include "ApiClientAuthTest.h"
#include "DownloadTransferTest.h"
#include "HomeRowsTests.h"
#include "HomeShelfLoadTests.h"
#include "SearchResultsTests.h"
#include "SearchInputTests.h"
#include "PeopleTests.h"
#include "MagnetTests.h"
#include "HistoryRowsTests.h"
#include "DetailPageTests.h"
#include "SourcesSheetTests.h"
#include "WatchOutboxTests.h"
#include "CallQueueTests.h"
#include "SegmentPlannerTests.h"
#include "DownloadsPageTests.h"
#include "SettingsPageTests.h"
#include "ShellInputTests.h"
#include "SignInTests.h"
#include "DeviceBackendTests.h"
#include "DeviceModeTests.h"
#include "AddonResolveTests.h"
#include "PlaybackSourceResolverTest.h"
#include "StorageTests.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
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

    void TestCatalogCommitDecisions()
    {
        Require(
            HaloDesktop::Services::DecideCatalogCommit(0, 0) ==
                HaloDesktop::Services::CatalogCommitDecision::Commit,
            "zero eligible catalogs were not accepted as a valid empty snapshot");
        Require(
            HaloDesktop::Services::DecideCatalogCommit(3, 0) ==
                HaloDesktop::Services::CatalogCommitDecision::PreserveAndFail,
            "an all-failed catalog load replaced the previous snapshot");
        Require(
            HaloDesktop::Services::DecideCatalogCommit(3, 1) ==
                HaloDesktop::Services::CatalogCommitDecision::Commit,
            "a partial catalog success was rejected");
    }

    void TestStreamVideoSizeValidation()
    {
        auto parseSize = [](std::wstring_view value)
        {
            auto const json = std::wstring{
                LR"({"results":[{"addon":{"id":"addon","name":"Addon"},"streams":[{"url":"https://example.test/video","behaviorHints":{"videoSize":)" }
                + std::wstring{ value }
                + LR"(}}]}],"errors":[]})";
            auto const payload = HaloDesktop::Api::Mappers::ParseStreams(
                winrt::Windows::Data::Json::JsonValue::Parse(json));
            Require(payload.Results.size() == 1 && payload.Results.front().Streams.size() == 1,
                "an invalid optional stream size discarded an otherwise playable source");
            return payload.Results.front().Streams.front().VideoSize;
        };

        Require(parseSize(L"123456") == 123456,
            "a valid stream size was not retained");
        // The reference's wholeBytes rounds an addon's fractional figure rather than
        // dropping it, and reads zero as unknown.
        Require(parseSize(L"1.5") == 2,
            "a fractional stream size was not rounded to whole bytes");
        Require(parseSize(L"1234.6") == 1235,
            "a fractional stream size was not rounded to the nearest byte");
        Require(!parseSize(L"0"),
            "a zero stream size was kept as a size");
        Require(!parseSize(L"0.4"),
            "a stream size rounding to zero was kept as a size");
        Require(!parseSize(L"1e300"),
            "an out-of-range stream size reached an integer conversion");
        Require(!parseSize(L"18446744073709551616"),
            "2^64 was accepted as a uint64 stream size");
    }

    void TestNumericBoundaryValidation()
    {
        using HaloDesktop::Api::CheckedNonnegativeInt64;
        using HaloDesktop::Api::CheckedPositiveInt64;
        using HaloDesktop::Api::CheckedTokenExpiry;

        Require(CheckedPositiveInt64(9'223'372'036'854'774'784.0).has_value(),
            "the largest safely representable positive int64 double was rejected");
        Require(!CheckedPositiveInt64(9'223'372'036'854'775'808.0).has_value(),
            "2^63 reached the positive int64 conversion");
        Require(!CheckedNonnegativeInt64(9'223'372'036'854'775'808.0).has_value(),
            "2^63 reached the nonnegative int64 conversion");
        Require(CheckedTokenExpiry(60.0, 1'000).value_or(0) == 61'000,
            "a valid token lifetime was rejected");
        Require(!CheckedTokenExpiry(1.5, 1'000).has_value(),
            "a fractional token lifetime was accepted");
        Require(!CheckedTokenExpiry(9'223'372'036'854'775'808.0, 1'000).has_value(),
            "an oversized token lifetime was accepted");
        Require(!CheckedTokenExpiry(60.0, (std::numeric_limits<std::int64_t>::max)() - 59'999).has_value(),
            "a token expiry addition overflow was accepted");

        auto rejected = false;
        try
        {
            static_cast<void>(HaloDesktop::Api::Mappers::ParseMe(
                winrt::Windows::Data::Json::JsonValue::Parse(
                    LR"({"id":"user","username":"name","isAdmin":false,"createdAt":9223372036854775808})")));
        }
        catch (...)
        {
            rejected = true;
        }
        Require(rejected, "an oversized account timestamp was accepted");

        auto const oversizedWatch = HaloDesktop::Api::Mappers::ParseWatchState(
            winrt::Windows::Data::Json::JsonValue::Parse(
                LR"([{"videoId":"video","itemId":"movie:item","positionSec":1,"durationSec":9223372036854775808,"watched":false,"updatedAt":1}])"));
        Require(oversizedWatch.empty(), "an oversized watch duration was accepted");

        // The row is refused on its own; the rest of the list still reads.
        auto const oversizedRemoval = HaloDesktop::Api::Mappers::ParseLibrary(
            winrt::Windows::Data::Json::JsonValue::Parse(
                LR"([{"id":"movie:item","type":"movie","name":"Item","addedAt":1,"removedAt":9223372036854775808,"updatedAt":1},)"
                LR"({"id":"movie:kept","type":"movie","name":"Kept","addedAt":1,"updatedAt":1}])"));
        Require(oversizedRemoval.size() == 1 && oversizedRemoval.front().Id == L"movie:kept",
            "an oversized library removal timestamp was accepted");

        HaloDesktop::Api::Dto::StreamRecord stream;
        stream.Url = L"https://example.test/video";
        stream.Title = L"9999999999999999999 GB";
        Require(!HaloDesktop::Services::ParseStreamInfo(stream).SizeBytes,
            "an out-of-range textual source size was converted to uint64");
    }

    void TestSettingsLoadPolicy()
    {
        using HaloDesktop::Services::ShouldApplyLoadedSettings;
        Require(!ShouldApplyLoadedSettings(99, 100, 1, 1),
            "an older settings payload replaced a local document");
        Require(!ShouldApplyLoadedSettings(101, 100, 1, 2),
            "a settings payload from before a local write was applied");
        Require(!ShouldApplyLoadedSettings(100, 100, 1, 1),
            "an equal-timestamp settings payload replaced the local document");
        Require(ShouldApplyLoadedSettings(101, 100, 0, 0),
            "a newer remote settings payload was rejected");

        // A save replaces the whole document, so none goes out before a real
        // read (the reference's syncedSettings waits for the read and fails the
        // save when there is still nothing).
        using HaloDesktop::Services::CanSaveSettings;
        using HaloDesktop::Services::DecideSettingsLoad;
        using HaloDesktop::Services::SettingsLoadAction;
        Require(!CanSaveSettings(false), "settings could be saved before they were read");
        Require(CanSaveSettings(true), "settings that were read could not be saved");

        Require(DecideSettingsLoad({ .DocumentAlreadyRead = false, .ReceivedDocument = true, .IsLatestRequest = true,
                    .PayloadUpdatedAt = 0, .LocalUpdatedAt = 0, .RequestWriteVersion = 3, .CurrentWriteVersion = 3 })
                == SettingsLoadAction::AdoptFirstRead,
            "a never-saved account's first document was not adopted");
        Require(DecideSettingsLoad({ .DocumentAlreadyRead = false, .ReceivedDocument = true, .IsLatestRequest = false,
                    .PayloadUpdatedAt = 50, .LocalUpdatedAt = 0, .RequestWriteVersion = 1, .CurrentWriteVersion = 1 })
                == SettingsLoadAction::AdoptFirstRead,
            "an older read was ignored while nothing had been read yet");
        Require(DecideSettingsLoad({ .DocumentAlreadyRead = false, .ReceivedDocument = false, .IsLatestRequest = true })
                == SettingsLoadAction::DiscardUnreadEdits,
            "edits waiting on a read that failed were kept for a later save");
        Require(DecideSettingsLoad({ .DocumentAlreadyRead = false, .ReceivedDocument = false, .IsLatestRequest = false })
                == SettingsLoadAction::Keep,
            "a superseded failed read discarded edits a newer read can still land");
        Require(DecideSettingsLoad({ .DocumentAlreadyRead = true, .ReceivedDocument = false, .IsLatestRequest = true })
                == SettingsLoadAction::Keep,
            "a failed refresh dropped settings already read");
        Require(DecideSettingsLoad({ .DocumentAlreadyRead = true, .ReceivedDocument = true, .IsLatestRequest = true,
                    .PayloadUpdatedAt = 101, .LocalUpdatedAt = 100, .RequestWriteVersion = 2, .CurrentWriteVersion = 2 })
                == SettingsLoadAction::ApplyNewer,
            "a newer document was not applied after the first read");
        Require(DecideSettingsLoad({ .DocumentAlreadyRead = true, .ReceivedDocument = true, .IsLatestRequest = true,
                    .PayloadUpdatedAt = 101, .LocalUpdatedAt = 100, .RequestWriteVersion = 2, .CurrentWriteVersion = 3 })
                == SettingsLoadAction::Keep,
            "a document read before a local edit replaced the edit");

        // A document whose save failed is sent again only while it is newer
        // than the server's; a newer server document replaces it through the
        // ordinary rule above.
        using HaloDesktop::Services::ShouldResendUnsentSettings;
        using HaloDesktop::Services::UnsentSettingsAreNewer;
        Require(UnsentSettingsAreNewer(true, 101, 100), "a newer unsent document was not sent again");
        Require(!UnsentSettingsAreNewer(true, 100, 100), "an unsent document was sent over an equally new server one");
        Require(!UnsentSettingsAreNewer(true, 99, 100), "an unsent document was sent over a newer server one");
        Require(!UnsentSettingsAreNewer(false, 101, 100), "a document the server already holds was sent again");
        Require(DecideSettingsLoad({ .DocumentAlreadyRead = true, .ReceivedDocument = true, .IsLatestRequest = true,
                    .PayloadUpdatedAt = 120, .LocalUpdatedAt = 110, .RequestWriteVersion = 4, .CurrentWriteVersion = 4 })
                == SettingsLoadAction::ApplyNewer,
            "a newer server document did not replace an unsent local one");

        HaloDesktop::Services::SettingsResendCheck const resend{
            .FromServer = true, .MayResend = true, .Unsent = true,
            .LocalUpdatedAt = 110, .ServerUpdatedAt = 100, .RequestWriteVersion = 4, .CurrentWriteVersion = 4 };
        Require(ShouldResendUnsentSettings(resend), "a successful load did not send the unsent document again");
        auto fromMirror = resend;
        fromMirror.FromServer = false;
        Require(!ShouldResendUnsentSettings(fromMirror), "an offline load (the mirror) tried to send");
        auto afterFailedSave = resend;
        afterFailedSave.MayResend = false;
        Require(!ShouldResendUnsentSettings(afterFailedSave), "the load a failed save starts sent again, which can loop");
        auto editedMeanwhile = resend;
        editedMeanwhile.CurrentWriteVersion = 5;
        Require(!ShouldResendUnsentSettings(editedMeanwhile), "a load sent a document an edit is already saving");
        auto serverNewer = resend;
        serverNewer.ServerUpdatedAt = 111;
        Require(!ShouldResendUnsentSettings(serverNewer), "an unsent document was sent over a newer server document");
    }

    winrt::Windows::Data::Json::IJsonValue Json(std::wstring_view text)
    {
        return winrt::Windows::Data::Json::JsonValue::Parse(winrt::hstring{ text });
    }

    void TestCatalogExtrasAndAddonFields()
    {
        auto const payload = HaloDesktop::Api::Mappers::ParseAddons(Json(LR"({
            "global": [
                { "id": "g1", "manifest": { "id": "tmdb", "name": "TMDB", "version": "1.0.0", "catalogs": [
                    { "type": "movie", "id": "lang", "name": "Language",
                      "extra": [ { "name": "genre", "options": [ "Hindi", 5, " Tamil " ] }, { "name": "skip" },
                                 { "name": "search", "isRequired": true } ],
                      "extraSupported": [ "skip", 7 ], "extraRequired": [ "search" ] },
                    { "type": "movie", "id": "year", "extra": [ { "name": "genre", "isRequired": true } ] },
                    { "type": "series", "id": "odd", "extra": [ { "name": "genre", "options": "Hindi" } ] } ] },
                  "position": 0, "providedGlobally": true }
            ],
            "user": [
                { "id": "u1", "transportUrl": "https://a.test/manifest.json",
                  "manifest": { "id": "a", "name": "A", "version": "1" }, "position": 0, "providedGlobally": true },
                { "id": "u2", "transportUrl": "https://b.test/manifest.json",
                  "manifest": { "id": "b", "name": "B", "version": "1" }, "position": 1, "providedGlobally": "yes" }
            ]
        })"));
        Require(payload.Global.size() == 1 && payload.User.size() == 2, "the addon lists did not parse");
        auto const& catalogs = payload.Global.front().Catalogs;
        Require(catalogs.size() == 3, "a catalog with an odd optional extra was dropped");
        auto const& language = catalogs[0];
        Require(language.SupportsSearch && language.HasRequiredExtra, "the existing catalog flags changed");
        Require(language.Extras.size() == 3 && language.ExtraSupported.size() == 1 && language.ExtraRequired.size() == 1,
            "the catalog extras were not read");
        auto const genres = HaloDesktop::Api::GenreOptions(language);
        Require(genres.size() == 2 && genres[0] == L"Hindi" && genres[1] == L"Tamil", "the genre options were not read");
        Require(HaloDesktop::Api::SupportsSkip(language), "a catalog declaring skip did not page");
        Require(HaloDesktop::Api::SearchNeedsNothingElse(language), "a catalog needing only the query was refused a search");
        Require(!HaloDesktop::Api::SearchNeedsNothingElse(catalogs[1]), "a catalog needing a genre was offered a bare search");
        Require(!HaloDesktop::Api::SupportsSkip(catalogs[1]), "a catalog without skip was paged");
        Require(HaloDesktop::Api::GenreOptions(catalogs[2]).empty(), "options that are not a list were read as genres");

        Require(!payload.Global.front().ProvidedGlobally, "a global entry was marked as provided globally");
        Require(payload.User[0].ProvidedGlobally, "a personal entry the admin also installs was not marked");
        Require(!payload.User[1].ProvidedGlobally, "a malformed providedGlobally was read as true");
        Require(payload.User[0].TransportUrl.value_or(L"") == L"https://a.test/manifest.json",
            "a personal addon's transport URL was not read");

        // A hidden URL may come as null or as something other than a string; the
        // row stays, without a URL, and the rest of the list still parses.
        auto const hidden = HaloDesktop::Api::Mappers::ParseAddons(Json(LR"({
            "global": [
                { "id": "g1", "transportUrl": null, "manifest": { "id": "a", "name": "A", "version": "1" }, "position": 0 },
                { "id": "g2", "transportUrl": 7, "manifest": { "id": "b", "name": "B", "version": "1" }, "position": 1 },
                { "id": "g3", "manifest": { "id": "c", "name": "C", "version": "1" }, "position": 2 }
            ],
            "user": [
                { "id": "u1", "transportUrl": "https://u.test/manifest.json",
                  "manifest": { "id": "u", "name": "U", "version": "1" }, "position": 0 }
            ]
        })"));
        Require(hidden.Global.size() == 3 && hidden.User.size() == 1, "a null transport URL failed the addon list");
        Require(!hidden.Global[0].TransportUrl && !hidden.Global[1].TransportUrl && !hidden.Global[2].TransportUrl,
            "a null or malformed transport URL was read as a URL");
        Require(hidden.User[0].TransportUrl.value_or(L"") == L"https://u.test/manifest.json",
            "a row beside a hidden URL lost its own");

        // One unreadable row costs only itself: a blank name falls back to the
        // manifest id, then "Addon"; a row without an id, a non-object row, a
        // bad position or a manifest that is not an object is left out; a
        // catalog whose extra lists are not lists is dropped, the addon kept;
        // odd extra fields are read without failing anything.
        auto const mixed = HaloDesktop::Api::Mappers::ParseAddons(Json(LR"({
            "global": [ 5, { "id": "g1", "manifest": { "id": "g", "name": "G", "version": "1" }, "position": 0 } ],
            "user": [
                { "id": "u1", "manifest": { "id": "spaces", "name": "  \t ", "version": "1" }, "position": 0 },
                { "id": "u2", "manifest": { "name": 7, "version": "1" }, "position": 1 },
                { "manifest": { "id": "noid", "name": "No id", "version": "1" }, "position": 2 },
                { "id": "u4", "manifest": "x", "position": 3 },
                { "id": "u5", "manifest": { "id": "p", "name": "P" }, "position": -1 },
                { "id": "u6", "hideCatalogs": "yes", "manifest": { "id": "c", "name": " Cats ", "catalogs": [
                    { "type": "movie", "id": "odd", "extra": [ { "name": 5, "isRequired": "yes" }, { "name": "genre", "isRequired": 1 } ] },
                    { "type": "movie", "id": "plain", "extra": null },
                    { "type": "movie", "id": "broken", "extra": { "name": "genre" } },
                    { "type": "movie", "id": "broken2", "extraRequired": "search" } ] }, "position": 5 }
            ]
        })"));
        Require(mixed.Global.size() == 1 && mixed.Global[0].Id == L"g1", "a malformed global row failed the list");
        Require(mixed.User.size() == 3 && mixed.User[0].Id == L"u1" && mixed.User[1].Id == L"u2" && mixed.User[2].Id == L"u6",
            "unreadable addon rows were kept, or readable ones lost");
        Require(mixed.User[0].Name == L"spaces" && mixed.User[1].Name == L"Addon" && mixed.User[2].Name == L"Cats",
            "a blank or malformed addon name did not fall back");
        auto const& oddCatalogs = mixed.User[2].Catalogs;
        Require(!mixed.User[2].HideCatalogs && oddCatalogs.size() == 2
                && oddCatalogs[0].Id == L"odd" && oddCatalogs[0].HasRequiredExtra && oddCatalogs[0].Extras.size() == 1
                && oddCatalogs[0].Extras[0].IsRequired && oddCatalogs[1].Id == L"plain" && !oddCatalogs[1].HasRequiredExtra,
            "odd catalog extras failed the addon, or a catalog with unknown needs was kept");

        bool notAList = false;
        try
        {
            static_cast<void>(HaloDesktop::Api::Mappers::ParseAddons(Json(LR"({ "global": [], "user": {} })")));
        }
        catch (...)
        {
            notAList = true;
        }
        Require(notAList, "an addons answer without its lists was read as a Halo answer");

        auto const me = HaloDesktop::Api::Mappers::ParseMe(Json(
            LR"({"id":"u","username":"kenneth","isAdmin":false,"createdAt":1,"plan":" premium ","avatarVersion":"v2"})"));
        Require(me.Plan.value_or(L"") == L"premium" && me.AvatarVersion.value_or(L"") == L"v2", "the account tier or picture was not read");
        auto const plain = HaloDesktop::Api::Mappers::ParseMe(Json(
            LR"({"id":"u","username":"kenneth","isAdmin":false,"createdAt":1,"plan":5,"avatarVersion":null})"));
        Require(!plain.Plan && !plain.AvatarVersion, "a malformed tier or picture version was kept");
    }

    void TestMetaFieldsAndActionFiltering()
    {
        // Mirrors actionEntries.test.ts "addon streams are filtered as they are read":
        // an action stream is dropped and a group left with nothing is dropped too.
        auto const streams = HaloDesktop::Api::Mappers::ParseStreams(Json(LR"({
            "results": [
                { "addon": { "id": "a", "name": "A" }, "streams": [
                    { "url": "https://cdn.test/film.mkv", "name": "Film" },
                    { "url": "https://mf.test/streaming_provider/s/delete_all_watchlist?provider=torbox", "name": "TorBox" } ] },
                { "addon": { "id": "b", "name": "B" }, "streams": [
                    { "url": "https://cdn.test/v.mkv", "description": "Delete all files in TorBox watchlist." } ] }
            ],
            "errors": []
        })"));
        Require(streams.Results.size() == 1 && streams.Results.front().Streams.size() == 1
                && streams.Results.front().Streams.front().Url == L"https://cdn.test/film.mkv",
            "an action stream, or a group of nothing but actions, was offered");

        auto const next = HaloDesktop::Api::Mappers::ParseNextEpisode(Json(LR"({
            "video": { "id": "tt1:1:2", "title": "Two" },
            "stream": { "url": "https://mf.test/delete_all_watchlist", "name": "Next" }
        })"));
        Require(next.Video && !next.Stream, "an action stream was kept as the next episode's source");

        // A stream's title and description keep their lines, so a stream that
        // names no file is named by the first line, not by the whole title.
        auto const lined = HaloDesktop::Api::Mappers::ParseStreams(Json(LR"({
            "results": [ { "addon": { "id": "a", "name": "A" }, "streams": [
                { "url": "https://cdn.test/one.mkv", "name": "Torrent\nio",
                  "title": "Film.2019.1080p.WEB-DL.mkv\r\n⚙ Group\r\u0007 4.2 GB" } ] } ],
            "errors": []
        })"));
        Require(lined.Results.size() == 1 && lined.Results.front().Streams.size() == 1, "a stream with a lined title was dropped");
        auto const& linedStream = lined.Results.front().Streams.front();
        Require(linedStream.Title == winrt::hstring{ L"Film.2019.1080p.WEB-DL.mkv\n\x2699 Group\n  4.2 GB" },
            "a stream title lost its line breaks, or kept another control character");
        Require(linedStream.Name == winrt::hstring{ L"Torrent io" }, "a stream name kept a line break");
        auto const linedInfo = HaloDesktop::Services::ParseStreamInfo(linedStream);
        Require(linedInfo.Filename == L"Film.2019.1080p.WEB-DL.mkv", "a stream without a file name was not named by its title's first line");

        // requestHeaders.test.ts at the mapper boundary: each unusable header is
        // dropped on its own, so the stream keeps the rest and still plays.
        auto const headered = HaloDesktop::Api::Mappers::ParseNextEpisode(Json(LR"({
            "video": { "id": "tt1:1:2", "title": "Two" },
            "stream": { "url": "https://cdn.test/two.mkv", "behaviorHints": { "proxyHeaders": { "request": {
                "Referer": "https://site.test/", "Range": "bytes=0-1", "Bad Name": "x",
                "X-Injected": "ok\r\nEvil: 1", "X-Number": 5, "Cookie": "a=1, b=2" } } } }
        })"));
        Require(headered.Stream.has_value(), "a stream with some unusable headers was dropped");
        auto const& kept = headered.Stream->RequestHeaders;
        auto const keeps = [&kept](wchar_t const* name, wchar_t const* value)
        {
            return std::any_of(kept.begin(), kept.end(), [name, value](auto const& header)
            {
                return header.first == name && header.second == value;
            });
        };
        Require(kept.size() == 2 && keeps(L"Referer", L"https://site.test/") && keeps(L"Cookie", L"a=1, b=2"),
            "the usable headers were not kept on their own");

        // An action entry is dropped without taking one of the page's thirty slots.
        std::wstring catalog = LR"({"metas":[{"id":"dltorbox","type":"movie","name":"Delete all files"})";
        for (int index = 0; index < 30; ++index)
        {
            catalog += LR"(,{"id":"tt)" + std::to_wstring(index + 1) + LR"(","type":"movie","name":"Film"})";
        }
        catalog += L"]}";
        auto const page = HaloDesktop::Api::Mappers::ParseCatalog(Json(catalog));
        Require(page.size() == 30 && page.front().Id == L"tt1", "an action entry took a catalog slot or survived");

        auto const previews = HaloDesktop::Api::Mappers::ParseCatalog(Json(LR"({"metas":[
            { "id": "tmdb:1", "type": "movie", "name": "Film", "logo": "https://img.test/logo.png",
              "behaviorHints": { "defaultVideoId": " tt9 " }, "imdb_id": "tt5",
              "genres": [ "Comedy", { "name": "Family" }, 7, { "name": 3 } ] },
            { "id": "tt2", "type": "movie", "name": "Other", "behaviorHints": "odd", "imdb_id": "x5", "genres": "Drama" }
        ]})"));
        Require(previews.size() == 2, "a preview with odd optional fields was dropped");
        Require(previews[0].Logo && previews[0].DefaultVideoId.value_or(L"") == L"tt9"
                && previews[0].ImdbId.value_or(L"") == L"tt5"
                && previews[0].Genres == std::vector<winrt::hstring>{ L"Comedy", L"Family" },
            "the preview's new fields were not read");
        Require(!previews[1].DefaultVideoId && !previews[1].ImdbId && previews[1].Genres.empty(),
            "malformed preview fields were kept");

        auto const meta = HaloDesktop::Api::Mappers::ParseMeta(Json(LR"({"meta":{
            "id": "tt1", "type": "series", "name": "Show", "imdbRating": "7.1", "genres": [ "Drama" ],
            "released": "2020-01-02T00:00:00.000Z", "ageRating": "U/A 13+", "country": "India",
            "language": "Hindi", "awards": "2 wins",
            "trailerStreams": [ { "title": "Trailer", "ytId": "abc123xyz" }, { "title": "No id" } ],
            "trailers": [ { "source": "zzz999zzz", "type": "Trailer" }, { "type": "Clip" } ],
            "app_extras": { "cast": [
                { "name": "Actor", "character": "Lead", "photo": "https://img.test/a.jpg" },
                { "name": "Other", "photo": "http://img.test/b.jpg" },
                { "character": "Nobody" } ] },
            "videos": [
                { "id": "tt1:1:1", "name": "Pilot", "season": 1, "episode": 1 },
                { "id": "tt1:1:2", "season": 1, "episode": 2 },
                { "id": "tt1:1:3", "title": "   ", "season": 1, "episode": 3 },
                { "id": "tt1:1:4", "title": " \t ", "name": "  Finale  ", "season": 1, "episode": 4 } ]
        }})"));
        Require(meta.Preview.Genres.size() == 1 && meta.Released && meta.AgeRating.value_or(L"") == L"U/A 13+"
                && meta.Country && meta.Language && meta.Awards,
            "the title's facts were not read");
        Require(meta.TrailerStreamIds == std::vector<winrt::hstring>{ L"abc123xyz" }
                && meta.Trailers.size() == 1 && meta.Trailers.front().Source == L"zzz999zzz",
            "the trailers were not read");
        Require(meta.PicturedCast.size() == 2 && meta.PicturedCast[0].Photo && meta.PicturedCast[0].Character
                && !meta.PicturedCast[1].Photo,
            "the pictured cast did not follow the reference rules");
        // An untitled episode no longer reads "Episode": as the reference, each
        // reader picks its own fallback (the id in rows, the show's name in the
        // sources sheet and the player), so the mapper leaves the title empty.
        Require(meta.Videos.size() == 4 && meta.Videos[0].HasTitle && meta.Videos[0].Title == L"Pilot"
                && !meta.Videos[1].HasTitle && meta.Videos[1].Title.empty(),
            "a video's own name was not told apart from an untitled video");
        Require(!meta.Videos[2].HasTitle && meta.Videos[2].Title.empty(),
            "a title of nothing but spaces was kept as the video's title");
        Require(meta.Videos[3].HasTitle && meta.Videos[3].Title == L"Finale",
            "a blank title did not fall back to the video's trimmed name");

        auto const odd = HaloDesktop::Api::Mappers::ParseMeta(Json(LR"({"meta":{
            "id": "tt1", "type": "movie", "name": "Film",
            "trailerStreams": "x", "trailers": 5, "app_extras": [], "ageRating": 12, "released": null
        }})"));
        Require(odd.TrailerStreamIds.empty() && odd.Trailers.empty() && odd.PicturedCast.empty() && !odd.AgeRating && !odd.Released,
            "malformed title fields were kept");

        auto const watch = HaloDesktop::Api::Mappers::ParseWatchState(Json(LR"([
            {"videoId":"a","itemId":"movie:a","positionSec":1,"durationSec":10,"watched":false,"updatedAt":5,"removedAt":4},
            {"videoId":"b","itemId":"movie:b","positionSec":1,"durationSec":10,"watched":false,"updatedAt":5,"removedAt":"x"},
            {"videoId":"c","itemId":"movie:c","positionSec":1,"durationSec":10,"watched":false,"updatedAt":5,"removedAt":-1}
        ])"));
        Require(watch.size() == 3 && watch[0].RemovedAt.value_or(0) == 4 && !watch[1].RemovedAt && !watch[2].RemovedAt,
            "a watch-state removal stamp was misread or failed the row");

        // A malformed row is left out and the rest kept, so a PUT's answer
        // still settles the rows that were sent.
        auto const partly = HaloDesktop::Api::Mappers::ParseWatchState(Json(LR"([
            {"videoId":"a","itemId":"movie:a","positionSec":1,"durationSec":10,"updatedAt":5},
            {"videoId":"b","itemId":"movie:b","positionSec":1,"durationSec":10,"watched":"yes","updatedAt":5},
            {"videoId":" ","itemId":"movie:c","positionSec":1,"durationSec":10,"watched":false,"updatedAt":5},
            {"videoId":"d","itemId":"movie:d","positionSec":1,"durationSec":10,"watched":false,"updatedAt":0},
            "row",
            {"videoId":"e","itemId":"movie:e","positionSec":"1","durationSec":10,"watched":false,"updatedAt":5},
            {"videoId":"f","itemId":"movie:f","positionSec":12,"durationSec":10,"watched":true,"updatedAt":5},
            {"videoId":"g","itemId":"movie:g","positionSec":2,"durationSec":10,"watched":false,"updatedAt":6}
        ])"));
        Require(partly.size() == 2 && partly[0].VideoId == L"f" && partly[0].PositionSec == 10 && partly[1].VideoId == L"g",
            "a malformed watch-state row failed the list or was kept");
        auto const library = HaloDesktop::Api::Mappers::ParseLibrary(Json(LR"([
            {"id":"movie:a","type":"movie","addedAt":1,"updatedAt":1},
            7,
            {"id":"movie:b","type":"movie","name":"B","addedAt":1,"removedAt":"x","updatedAt":1},
            {"id":"movie:c","type":"movie","name":"C","addedAt":1,"removedAt":null,"updatedAt":2}
        ])"));
        Require(library.size() == 1 && library[0].Id == L"movie:c" && !library[0].RemovedAt,
            "a malformed library row failed the list or was kept");
        bool libraryNotAList = false;
        try
        {
            static_cast<void>(HaloDesktop::Api::Mappers::ParseLibrary(Json(LR"({"items":[]})")));
        }
        catch (...)
        {
            libraryNotAList = true;
        }
        Require(libraryNotAList, "a library answer that is not a list was read");
    }

    void TestPlanRefusal()
    {
        using HaloDesktop::Api::Mappers::PlanRefusalMessage;
        Require(PlanRefusalMessage(LR"({"code":"plan_required","error":"Downloads need Premium."})").value_or(L"")
                == L"Downloads need Premium.",
            "a plan refusal did not surface the server's sentence");
        Require(PlanRefusalMessage(LR"({"code":"plan_limit_reached","error":" Limit\nreached "})").value_or(L"")
                == L"Limit reached",
            "a plan refusal sentence was not sanitised");
        Require(!PlanRefusalMessage(LR"({"code":"forbidden","error":"No."})"), "another refusal was read as a plan's");
        Require(!PlanRefusalMessage(LR"({"code":"plan_required","error":""})"), "an empty plan sentence was surfaced");
        Require(!PlanRefusalMessage(LR"({"code":"plan_required","error":5})"), "a malformed plan sentence was surfaced");
        Require(!PlanRefusalMessage(L"not json"), "a body that is not JSON was read as a plan refusal");

        namespace ApiError = HaloDesktop::Api::ApiError;
        Require(ApiError::IsPlanRefusal(ApiError::MakePlanRefusal(403))
                && ApiError::HttpStatus(ApiError::MakePlanRefusal(403)).value_or(0) == 403,
            "a plan refusal lost its HTTP status");
        Require(!ApiError::IsPlanRefusal(ApiError::MakeHttpStatus(403))
                && ApiError::HttpStatus(ApiError::MakeHttpStatus(404)).value_or(0) == 404,
            "an ordinary HTTP error was read as a plan refusal");
    }

    // Mirrors the tests in the reference's crash_log.rs.
    void TestCrashLog()
    {
        using HaloDesktop::Diagnostics::CrashLog;
        using HaloDesktop::Diagnostics::CrashLogRotateBytes;
        using HaloDesktop::Diagnostics::CrashLogTimestamp;
        auto const at = [](std::int64_t seconds)
        {
            return CrashLogTimestamp(std::chrono::system_clock::time_point{ std::chrono::seconds{ seconds } });
        };

        // Timestamps are UTC calendar dates.
        Require(at(0) == "1970-01-01T00:00:00Z", "the epoch was misprinted");
        Require(at(951'782'400) == "2000-02-29T00:00:00Z", "a leap day was misprinted");
        Require(at(1'700'000'000) == "2023-11-14T22:13:20Z", "a timestamp was misprinted");
        Require(at(1'735'689'599) == "2024-12-31T23:59:59Z", "the last second of a year was misprinted");
        Require(at(1'735'689'600) == "2025-01-01T00:00:00Z", "the first second of a year was misprinted");
        Require(at(4'107'542'400) == "2100-03-01T00:00:00Z", "a century without a leap day was misprinted");

        auto const scratch = [](std::wstring const& name)
        {
            auto const directory = std::filesystem::temp_directory_path()
                / (L"halo-crash-log-" + name + L"-" + std::to_wstring(GetCurrentProcessId()));
            std::error_code error;
            std::filesystem::remove_all(directory, error);
            return directory;
        };
        auto const readAll = [](std::filesystem::path const& path)
        {
            std::ifstream input{ path, std::ios::binary };
            return std::string{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
        };

        // Each record is one line, in a folder it creates.
        {
            auto const directory = scratch(L"lines");
            CrashLog log{ directory / L"logs" };
            log.Record(L"first");
            log.Record(L"second\r\nstill second");
            auto const text = readAll(log.Path());
            auto const firstEnd = text.find('\n');
            Require(firstEnd != std::string::npos && text.back() == '\n'
                    && text.find('\n', firstEnd + 1) == text.size() - 1,
                "the crash log did not hold exactly two lines");
            auto const first = text.substr(0, firstEnd);
            auto const second = text.substr(firstEnd + 1, text.size() - firstEnd - 2);
            Require(first.ends_with("Z first"), "the first crash line was wrong");
            Require(second.ends_with("Z second  still second"), "line breaks inside a record were not flattened");
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }

        // A full log moves aside and starts over.
        {
            auto const directory = scratch(L"rotate");
            CrashLog log{ directory };
            std::filesystem::create_directories(directory);
            {
                std::ofstream full{ log.Path(), std::ios::binary };
                std::string const filler(static_cast<std::size_t>(CrashLogRotateBytes), 'x');
                full.write(filler.data(), static_cast<std::streamsize>(filler.size()));
            }
            log.Record(L"fresh");
            Require(readAll(log.Path()).ends_with("Z fresh\n"), "a full crash log did not start over");
            Require(std::filesystem::file_size(directory / L"crash.old.log") == CrashLogRotateBytes,
                "a full crash log was not kept aside whole");
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }
    }

    class FakeDiscordTransport final : public HaloDesktop::Services::IDiscordPresenceTransport
    {
    public:
        [[nodiscard]] bool Send(
            std::wstring const& applicationId,
            std::string const& payload) noexcept override
        {
            std::scoped_lock const lock{ Mutex };
            ApplicationIds.push_back(applicationId);
            Payloads.push_back(payload);
            Changed.notify_all();
            if (FailuresRemaining > 0)
            {
                --FailuresRemaining;
                return false;
            }
            return true;
        }

        [[nodiscard]] bool WaitForCount(std::size_t count)
        {
            std::unique_lock lock{ Mutex };
            return Changed.wait_for(lock, std::chrono::seconds{ 2 }, [this, count]
            {
                return Payloads.size() >= count;
            });
        }

        std::mutex Mutex;
        std::condition_variable Changed;
        std::vector<std::wstring> ApplicationIds;
        std::vector<std::string> Payloads;
        int FailuresRemaining{};
    };

    HaloDesktop::Playback::PlaybackState PresenceState()
    {
        return {
            .PositionSeconds = 120.0,
            .DurationSeconds = 3600.0,
            .Speed = 1.0,
            .FileSerial = 7,
            .SeekSerial = 2,
        };
    }

    void TestDiscordPresencePolicyAndService()
    {
        using HaloDesktop::Services::BuildPresenceActivity;
        using HaloDesktop::Services::DiscordPresenceService;
        using HaloDesktop::Services::PresenceMedia;
        using HaloDesktop::Services::PresencePlaybackState;
        using HaloDesktop::Services::SerializeClearActivity;
        using HaloDesktop::Services::SerializeSetActivity;

        auto const capturedAt = std::chrono::system_clock::time_point{ std::chrono::seconds{ 2'000 } };
        auto state = PresenceState();
        auto movie = BuildPresenceActivity(
            { L"Arrival", L"Arrival", L"", L"movie", L"https://images.example.com/posters/arrival.jpg" },
            state,
            capturedAt);
        Require(movie.has_value(), "a playing movie did not produce presence");
        Require(movie->Details == L"Arrival" && movie->State == L"Movie",
            "movie presence did not contain its title and media kind");
        auto movieJson = SerializeSetActivity(*movie, 42);
        Require(movieJson.find("\"start\":1880") != std::string::npos,
            "elapsed playback time was not anchored to the captured position");
        Require(movieJson.find("\"end\":5480") != std::string::npos,
            "remaining playback time was not anchored to the captured duration");
        Require(movieJson.find("https://images.example.com/posters/arrival.jpg") != std::string::npos,
            "the public movie poster was not used as Discord artwork");
        Require(movieJson.find("\"type\":3") != std::string::npos,
            "Discord presence was not identified as Watching");

        constexpr wchar_t const* unsafePosters[]{
            L"http://images.example.com/poster.jpg",
            L"https://user:password@images.example.com/poster.jpg",
            L"https://images.example.com/poster.jpg?token=secret",
            L"https://images.example.com/poster.jpg#fragment",
            L"https://127.0.0.1/poster.jpg",
            L"https://192.168.1.2/poster.jpg",
            L"https://poster.local/poster.jpg",
            L"file:///C:/poster.jpg",
        };
        for (auto const* unsafePoster : unsafePosters)
        {
            auto fallback = BuildPresenceActivity(
                { L"Arrival", L"Arrival", L"", L"movie", unsafePoster }, state, capturedAt);
            Require(fallback && fallback->ArtworkUrl.empty(),
                "an unsafe poster URL crossed the Discord presence boundary");
            auto const fallbackJson = SerializeSetActivity(*fallback, 42);
            Require(fallbackJson.find("\"large_image\":\"halo\"") != std::string::npos,
                "unsafe poster artwork did not fall back to the Halo asset");
        }

        auto episode = BuildPresenceActivity(
            { L"The Ones Who Live", L"The Walking Dead", L"S01E06", L"series" }, state, capturedAt);
        Require(episode.has_value(), "a playing episode did not produce presence");
        Require(episode->Details == L"The Walking Dead"
                && episode->State == L"S01E06 \u00b7 The Ones Who Live",
            "episode presence did not contain the series, episode, and title");

        state.Paused = true;
        auto paused = BuildPresenceActivity({ L"Arrival", L"", L"" }, state, capturedAt);
        Require(paused && paused->Playback == PresencePlaybackState::Paused,
            "paused playback was not represented as paused");
        auto pausedJson = SerializeSetActivity(*paused, 42);
        Require(pausedJson.find("Paused") != std::string::npos
                && pausedJson.find("timestamps") == std::string::npos,
            "paused presence showed moving timestamps or omitted its state");

        state.Paused = false;
        state.Buffering = true;
        auto buffering = BuildPresenceActivity({ L"Arrival", L"", L"" }, state, capturedAt);
        auto bufferingJson = SerializeSetActivity(*buffering, 42);
        Require(bufferingJson.find("Buffering") != std::string::npos
                && bufferingJson.find("timestamps") == std::string::npos,
            "buffering presence showed moving timestamps or omitted its state");

        state = PresenceState();
        std::wstring invalidTitle(200, L'x');
        invalidTitle[2] = static_cast<wchar_t>(0xd800);
        invalidTitle[3] = L'\n';
        auto sanitized = BuildPresenceActivity({ invalidTitle, L"", L"" }, state, capturedAt);
        Require(sanitized && winrt::to_string(winrt::hstring{ sanitized->Details }).size() <= 128,
            "Discord text exceeded its UTF-8 byte limit");
        auto sanitizedJson = SerializeSetActivity(*sanitized, 42);
        Require(sanitizedJson.find("https://private.invalid") == std::string::npos
                && sanitizedJson.find("Authorization") == std::string::npos
                && sanitizedJson.find("addon-secret") == std::string::npos,
            "presence serialization admitted data outside the display-safe contract");
        Require(SerializeClearActivity(42).find("\"activity\":null") != std::string::npos,
            "clearing presence did not send a null activity");

        auto transport = std::make_shared<FakeDiscordTransport>();
        {
            DiscordPresenceService service{ false, transport, std::chrono::milliseconds{ 5 } };
            service.SetMedia({ L"Arrival", L"", L"" });
            service.Update(PresenceState());
            Require(!transport->WaitForCount(1), "disabled Rich Presence published activity");
            service.SetEnabled(true);
            Require(transport->WaitForCount(1), "enabling Rich Presence did not publish current playback");
            {
                std::scoped_lock const lock{ transport->Mutex };
                Require(transport->ApplicationIds.front() == L"1544266293249712128",
                    "the Discord transport received the wrong Application ID");
            }
            service.SetEnabled(false);
            Require(transport->WaitForCount(2), "disabling Rich Presence did not clear current playback");
            {
                std::scoped_lock const lock{ transport->Mutex };
                Require(transport->Payloads.back().find("\"activity\":null") != std::string::npos,
                    "disabling Rich Presence did not send a null activity");
            }
            service.SetEnabled(true);
            service.Update(PresenceState());
            Require(transport->WaitForCount(3), "re-enabling Rich Presence did not replay current playback");
            service.Clear();
            Require(transport->WaitForCount(4), "clearing Rich Presence did not reach the transport");
            std::scoped_lock const lock{ transport->Mutex };
            Require(transport->Payloads.back().find("\"activity\":null") != std::string::npos,
                "the service clear operation did not send a null activity");
        }

        auto localFileTransport = std::make_shared<FakeDiscordTransport>();
        {
            DiscordPresenceService service{ true, localFileTransport, std::chrono::milliseconds{ 5 } };
            service.SetMedia({
                L"Spider-Man: No Way Home",
                L"Spider-Man: No Way Home",
                L"",
                L"movie",
                L"https://images.example.com/posters/spider-man.jpg" });
            auto loading = PresenceState();
            loading.FileSerial = 0;
            loading.Buffering = true;
            service.Update(loading);
            Require(!localFileTransport->WaitForCount(1),
                "pre-load local playback published an invalid activity");
            service.Update(PresenceState());
            Require(localFileTransport->WaitForCount(1),
                "a local file lost its metadata before FILE_LOADED");
            std::scoped_lock const lock{ localFileTransport->Mutex };
            Require(localFileTransport->Payloads.back().find("Spider-Man: No Way Home") != std::string::npos,
                "local file presence did not publish its title");
            Require(localFileTransport->Payloads.back().find("https://images.example.com/posters/spider-man.jpg") != std::string::npos,
                "local file presence did not publish its stored poster");
        }

        auto retryTransport = std::make_shared<FakeDiscordTransport>();
        retryTransport->FailuresRemaining = 1;
        auto const shutdownStart = std::chrono::steady_clock::now();
        {
            DiscordPresenceService service{ true, retryTransport, std::chrono::milliseconds{ 5 } };
            service.SetMedia({ L"Arrival", L"", L"" });
            service.Update(PresenceState());
            Require(retryTransport->WaitForCount(2), "a failed Discord connection was not retried");
        }
        Require(std::chrono::steady_clock::now() - shutdownStart < std::chrono::seconds{ 2 },
            "Discord worker shutdown blocked application teardown");
    }

    void TestCatalogDirtySingleFlight()
    {
        HaloDesktop::Services::CatalogRefreshState state;
        Require(!state.HasLoaded() && state.IsDirty(), "initial catalog state was not refreshable");

        auto const first = state.TryBeginRefresh();
        Require(first.has_value(), "initial catalog refresh could not begin");
        Require(!state.TryBeginRefresh().has_value(), "a competing catalog refresh was admitted");

        state.Invalidate();
        state.CompleteRefresh(*first, true);
        Require(state.HasLoaded(), "a committed catalog snapshot was not marked loaded");
        Require(state.IsDirty(), "an in-flight refresh cleared a newer invalidation");

        auto const second = state.TryBeginRefresh();
        Require(second.has_value(), "the newer catalog invalidation was not retryable");
        state.CompleteRefresh(*second, true);
        Require(!state.IsDirty(), "the current catalog refresh did not clear dirty state");

        state.Invalidate();
        auto const failed = state.TryBeginRefresh();
        Require(failed.has_value(), "a dirty catalog refresh could not begin");
        state.CompleteRefresh(*failed, false);
        Require(state.IsDirty() && !state.RefreshInFlight(), "a failed refresh did not remain retryable");
    }

    void TestHomeVisibility()
    {
        auto const retained = HaloDesktop::ViewModels::ResolveHomeVisibility(true, false, true);
        Require(retained.ShowContent && !retained.ShowLoading,
                "existing Home content was hidden by a refresh");

        auto const continueOnly = HaloDesktop::ViewModels::ResolveHomeVisibility(false, false, true);
        Require(continueOnly.ShowContent && !continueOnly.ShowEmpty,
                "Continue Watching overlapped the empty state");

        auto const empty = HaloDesktop::ViewModels::ResolveHomeVisibility(false, false, false);
        Require(empty.ShowEmpty && !empty.ShowContent, "a valid empty Home snapshot was not shown as empty");

        auto const failed = HaloDesktop::ViewModels::ResolveHomeVisibility(false, true, false);
        Require(failed.ShowError && !failed.ShowEmpty, "an initial Home failure was presented as empty");
    }

    void TestFilteredFeaturedSelection()
    {
        std::vector<HaloDesktop::ViewModels::HomeMediaKind> const kinds{
            HaloDesktop::ViewModels::HomeMediaKind::Series,
            HaloDesktop::ViewModels::HomeMediaKind::Movie,
            HaloDesktop::ViewModels::HomeMediaKind::Series,
        };

        auto const all = HaloDesktop::ViewModels::FirstMatchingHomeItem(
            HaloDesktop::ViewModels::HomeFilter::All, kinds);
        auto const movie = HaloDesktop::ViewModels::FirstMatchingHomeItem(
            HaloDesktop::ViewModels::HomeFilter::Movies, kinds);
        auto const series = HaloDesktop::ViewModels::FirstMatchingHomeItem(
            HaloDesktop::ViewModels::HomeFilter::Series, kinds);
        Require(all && *all == 0, "the All filter did not select the first catalog title");
        Require(movie && *movie == 1, "the Movies filter did not select the first movie");
        Require(series && *series == 0, "the Series filter did not select the first series");

        std::vector<HaloDesktop::ViewModels::HomeMediaKind> const moviesOnly{
            HaloDesktop::ViewModels::HomeMediaKind::Movie,
        };
        Require(
            !HaloDesktop::ViewModels::FirstMatchingHomeItem(
                HaloDesktop::ViewModels::HomeFilter::Series, moviesOnly),
            "a filtered hero was fabricated when no catalog title matched");
    }

    // Backdrops lead, order is catalog order inside each group, keys never
    // repeat and unaddressable titles never earn a slot.
    void TestSelectFeaturedItems()
    {
        using HaloDesktop::ViewModels::FeaturedCandidate;
        using HaloDesktop::ViewModels::HomeFilter;
        using HaloDesktop::ViewModels::HomeMediaKind;
        using HaloDesktop::ViewModels::SelectFeaturedItems;

        std::vector<FeaturedCandidate> const candidates{
            { L"movie:a", HomeMediaKind::Movie, false },
            { L"movie:b", HomeMediaKind::Movie, true },
            { L"series:c", HomeMediaKind::Series, true },
            { L"movie:a", HomeMediaKind::Movie, true }, // same title again, higher in rank
            { L"movie:d", HomeMediaKind::Movie, false },
            { L"", HomeMediaKind::Movie, true }, // unaddressable: no type:id to open
            { L"series:e", HomeMediaKind::Series, false },
        };

        auto const allTwo = SelectFeaturedItems(HomeFilter::All, candidates, 2);
        Require(allTwo.size() == 2 && allTwo[0] == 1 && allTwo[1] == 2,
            "the featured strip did not lead with the top backdrops in catalog order");

        // index 3 is the duplicate of index 0, index 5 has an empty key, so the
        // fifth slot is the last non-backdrop in catalog order, not a repeat.
        auto const allFive = SelectFeaturedItems(HomeFilter::All, candidates, 5);
        Require(allFive == std::vector<std::size_t>({ 1, 2, 3, 4, 6 }),
            "non-backdrops did not top up the strip in catalog order, or a duplicate took a slot");

        auto const movies = SelectFeaturedItems(HomeFilter::Movies, candidates, 10);
        Require(movies == std::vector<std::size_t>({ 1, 3, 4 }),
            "a non-movie leaked into the Movies strip");

        auto const series = SelectFeaturedItems(HomeFilter::Series, candidates, 10);
        Require(series == std::vector<std::size_t>({ 2, 6 }),
            "a non-series leaked into the Series strip");

        Require(SelectFeaturedItems(HomeFilter::All, candidates, 0).empty(),
            "a zero-width featured strip was filled");
        Require(SelectFeaturedItems(HomeFilter::All, {}, 5).empty(),
            "a featured strip was fabricated from an empty pool");
    }

    HaloDesktop::Services::Downloads::DownloadStartRequest ProtectedVideoRequest()
    {
        return {
            .Media = {
                .VideoId = L"movie:test",
                .ItemId = L"movie:test",
                .MediaType = L"movie",
                .Title = L"Test movie",
            },
            .Request = {
                .Url = L"https://video.invalid/protected",
                .Headers = { { L"Authorization", L"video-secret" } },
            },
        };
    }

    void TestOptionalSubtitleFallback()
    {
        constexpr std::string_view failureStages[]{ "hash", "lookup", "proxy" };
        for (auto const failureStage : failureStages)
        {
            auto prepared = HaloDesktop::Services::Downloads::
                PrepareDownloadWithOptionalSubtitleAsync(
                    ProtectedVideoRequest(),
                    [failureStage]()
                    {
                        return concurrency::create_task([failureStage]()
                            -> std::optional<HaloDesktop::Services::Downloads::SubtitleRequest>
                        {
                            throw std::runtime_error{ std::string{ failureStage } };
                        });
                    }).get();
            Require(!prepared.Request.Subtitle, "a failed subtitle preparation left a sidecar attached");
            Require(
                prepared.Request.Url == L"https://video.invalid/protected",
                "a subtitle failure changed the protected video URL");
            Require(
                prepared.Request.Headers.at(L"Authorization") == L"video-secret",
                "a subtitle failure changed the protected video headers");
        }

        auto prepared = HaloDesktop::Services::Downloads::PrepareDownloadWithOptionalSubtitleAsync(
            ProtectedVideoRequest(),
            []()
            {
                return concurrency::task_from_result(
                    std::optional<HaloDesktop::Services::Downloads::SubtitleRequest>{ {
                        .Url = L"https://subtitle.invalid/protected",
                        .Language = L"eng",
                        .Id = L"subtitle:test",
                        .Headers = { { L"Authorization", L"subtitle-secret" } },
                    } });
            }).get();
        Require(prepared.Request.Subtitle.has_value(), "a prepared subtitle sidecar was discarded");
        Require(
            prepared.Request.Subtitle->Headers.at(L"Authorization") == L"subtitle-secret",
            "protected subtitle headers were not preserved");
    }

    void TestDownloadPageOperationLifetime()
    {
        HaloDesktop::Services::Downloads::DownloadPageOperationState state;
        state.NavigatedTo();
        Require(!state.TryBegin(), "an unloaded Sources page admitted a download action");
        state.Loaded();

        auto const first = state.TryBegin();
        Require(first.has_value(), "a loaded Sources page rejected its first download action");
        Require(!state.TryBegin(), "competing download preparation was admitted");
        Require(state.CanApply(*first), "the current download operation was treated as stale");

        state.Unloaded();
        Require(!state.CanApply(*first), "a navigated-away Sources page could still apply completion");
        state.Complete(*first);
        Require(!state.InFlight(), "a stale completed operation left the page permanently busy");

        state.NavigatedTo();
        state.Loaded();
        auto const second = state.TryBegin();
        Require(second.has_value(), "a reloaded Sources page could not start a new operation");
        Require(!state.CanApply(*first), "an old page operation matched a newer page generation");
        Require(state.CanApply(*second), "the newer page operation was not current");
        state.Complete(*second);
    }

    void TestMutableDownloadRowBindings()
    {
        auto xamlPath = std::filesystem::path{ L"DownloadsPage.xaml" };
        if (!std::filesystem::is_regular_file(xamlPath))
        {
            xamlPath = L"src/HaloDesktop/Views/DownloadsPage.xaml";
        }
        std::ifstream input{ xamlPath, std::ios::binary };
        if (!input)
        {
            throw std::runtime_error{ "the Downloads page XAML test resource was unavailable" };
        }
        auto const xaml = std::string{
            std::istreambuf_iterator<char>{ input },
            std::istreambuf_iterator<char>{} };
        // Every row property the page draws that changes while the row lives
        // (progress ticks, state changes, a source chosen again, details
        // opened). Each must be bound, and bound one-way, or a reused row keeps
        // what it first showed.
        constexpr std::string_view mutableProperties[]{
            "EpisodeLabel",
            "Name",
            "CardTitle",
            "AccessibleName",
            "MetaId",
            "RowArtwork",
            "Progress",
            "IsPaused",
            "Lead",
            "MetaLead",
            "MetaMiddle",
            "MetaLast",
            "ToneNormalVisibility",
            "ToneCautionVisibility",
            "ToneCriticalVisibility",
            "ThumbOpacity",
            "ChooseSourceVisibility",
            "PauseVisibility",
            "ResumeVisibility",
            "RetryVisibility",
            "ChevronRotation",
            "DetailsVisibility",
            "QualityLine",
            "SizeFactLabel",
            "DownloadedLine",
            "Subs",
            "AddedFact",
            "FileName",
        };
        for (auto const property : mutableProperties)
        {
            auto const binding = std::string{ "{x:Bind " } + std::string{ property };
            auto position = xaml.find(binding);
            auto foundExact = false;
            while (position != std::string::npos)
            {
                auto const mode = position + binding.size();
                if (mode < xaml.size() && (xaml[mode] == ',' || xaml[mode] == '}'))
                {
                    foundExact = true;
                    Require(
                        xaml.compare(mode, std::string_view{ ", Mode=OneWay}" }.size(), ", Mode=OneWay}") == 0,
                        "a mutable download row property used a one-time binding");
                }
                position = xaml.find(binding, mode);
            }
            Require(foundExact, "a mutable download row property was not bound");
        }
    }

    // The ready card is handed a live row and reads every field of it, so each
    // of its bindings must follow the row as a transfer finishes or a selection moves.
    void TestReadyCardBindingsFollowTheRow()
    {
        auto xamlPath = std::filesystem::path{ L"DownloadReadyCard.xaml" };
        if (!std::filesystem::is_regular_file(xamlPath))
        {
            xamlPath = L"src/HaloDesktop/Views/DownloadReadyCard.xaml";
        }
        std::ifstream input{ xamlPath, std::ios::binary };
        if (!input)
        {
            throw std::runtime_error{ "the ready card XAML test resource was unavailable" };
        }
        auto const xaml = std::string{
            std::istreambuf_iterator<char>{ input },
            std::istreambuf_iterator<char>{} };
        constexpr std::string_view binding{ "{x:Bind Row." };
        auto count = 0;
        for (auto position = xaml.find(binding); position != std::string::npos; position = xaml.find(binding, position + binding.size()))
        {
            ++count;
            auto const close = xaml.find('}', position);
            Require(close != std::string::npos, "a ready card binding was not closed");
            Require(
                std::string_view{ xaml }.substr(position, close - position + 1).ends_with(", Mode=OneWay}"),
                "a ready card binding to its row was one-time");
        }
        Require(count >= 10, "the ready card no longer binds its row");
    }

    void TestLandscapeArtworkSelection()
    {
        HaloDesktop::Services::LandscapeArtworkSet artwork{
            .Background = L"https://images.example.test/backdrop.jpg",
            .Thumbnails = {
                { L"series:one:1:2", L"https://images.example.test/episode.jpg" },
            },
        };
        Require(
            HaloDesktop::Services::SelectLandscapeArtwork(L"series:one:1:2", artwork)
                == L"https://images.example.test/episode.jpg",
            "an episode did not prefer its exact thumbnail");
        Require(
            HaloDesktop::Services::SelectLandscapeArtwork(L"series:one:1:3", artwork)
                == L"https://images.example.test/backdrop.jpg",
            "a missing episode thumbnail did not fall back to the title backdrop");
        artwork.Background.clear();
        Require(
            HaloDesktop::Services::SelectLandscapeArtwork(L"movie:one", artwork).empty(),
            "missing landscape metadata invented an artwork URL");
    }

    void TestBrowserSignInListenerCancellation()
    {
        auto cancelAndAwait = []()
        {
            HaloDesktop::Services::Auth::LoopbackListener listener{ std::chrono::seconds{ 2 } };
            auto pending = listener.WaitAsync();
            listener.Cancel();
            auto cancelled = false;
            try
            {
                static_cast<void>(pending.get());
            }
            catch (std::runtime_error const&)
            {
                cancelled = true;
            }
            Require(cancelled, "browser sign-in cancellation did not complete its listener");
        };

        cancelAndAwait();
        cancelAndAwait();

        HaloDesktop::Services::Auth::LoopbackListener listener{ std::chrono::seconds{ 5 } };
        auto pending = listener.WaitAsync();
        auto const stalledClient = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        Require(stalledClient != INVALID_SOCKET,
            "the cancellation test could not create a stalled loopback client");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(17871);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        Require(connect(stalledClient, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != SOCKET_ERROR,
            "the cancellation test could not occupy the loopback listener");
        auto const started = std::chrono::steady_clock::now();
        listener.Cancel();
        auto const elapsed = std::chrono::steady_clock::now() - started;
        closesocket(stalledClient);
        Require(elapsed < std::chrono::seconds{ 2 },
            "browser sign-in cancellation waited for a stalled loopback client");
        auto stalledCancelled = false;
        try
        {
            static_cast<void>(pending.get());
        }
        catch (std::runtime_error const&)
        {
            stalledCancelled = true;
        }
        Require(stalledCancelled,
            "a stalled browser sign-in listener completed successfully after cancellation");
    }

    // The first line the listener answers a request with.
    std::string SendToSignInListener(std::string const& path)
    {
        auto const client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        Require(client != INVALID_SOCKET, "the listener test could not create a client");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(17871);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        std::string answer;
        if (connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != SOCKET_ERROR)
        {
            auto const request = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
            static_cast<void>(send(client, request.data(), static_cast<int>(request.size()), 0));
            std::array<char, 4096> buffer{};
            int received{};
            while ((received = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0)) > 0)
            {
                answer.append(buffer.data(), static_cast<std::size_t>(received));
            }
        }
        closesocket(client);
        return answer.substr(0, answer.find("\r\n"));
    }

    // Another program or page on this PC can reach the port: neither `/cancel`
    // nor a callback for another sign-in ends this one.
    void TestBrowserSignInListenerOnlyAnswersItsOwnCallback()
    {
        using HaloDesktop::Services::Auth::CallbackCarriesState;
        Require(CallbackCarriesState("/callback?code=a&state=s1", L"s1"), "the sign-in's own state was not recognised");
        Require(CallbackCarriesState("/callback?state=a%2Bb", L"a+b"), "an encoded state was not decoded");
        Require(CallbackCarriesState("/callback?error=access_denied&state=s1", L"s1"), "a provider error with the state was refused");
        Require(!CallbackCarriesState("/callback?code=a&state=s2", L"s1"), "another sign-in's state was accepted");
        Require(!CallbackCarriesState("/callback?code=a", L"s1"), "a callback without a state was accepted");
        Require(!CallbackCarriesState("/callback", L"s1"), "a callback without a query was accepted");
        Require(!CallbackCarriesState("/callback?state=s1x", L"s1"), "a longer state was accepted");

        using HaloDesktop::Services::Auth::LoopbackListener;
        {
            LoopbackListener listener{ std::chrono::seconds{ 10 }, std::wstring{ L"s1" } };
            auto pending = listener.WaitAsync();
            Require(SendToSignInListener("/cancel").starts_with("HTTP/1.1 404"), "a request for /cancel was not refused");
            Require(SendToSignInListener("/callback?code=x&state=other").starts_with("HTTP/1.1 404"),
                "a callback for another sign-in was not refused");
            Require(SendToSignInListener("/callback?code=x").starts_with("HTTP/1.1 404"),
                "a callback without the state was not refused");
            Require(!pending.is_done(), "a request that was not this sign-in's callback ended it");
            Require(SendToSignInListener("/callback?error=access_denied&state=s1").starts_with("HTTP/1.1 200"),
                "the provider's error answer for this sign-in was not accepted");
            Require(pending.get() == L"/callback?error=access_denied&state=s1",
                "the sign-in did not complete with its own callback");
        }
        {
            // Without an expected state the first callback still completes it,
            // as before; only the cancel path is gone.
            LoopbackListener listener{ std::chrono::seconds{ 10 } };
            auto pending = listener.WaitAsync();
            Require(SendToSignInListener("/cancel").starts_with("HTTP/1.1 404"), "a request for /cancel was not refused");
            Require(!pending.is_done(), "a request for /cancel ended the sign-in");
            Require(SendToSignInListener("/callback?code=x&state=any").starts_with("HTTP/1.1 200"),
                "a callback was refused without an expected state");
            Require(pending.get() == L"/callback?code=x&state=any", "the first callback did not complete the sign-in");
        }
    }
    void TestEpisodePositionParsing()
    {
        using HaloDesktop::Services::Downloads::ParseEpisodePosition;

        auto const at = [](int season, int episode) { return std::optional{ std::pair{ season, episode } }; };

        Require(ParseEpisodePosition(L"S01E05") == at(1, 5), "the label form we write did not parse");
        Require(ParseEpisodePosition(L"S10E05") == at(10, 5), "a two digit season did not parse");

        // The label is written with a minimum width, not a fixed one, so a show
        // past its hundredth episode writes more digits than the season does.
        Require(ParseEpisodePosition(L"S01E100") == at(1, 100), "a three digit episode did not parse");
        Require(ParseEpisodePosition(L"S01E1085") == at(1, 1085), "a four digit episode did not parse");
        Require(ParseEpisodePosition(L"S100E01") == at(100, 1), "a three digit season did not parse");

        // Ordering is what selects the next episode, so it has to hold across a
        // change in digit count rather than comparing the labels as text.
        Require(ParseEpisodePosition(L"S01E99") < ParseEpisodePosition(L"S01E100"), "episode 100 did not sort after episode 99");
        Require(ParseEpisodePosition(L"S01E100") < ParseEpisodePosition(L"S02E01"), "the next season did not sort after this one");

        Require(ParseEpisodePosition(L"s01e05") == at(1, 5), "a lowercase label did not parse");
        Require(ParseEpisodePosition(L"S1E5") == at(1, 5), "an unpadded label did not parse");
        Require(ParseEpisodePosition(L"S01 E05") == at(1, 5), "a spaced label did not parse");
        Require(ParseEpisodePosition(L" S01E05 ") == at(1, 5), "a padded label did not parse");
        Require(ParseEpisodePosition(L"S00E01") == at(0, 1), "a specials season did not parse");

        Require(!ParseEpisodePosition(std::nullopt), "a missing label parsed");
        Require(!ParseEpisodePosition(L""), "an empty label parsed");
        Require(!ParseEpisodePosition(L"S01"), "a season without an episode parsed");
        Require(!ParseEpisodePosition(L"E05"), "an episode without a season parsed");
        Require(!ParseEpisodePosition(L"SE"), "a label with no numbers parsed");
        Require(!ParseEpisodePosition(L"S01E05x"), "trailing text was ignored");
        Require(!ParseEpisodePosition(L"Season 1"), "free text parsed");
        // Refused rather than truncated: a run this long is not an episode number,
        // and accepting it would be accepting an overflow.
        Require(!ParseEpisodePosition(L"S01E123456"), "an implausibly long number parsed");
    }

    HaloDesktop::Services::ContinueWatchRow WatchRow(
        wchar_t const* itemId,
        wchar_t const* videoId,
        wchar_t const* name,
        double position,
        double duration,
        bool watched,
        std::int64_t updatedAt)
    {
        return { itemId, videoId, name, L"poster.jpg", position, duration, watched, updatedAt };
    }

    void TestContinueShelf()
    {
        using HaloDesktop::Services::BuildContinueShelf;
        using HaloDesktop::Services::ContinueCardKind;
        using HaloDesktop::Services::ContinueWatchRow;
        using HaloDesktop::Services::IsFinishedWatchRow;
        using HaloDesktop::Services::MetaIdFromItemId;
        using HaloDesktop::Services::NextEpisodeLookup;
        using HaloDesktop::Services::NextEpisodeState;
        using HaloDesktop::Services::TypeFromItemId;

        Require(TypeFromItemId(L"series:tt5651844") == L"series", "a series item id did not report its type");
        Require(TypeFromItemId(L"tt10872600") == L"movie", "a bare item id was not treated as a film");
        Require(MetaIdFromItemId(L"series:tt5651844") == L"tt5651844", "the meta id was not taken from the item id");
        Require(MetaIdFromItemId(L"tt10872600") == L"tt10872600", "a bare item id lost its meta id");

        // The reporter sets the flag past ninety percent, and the fraction covers a
        // row written by something that did not set it.
        Require(IsFinishedWatchRow(WatchRow(L"series:ttD", L"ttD:1:1", L"D", 960.0, 1000.0, false, 1)),
                "a row past the finished mark was not treated as finished");
        Require(!IsFinishedWatchRow(WatchRow(L"series:ttD", L"ttD:1:1", L"D", 940.0, 1000.0, false, 1)),
                "a row short of the finished mark was treated as finished");

        auto const unresolved = [](std::wstring const&) { return NextEpisodeLookup{}; };

        // The shape that reported this: a series watched through in order with its
        // newest episode finished, a stray mis-click on an older one, one film seen
        // to the end, and one thing genuinely left partway through.
        std::vector<ContinueWatchRow> const rows{
            WatchRow(L"series:tt5651844", L"tt5651844:3:3", L"Travelers", 2735.0, 2860.0, true, 1000),
            WatchRow(L"movie:tt10872600", L"tt10872600", L"Spider-Man", 9415.0, 9415.0, true, 950),
            WatchRow(L"series:tt5651844", L"tt5651844:2:7", L"Travelers", 49.0, 2683.0, false, 900),
            WatchRow(L"series:tt14688458", L"tt14688458:1:1", L"Silo", 332.0, 3563.0, false, 800),
        };

        auto const cold = BuildContinueShelf(rows, unresolved, 8, 8);
        Require(cold.Cards.size() == 1, "the shelf showed something before the lookup answered");
        Require(cold.Cards[0].Name == L"Silo", "the in-progress row was lost");
        Require(cold.Requests.size() == 1, "the finished series did not produce exactly one request");
        Require(cold.Requests[0].Type == L"series", "the request carried the wrong type");
        Require(cold.Requests[0].MetaId == L"tt5651844", "the request carried the wrong meta id");
        Require(cold.Requests[0].VideoId == L"tt5651844:3:3",
                "the request did not step past the newest finished episode");

        auto const warm = BuildContinueShelf(
            rows,
            [](std::wstring const& key)
            {
                return key == L"tt5651844:3:3"
                    ? NextEpisodeLookup{ NextEpisodeState::Resolved, L"tt5651844:3:4" }
                    : NextEpisodeLookup{};
            },
            8,
            8);
        Require(warm.Cards.size() == 2, "the resolved shelf did not gain the promoted card");
        Require(warm.Cards[0].Name == L"Travelers", "the promoted card did not sort by its finished row");
        Require(warm.Cards[0].Kind == ContinueCardKind::NextEpisode, "the promoted card was not marked as one");
        Require(warm.Cards[0].VideoId == L"tt5651844:3:4", "the promoted card did not point at the next episode");
        Require(warm.Cards[0].PositionSec == 0.0 && warm.Cards[0].DurationSec == 0.0,
                "an episode that was never started carried progress");
        Require(warm.Cards[1].Name == L"Silo" && warm.Cards[1].Kind == ContinueCardKind::Resume,
                "the resume card did not survive promotion happening above it");
        Require(warm.Requests.empty(), "a series that was already resolved was asked about again");

        // The finished episode answers for its show whether or not it draws
        // anything, so the older mis-click cannot speak in its place.
        auto const finale = BuildContinueShelf(
            rows,
            [](std::wstring const&) { return NextEpisodeLookup{ NextEpisodeState::None, L"" }; },
            8,
            8);
        Require(finale.Cards.size() == 1 && finale.Cards[0].Name == L"Silo",
                "a series stayed on the shelf after its last episode");
        Require(finale.Requests.empty(), "a series known to have ended was asked about again");

        // A row that cannot be drawn, one with no duration, and one barely opened.
        // None of them may produce a card or a lookup.
        std::vector<ContinueWatchRow> const unusable{
            WatchRow(L"series:ttA", L"ttA:1:1", L"", 100.0, 1000.0, false, 50),
            WatchRow(L"movie:ttB", L"ttB", L"No duration", 100.0, 0.0, false, 40),
            WatchRow(L"movie:ttC", L"ttC", L"Barely opened", 10.0, 1000.0, false, 30),
        };
        auto const skipped = BuildContinueShelf(unusable, unresolved, 8, 8);
        Require(skipped.Cards.empty(), "an unusable row produced a card");
        Require(skipped.Requests.empty(), "an unusable row produced a lookup");

        // A full shelf still leads with the promoted card, because the walk is in
        // newest-first order and the cap only trims the tail.
        std::vector<ContinueWatchRow> many{
            WatchRow(L"series:tt9000000", L"tt9000000:1:1", L"Newest", 600.0, 610.0, true, 100),
        };
        for (int index = 0; index < 10; ++index)
        {
            auto const suffix = std::to_wstring(index);
            many.push_back({ L"movie:tt" + suffix, L"tt" + suffix, L"Film " + suffix, L"", 100.0, 1000.0, false, 90 - index });
        }
        auto const capped = BuildContinueShelf(
            many,
            [](std::wstring const&) { return NextEpisodeLookup{ NextEpisodeState::Resolved, L"tt9000000:1:2" }; },
            8,
            8);
        Require(capped.Cards.size() == 8, "the shelf exceeded its cap");
        Require(capped.Cards[0].Kind == ContinueCardKind::NextEpisode,
                "the promoted card lost the front of a full shelf");
        Require(capped.Cards[7].Name == L"Film 6", "the cap trimmed from the wrong end");
    }

    HaloDesktop::Api::Dto::WatchEntry StoredWatchEntry(
        wchar_t const* videoId,
        double position,
        double duration,
        bool watched,
        std::int64_t updatedAt)
    {
        HaloDesktop::Api::Dto::WatchEntry row;
        row.VideoId = videoId;
        row.ItemId = L"series:tt1";
        row.PositionSec = position;
        row.DurationSec = duration;
        row.Watched = watched;
        row.Name = winrt::hstring{ L"Stored name" };
        row.Poster = winrt::hstring{ L"https://example.test/stored.jpg" };
        row.UpdatedAt = updatedAt;
        return row;
    }

    void TestWatchMarks()
    {
        using HaloDesktop::Services::EarlierEpisodes;
        using HaloDesktop::Services::EpisodeSlot;
        using HaloDesktop::Services::UnwatchedRow;
        using HaloDesktop::Services::WatchedRow;
        using HaloDesktop::Services::WatchedRowsInOrder;
        using HaloDesktop::Services::WatchMarkTarget;

        WatchMarkTarget const target{ L"tt1:1:2", L"series:tt1", L"Show", L"https://example.test/poster.jpg" };

        // A measured length beats the addon's runtime, which beats nothing at all.
        auto const measured = WatchedRow(target, StoredWatchEntry(L"tt1:1:2", 300.0, 2700.0, false, 10), 3000.0, 1000);
        Require(measured.Watched && measured.DurationSec == 2700.0 && measured.PositionSec == 2700.0,
                "marking watched replaced a measured length");
        auto const estimated = WatchedRow(target, std::nullopt, 3000.0, 1000);
        Require(estimated.DurationSec == 3000.0 && estimated.PositionSec == 3000.0,
                "marking watched ignored the addon's runtime");
        auto const unknown = WatchedRow(target, std::nullopt, 0.0, 1000);
        Require(unknown.Watched && unknown.DurationSec == 0.0 && unknown.PositionSec == 0.0,
                "marking watched invented a length");
        Require(estimated.UpdatedAt == 1000, "a first mark was not stamped with the current time");
        Require(estimated.Name && *estimated.Name == L"Show" && estimated.Poster
                    && *estimated.Poster == L"https://example.test/poster.jpg",
                "a mark lost the display fields it was given");

        // A row from a device whose clock ran ahead still loses to the mark.
        auto const ahead = WatchedRow(target, StoredWatchEntry(L"tt1:1:2", 300.0, 2700.0, false, 5000), 0.0, 1000);
        Require(ahead.UpdatedAt == 5001, "a mark was not newer than the row it replaces");

        auto const cleared = UnwatchedRow(target, StoredWatchEntry(L"tt1:1:2", 2700.0, 2700.0, true, 10), 1000);
        Require(!cleared.Watched && cleared.PositionSec == 0.0 && cleared.DurationSec == 0.0,
                "marking unwatched left progress behind");

        WatchMarkTarget const bare{ L"tt1:1:2", L"series:tt1", L"", L"" };
        auto const kept = UnwatchedRow(bare, StoredWatchEntry(L"tt1:1:2", 10.0, 2700.0, false, 10), 1000);
        Require(kept.Name && *kept.Name == L"Stored name" && kept.Poster
                    && *kept.Poster == L"https://example.test/stored.jpg",
                "a mark without display fields dropped the stored ones");
        auto const nameless = UnwatchedRow(bare, std::nullopt, 1000);
        Require(!nameless.Name && !nameless.Poster, "a mark without a name sent an empty one");

        WatchMarkTarget const longName{ L"tt1:1:2", L"series:tt1", winrt::hstring{ std::wstring(600, L'x') }, L"" };
        Require(WatchedRow(longName, std::nullopt, 0.0, 1000).Name->size() == 512,
                "a mark sent a name longer than the server accepts");

        // Stamped strictly in the order given, even past rows written in the future.
        std::vector<WatchMarkTarget> const run{
            { L"tt1:1:1", L"series:tt1", L"Show", L"" },
            { L"tt1:1:2", L"series:tt1", L"Show", L"" },
            { L"tt1:1:3", L"series:tt1", L"Show", L"" },
        };
        auto const ordered = WatchedRowsInOrder(
            run, { StoredWatchEntry(L"tt1:1:2", 100.0, 2700.0, false, 9000) }, 3000.0, 1000);
        Require(ordered.size() == 3, "a run of marks lost a row");
        Require(ordered[0].UpdatedAt == 1000 && ordered[1].UpdatedAt == 9001 && ordered[2].UpdatedAt == 9002,
                "a run of marks was not stamped in order");
        Require(ordered[1].DurationSec == 2700.0 && ordered[2].DurationSec == 3000.0,
                "a run of marks did not keep each row's own length");

        // Earlier seasons and earlier episodes of the same one, never the specials,
        // first to last whatever order the addon listed them in.
        std::vector<EpisodeSlot> const episodes{
            { 2, 2 }, { 0, 1 }, { 1, 2 }, { 2, 3 }, { 1, 1 }, { 3, 1 }, { 0, 2 }, { 2, 1 },
        };
        auto const beforeS2E3 = EarlierEpisodes(episodes, 3);
        Require(beforeS2E3 == std::vector<std::size_t>{ 4, 2, 7, 0 },
                "the episodes before a regular episode were wrong");
        Require(EarlierEpisodes(episodes, 6) == std::vector<std::size_t>{ 1 },
                "a special counted something other than an earlier special");
        Require(EarlierEpisodes(episodes, 4).empty(), "the first episode had something before it");
        Require(EarlierEpisodes(episodes, episodes.size()).empty(), "an episode out of range had something before it");
    }

    // Mirrors "a removal keeps the row as it was, stamped removed and updated now"
    // in apps/desktop/tests/watchMarks.test.ts, plus the batching, the item
    // selection and the old-server echo check the removal is built from.
    void TestHistoryRemoval()
    {
        using HaloDesktop::Services::ChunkRows;
        using HaloDesktop::Services::EchoesRemovedRows;
        using HaloDesktop::Services::HistoryRowsOf;
        using HaloDesktop::Services::RemovedRows;

        auto const row = StoredWatchEntry(L"tt1:1:2", 90.0, 1200.0, false, 100);
        auto const removed = RemovedRows({ row }, 500);
        Require(removed.size() == 1, "a removal lost a row");
        auto const& tombstone = removed.front();
        Require(tombstone.RemovedAt && *tombstone.RemovedAt == 500 && tombstone.UpdatedAt == 500,
                "a removal was not stamped removed and updated now");
        Require(tombstone.VideoId == row.VideoId && tombstone.ItemId == row.ItemId
                    && tombstone.PositionSec == 90.0 && tombstone.DurationSec == 1200.0 && !tombstone.Watched
                    && tombstone.Name == row.Name && tombstone.Poster == row.Poster,
                "a removal changed the row it removes");

        // A row from a device whose clock ran ahead still loses to the removal.
        auto const ahead = RemovedRows({ StoredWatchEntry(L"tt1:1:3", 0.0, 0.0, false, 9000) }, 500);
        Require(ahead.front().UpdatedAt == 9001 && ahead.front().RemovedAt && *ahead.front().RemovedAt == 9001,
                "a removal was not newer than the row it replaces");

        // Every live row of the title, and only those.
        auto other = StoredWatchEntry(L"tt2", 10.0, 6000.0, false, 50);
        other.ItemId = L"movie:tt2";
        auto gone = StoredWatchEntry(L"tt1:1:1", 10.0, 1200.0, false, 40);
        gone.RemovedAt = 40;
        std::vector<HaloDesktop::Api::Dto::WatchEntry> const history{
            row, other, gone, StoredWatchEntry(L"tt1:1:3", 0.0, 0.0, true, 60) };
        auto const ofShow = HistoryRowsOf(history, L"series:tt1");
        Require(ofShow.size() == 2 && ofShow[0].VideoId == L"tt1:1:2" && ofShow[1].VideoId == L"tt1:1:3",
                "the rows of a title were not every live row of it");
        Require(HistoryRowsOf(history, L"").empty(), "an empty item id matched rows");

        // Mirrors liveWatchStates: a tombstone never reads as a play.
        auto const live = HaloDesktop::Services::LiveWatchRows(history);
        Require(live.size() == 3 && live[0].VideoId == L"tt1:1:2" && live[1].VideoId == L"tt2"
                    && live[2].VideoId == L"tt1:1:3",
                "a removal tombstone was read as part of the watch history");

        // 500 rows a request, the last batch holding the rest.
        std::vector<HaloDesktop::Api::Dto::WatchEntry> many(1001, row);
        auto const chunks = ChunkRows(many, HaloDesktop::Services::WatchRemovalChunkSize);
        Require(chunks.size() == 3 && chunks[0].size() == 500 && chunks[1].size() == 500 && chunks[2].size() == 1,
                "a long removal was not split into batches of 500");
        Require(ChunkRows({}, 500).empty(), "an empty removal made a request");
        Require(ChunkRows(many, 0).size() == many.size(), "a zero batch size did not fall back to one row");

        // A server that stores the removed rows and sends them back is too old.
        Require(EchoesRemovedRows({ row, other }, removed), "an echoed removal was not caught");
        Require(!EchoesRemovedRows({ other }, removed), "a removal the server applied was reported as echoed");
        Require(!EchoesRemovedRows({ tombstone, other }, removed), "a returned tombstone was reported as echoed");
    }

} // namespace

int main()
{
    try
    {
        TestCatalogCommitDecisions();
        TestStreamVideoSizeValidation();
        TestNumericBoundaryValidation();
        TestSettingsLoadPolicy();
        TestCatalogExtrasAndAddonFields();
        TestMetaFieldsAndActionFiltering();
        TestPlanRefusal();
        TestCrashLog();
        TestDiscordPresencePolicyAndService();
        TestCatalogDirtySingleFlight();
        TestHomeVisibility();
        TestFilteredFeaturedSelection();
        TestSelectFeaturedItems();
        TestOptionalSubtitleFallback();
        TestContinueShelf();
        RunHomeRowsTests();
        RunHomeShelfLoadTests();
        RunSearchResultsTests();
        RunSearchInputTests();
        RunPeopleTests();
        RunMagnetTests();
        TestWatchMarks();
        TestHistoryRemoval();
        RunHistoryRowsTests();
        RunDetailPageTests();
        RunSourcesSheetTests();
        RunWatchOutboxTests();
        RunCallQueueTests();
        RunDownloadsPageTests();
        RunSettingsPageTests();
        RunShellInputTests();
        RunSignInTests();
        TestEpisodePositionParsing();
        TestDownloadPageOperationLifetime();
        TestMutableDownloadRowBindings();
        TestReadyCardBindingsFollowTheRow();
        TestLandscapeArtworkSelection();
        TestBrowserSignInListenerCancellation();
        TestBrowserSignInListenerOnlyAnswersItsOwnCallback();
        RunStandaloneStorageTests();
        RunSegmentPlannerTests();
        RunDownloadTransferStabilityTest();
        RunPlaybackSourceResolverTest();
        RunApiClientAuthTest();
        RunAddonResolveTests();
        RunDeviceBackendTests();
        RunDeviceModeTests();
        std::cout << "StabilityTests passed\n";
        return 0;
    }
    catch (winrt::hresult_error const& error)
    {
        std::wcerr << L"StabilityTests failed with HRESULT 0x"
                   << std::hex << static_cast<std::uint32_t>(error.code().value)
                   << L": " << error.message().c_str() << L'\n';
        return 1;
    }
    catch (std::exception const& error)
    {
        std::cerr << "StabilityTests failed: " << error.what() << '\n';
        return 1;
    }
    catch (...)
    {
        std::cerr << "StabilityTests failed with an unknown exception.\n";
        return 1;
    }
}
