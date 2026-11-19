#include "MagnetTests.h"

#include "Api/Dto.h"
#include "Services/MagnetCardPolicy.h"
#include "Services/MagnetSourcePolicy.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <winrt/Windows.Data.Json.h>

namespace
{
    namespace Dto = HaloDesktop::Api::Dto;
    namespace Mappers = HaloDesktop::Api::Mappers;
    namespace Services = HaloDesktop::Services;

    // Stand-in servers, never real ones: the app's server, and the public
    // API address the server writes its play links on.
    constexpr std::wstring_view Server = L"https://halo.example.test";
    constexpr std::wstring_view Token = L"m1.abc_DEF-123.Sig_9-z";
    constexpr std::wstring_view PlayBase = L"https://api.example.test/v1/halo/play/m1.abc_DEF-123.Sig_9-z";
    constexpr std::wstring_view RebuiltBase = L"https://halo.example.test/play/m1.abc_DEF-123.Sig_9-z";

    void Require(bool condition, std::string const& message)
    {
        if (!condition)
        {
            throw std::runtime_error{ message };
        }
    }

    winrt::Windows::Data::Json::IJsonValue Json(std::wstring_view text)
    {
        return winrt::Windows::Data::Json::JsonValue::Parse(winrt::hstring{ text });
    }

    template <typename Action>
    bool Throws(Action const& action)
    {
        try
        {
            action();
        }
        catch (...)
        {
            return true;
        }
        return false;
    }

    std::wstring File(std::wstring_view id, std::wstring_view name, std::wstring_view size, std::wstring_view season, std::wstring_view episode, std::wstring_view playUrl)
    {
        return std::wstring{ L"{\"id\":\"" } + std::wstring{ id } + L"\",\"name\":\"" + std::wstring{ name } + L"\",\"sizeBytes\":"
            + std::wstring{ size } + L",\"season\":" + std::wstring{ season } + L",\"episode\":" + std::wstring{ episode }
            + L",\"playUrl\":\"" + std::wstring{ playUrl } + L"\"}";
    }

    std::wstring Answer(std::wstring_view state, std::wstring_view files, std::wstring_view identified, std::wstring_view extra = L"")
    {
        std::wstring text = L"{\"id\":\"mg_6f1c2d3e-0a1b-4c5d-8e9f-0123456789ab\",\"state\":\"" + std::wstring{ state }
            + L"\",\"reason\":null,\"name\":\"Dune.2021.2160p\",\"sizeBytes\":67000000000,\"progress\":1";
        if (!files.empty()) text += L",\"files\":[" + std::wstring{ files } + L"]";
        text += L",\"identified\":" + std::wstring{ identified.empty() ? L"null" : identified };
        text += extra;
        text += L"}";
        return text;
    }

    constexpr std::wstring_view DuneIdentity =
        LR"({"type":"movie","id":"tt1160419","name":"Dune","year":"2021","poster":"https://image.tmdb.org/t/p/w342/abc.jpg"})";
    constexpr std::wstring_view SeriesIdentity =
        LR"({"type":"series","id":"tt11280740","name":"Severance","year":"2022","poster":null})";

    Dto::MagnetRequest Parse(std::wstring const& text)
    {
        return Mappers::ParseMagnet(Json(text), Server);
    }

    std::wstring Play(std::wstring_view file)
    {
        return std::wstring{ PlayBase } + L"/" + std::wstring{ file };
    }

    void TestMagnetCapabilities()
    {
        auto const on = Mappers::ParseCapabilities(Json(LR"({"people":true,"lookup":true,"magnets":{"enabled":true,"cachedOnly":false}})"));
        Require(on.Magnets && !on.MagnetsCachedOnly, "the magnets capability a server reports was not read");
        auto const off = Mappers::ParseCapabilities(Json(LR"({"people":true,"lookup":true,"magnets":{"enabled":false,"cachedOnly":true}})"));
        Require(!off.Magnets && off.MagnetsCachedOnly && off.People, "magnets switched off read as on");
        auto const older = Mappers::ParseCapabilities(Json(LR"({"people":true,"lookup":true})"));
        Require(!older.Magnets && older.MagnetsCachedOnly, "a server without magnets read as offering them");
        auto const hostile = Mappers::ParseCapabilities(Json(LR"({"magnets":{"enabled":"true","cachedOnly":0}})"));
        Require(!hostile.Magnets && hostile.MagnetsCachedOnly, "a magnets capability that is not a boolean read as on");
        auto const notObject = Mappers::ParseCapabilities(Json(LR"({"magnets":true})"));
        Require(!notObject.Magnets, "a magnets capability that is not an object read as on");
    }

    void TestMagnetMapperGood()
    {
        auto const ready = Parse(Answer(L"ready",
            File(L"3", L"Dune.2021.2160p.REMUX.mkv", L"62400000000", L"null", L"null", Play(L"Dune.2021.2160p.REMUX.mkv"))
                + L"," + File(L"4", L"Extras.mkv", L"900000000", L"null", L"null", Play(L"Extras.mkv")),
            DuneIdentity));
        Require(ready.State == Dto::MagnetState::Ready && ready.Files.size() == 2, "a ready magnet was not read with its files");
        Require(ready.Files[0].Id == L"3" && ready.Files[0].SizeBytes == 62400000000ull && !ready.Files[0].Season, "a file's fields were not read");
        Require(ready.Files[0].PlayUrl == std::wstring{ RebuiltBase } + L"/Dune.2021.2160p.REMUX.mkv",
            "a file's play link was not rebuilt on the app's server");
        Require(ready.Identified && ready.Identified->Type == L"movie" && ready.Identified->Id == L"tt1160419"
            && ready.Identified->Year == winrt::hstring{ L"2021" } && ready.Identified->Poster, "the matched title was not read");
        Require(ready.Name == L"Dune.2021.2160p" && ready.SizeBytes == 67000000000ull, "the magnet's name and size were not read");

        auto const downloading = Parse(LR"({"id":"mg_abc","state":"downloading","reason":null,"name":"x","sizeBytes":0,"progress":0.38,"identified":null})");
        Require(downloading.State == Dto::MagnetState::Downloading && downloading.Progress == 0.38 && downloading.Files.empty(),
            "a downloading magnet was not read");

        auto const refused = Parse(LR"({"id":"mg_abc","state":"failed","reason":"unidentified","name":"holiday","sizeBytes":5,"progress":0,"identified":null})");
        Require(refused.State == Dto::MagnetState::Failed && refused.Reason == winrt::hstring{ L"unidentified" }, "a refusal's reason was not read");

        // Files on a request that is not ready are not its business.
        auto const notCached = Parse(LR"({"id":"mg_abc","state":"not_cached","reason":null,"name":"","sizeBytes":0,"progress":0,"identified":null,"files":[{"id":"bad id"}]})");
        Require(notCached.State == Dto::MagnetState::NotCached && notCached.Files.empty(), "a not-cached answer's files were read");

        // Unknown fields are ignored, and a series' episode numbers kept.
        auto const series = Parse(Answer(L"ready",
            File(L"7", L"Severance.S02E01.mkv", L"5000000000", L"2", L"1", Play(L"Severance.S02E01.mkv")),
            SeriesIdentity, LR"(,"unknown":{"a":[1,2]})"));
        Require(series.Files[0].Season == 2 && series.Files[0].Episode == 1 && !series.Identified->Poster, "a series answer was not read");

        // A poster off TMDB's image host is left out, not trusted.
        auto const offHost = Parse(Answer(L"ready",
            File(L"3", L"a.mkv", L"100", L"null", L"null", Play(L"a.mkv")),
            LR"({"type":"movie","id":"tt1","name":"A","year":"","poster":"https://evil.example/p.jpg"})"));
        Require(offHost.Identified && !offHost.Identified->Poster && !offHost.Identified->Year, "a poster off TMDB's host, or an empty year, was kept");
    }

    void TestMagnetMapperMissing()
    {
        Require(Throws([] { static_cast<void>(Parse(LR"([])")); }), "a magnet answer that is not an object was accepted");
        Require(Throws([] { static_cast<void>(Parse(LR"({"state":"ready","sizeBytes":0})")); }), "a magnet answer without an id was accepted");
        Require(Throws([] { static_cast<void>(Parse(LR"({"id":"mg_a","sizeBytes":0})")); }), "a magnet answer without a state was accepted");
        Require(Throws([] { static_cast<void>(Parse(LR"({"id":"mg_a","state":"downloading"})")); }), "a magnet answer without a size was accepted");
        Require(Throws([] { static_cast<void>(Parse(Answer(L"ready", L"", DuneIdentity))); }), "a ready magnet without files was accepted");
        Require(Throws([] { static_cast<void>(Parse(Answer(L"ready", L"", DuneIdentity, LR"(,"files":[])"))); }),
            "a ready magnet with an empty file list was accepted");
        Require(Throws([] { static_cast<void>(Parse(Answer(L"ready", LR"({"id":"3","name":"a.mkv","sizeBytes":1})", DuneIdentity))); }),
            "a file without a play link was accepted");
        // Optional fields may be missing.
        auto const bare = Parse(LR"({"id":"mg_a","state":"no_video","sizeBytes":0})");
        Require(bare.State == Dto::MagnetState::NoVideo && bare.Name.empty() && bare.Progress == 0.0 && !bare.Identified && !bare.Reason,
            "a magnet answer without its optional fields was refused");
    }

    void TestMagnetMapperHostile()
    {
        auto const refused = [](std::wstring const& text, char const* message)
        {
            Require(Throws([&text] { static_cast<void>(Parse(text)); }), message);
        };
        refused(LR"({"id":"mg_a","state":"exploded","sizeBytes":0})", "an unknown state was accepted");
        refused(LR"({"id":"mg_a","state":"READY","sizeBytes":0})", "a state in another case was accepted");
        refused(LR"({"id":"mg_a","state":"downloading","sizeBytes":-1})", "a negative size was accepted");
        refused(LR"({"id":"mg_a","state":"downloading","sizeBytes":1.5})", "a fractional size was accepted");
        refused(LR"({"id":"mg_a","state":"downloading","sizeBytes":"10"})", "a size as text was accepted");
        refused(LR"({"id":"mg_a","state":"downloading","sizeBytes":1e300})", "a size past what a double holds exactly was accepted");
        refused(LR"({"id":"mg_a","state":"downloading","sizeBytes":0,"progress":1.5})", "a progress over one was accepted");
        refused(LR"({"id":"mg_a","state":"downloading","sizeBytes":0,"progress":-0.1})", "a negative progress was accepted");
        refused(LR"({"id":"mg_../../admin","state":"downloading","sizeBytes":0})", "an id with a path in it was accepted");
        refused(LR"({"id":"xx_abc","state":"downloading","sizeBytes":0})", "an id without the server's prefix was accepted");
        refused(LR"({"id":"mg_","state":"downloading","sizeBytes":0})", "an id with nothing after the prefix was accepted");
        refused(LR"({"id":"mg_a","state":"failed","reason":"Unidentified!","sizeBytes":0})", "a reason of the wrong shape was accepted");
        refused(LR"({"id":"mg_a","state":"failed","reason":7,"sizeBytes":0})", "a reason that is not text was accepted");
        refused(Answer(L"ready", L"", LR"({"type":"person","id":"tt1","name":"A"})", LR"(,"files":[])"), "an identified type outside movie and series was accepted");
        refused(Answer(L"ready", File(L"3", L"a.mkv", L"1", L"null", L"null", Play(L"a.mkv")), LR"({"type":"movie","id":"nm123","name":"A"})"),
            "an identified id that is not a title id was accepted");
        refused(Answer(L"ready", File(L"3", L"a.mkv", L"1", L"null", L"null", Play(L"a.mkv")), LR"({"type":"movie","id":"tt1","name":"A","year":"21"})"),
            "an identified year of the wrong shape was accepted");
        refused(Answer(L"ready", File(L"3", L"a.mkv", L"1", L"null", L"null", Play(L"a.mkv")), LR"({"type":"movie","id":"tt1","name":"   "})"),
            "an identified title without a name was accepted");
        refused(Answer(L"ready", File(L"a/b", L"a.mkv", L"1", L"null", L"null", Play(L"a.mkv")), DuneIdentity), "a file id with a slash was accepted");
        refused(Answer(L"ready", File(L"3", L"", L"1", L"null", L"null", Play(L"a.mkv")), DuneIdentity), "a file without a name was accepted");
        refused(Answer(L"ready", File(L"3", L"a.mkv", L"-5", L"null", L"null", Play(L"a.mkv")), DuneIdentity), "a negative file size was accepted");
        refused(Answer(L"ready", File(L"3", L"a.mkv", L"1", L"-1", L"2", Play(L"a.mkv")), DuneIdentity), "a negative season was accepted");
        refused(Answer(L"ready", File(L"3", L"a.mkv", L"1", L"1", L"2.5", Play(L"a.mkv")), DuneIdentity), "a fractional episode was accepted");
        refused(Answer(L"ready", File(L"3", L"a.mkv", L"1", L"\"1\"", L"2", Play(L"a.mkv")), DuneIdentity), "a season as text was accepted");
        refused(Answer(L"ready",
            File(L"3", L"a.mkv", L"1", L"null", L"null", Play(L"a.mkv")) + L"," + File(L"3", L"b.mkv", L"1", L"null", L"null", Play(L"b.mkv")),
            DuneIdentity), "one file listed twice was accepted");
        std::wstring many;
        for (int index = 0; index < 201; ++index)
        {
            if (!many.empty()) many += L",";
            many += File(std::to_wstring(index), L"a.mkv", L"1", L"null", L"null", Play(L"a.mkv"));
        }
        refused(Answer(L"ready", many, DuneIdentity), "more files than the server ever lists were accepted");

        // Names are display only: control characters go and long ones are cut.
        auto const named = Parse(std::wstring{ LR"({"id":"mg_a","state":"downloading","sizeBytes":0,"name":"a\u0007b)" } + std::wstring(400, L'x') + L"\"}");
        Require(named.Name.size() == 300 && std::wstring_view{ named.Name }.substr(0, 3) == L"a b", "a magnet's name was not cleaned and cut");
    }

    void TestMagnetPlayLinkRebuilt()
    {
        auto const rebuilt = [](std::wstring_view url) { return Mappers::MagnetPlayLinkOn(url, Server); };
        auto const expected = std::wstring{ Server } + L"/play/" + std::wstring{ Token } + L"/a%20b.mkv";

        // Whatever host the server wrote the link on, it comes out on the app's server.
        for (auto const url : {
                 L"https://api.example.test/v1/halo/play/m1.abc_DEF-123.Sig_9-z/a%20b.mkv",
                 L"https://halo.example.test/play/m1.abc_DEF-123.Sig_9-z/a%20b.mkv",
                 L"http://cdn.elsewhere.example:8443/play/m1.abc_DEF-123.Sig_9-z/a%20b.mkv",
                 L"HTTPS://API.Example.Test/x/y/z/play/m1.abc_DEF-123.Sig_9-z/a%20b.mkv" })
        {
            auto const link = rebuilt(url);
            Require(link && *link == expected, "a play link on another host was not rebuilt on the app's server");
        }
        // Joined onto the server as every other call is, path and all.
        Require(Mappers::MagnetPlayLinkOn(L"https://api.example.test/play/m1.a.b/c.mkv", L"http://127.0.0.1:8787/v1/halo/")
                == std::wstring{ L"http://127.0.0.1:8787/v1/halo/play/m1.a.b/c.mkv" },
            "a self-hosted server's base was not where the link was rebuilt");

        for (auto const url : {
                 // Not a magnet token: a stream addon's, or of the wrong shape.
                 L"https://api.example.test/v1/halo/play/v2.abc.def/a.mkv",
                 L"https://api.example.test/v1/halo/play/abc.def/a.mkv",
                 L"https://api.example.test/v1/halo/play/m1.abc/a.mkv",
                 L"https://api.example.test/v1/halo/play/m1.abc.def.ghi/a.mkv",
                 L"https://api.example.test/v1/halo/play/m1..def/a.mkv",
                 L"https://api.example.test/v1/halo/play/m1.ab+c.def/a.mkv",
                 // Not exactly a token and a file name.
                 L"https://api.example.test/v1/halo/play/m1.abc.def",
                 L"https://api.example.test/v1/halo/play/m1.abc.def/",
                 L"https://api.example.test/v1/halo/play/m1.abc.def/a/b.mkv",
                 L"https://api.example.test/v1/halo/play/m1.x/../y",
                 L"https://api.example.test/v1/halo/play/m1.abc.def/..",
                 L"https://api.example.test/v1/halo/play/m1.abc.def/%2e%2e",
                 L"https://api.example.test/v1/halo/play/m1.abc.def/a%2Fb.mkv",
                 L"https://api.example.test/v1/halo/play/m1.abc.def/a%5Cb.mkv",
                 L"https://api.example.test/v1/halo/stream/m1.abc.def/a.mkv",
                 // A query, a fragment, a backslash, white space, other characters.
                 L"https://api.example.test/v1/halo/play/m1.abc.def/a.mkv?x=1",
                 L"https://api.example.test/v1/halo/play/m1.abc.def/a.mkv#frag",
                 L"https://api.example.test/v1/halo/play/m1.abc.def/a\\b.mkv",
                 L"https://api.example.test/v1/halo/play/m1.abc.def/a b.mkv",
                 L"https://api.example.test/v1/halo/play/m1.abc.def/\x00E9.mkv",
                 // Not a well-formed http(s) link at all.
                 L"https://user:pass@api.example.test/v1/halo/play/m1.abc.def/a.mkv",
                 L"https://api.example.test\\@evil.example/play/m1.abc.def/a.mkv",
                 L"//api.example.test/play/m1.abc.def/a.mkv",
                 L"/play/m1.abc.def/a.mkv",
                 L"file:///C:/play/m1.abc.def/a.mkv",
                 L"C:\\play\\m1.abc.def\\a.mkv",
                 L"\\\\host\\play\\m1.abc.def\\a.mkv",
                 L"javascript:alert(1)//play/m1.abc.def/a.mkv",
                 L"" })
        {
            Require(!rebuilt(url), "a play link whose tail is not /play/m1.<token>/<name> was accepted");
        }
        Require(!rebuilt(std::wstring{ L"https://api.example.test/play/m1." } + std::wstring(3000, L'a') + L".b/a.mkv"),
            "an overlong play token was accepted");
        Require(!rebuilt(std::wstring{ L"https://api.example.test/play/m1.a.b/" } + std::wstring(5000, L'a')),
            "an overlong play link was accepted");
        Require(!Mappers::MagnetPlayLinkOn(L"https://api.example.test/play/m1.a.b/c.mkv", L"not a server"),
            "a link was rebuilt on a server address that is not one");

        // In an answer, one bad link makes the whole answer an error.
        Require(Throws([] { static_cast<void>(Parse(Answer(L"ready",
            File(L"3", L"a.mkv", L"1", L"null", L"null", Play(L"a.mkv")) + L","
                + File(L"4", L"b.mkv", L"1", L"null", L"null", L"https://cdn.evil.example/b.mkv"),
            DuneIdentity))); }), "a ready answer with a play link of the wrong shape was accepted");
    }

    Dto::MagnetFile MakeFile(std::wstring_view id, std::uint64_t size, std::optional<std::int32_t> season = std::nullopt, std::optional<std::int32_t> episode = std::nullopt)
    {
        return Dto::MagnetFile{
            .Id = winrt::hstring{ id },
            .Name = winrt::hstring{ std::wstring{ id } + L".mkv" },
            .SizeBytes = size,
            .Season = season,
            .Episode = episode,
            .PlayUrl = winrt::hstring{ Play(id) },
        };
    }

    Dto::MagnetRequest ReadyMagnet(std::wstring_view type, std::wstring_view id, std::vector<Dto::MagnetFile> files)
    {
        Dto::MagnetRequest request;
        request.Id = L"mg_a";
        request.State = Dto::MagnetState::Ready;
        request.Name = L"Release";
        request.SizeBytes = 10;
        request.Files = std::move(files);
        request.Identified = Dto::MagnetIdentity{ .Type = winrt::hstring{ type }, .Id = winrt::hstring{ id }, .Name = L"Title", .Year = winrt::hstring{ L"2021" } };
        return request;
    }

    void TestMagnetFileFor()
    {
        auto const film = ReadyMagnet(L"movie", L"tt1160419", { MakeFile(L"small", 10), MakeFile(L"big", 90), MakeFile(L"mid", 50) });
        auto const filmFile = Services::MagnetFileFor(film, L"movie", L"tt1160419");
        Require(filmFile && filmFile->Id == L"big", "a film's largest file was not offered for it");
        Require(!Services::MagnetFileFor(film, L"movie", L"tt0000001"), "a film's file was offered for another film");
        Require(!Services::MagnetFileFor(film, L"series", L"tt1160419:1:1"), "a film's file was offered for an episode");

        auto const pack = ReadyMagnet(L"series", L"tt11280740", {
            MakeFile(L"e1", 30, 2, 1),
            MakeFile(L"e2", 31, 2, 2),
            MakeFile(L"e2-sample", 5, 2, 2),
            MakeFile(L"extras", 99),
            MakeFile(L"noseason", 40, std::nullopt, 3) });
        auto const episode = Services::MagnetFileFor(pack, L"series", L"tt11280740:2:2");
        Require(episode && episode->Id == L"e2", "an episode's file was not the largest one with its season and episode");
        Require(!Services::MagnetFileFor(pack, L"series", L"tt11280740:2:9"), "a file was offered for an episode the magnet does not hold");
        Require(!Services::MagnetFileFor(pack, L"series", L"tt11280740:0:3"), "a file without a season was offered for an episode");
        Require(!Services::MagnetFileFor(pack, L"series", L"tt11280740"), "a season pack offered a file for the series id alone");
        for (auto const id : { L"tt1128074:2:2", L"tt11280740:2", L"tt11280740:2:2:1", L"tt11280740:x:2", L"tt11280740::2", L"tt11280740:2:" })
        {
            Require(!Services::MagnetFileFor(pack, L"series", id), "a file was offered for a video id of the wrong shape or title");
        }
        Require(!Services::MagnetFileFor(pack, L"movie", L"tt11280740"), "a series' file was offered as a film");

        auto unidentified = film;
        unidentified.Identified.reset();
        Require(!Services::MagnetFileFor(unidentified, L"movie", L"tt1160419"), "an unidentified magnet was offered on a title page");
        auto downloading = film;
        downloading.State = Dto::MagnetState::Downloading;
        Require(!Services::MagnetFileFor(downloading, L"movie", L"tt1160419"), "a magnet not yet ready was offered");
    }

    void TestMagnetSourceRow()
    {
        auto const row = Services::BuildMagnetSourceRow(MakeFile(L"Dune.2021", 9'040'000'000ull));
        Require(row.Name == L"Your magnet" && row.FileName == L"Dune.2021.mkv" && row.Size == L"8.42 GB", "the magnet's source row was not named as it should be");
        Require(Services::BuildMagnetSourceRow(MakeFile(L"a", 0)).Size == L"UNKNOWN", "a file without a size did not read as unknown");
    }

    Services::MagnetCard Card(Services::MagnetPhase phase, std::optional<Dto::MagnetRequest> request = std::nullopt)
    {
        return Services::BuildMagnetCard({ .Phase = phase, .LinkName = L"Dune.2021.2160p", .Request = std::move(request) });
    }

    Dto::MagnetRequest InState(Dto::MagnetState state, std::optional<std::wstring> reason = std::nullopt)
    {
        Dto::MagnetRequest request;
        request.Id = L"mg_a";
        request.State = state;
        request.Name = L"Release.Name";
        if (reason) request.Reason = winrt::hstring{ *reason };
        return request;
    }

    void TestMagnetCardStates()
    {
        using Tone = Services::MagnetCardTone;
        using Action = Services::MagnetCardAction;

        auto const unavailable = Card(Services::MagnetPhase::Unavailable);
        Require(unavailable.Tone == Tone::Mute && unavailable.Title == L"Magnet links aren't available"
            && unavailable.Action == Action::None, "the not-available card changed");
        auto const checking = Card(Services::MagnetPhase::Checking);
        Require(checking.Tone == Tone::Accent && checking.Title == L"Checking this torrent…" && checking.Mono == L"Dune.2021.2160p"
            && checking.Secondary == L"Looking for a video file.", "the checking card changed");
        auto const failed = Card(Services::MagnetPhase::Failed);
        Require(failed.Tone == Tone::Bad && failed.Title == L"Couldn't check this torrent" && failed.Action == Action::Retry
            && failed.ActionLabel == L"Try again", "the error card changed");
        Require(Card(Services::MagnetPhase::Answered).Action == Action::Retry, "an answer without a request did not read as an error");

        auto const notCached = Card(Services::MagnetPhase::Answered, InState(Dto::MagnetState::NotCached));
        Require(notCached.Tone == Tone::Warn && notCached.Title == L"This torrent isn't ready" && notCached.Action == Action::None, "the not-cached card changed");
        auto const noVideo = Card(Services::MagnetPhase::Answered, InState(Dto::MagnetState::NoVideo));
        Require(noVideo.Tone == Tone::Bad && noVideo.Title == L"No video in this torrent", "the no-video card changed");
        auto const tooLarge = Card(Services::MagnetPhase::Answered, InState(Dto::MagnetState::TooLarge));
        Require(tooLarge.Tone == Tone::Bad && tooLarge.Title == L"This torrent is too large", "the too-large card changed");
        auto const refused = Card(Services::MagnetPhase::Answered, InState(Dto::MagnetState::Failed, L"unidentified"));
        Require(refused.Tone == Tone::Bad && refused.Title == L"Can't open this magnet" && refused.Action == Action::None, "the refused card changed");
        auto const upstream = Card(Services::MagnetPhase::Answered, InState(Dto::MagnetState::Failed, L"upstream"));
        Require(upstream.Action == Action::Retry && upstream.Title == L"Couldn't check this torrent", "another failure did not offer to try again");

        auto downloadingRequest = InState(Dto::MagnetState::Downloading);
        downloadingRequest.Progress = 0.386;
        auto const downloading = Card(Services::MagnetPhase::Answered, downloadingRequest);
        Require(downloading.Tone == Tone::Accent && downloading.Title == L"Getting this torrent ready…" && downloading.Mono == L"Release.Name"
            && downloading.Tail == L"38%" && downloading.Progress && *downloading.Progress == 0.386 && downloading.Action == Action::Cancel,
            "the downloading card changed");
        downloadingRequest.Name = L"";
        Require(Card(Services::MagnetPhase::Answered, downloadingRequest).Mono == L"Dune.2021.2160p", "a download without a name did not show the link's");

        auto const stopped = Card(Services::MagnetPhase::Stopped);
        Require(stopped.Tone == Tone::Mute && stopped.Action == Action::Retry, "a cancelled download's card did not offer to start again");

        // A film: the poster, one Open title, never a file list or Play.
        auto const film = Card(Services::MagnetPhase::Answered, ReadyMagnet(L"movie", L"tt1160419", { MakeFile(L"small", 10), MakeFile(L"Dune.REMUX", 67'000'000'000ull) }));
        Require(film.ShowsPoster && film.Title == L"Title" && film.Mono == L"MOVIE" && film.Plain == L"2021" && film.Tail == L"62.4 GB"
            && film.Secondary == L"Dune.REMUX.mkv" && film.Action == Action::OpenTitle && film.ActionLabel == L"Open title"
            && film.Identity && film.Identity->Id == L"tt1160419" && !film.UnidentifiedFile, "the ready film card changed");

        auto const series = Card(Services::MagnetPhase::Answered, ReadyMagnet(L"series", L"tt11280740", {
            MakeFile(L"e1", 1'073'741'824ull, 2, 1), MakeFile(L"e2", 1'073'741'824ull, 2, 2), MakeFile(L"extras", 99) }));
        Require(series.Mono == L"SERIES" && series.Plain == L"Season 2" && series.Tail == L"2 episodes \x00B7 2.00 GB"
            && series.Secondary == L"Open the title to pick an episode." && series.Action == Action::OpenTitle, "the ready season pack card changed");
        auto const seasons = Card(Services::MagnetPhase::Answered, ReadyMagnet(L"series", L"tt1", { MakeFile(L"a", 1, 1, 1), MakeFile(L"b", 1, 2, 1) }));
        Require(seasons.Plain == L"2 seasons", "a pack of several seasons did not say how many");
        auto const single = Card(Services::MagnetPhase::Answered, ReadyMagnet(L"series", L"tt1", { MakeFile(L"a", 1, 1, 1) }));
        Require(single.Tail.starts_with(L"1 episode \x00B7"), "one episode was not said in the singular");

        // Unidentified with files: may be played, away from history.
        auto unidentifiedRequest = ReadyMagnet(L"movie", L"tt1", { MakeFile(L"holiday_cut_final", 1'932'735'283ull), MakeFile(L"tiny", 1) });
        unidentifiedRequest.Identified.reset();
        auto const unidentified = Card(Services::MagnetPhase::Answered, unidentifiedRequest);
        Require(unidentified.Tone == Tone::Warn && unidentified.Title == L"Unidentified video" && !unidentified.ShowsPoster
            && unidentified.Mono == L"holiday_cut_final.mkv" && unidentified.Tail == L"1.80 GB"
            && unidentified.Action == Action::PlayUnidentified && unidentified.ActionLabel == L"Play as unidentified video"
            && unidentified.UnidentifiedFile && unidentified.UnidentifiedFile->Id == L"holiday_cut_final",
            "the unidentified video card changed");

        // No card text uses the em dash.
        for (auto const& card : { unavailable, checking, failed, notCached, noVideo, tooLarge, refused, downloading, stopped, film, series, unidentified })
        {
            for (auto const* text : { &card.Title, &card.Secondary, &card.Mono, &card.Plain, &card.Tail, &card.ActionLabel })
            {
                Require(text->find(L'\x2014') == std::wstring::npos, "a magnet card used the em dash");
            }
        }
    }
}

void RunMagnetTests()
{
    TestMagnetCapabilities();
    TestMagnetMapperGood();
    TestMagnetMapperMissing();
    TestMagnetMapperHostile();
    TestMagnetPlayLinkRebuilt();
    TestMagnetFileFor();
    TestMagnetSourceRow();
    TestMagnetCardStates();
}
