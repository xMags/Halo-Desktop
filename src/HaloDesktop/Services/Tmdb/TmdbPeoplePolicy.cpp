#include "Services/Tmdb/TmdbPeoplePolicy.h"

#include "Services/SearchResultsPolicy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <set>
#include <unordered_set>
#include <utility>

namespace
{
    namespace Json = winrt::Windows::Data::Json;
    using HaloDesktop::Services::Tmdb::CreditCandidate;
    using HaloDesktop::Services::Tmdb::CreditRole;
    using HaloDesktop::Services::Tmdb::PersonSummary;
    using HaloDesktop::Services::Tmdb::TitleHit;
    using HaloDesktop::Services::Tmdb::TitleType;

    constexpr std::wstring_view ImageBase = L"https://image.tmdb.org/t/p/";
    // Number.MAX_SAFE_INTEGER.
    constexpr double MaximumSafeInteger = 9007199254740991.0;
    // TMDB TV genres that are not shows a person acted in: News, Reality, Talk.
    constexpr std::array<double, 3> ExcludedTvGenres{ 10763.0, 10764.0, 10767.0 };

    Json::JsonObject ObjectOf(Json::IJsonValue const& value)
    {
        if (!value || value.ValueType() != Json::JsonValueType::Object) return nullptr;
        return value.GetObject();
    }

    Json::IJsonValue FieldOf(Json::JsonObject const& object, wchar_t const* key)
    {
        if (!object || !object.HasKey(key)) return nullptr;
        return object.GetNamedValue(key);
    }

    std::vector<Json::IJsonValue> ArrayOf(Json::IJsonValue const& value)
    {
        std::vector<Json::IJsonValue> items;
        if (!value || value.ValueType() != Json::JsonValueType::Array) return items;
        auto const array = value.GetArray();
        items.reserve(array.Size());
        for (auto const& item : array) items.push_back(item);
        return items;
    }

    std::optional<std::wstring> StringOf(Json::IJsonValue const& value)
    {
        if (!value || value.ValueType() != Json::JsonValueType::String) return std::nullopt;
        return std::wstring{ value.GetString() };
    }

    std::optional<double> NumberOf(Json::IJsonValue const& value)
    {
        if (!value || value.ValueType() != Json::JsonValueType::Number) return std::nullopt;
        return value.GetNumber();
    }

    // A string, trimmed, not empty and at most MaximumText characters.
    std::optional<std::wstring> BoundedText(Json::IJsonValue const& value)
    {
        auto const text = StringOf(value);
        if (!text) return std::nullopt;
        auto trimmed = HaloDesktop::Services::TrimmedText(*text);
        if (trimmed.empty() || trimmed.size() > HaloDesktop::Services::Tmdb::MaximumText) return std::nullopt;
        return trimmed;
    }

    std::optional<std::int64_t> PositiveSafeInteger(Json::IJsonValue const& value)
    {
        auto const number = NumberOf(value);
        if (!number || !std::isfinite(*number) || *number <= 0.0 || *number > MaximumSafeInteger
            || std::floor(*number) != *number)
        {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(*number);
    }

    double PopularityOf(Json::IJsonValue const& value)
    {
        auto const number = NumberOf(value);
        return number && std::isfinite(*number) && *number > 0.0 ? *number : 0.0;
    }

    std::optional<std::wstring> DepartmentOf(Json::IJsonValue const& value)
    {
        auto const text = StringOf(value);
        if (text && (*text == L"Acting" || *text == L"Directing" || *text == L"Writing")) return text;
        return std::nullopt;
    }

    bool IsDigit(wchar_t character) noexcept
    {
        return character >= L'0' && character <= L'9';
    }

    // `YYYY-MM-DD` gives its year.
    std::optional<std::wstring> YearOf(Json::IJsonValue const& value)
    {
        auto const text = StringOf(value);
        if (!text || text->size() != 10) return std::nullopt;
        auto const& date = *text;
        for (std::size_t index = 0; index < date.size(); ++index)
        {
            auto const separator = index == 4 || index == 7;
            if (separator ? date[index] != L'-' : !IsDigit(date[index])) return std::nullopt;
        }
        return date.substr(0, 4);
    }

    // TMDB image paths look like `/abc123XYZ.jpg`.
    std::optional<std::wstring> ImageUrl(wchar_t const* size, Json::IJsonValue const& value)
    {
        auto const path = StringOf(value);
        if (!path || path->size() < 2 || path->front() != L'/') return std::nullopt;
        auto const dot = path->rfind(L'.');
        if (dot == std::wstring::npos || dot < 2 || dot - 1 > 100) return std::nullopt;
        for (std::size_t index = 1; index < dot; ++index)
        {
            auto const character = (*path)[index];
            auto const plain = IsDigit(character) || (character >= L'a' && character <= L'z')
                || (character >= L'A' && character <= L'Z') || character == L'_' || character == L'-';
            if (!plain) return std::nullopt;
        }
        auto const extension = path->substr(dot + 1);
        if (extension != L"jpg" && extension != L"jpeg" && extension != L"png" && extension != L"webp") return std::nullopt;
        return std::wstring{ ImageBase } + size + *path;
    }

    std::optional<TitleHit> TitleHitOf(Json::IJsonValue const& entry, TitleType type)
    {
        auto const object = ObjectOf(entry);
        if (!object) return std::nullopt;
        auto const tmdbId = PositiveSafeInteger(FieldOf(object, L"id"));
        auto name = BoundedText(FieldOf(object, type == TitleType::Movie ? L"title" : L"name"));
        if (!tmdbId || !name) return std::nullopt;
        return TitleHit{
            .Type = type,
            .TmdbId = *tmdbId,
            .Name = std::move(*name),
            .Year = YearOf(FieldOf(object, type == TitleType::Movie ? L"release_date" : L"first_air_date")),
            .Poster = ImageUrl(L"w342", FieldOf(object, L"poster_path")),
            .Popularity = PopularityOf(FieldOf(object, L"popularity")),
        };
    }

    // A credited title; empty for anything not a film or a series, or a dropped TV genre.
    std::optional<TitleHit> CreditTitleOf(Json::IJsonValue const& entry)
    {
        auto const object = ObjectOf(entry);
        if (!object) return std::nullopt;
        auto const kind = StringOf(FieldOf(object, L"media_type"));
        if (kind == L"tv")
        {
            for (auto const& genre : ArrayOf(FieldOf(object, L"genre_ids")))
            {
                auto const id = NumberOf(genre);
                if (id && std::find(ExcludedTvGenres.begin(), ExcludedTvGenres.end(), *id) != ExcludedTvGenres.end())
                {
                    return std::nullopt;
                }
            }
            return TitleHitOf(entry, TitleType::Series);
        }
        if (kind == L"movie") return TitleHitOf(entry, TitleType::Movie);
        return std::nullopt;
    }

    std::wstring Lower(std::wstring text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(std::towlower(character));
        });
        return text;
    }

    // "Self", "Himself", "Herself", alone or followed by " - Host",
    // " (archive footage)" and the like: an appearance, not a role.
    bool IsSelfCharacter(std::wstring const& character)
    {
        auto const lower = Lower(character);
        for (std::wstring_view const word : { L"self", L"himself", L"herself" })
        {
            if (!lower.starts_with(word)) continue;
            auto rest = std::wstring_view{ lower }.substr(word.size());
            if (rest.empty()) return true;
            while (!rest.empty() && std::iswspace(rest.front())) rest.remove_prefix(1);
            if (!rest.empty() && (rest.front() == L'-' || rest.front() == L'(' || rest.front() == L':')) return true;
        }
        return false;
    }

    int RoleOrder(CreditRole role) noexcept
    {
        switch (role)
        {
        case CreditRole::Cast: return 0;
        case CreditRole::Director: return 1;
        case CreditRole::Writer: return 2;
        }
        return 3;
    }

    wchar_t const* RoleName(CreditRole role) noexcept
    {
        switch (role)
        {
        case CreditRole::Cast: return L"cast";
        case CreditRole::Director: return L"director";
        case CreditRole::Writer: return L"writer";
        }
        return L"cast";
    }

    Json::IJsonValue TextOrNull(std::optional<std::wstring> const& text)
    {
        return text ? Json::JsonValue::CreateStringValue(*text) : Json::JsonValue::CreateNullValue();
    }

    wchar_t const* TypeName(TitleType type) noexcept
    {
        return type == TitleType::Movie ? L"movie" : L"series";
    }

    // JavaScript's `\s`, for the lookahead after a sentence's end.
    bool IsJsSpace(wchar_t character) noexcept
    {
        return std::iswspace(character) != 0 || character == 0xA0 || character == 0xFEFF
            || character == 0x2028 || character == 0x2029;
    }
}

namespace HaloDesktop::Services::Tmdb
{
    std::vector<PersonSummary> ParsePeopleSearch(Json::IJsonValue const& body)
    {
        std::vector<PersonSummary> people;
        for (auto const& entry : ArrayOf(FieldOf(ObjectOf(body), L"results")))
        {
            if (people.size() >= MaximumSearchPeople) break;
            auto const object = ObjectOf(entry);
            if (!object) continue;
            auto const tmdbId = PositiveSafeInteger(FieldOf(object, L"id"));
            auto name = BoundedText(FieldOf(object, L"name"));
            if (!tmdbId || !name) continue;
            std::vector<std::wstring> knownForTitles;
            for (auto const& title : ArrayOf(FieldOf(object, L"known_for")))
            {
                if (knownForTitles.size() >= MaximumKnownForTitles) break;
                auto const titleObject = ObjectOf(title);
                if (!titleObject) continue;
                auto text = BoundedText(FieldOf(titleObject, L"title"));
                if (!text) text = BoundedText(FieldOf(titleObject, L"name"));
                if (text) knownForTitles.push_back(std::move(*text));
            }
            people.push_back(PersonSummary{
                .TmdbId = *tmdbId,
                .Name = std::move(*name),
                .KnownFor = DepartmentOf(FieldOf(object, L"known_for_department")),
                .Photo = ImageUrl(L"w185", FieldOf(object, L"profile_path")),
                .Popularity = PopularityOf(FieldOf(object, L"popularity")),
                .KnownForTitles = std::move(knownForTitles),
            });
        }
        return people;
    }

    std::vector<CreditCandidate> SelectCredits(Json::IJsonValue const& combinedCredits)
    {
        auto const credits = ObjectOf(combinedCredits);
        if (!credits) return {};
        // In first-seen order, which breaks ties in popularity as the server's does.
        std::vector<CreditCandidate> byTitle;
        auto const add = [&byTitle](Json::IJsonValue const& entry, CreditRole role)
        {
            auto hit = CreditTitleOf(entry);
            if (!hit) return;
            std::optional<std::wstring> character;
            if (role == CreditRole::Cast)
            {
                character = BoundedText(FieldOf(ObjectOf(entry), L"character"));
                if (character && IsSelfCharacter(*character)) return;
            }
            auto const existing = std::find_if(byTitle.begin(), byTitle.end(), [&hit](CreditCandidate const& candidate)
            {
                return candidate.Title.Type == hit->Type && candidate.Title.TmdbId == hit->TmdbId;
            });
            if (existing == byTitle.end())
            {
                byTitle.push_back(CreditCandidate{ .Title = std::move(*hit), .Roles = { role }, .Character = std::move(character) });
                return;
            }
            if (std::find(existing->Roles.begin(), existing->Roles.end(), role) == existing->Roles.end())
            {
                existing->Roles.push_back(role);
            }
            if (!existing->Character) existing->Character = std::move(character);
            existing->Title.Popularity = (std::max)(existing->Title.Popularity, hit->Popularity);
        };
        for (auto const& entry : ArrayOf(FieldOf(credits, L"cast"))) add(entry, CreditRole::Cast);
        for (auto const& entry : ArrayOf(FieldOf(credits, L"crew")))
        {
            auto const object = ObjectOf(entry);
            if (!object) continue;
            if (StringOf(FieldOf(object, L"job")) == L"Director") add(entry, CreditRole::Director);
            if (StringOf(FieldOf(object, L"department")) == L"Writing") add(entry, CreditRole::Writer);
        }
        for (auto& credit : byTitle)
        {
            std::stable_sort(credit.Roles.begin(), credit.Roles.end(), [](CreditRole left, CreditRole right)
            {
                return RoleOrder(left) < RoleOrder(right);
            });
        }
        std::stable_sort(byTitle.begin(), byTitle.end(), [](CreditCandidate const& left, CreditCandidate const& right)
        {
            if (left.Title.Popularity != right.Title.Popularity) return left.Title.Popularity > right.Title.Popularity;
            return left.Title.TmdbId < right.Title.TmdbId;
        });
        if (byTitle.size() > MaximumPersonCredits) byTitle.resize(MaximumPersonCredits);
        return byTitle;
    }

    std::wstring EndAtSentence(std::wstring const& text, std::size_t max)
    {
        if (text.size() <= max) return text;
        auto const cut = text.substr(0, max);
        std::optional<std::size_t> end;
        for (std::size_t index = 0; index < cut.size(); ++index)
        {
            auto const character = cut[index];
            if (character != L'.' && character != L'!' && character != L'?') continue;
            auto after = index + 1;
            if (after < cut.size() && (cut[after] == L'"' || cut[after] == L'\'' || cut[after] == L')' || cut[after] == L']'))
            {
                // The closing mark counts only when a space follows it.
                if (after + 1 < cut.size() && IsJsSpace(cut[after + 1]))
                {
                    end = after + 1;
                    continue;
                }
            }
            if (after < cut.size() && IsJsSpace(cut[after])) end = after;
        }
        return end && *end * 2 >= max ? cut.substr(0, *end) : cut;
    }

    std::optional<PersonDetails> ParsePersonDetails(Json::IJsonValue const& body)
    {
        auto const object = ObjectOf(body);
        if (!object) return std::nullopt;
        auto const tmdbId = PositiveSafeInteger(FieldOf(object, L"id"));
        auto name = BoundedText(FieldOf(object, L"name"));
        if (!tmdbId || !name) return std::nullopt;
        std::optional<std::wstring> biography;
        if (auto const text = StringOf(FieldOf(object, L"biography")))
        {
            auto cut = EndAtSentence(HaloDesktop::Services::TrimmedText(*text), MaximumBiography);
            if (!cut.empty()) biography = std::move(cut);
        }
        return PersonDetails{
            .TmdbId = *tmdbId,
            .Name = std::move(*name),
            .Photo = ImageUrl(L"w185", FieldOf(object, L"profile_path")),
            .KnownFor = DepartmentOf(FieldOf(object, L"known_for_department")),
            .Biography = std::move(biography),
            .Credits = SelectCredits(FieldOf(object, L"combined_credits")),
        };
    }

    FindResult ParseFind(Json::IJsonValue const& body)
    {
        FindResult result;
        auto const object = ObjectOf(body);
        if (!object) return result;
        for (auto const& entry : ArrayOf(FieldOf(object, L"movie_results")))
        {
            if (auto hit = TitleHitOf(entry, TitleType::Movie)) result.Movies.push_back(std::move(*hit));
        }
        for (auto const& entry : ArrayOf(FieldOf(object, L"tv_results")))
        {
            if (auto hit = TitleHitOf(entry, TitleType::Series)) result.Series.push_back(std::move(*hit));
        }
        for (auto const& entry : ArrayOf(FieldOf(object, L"person_results")))
        {
            auto const person = ObjectOf(entry);
            if (!person) continue;
            auto const tmdbId = PositiveSafeInteger(FieldOf(person, L"id"));
            auto name = BoundedText(FieldOf(person, L"name"));
            if (!tmdbId || !name) continue;
            result.People.push_back(PersonHit{ .TmdbId = *tmdbId, .Name = std::move(*name), .Photo = ImageUrl(L"w185", FieldOf(person, L"profile_path")) });
        }
        return result;
    }

    std::optional<std::wstring> ParseImdbId(Json::IJsonValue const& body)
    {
        auto const id = StringOf(FieldOf(ObjectOf(body), L"imdb_id"));
        if (!id || id->size() < 3 || id->size() > 12 || !id->starts_with(L"tt")) return std::nullopt;
        if (!std::all_of(id->begin() + 2, id->end(), IsDigit)) return std::nullopt;
        return id;
    }

    std::vector<std::wstring> ClosestPeopleWords(std::wstring_view query)
    {
        constexpr std::size_t MinimumWordLength = 3;
        constexpr std::size_t MaximumWordsSearched = 3;
        auto const typed = HaloDesktop::Services::NameKey(query);
        std::vector<std::wstring> words;
        std::set<std::wstring, std::less<>> seen;
        std::size_t start = 0;
        while (start <= typed.size())
        {
            auto end = typed.find(L' ', start);
            if (end == std::wstring::npos) end = typed.size();
            auto word = typed.substr(start, end - start);
            if (!word.empty() && seen.insert(word).second) words.push_back(std::move(word));
            start = end + 1;
        }
        if (words.size() < 2) return {};
        std::erase_if(words, [](std::wstring const& word) { return word.size() < MinimumWordLength; });
        std::stable_sort(words.begin(), words.end(), [](std::wstring const& left, std::wstring const& right)
        {
            return left.size() > right.size();
        });
        if (words.size() > MaximumWordsSearched) words.resize(MaximumWordsSearched);
        return words;
    }

    std::size_t AllowedDistance(std::size_t length) noexcept
    {
        return (std::max)(std::size_t{ 1 }, length / 4);
    }

    std::size_t EditDistance(std::wstring_view a, std::wstring_view b, std::size_t limit)
    {
        auto const difference = a.size() > b.size() ? a.size() - b.size() : b.size() - a.size();
        if (difference > limit) return limit + 1;
        std::vector<std::size_t> previous(b.size() + 1);
        for (std::size_t index = 0; index <= b.size(); ++index) previous[index] = index;
        std::vector<std::size_t> current(b.size() + 1);
        for (std::size_t i = 1; i <= a.size(); ++i)
        {
            current[0] = i;
            auto best = i;
            for (std::size_t j = 1; j <= b.size(); ++j)
            {
                auto const cost = a[i - 1] == b[j - 1] ? 0u : 1u;
                current[j] = (std::min)({ previous[j] + 1, current[j - 1] + 1, previous[j - 1] + cost });
                best = (std::min)(best, current[j]);
            }
            if (best > limit) return limit + 1;
            std::swap(previous, current);
        }
        return previous[b.size()];
    }

    std::optional<ClosestPeople> RankClosestPeople(
        std::wstring_view query,
        std::vector<std::vector<PersonSummary>> const& answers)
    {
        constexpr std::size_t MaximumClosestPeople = 3;
        auto const typed = HaloDesktop::Services::NameKey(query);
        auto const limit = AllowedDistance(typed.size());
        struct Near final
        {
            PersonSummary const* Person{};
            std::size_t Distance{};
        };
        std::vector<Near> near;
        std::unordered_set<std::int64_t> seen;
        for (auto const& answer : answers)
        {
            for (auto const& person : answer)
            {
                if (!seen.insert(person.TmdbId).second) continue;
                auto const distance = EditDistance(typed, HaloDesktop::Services::NameKey(person.Name), limit);
                if (distance <= limit) near.push_back(Near{ .Person = &person, .Distance = distance });
            }
        }
        std::stable_sort(near.begin(), near.end(), [](Near const& left, Near const& right)
        {
            if (left.Distance != right.Distance) return left.Distance < right.Distance;
            return left.Person->Popularity > right.Person->Popularity;
        });
        if (near.empty()) return std::nullopt;
        ClosestPeople result;
        result.Corrected = near.front().Person->Name;
        for (std::size_t index = 0; index < near.size() && index < MaximumClosestPeople; ++index)
        {
            result.People.push_back(*near[index].Person);
        }
        return result;
    }

    Json::JsonObject PeopleSearchAnswer(std::vector<PersonSummary> const& people, std::optional<std::wstring> const& corrected)
    {
        Json::JsonArray list;
        for (auto const& person : people)
        {
            Json::JsonObject entry;
            entry.Insert(L"tmdbId", Json::JsonValue::CreateNumberValue(static_cast<double>(person.TmdbId)));
            entry.Insert(L"name", Json::JsonValue::CreateStringValue(person.Name));
            entry.Insert(L"knownFor", TextOrNull(person.KnownFor));
            entry.Insert(L"photo", TextOrNull(person.Photo));
            entry.Insert(L"popularity", Json::JsonValue::CreateNumberValue(person.Popularity));
            Json::JsonArray titles;
            for (auto const& title : person.KnownForTitles) titles.Append(Json::JsonValue::CreateStringValue(title));
            entry.Insert(L"knownForTitles", titles);
            list.Append(entry);
        }
        Json::JsonObject answer;
        answer.Insert(L"people", list);
        if (corrected) answer.Insert(L"corrected", Json::JsonValue::CreateStringValue(*corrected));
        return answer;
    }

    Json::JsonObject PersonAnswer(PersonDetails const& person, std::vector<MappedCredit> const& credits, bool partial)
    {
        Json::JsonObject who;
        who.Insert(L"tmdbId", Json::JsonValue::CreateNumberValue(static_cast<double>(person.TmdbId)));
        who.Insert(L"name", Json::JsonValue::CreateStringValue(person.Name));
        who.Insert(L"photo", TextOrNull(person.Photo));
        who.Insert(L"knownFor", TextOrNull(person.KnownFor));
        who.Insert(L"biography", TextOrNull(person.Biography));
        Json::JsonArray list;
        for (auto const& mapped : credits)
        {
            auto const& title = mapped.Credit.Title;
            Json::JsonObject entry;
            entry.Insert(L"type", Json::JsonValue::CreateStringValue(TypeName(title.Type)));
            entry.Insert(L"imdbId", Json::JsonValue::CreateStringValue(mapped.ImdbId));
            entry.Insert(L"tmdbId", Json::JsonValue::CreateNumberValue(static_cast<double>(title.TmdbId)));
            entry.Insert(L"name", Json::JsonValue::CreateStringValue(title.Name));
            entry.Insert(L"year", TextOrNull(title.Year));
            entry.Insert(L"poster", TextOrNull(title.Poster));
            Json::JsonArray roles;
            for (auto const role : mapped.Credit.Roles) roles.Append(Json::JsonValue::CreateStringValue(RoleName(role)));
            entry.Insert(L"roles", roles);
            entry.Insert(L"character", TextOrNull(mapped.Credit.Character));
            entry.Insert(L"popularity", Json::JsonValue::CreateNumberValue(title.Popularity));
            list.Append(entry);
        }
        Json::JsonObject answer;
        answer.Insert(L"person", who);
        answer.Insert(L"credits", list);
        answer.Insert(L"partial", Json::JsonValue::CreateBooleanValue(partial));
        return answer;
    }

    Json::JsonObject PersonLookupAnswer(PersonHit const& person)
    {
        Json::JsonObject answer;
        answer.Insert(L"kind", Json::JsonValue::CreateStringValue(L"person"));
        answer.Insert(L"tmdbId", Json::JsonValue::CreateNumberValue(static_cast<double>(person.TmdbId)));
        answer.Insert(L"name", Json::JsonValue::CreateStringValue(person.Name));
        answer.Insert(L"photo", TextOrNull(person.Photo));
        return answer;
    }

    Json::JsonObject TitleLookupAnswer(TitleHit const& title, std::wstring const& imdbId)
    {
        Json::JsonObject answer;
        answer.Insert(L"kind", Json::JsonValue::CreateStringValue(L"title"));
        answer.Insert(L"type", Json::JsonValue::CreateStringValue(TypeName(title.Type)));
        answer.Insert(L"id", Json::JsonValue::CreateStringValue(imdbId));
        answer.Insert(L"name", Json::JsonValue::CreateStringValue(title.Name));
        answer.Insert(L"year", TextOrNull(title.Year));
        answer.Insert(L"poster", TextOrNull(title.Poster));
        return answer;
    }
}
