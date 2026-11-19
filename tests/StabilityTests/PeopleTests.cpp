#include "PeopleTests.h"

#include "Api/Dto.h"
#include "Services/FilmographyPolicy.h"
#include "Services/PeopleRowPolicy.h"
#include "Services/SearchResultsPolicy.h"

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

    void TestCapabilitiesMapping()
    {
        auto const full = Mappers::ParseCapabilities(Json(LR"({"people":true,"lookup":true,"magnets":{"enabled":false,"cachedOnly":true}})"));
        Require(full.People && full.Lookup, "the capabilities a server reports were not read");
        auto const missing = Mappers::ParseCapabilities(Json(LR"({})"));
        Require(!missing.People && !missing.Lookup, "a missing capability read as available");
        auto const hostile = Mappers::ParseCapabilities(Json(LR"({"people":"true","lookup":1})"));
        Require(!hostile.People && !hostile.Lookup, "a capability that is not a boolean read as available");
        Require(Throws([] { static_cast<void>(Mappers::ParseCapabilities(Json(LR"([true])"))); }),
            "a capabilities answer that is not an object was accepted");
    }

    void TestTmdbImageUrls()
    {
        Require(Mappers::IsTmdbImageUrl(L"https://image.tmdb.org/t/p/w185/abc_DEF-1.jpg"), "a TMDB photo was refused");
        for (auto const url : {
                 L"http://image.tmdb.org/t/p/w185/abc.jpg",
                 L"https://image.tmdb.org.evil.example/t/p/w185/abc.jpg",
                 L"https://evil.example/image.tmdb.org/t/p/w185/abc.jpg",
                 L"https://user@image.tmdb.org/t/p/w185/abc.jpg",
                 L"https://image.tmdb.org:444/t/p/w185/abc.jpg",
                 L"https://image.tmdb.org/t/p/",
                 L"https://image.tmdb.org/t/p/../../x.jpg",
                 L"https://image.tmdb.org/t/p/w185/abc.jpg?x=1",
                 L"https://image.tmdb.org/t/p/w185/a b.jpg",
                 L"https://image.tmdb.org/other/w185/abc.jpg",
                 L"javascript:alert(1)",
                 L"" })
        {
            Require(!Mappers::IsTmdbImageUrl(url), "an image URL off TMDB's image host, or of the wrong shape, was accepted");
        }
        Require(!Mappers::IsTmdbImageUrl(std::wstring{ L"https://image.tmdb.org/t/p/w185/" } + std::wstring(400, L'a') + L".jpg"),
            "an overlong image URL was accepted");
    }

    void TestPeopleSearchMapping()
    {
        auto const people = Mappers::ParsePeopleSearch(Json(LR"({"people":[
            {"tmdbId":137427,"name":"  Denis Villeneuve ","knownFor":"Directing","photo":"https://image.tmdb.org/t/p/w185/dv.jpg","popularity":12.5,"knownForTitles":["Dune","Arrival","Sicario","Enemy"],"extra":{"ignored":true}},
            {"tmdbId":2,"name":"No Photo","knownFor":null,"photo":null,"popularity":"high"},
            {"tmdbId":3,"name":"Evil Photo","knownFor":"Producing","photo":"https://image.tmdb.org.evil.example/t/p/w185/x.jpg","popularity":-4},
            {"tmdbId":137427,"name":"Repeated","popularity":1},
            {"tmdbId":0,"name":"Zero id"},
            {"tmdbId":-5,"name":"Negative id"},
            {"tmdbId":1.5,"name":"Fractional id"},
            {"tmdbId":9007199254740992,"name":"Unsafe id"},
            {"tmdbId":"7","name":"String id"},
            {"tmdbId":8},
            {"tmdbId":9,"name":"   "},
            "not an object",
            {"tmdbId":10,"name":"Line\u0007Break","knownForTitles":[1,"",null,"Real"]}
        ]})"));
        Require(people.size() == 4, "the people search kept a bad entry or lost a good one");
        Require(people[0].TmdbId == 137427 && people[0].Name == L"Denis Villeneuve", "a person's id or trimmed name was not read");
        Require(people[0].KnownFor == Dto::PersonDepartment::Directing, "a person's department was not read");
        Require(people[0].Photo && *people[0].Photo == L"https://image.tmdb.org/t/p/w185/dv.jpg", "a TMDB photo was not kept");
        Require(people[0].Popularity == 12.5, "a person's popularity was not read");
        Require(people[0].KnownForTitles.size() == 3 && people[0].KnownForTitles[2] == L"Sicario",
            "a person's known-for titles were not capped at three in order");
        Require(!people[1].Photo && people[1].KnownFor == Dto::PersonDepartment::Unknown && people[1].Popularity == 0.0,
            "missing or mistyped optional fields were not read as absent");
        Require(people[2].TmdbId == 3 && !people[2].Photo, "a photo on another host was kept");
        Require(people[2].KnownFor == Dto::PersonDepartment::Unknown && people[2].Popularity == 0.0,
            "an unknown department or a negative popularity was kept");
        Require(people[3].Name == L"Line Break", "a control character in a name was kept");
        Require(people[3].KnownForTitles.size() == 1 && people[3].KnownForTitles[0] == L"Real",
            "a known-for title that is not text was kept");

        std::wstring many = LR"({"people":[)";
        for (int index = 1; index <= 12; ++index)
        {
            if (index > 1) many += L",";
            many += L"{\"tmdbId\":" + std::to_wstring(index) + L",\"name\":\"Person " + std::to_wstring(index) + L"\"}";
        }
        many += L"]}";
        Require(Mappers::ParsePeopleSearch(Json(many)).size() == 8, "the people search was not capped at eight");
        Require(Mappers::ParsePeopleSearch(Json(LR"({"people":"none"})")).empty(), "a people list that is not an array was read");
        Require(Mappers::ParsePeopleSearch(Json(LR"({})")).empty(), "a missing people list was not read as nobody");
        Require(Throws([] { static_cast<void>(Mappers::ParsePeopleSearch(Json(LR"("people")"))); }),
            "a people answer that is not an object was accepted");
        Require(Mappers::ParsePeopleSearch(Json(std::wstring{ LR"({"people":[{"tmdbId":1,"name":")" } + std::wstring(201, L'x') + LR"("}]})")).empty(),
            "an overlong name was accepted");
    }

    void TestPersonMapping()
    {
        auto const detail = Mappers::ParsePerson(Json(LR"({
            "person":{"tmdbId":137427,"name":"Denis Villeneuve","photo":"https://image.tmdb.org/t/p/w185/dv.jpg","knownFor":"Directing","biography":"Line one.\r\nLine two\u0001."},
            "credits":[
                {"type":"movie","imdbId":"tt1160419","tmdbId":438631,"name":"Dune","year":"2021","poster":"https://image.tmdb.org/t/p/w342/d.jpg","roles":["director","writer","writer","producer"],"character":null,"popularity":80},
                {"type":"series","imdbId":"tt0000001","name":"A Series","year":"21","poster":"http://image.tmdb.org/t/p/w342/s.jpg","roles":["cast"],"character":"Himself","popularity":1},
                {"type":"tv","imdbId":"tt0000002","name":"Wrong kind","roles":["cast"]},
                {"type":"movie","imdbId":"nm0000003","name":"Person id","roles":["cast"]},
                {"type":"movie","imdbId":"tt","name":"No digits","roles":["cast"]},
                {"type":"movie","imdbId":"tt12345678901","name":"Too long","roles":["cast"]},
                {"type":"movie","imdbId":"tt0000004","name":"No roles","roles":[]},
                {"type":"movie","imdbId":"tt0000005","name":"Unknown roles","roles":["producer"]},
                {"type":"movie","imdbId":"tt0000006","roles":["cast"]},
                {"type":"movie","imdbId":"tt1160419","name":"Dune again","roles":["cast"]}
            ],
            "partial":true,
            "extra":42
        })"));
        Require(detail.Person.TmdbId == 137427 && detail.Person.Name == L"Denis Villeneuve", "the person was not read");
        Require(detail.Person.KnownFor == Dto::PersonDepartment::Directing, "the person's department was not read");
        Require(detail.Person.Biography && *detail.Person.Biography == L"Line one.\nLine two .",
            "a biography's line breaks were not kept or its control characters were not replaced");
        Require(detail.Partial, "a partial filmography was not flagged");
        Require(detail.Credits.size() == 2, "a credit of the wrong shape was kept, or a good one lost");
        auto const& dune = detail.Credits[0];
        Require(dune.Type == L"movie" && dune.ImdbId == L"tt1160419" && dune.TmdbId == 438631 && dune.Name == L"Dune",
            "a credit's identity was not read");
        Require(dune.Year && *dune.Year == L"2021" && dune.Poster, "a credit's year or poster was not read");
        Require(dune.Roles == std::vector<Dto::CreditRole>{ Dto::CreditRole::Director, Dto::CreditRole::Writer },
            "a credit's roles were not read once each, unknown ones left out");
        Require(!dune.Character && dune.Popularity == 80.0, "a credit's character or popularity was misread");
        auto const& series = detail.Credits[1];
        Require(series.Type == L"series" && !series.Year && !series.Poster && series.TmdbId == 0,
            "a malformed year, an http poster or a missing TMDB id was kept");
        Require(series.Character && *series.Character == L"Himself", "a credit's character was not read");

        auto const bare = Mappers::ParsePerson(Json(LR"({"person":{"tmdbId":5,"name":"Bare"}})"));
        Require(bare.Credits.empty() && !bare.Partial && !bare.Person.Photo && !bare.Person.Biography,
            "a person without credits or optional fields was not read as such");

        std::wstring longBiography = LR"({"person":{"tmdbId":5,"name":"Long","biography":")" + std::wstring(2500, L'b') + LR"("}})";
        auto const longOne = Mappers::ParsePerson(Json(longBiography));
        Require(longOne.Person.Biography && longOne.Person.Biography->size() == 2000, "a long biography was not cut at 2000 characters");

        std::wstring many = LR"({"person":{"tmdbId":5,"name":"Busy"},"credits":[)";
        for (int index = 1; index <= 70; ++index)
        {
            if (index > 1) many += L",";
            many += L"{\"type\":\"movie\",\"imdbId\":\"tt" + std::to_wstring(1000000 + index) + L"\",\"name\":\"Film\",\"roles\":[\"cast\"]}";
        }
        many += L"]}";
        Require(Mappers::ParsePerson(Json(many)).Credits.size() == 60, "a filmography was not capped at sixty");

        Require(Throws([] { static_cast<void>(Mappers::ParsePerson(Json(LR"({"credits":[]})"))); }), "a person answer without a person was accepted");
        Require(Throws([] { static_cast<void>(Mappers::ParsePerson(Json(LR"({"person":{"tmdbId":0,"name":"Zero"}})"))); }), "a person with a bad id was accepted");
        Require(Throws([] { static_cast<void>(Mappers::ParsePerson(Json(LR"({"person":{"tmdbId":5}})"))); }), "a person without a name was accepted");
        Require(Throws([] { static_cast<void>(Mappers::ParsePerson(Json(LR"([])"))); }), "a person answer that is not an object was accepted");
    }

    void TestLookupMapping()
    {
        auto const title = Mappers::ParseIdLookup(Json(LR"({"kind":"title","type":"series","id":"tt0944947","name":"Game of Thrones","year":"2011","poster":"https://image.tmdb.org/t/p/w342/g.jpg"})"));
        Require(title.Kind == Dto::IdLookupKind::Title && title.Type == L"series" && title.Id == L"tt0944947"
            && title.Name == L"Game of Thrones" && title.Year && title.Poster, "a title lookup was not read");
        auto const person = Mappers::ParseIdLookup(Json(LR"({"kind":"person","tmdbId":137427,"name":"Denis Villeneuve","photo":"https://evil.example/x.jpg"})"));
        Require(person.Kind == Dto::IdLookupKind::Person && person.TmdbId == 137427 && person.Name == L"Denis Villeneuve" && !person.Photo,
            "a person lookup was not read, or its foreign photo was kept");
        for (auto const body : {
                 LR"({"kind":"title","type":"tv","id":"tt1","name":"X"})",
                 LR"({"kind":"title","type":"movie","id":"nm1","name":"X"})",
                 LR"({"kind":"title","type":"movie","id":"tt1"})",
                 LR"({"kind":"person","name":"X"})",
                 LR"({"kind":"episode","name":"X"})",
                 LR"({"name":"X"})",
                 LR"(42)" })
        {
            Require(Throws([body] { static_cast<void>(Mappers::ParseIdLookup(Json(body))); }), "a malformed lookup answer was accepted");
        }
    }

    void TestCastTmdbIds()
    {
        auto const meta = Mappers::ParseMeta(Json(LR"({"meta":{"id":"tt1","type":"movie","name":"Film","app_extras":{"cast":[
            {"name":"Server Id","tmdbId":11,"photo":"https://image.tmdb.org/t/p/w185/a.jpg"},
            {"name":"Addon Id","id":12},
            {"name":"Bad Id","id":"13"},
            {"name":"Fraction","tmdbId":1.25,"id":0},
            {"name":"Both","tmdbId":14,"id":15}
        ]}}})"));
        Require(meta.PicturedCast.size() == 5, "the pictured cast lost an entry");
        Require(meta.PicturedCast[0].TmdbId == 11, "the server's tmdbId was not read");
        Require(meta.PicturedCast[1].TmdbId == 12, "an addon's own id was not read as a TMDB id");
        Require(!meta.PicturedCast[2].TmdbId && !meta.PicturedCast[3].TmdbId, "a malformed person id was kept");
        Require(meta.PicturedCast[4].TmdbId == 14, "tmdbId did not win over id");
    }

    Dto::PersonSummary Person(std::int64_t id, std::wstring const& name, double popularity)
    {
        return Dto::PersonSummary{ .TmdbId = id, .Name = winrt::hstring{ name }, .Popularity = popularity };
    }

    void TestPeopleRow()
    {
        std::vector<Dto::PersonSummary> const people{
            Person(1, L"Denis Villeneuve Jr", 50.0),
            Person(2, L"Denis Villeneuve", 10.0),
            Person(3, L"Denis Villeneuves", 99.0),
            Person(4, L"Jacques Villeneuve", 99.0),
            Person(5, L"Denis Villeneuve", 30.0),
            Person(6, L"Denis Villeneuve Sr", 70.0),
        };
        auto const row = Services::ChoosePeopleRow(people, L"  denis villeneuve ", std::nullopt);
        Require(row.size() == 3, "the people row was not capped at three");
        Require(row[0].TmdbId == 5 && row[1].TmdbId == 2, "exact names did not come first, most popular first");
        Require(row[2].TmdbId == 6, "a name starting with the query's words did not follow, most popular first");
        for (auto const& person : row)
        {
            Require(person.TmdbId != 3 && person.TmdbId != 4, "a loosely named person reached the people row");
        }

        auto const capped = Services::ChoosePeopleRow(people, L"denis villeneuve", 0);
        Require(capped.size() == 2 && capped[0].TmdbId == 5 && capped[1].TmdbId == 2,
            "a person named worse than the best title was shown");
        auto const surname = Services::ChoosePeopleRow(people, L"villeneuve", 1);
        Require(surname.size() == 3 && surname[0].TmdbId == 4 && surname[1].TmdbId == 6 && surname[2].TmdbId == 1,
            "a surname alone did not find its people, most popular first");
        Require(Services::ChoosePeopleRow(people, L"villeneuve", 0).empty(),
            "a surname stayed beside a title named exactly that");
        Require(Services::ChoosePeopleRow(people, L"spielberg", std::nullopt).empty(),
            "someone not named by the query reached the people row");
        Require(Services::ChoosePeopleRow({}, L"dune", std::nullopt).empty(), "nobody made a people row");

        // Beside someone with a photo, namesakes without one are left out.
        auto pictured = Person(7, L"Denis Villeneuve", 5.0);
        pictured.Photo = L"https://image.tmdb.org/t/p/w185/denis.jpg";
        auto withPictured = people;
        withPictured.push_back(pictured);
        auto const photoRow = Services::ChoosePeopleRow(withPictured, L"denis villeneuve", std::nullopt);
        Require(photoRow.size() == 1 && photoRow[0].TmdbId == 7, "a namesake without a photo stayed beside one with a photo");

        std::vector<Services::SearchTitle> titles(2);
        titles[0].Name = L"Dune: Part Two";
        titles[1].Name = L"Dune";
        Require(Services::BestTitleTier(titles, L"dune") == 0, "the best title tier was not the exact name");
        Require(Services::BestTitleTier(titles, L"dune 2021") == 0, "the best title tier did not read the name without its year");
        Require(!Services::BestTitleTier({}, L"dune"), "no titles gave a best tier");
    }

    void TestExactNamePerson()
    {
        std::vector<Dto::PersonSummary> const people{
            Person(1, L"Tom Hardy Jr", 90.0),
            Person(2, L"Tom Hardy", 20.0),
            Person(3, L"tom hardy", 40.0),
        };
        auto const chosen = Services::ExactNamePerson(people, L" Tom Hardy ");
        Require(chosen && people[*chosen].TmdbId == 3, "the most popular exact name was not chosen");
        Require(!Services::ExactNamePerson(people, L"Tom"), "a partial name was taken as exact");
        Require(!Services::ExactNamePerson(people, L"  "), "an empty name chose someone");
    }

    void TestPersonLines()
    {
        auto person = Person(1, L"Denis Villeneuve", 1.0);
        person.KnownFor = Dto::PersonDepartment::Directing;
        person.KnownForTitles = { L"Dune", L"Arrival", L"Sicario" };
        Require(Services::PersonRowLine(person) == L"Director · Dune, Arrival, Sicario", "the people row's line was misread");
        person.KnownForTitles.clear();
        Require(Services::PersonRowLine(person) == L"Director", "the line without titles was misread");
        person.KnownFor = Dto::PersonDepartment::Unknown;
        person.KnownForTitles = { L"Dune" };
        Require(Services::PersonRowLine(person) == L"Dune", "the line without a department was misread");
        person.KnownForTitles.clear();
        Require(Services::PersonRowLine(person).empty(), "an unknown person had a line");
        Require(Services::KnownForLabel(Dto::PersonDepartment::Acting) == L"Actor"
            && Services::KnownForLabel(Dto::PersonDepartment::Writing) == L"Writer", "a known-for label was misread");
        Require(Services::DepartmentLabel(Dto::PersonDepartment::Directing) == L"Directing"
            && Services::DepartmentLabel(Dto::PersonDepartment::Unknown).empty(), "a department label was misread");
    }

    Dto::PersonCredit Credit(std::wstring const& id, std::wstring const& year, std::vector<Dto::CreditRole> roles, double popularity)
    {
        Dto::PersonCredit credit{ .Type = L"movie", .ImdbId = winrt::hstring{ id }, .Name = winrt::hstring{ id }, .Roles = std::move(roles), .Popularity = popularity };
        if (!year.empty()) credit.Year = winrt::hstring{ year };
        return credit;
    }

    std::vector<std::wstring> Ids(std::vector<Dto::PersonCredit> const& credits)
    {
        std::vector<std::wstring> ids;
        for (auto const& credit : credits) ids.emplace_back(credit.ImdbId);
        return ids;
    }

    void TestFilmography()
    {
        using Role = Dto::CreditRole;
        std::vector<Dto::PersonCredit> const credits{
            Credit(L"tt1", L"2016", { Role::Director }, 50.0),
            Credit(L"tt2", L"", { Role::Cast }, 99.0),
            Credit(L"tt3", L"2021", { Role::Director, Role::Writer }, 10.0),
            Credit(L"tt4", L"2021", { Role::Writer }, 30.0),
            Credit(L"tt5", L"2024", { Role::Writer, Role::Director }, 5.0),
        };
        Require(Ids(Services::Filmography(credits, Services::FilmographyFilter::All))
                == std::vector<std::wstring>{ L"tt5", L"tt4", L"tt3", L"tt1", L"tt2" },
            "the filmography was not newest first, the more popular first within a year, undated last");
        Require(Ids(Services::Filmography(credits, Services::FilmographyFilter::Writing))
                == std::vector<std::wstring>{ L"tt5", L"tt4", L"tt3" },
            "the writing chip kept the wrong credits");
        Require(Ids(Services::Filmography(credits, Services::FilmographyFilter::Acting)) == std::vector<std::wstring>{ L"tt2" },
            "the acting chip kept the wrong credits");
        Require(Services::FilmographyFilters(credits) == std::vector<Services::FilmographyFilter>{
                    Services::FilmographyFilter::All, Services::FilmographyFilter::Acting,
                    Services::FilmographyFilter::Directing, Services::FilmographyFilter::Writing },
            "the chips were not All then each role held, in order");
        std::vector<Dto::PersonCredit> const directorOnly{ Credit(L"tt1", L"2016", { Role::Director }, 1.0) };
        Require(Services::FilmographyFilters(directorOnly) == std::vector<Services::FilmographyFilter>{
                    Services::FilmographyFilter::All, Services::FilmographyFilter::Directing },
            "a role nobody holds got a chip");
        Require(Services::FilmographyFilters({}) == std::vector<Services::FilmographyFilter>{ Services::FilmographyFilter::All },
            "an empty filmography offered roles");

        Require(Services::CreditLine(credits[4]) == L"2024 · Director · Writer", "a credit's line did not read year, then roles in order");
        Require(Services::CreditLine(credits[1]) == L"Actor", "an undated credit's line was misread");
        std::vector<Role> const all{ Role::Writer, Role::Cast, Role::Director };
        Require(Services::CreditRoleLabel(all) == L"Actor · Director · Writer", "the role label was not in its fixed order");
        Require(Services::CreditRoleLabel({}).empty(), "no roles had a label");
    }
}

void RunPeopleTests()
{
    TestCapabilitiesMapping();
    TestTmdbImageUrls();
    TestPeopleSearchMapping();
    TestPersonMapping();
    TestLookupMapping();
    TestCastTmdbIds();
    TestPeopleRow();
    TestExactNamePerson();
    TestPersonLines();
    TestFilmography();
}
