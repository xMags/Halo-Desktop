#include "PeopleTests.h"

#include "Api/Dto.h"
#include "Services/FilmographyPolicy.h"
#include "Services/PeopleRowPolicy.h"
#include "Services/SearchResultsPolicy.h"
#include "Services/Tmdb/TmdbCredential.h"
#include "Services/Tmdb/TmdbPeoplePolicy.h"

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
    namespace Tmdb = HaloDesktop::Services::Tmdb;

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

        // A corrected name counts only when it names someone in the answer.
        auto const corrected = Json(LR"({"people":[{"tmdbId":525,"name":"Christopher Nolan"}],"corrected":" Christopher Nolan "})");
        auto const correctedPeople = Mappers::ParsePeopleSearch(corrected);
        Require(Mappers::ParsePeopleCorrection(corrected, correctedPeople) == std::optional<winrt::hstring>{ L"Christopher Nolan" },
            "a corrected name was not read");
        Require(!Mappers::ParsePeopleCorrection(Json(LR"({"people":[{"tmdbId":525,"name":"Christopher Nolan"}],"corrected":"Somebody Else"})"), correctedPeople)
                    && !Mappers::ParsePeopleCorrection(Json(LR"({"people":[],"corrected":7})"), {})
                    && !Mappers::ParsePeopleCorrection(Json(LR"({"people":[]})"), {}),
            "a corrected name naming nobody in the answer was kept");

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

        // A person's films as Search titles: under their IMDb id, in the
        // order given, without the ones that could not be opened.
        std::vector<HaloDesktop::Api::Dto::PersonCredit> credits(4);
        credits[0].Type = L"movie"; credits[0].ImdbId = L"tt1160419"; credits[0].Name = L"Dune";
        credits[0].Year = L"2021"; credits[0].Poster = L"https://image.tmdb.org/t/p/w342/dune.jpg";
        credits[1].Type = L"series"; credits[1].ImdbId = L"tt0000002"; credits[1].Name = L"A Series";
        credits[2].Type = L"movie"; credits[2].Name = L"No Id";
        credits[3].Type = L"channel"; credits[3].ImdbId = L"tt0000004"; credits[3].Name = L"Not A Film";
        auto const creditTitles = Services::CreditTitles(credits);
        Require(creditTitles.size() == 2 && creditTitles[0].Id == L"tt1160419" && creditTitles[0].Type == L"movie"
                    && creditTitles[0].ImdbId == L"tt1160419" && creditTitles[0].Name == L"Dune"
                    && creditTitles[0].ReleaseInfo == L"2021" && creditTitles[0].Poster == L"https://image.tmdb.org/t/p/w342/dune.jpg"
                    && creditTitles[1].Id == L"tt0000002" && creditTitles[1].Type == L"series" && !creditTitles[1].Poster,
            "a person's films were not read as Search titles");

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

namespace
{
    // What a pasted TMDB credential is taken for, by shape only.
    void TestTmdbCredentials()
    {
        auto const key = Tmdb::ParseTmdbCredential(L"  0123456789abcdefABCDEF0123456789 \n");
        Require(key && key->Kind == Tmdb::TmdbCredentialKind::ApiKey && key->Value == L"0123456789abcdefABCDEF0123456789",
            "an API key with spaces around it was not taken");
        Require(!Tmdb::ParseTmdbCredential(L"0123456789abcdef0123456789abcde"), "a 31-character key was taken");
        Require(!Tmdb::ParseTmdbCredential(L"0123456789abcdef0123456789abcdeg"), "a key with a non-hex letter was taken");
        std::wstring const header(40, L'a');
        std::wstring const payload(60, L'B');
        std::wstring const signature(43, L'c');
        auto const token = Tmdb::ParseTmdbCredential(header + L"." + payload + L"." + signature);
        Require(token && token->Kind == Tmdb::TmdbCredentialKind::ReadToken, "a read access token was not taken");
        Require(!Tmdb::ParseTmdbCredential(header + L"." + payload + signature), "a token with two parts was taken");
        Require(!Tmdb::ParseTmdbCredential(header + L".." + payload + L"." + signature), "a token with an empty part was taken");
        Require(!Tmdb::ParseTmdbCredential(header + L"." + payload + L"." + signature + L"/"), "a token with a stray character was taken");
        Require(!Tmdb::ParseTmdbCredential(L""), "nothing was taken for a key");
    }

    // TMDB's people search, read as the server reads it.
    void TestTmdbPeopleSearch()
    {
        std::wstring body = LR"({"results":[
            {"id":525,"name":" Christopher Nolan ","known_for_department":"Directing","profile_path":"/xuAIuYSmsUzKlUMBFGVZaWsY3DZ.jpg","popularity":12.5,
             "known_for":[{"title":"Inception"},{"name":"Westworld"},{"title":"Interstellar"},{"title":"Tenet"}]},
            {"id":-4,"name":"Negative"},
            {"id":7,"name":""},
            {"id":8,"name":"Bad Photo","profile_path":"/../x.jpg","known_for_department":"Sound"},
            {"id":9,"name":"Two"},{"id":10,"name":"Three"},{"id":11,"name":"Four"},{"id":12,"name":"Five"},
            {"id":13,"name":"Six"},{"id":14,"name":"Seven"},{"id":15,"name":"Eight"}]})";
        auto const people = Tmdb::ParsePeopleSearch(Json(body));
        Require(people.size() == Tmdb::MaximumSearchPeople, "a people search kept more than eight people or dropped a valid one");
        Require(people[0].TmdbId == 525 && people[0].Name == L"Christopher Nolan", "a person's id or trimmed name was misread");
        Require(people[0].KnownFor == std::optional<std::wstring>{ L"Directing" }, "a department was misread");
        Require(people[0].Photo == std::optional<std::wstring>{ L"https://image.tmdb.org/t/p/w185/xuAIuYSmsUzKlUMBFGVZaWsY3DZ.jpg" },
            "a profile photo was not made a w185 address");
        Require(people[0].KnownForTitles == std::vector<std::wstring>{ L"Inception", L"Westworld", L"Interstellar" },
            "known-for titles were not read from title or name, three at most");
        Require(people[1].TmdbId == 8 && !people[1].Photo && !people[1].KnownFor, "an unsafe photo path or unknown department was kept");
    }

    // A person's credits: roles merged per title, appearances and talk shows dropped.
    void TestTmdbCredits()
    {
        std::wstring body = LR"({"id":525,"name":"Christopher Nolan","biography":"  First one. Second one. ","known_for_department":"Directing",
            "combined_credits":{
              "cast":[
                {"media_type":"movie","id":1,"title":"Tenet","release_date":"2020-08-22","popularity":40,"character":"Cameo","poster_path":"/tenet.jpg"},
                {"media_type":"tv","id":2,"name":"The Talk Show","first_air_date":"2010-01-01","popularity":90,"genre_ids":[10767]},
                {"media_type":"movie","id":3,"title":"A Documentary","popularity":80,"character":"Himself - Interviewee"},
                {"media_type":"person","id":4,"name":"Not a title","popularity":99}
              ],
              "crew":[
                {"media_type":"movie","id":1,"title":"Tenet","job":"Director","department":"Directing","popularity":40},
                {"media_type":"movie","id":1,"title":"Tenet","job":"Screenplay","department":"Writing","popularity":40},
                {"media_type":"movie","id":5,"title":"Interstellar","job":"Director","department":"Directing","popularity":60,"release_date":"2014"},
                {"media_type":"tv","id":6,"name":"Westworld","job":"Writer","department":"Writing","popularity":50,"genre_ids":[18]}
              ]}})";
        auto const person = Tmdb::ParsePersonDetails(Json(body));
        Require(person && person->TmdbId == 525 && person->Name == L"Christopher Nolan", "a person was not read");
        Require(person->Biography == std::optional<std::wstring>{ L"First one. Second one." }, "a biography was not trimmed");
        auto const& credits = person->Credits;
        Require(credits.size() == 3, "appearances, talk shows or non-titles were kept among the credits");
        Require(credits[0].Title.Name == L"Interstellar" && credits[1].Title.Name == L"Westworld" && credits[2].Title.Name == L"Tenet",
            "credits were not ordered most popular first");
        Require(credits[2].Roles == std::vector<Tmdb::CreditRole>{ Tmdb::CreditRole::Cast, Tmdb::CreditRole::Director, Tmdb::CreditRole::Writer },
            "a title's roles were not merged in order");
        Require(credits[2].Character == std::optional<std::wstring>{ L"Cameo" }, "a cast credit's character was lost");
        Require(credits[2].Title.Year == std::optional<std::wstring>{ L"2020" } && !credits[0].Title.Year,
            "a release year was misread, or one without a full date was kept");
        Require(credits[1].Title.Type == Tmdb::TitleType::Series, "a series credit was taken for a film");
        Require(!Tmdb::ParsePersonDetails(Json(LR"({"id":1,"name":"  "})")), "a person without a name was accepted");
    }

    void TestTmdbSentencesAndIds()
    {
        Require(Tmdb::EndAtSentence(L"Aaaa bbb. Cc dd. Eeeeeeeeeeee", 20) == L"Aaaa bbb. Cc dd.", "a biography was not cut at its last sentence");
        Require(Tmdb::EndAtSentence(L"Hi. aaaaaaaaaaaaaaaaaaaaaaaa", 20) == L"Hi. aaaaaaaaaaaaaaaa", "an early sentence end was taken over the cut");
        Require(Tmdb::EndAtSentence(L"Short.", 20) == L"Short.", "a short text was changed");
        Require(Tmdb::ParseImdbId(Json(LR"({"imdb_id":"tt0816692"})")) == std::optional<std::wstring>{ L"tt0816692" }, "an IMDb id was misread");
        Require(!Tmdb::ParseImdbId(Json(LR"({"imdb_id":"nm0634240"})")) && !Tmdb::ParseImdbId(Json(LR"({"imdb_id":null})")),
            "something other than a title id was taken for one");
        auto const found = Tmdb::ParseFind(Json(LR"({"movie_results":[{"id":157336,"title":"Interstellar","release_date":"2014-11-05"}],
            "tv_results":[],"person_results":[{"id":525,"name":"Christopher Nolan"}]})"));
        Require(found.Movies.size() == 1 && found.Movies[0].TmdbId == 157336 && found.People.size() == 1 && found.Series.empty(),
            "a find answer was misread");
    }

    // A misspelled name: searched word by word, kept only when nearly what was typed.
    void TestTmdbClosestPeople()
    {
        Require(Tmdb::ClosestPeopleWords(L"Crystophar Nolan") == std::vector<std::wstring>{ L"crystophar", L"nolan" },
            "a misspelled name was not searched by its words, longest first");
        Require(Tmdb::ClosestPeopleWords(L"Nolan").empty(), "a one-word name was searched word by word");
        Require(Tmdb::ClosestPeopleWords(L"al de pacino") == std::vector<std::wstring>{ L"pacino" }, "short words were searched");
        Require(Tmdb::EditDistance(L"kitten", L"sitting", 5) == 3, "an edit distance was wrong");
        Require(Tmdb::EditDistance(L"abc", L"xyzxyz", 1) == 2, "a distance over the limit was not cut short");
        Require(Tmdb::AllowedDistance(16) == 4 && Tmdb::AllowedDistance(2) == 1, "the allowed distance was wrong");

        std::vector<std::vector<Tmdb::PersonSummary>> const answers{
            { Tmdb::PersonSummary{ .TmdbId = 525, .Name = L"Christopher Nolan", .Popularity = 10 },
              Tmdb::PersonSummary{ .TmdbId = 527, .Name = L"Jonathan Nolan", .Popularity = 20 } },
            { Tmdb::PersonSummary{ .TmdbId = 525, .Name = L"Christopher Nolan", .Popularity = 10 } },
        };
        auto const closest = Tmdb::RankClosestPeople(L"crystophar nolan", answers);
        Require(closest && closest->Corrected == L"Christopher Nolan", "the person a misspelling means was not found");
        Require(closest->People.size() == 1 && closest->People[0].TmdbId == 525, "someone too far from the typed name was kept, or a person twice");
        Require(!Tmdb::RankClosestPeople(L"zzzzzz qqqqqq", answers), "a name near nobody found someone");
    }

    // The local answers read exactly as the server's do.
    void TestTmdbAnswersReadAsTheServers()
    {
        std::vector<Tmdb::PersonSummary> const people{
            Tmdb::PersonSummary{ .TmdbId = 525, .Name = L"Christopher Nolan", .KnownFor = L"Directing",
                .Photo = L"https://image.tmdb.org/t/p/w185/x.jpg", .Popularity = 12.5, .KnownForTitles = { L"Inception" } },
        };
        auto const search = Tmdb::PeopleSearchAnswer(people, std::wstring{ L"Christopher Nolan" });
        auto const read = Mappers::ParsePeopleSearch(search);
        Require(read.size() == 1 && read[0].TmdbId == 525 && read[0].Name == L"Christopher Nolan"
                && read[0].KnownFor == Dto::PersonDepartment::Directing && read[0].KnownForTitles.size() == 1,
            "a local people answer was not read as the server's");
        Require(Mappers::ParsePeopleCorrection(search, read) == std::optional<winrt::hstring>{ L"Christopher Nolan" },
            "a local spelling correction was not read");

        Tmdb::PersonDetails const person{ .TmdbId = 525, .Name = L"Christopher Nolan", .KnownFor = L"Directing", .Biography = L"Director." };
        std::vector<Tmdb::MappedCredit> const credits{
            Tmdb::MappedCredit{
                .Credit = Tmdb::CreditCandidate{
                    .Title = Tmdb::TitleHit{ .Type = Tmdb::TitleType::Movie, .TmdbId = 157336, .Name = L"Interstellar", .Year = L"2014", .Popularity = 60 },
                    .Roles = { Tmdb::CreditRole::Director, Tmdb::CreditRole::Writer } },
                .ImdbId = L"tt0816692" },
        };
        auto const detail = Mappers::ParsePerson(Tmdb::PersonAnswer(person, credits, true));
        Require(detail.Person.TmdbId == 525 && detail.Partial && detail.Credits.size() == 1, "a local person answer was not read");
        Require(detail.Credits[0].ImdbId == L"tt0816692" && detail.Credits[0].Type == L"movie" && detail.Credits[0].Roles.size() == 2,
            "a local credit was not read");

        auto const title = Mappers::ParseIdLookup(Tmdb::TitleLookupAnswer(
            Tmdb::TitleHit{ .Type = Tmdb::TitleType::Series, .TmdbId = 1399, .Name = L"Game of Thrones", .Year = L"2011" }, L"tt0944947"));
        Require(title.Kind == Dto::IdLookupKind::Title && title.Id == L"tt0944947" && title.Type == L"series",
            "a local title lookup was not read");
        auto const who = Mappers::ParseIdLookup(Tmdb::PersonLookupAnswer(Tmdb::PersonHit{ .TmdbId = 525, .Name = L"Christopher Nolan" }));
        Require(who.Kind == Dto::IdLookupKind::Person && who.TmdbId == 525, "a local person lookup was not read");
    }
}

void RunPeopleTests()
{
    TestTmdbCredentials();
    TestTmdbPeopleSearch();
    TestTmdbCredits();
    TestTmdbSentencesAndIds();
    TestTmdbClosestPeople();
    TestTmdbAnswersReadAsTheServers();
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
