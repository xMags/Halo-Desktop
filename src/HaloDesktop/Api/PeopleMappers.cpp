#include "pch.h"
#include "Api/Dto.h"
#include "Api/JsonNumberPolicy.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <stdexcept>
#include <string>
#include <string_view>

// The people, credits and lookups the server answers from TMDB. Everything
// here is untrusted: text is bounded and stripped of control characters,
// images are accepted only from TMDB's own image host over https, ids only
// in their exact shapes, and unknown fields are ignored. A bad entry is
// skipped rather than failing the whole answer.
namespace
{
    using winrt::Windows::Data::Json::IJsonValue;
    using winrt::Windows::Data::Json::JsonArray;
    using winrt::Windows::Data::Json::JsonObject;
    using winrt::Windows::Data::Json::JsonValueType;

    namespace Dto = ::HaloDesktop::Api::Dto;

    constexpr std::size_t MaximumName = 200;
    constexpr std::size_t MaximumBiography = 2000;
    constexpr std::size_t MaximumImageUrl = 300;
    constexpr std::size_t MaximumPeople = 8;
    constexpr std::size_t MaximumKnownForTitles = 3;
    constexpr std::size_t MaximumCredits = 60;
    constexpr std::int64_t MaximumSafeInteger = 9007199254740991;
    constexpr std::wstring_view TmdbImagePrefix = L"https://image.tmdb.org/t/p/";

    JsonObject RequireObject(IJsonValue const& value, char const* description)
    {
        if (!value || value.ValueType() != JsonValueType::Object)
        {
            throw std::invalid_argument{ description };
        }
        return value.GetObject();
    }

    std::optional<IJsonValue> Field(JsonObject const& object, wchar_t const* key, JsonValueType type)
    {
        if (!object.HasKey(key)) return std::nullopt;
        auto const value = object.GetNamedValue(key);
        if (value.ValueType() != type) return std::nullopt;
        return value;
    }

    bool IsSpace(wchar_t character) noexcept
    {
        return std::iswspace(character) != 0;
    }

    std::wstring Trimmed(std::wstring_view text)
    {
        auto const first = std::find_if_not(text.begin(), text.end(), IsSpace);
        auto const last = std::find_if_not(text.rbegin(), text.rend(), IsSpace).base();
        return first < last ? std::wstring{ first, last } : std::wstring{};
    }

    // Trimmed, control characters as spaces, and refused rather than cut when
    // longer than the bound: a clipped name would name someone else.
    std::optional<winrt::hstring> BoundedText(JsonObject const& object, wchar_t const* key, std::size_t maximum)
    {
        auto const value = Field(object, key, JsonValueType::String);
        if (!value) return std::nullopt;
        auto text = Trimmed(value->GetString());
        if (text.empty() || text.size() > maximum) return std::nullopt;
        std::replace_if(text.begin(), text.end(), [](wchar_t character) { return std::iswcntrl(character) != 0; }, L' ');
        return winrt::hstring{ text };
    }

    // Prose: line breaks kept, other control characters as spaces, cut (not
    // refused) at the bound, since a shortened biography still reads.
    std::optional<winrt::hstring> Prose(JsonObject const& object, wchar_t const* key, std::size_t maximum)
    {
        auto const value = Field(object, key, JsonValueType::String);
        if (!value) return std::nullopt;
        auto const raw = Trimmed(value->GetString());
        // A "\r\n" or a lone "\r" reads as one "\n".
        std::wstring text;
        text.reserve((std::min)(raw.size(), maximum));
        for (std::size_t index = 0; index < raw.size() && text.size() < maximum; ++index)
        {
            auto const character = raw[index];
            if (character == L'\r')
            {
                if (index + 1 < raw.size() && raw[index + 1] == L'\n') ++index;
                text.push_back(L'\n');
                continue;
            }
            text.push_back(character == L'\n' || std::iswcntrl(character) == 0 ? character : L' ');
        }
        // Never end on half of a surrogate pair.
        if (!text.empty() && text.back() >= 0xD800 && text.back() <= 0xDBFF) text.pop_back();
        text = Trimmed(text);
        if (text.empty()) return std::nullopt;
        return winrt::hstring{ text };
    }

    std::optional<std::int64_t> SafePositiveInteger(JsonObject const& object, wchar_t const* key)
    {
        auto const value = Field(object, key, JsonValueType::Number);
        if (!value) return std::nullopt;
        auto const number = ::HaloDesktop::Api::CheckedPositiveInt64(value->GetNumber());
        if (!number || *number > MaximumSafeInteger) return std::nullopt;
        return number;
    }

    double Popularity(JsonObject const& object)
    {
        auto const value = Field(object, L"popularity", JsonValueType::Number);
        if (!value) return 0.0;
        auto const number = value->GetNumber();
        return std::isfinite(number) && number > 0.0 ? number : 0.0;
    }

    bool Boolean(JsonObject const& object, wchar_t const* key)
    {
        auto const value = Field(object, key, JsonValueType::Boolean);
        return value && value->GetBoolean();
    }

    std::optional<winrt::hstring> TmdbImage(JsonObject const& object, wchar_t const* key)
    {
        auto const value = Field(object, key, JsonValueType::String);
        if (!value) return std::nullopt;
        auto const text = value->GetString();
        if (!::HaloDesktop::Api::Mappers::IsTmdbImageUrl(text)) return std::nullopt;
        return text;
    }

    bool AllDigits(std::wstring_view text) noexcept
    {
        return !text.empty() && std::all_of(text.begin(), text.end(), [](wchar_t character)
        {
            return character >= L'0' && character <= L'9';
        });
    }

    // `tt` and one to ten digits.
    std::optional<winrt::hstring> ImdbTitleId(JsonObject const& object, wchar_t const* key)
    {
        auto const value = Field(object, key, JsonValueType::String);
        if (!value) return std::nullopt;
        auto const text = value->GetString();
        std::wstring_view const view{ text };
        if (view.size() < 3 || view.size() > 12 || !view.starts_with(L"tt") || !AllDigits(view.substr(2))) return std::nullopt;
        return text;
    }

    std::optional<winrt::hstring> TitleType(JsonObject const& object)
    {
        auto const value = Field(object, L"type", JsonValueType::String);
        if (!value) return std::nullopt;
        auto const text = value->GetString();
        if (text != L"movie" && text != L"series") return std::nullopt;
        return text;
    }

    std::optional<winrt::hstring> Year(JsonObject const& object)
    {
        auto const value = Field(object, L"year", JsonValueType::String);
        if (!value) return std::nullopt;
        auto const text = value->GetString();
        if (text.size() != 4 || !AllDigits(text)) return std::nullopt;
        return text;
    }

    Dto::PersonDepartment Department(JsonObject const& object)
    {
        auto const value = Field(object, L"knownFor", JsonValueType::String);
        if (!value) return Dto::PersonDepartment::Unknown;
        auto const text = value->GetString();
        if (text == L"Acting") return Dto::PersonDepartment::Acting;
        if (text == L"Directing") return Dto::PersonDepartment::Directing;
        if (text == L"Writing") return Dto::PersonDepartment::Writing;
        return Dto::PersonDepartment::Unknown;
    }

    std::vector<Dto::CreditRole> Roles(JsonObject const& object)
    {
        std::vector<Dto::CreditRole> roles;
        auto const array = Field(object, L"roles", JsonValueType::Array);
        if (!array) return roles;
        for (auto const& item : array->GetArray())
        {
            if (roles.size() == 3) break;
            if (item.ValueType() != JsonValueType::String) continue;
            auto const text = item.GetString();
            std::optional<Dto::CreditRole> role;
            if (text == L"cast") role = Dto::CreditRole::Cast;
            else if (text == L"director") role = Dto::CreditRole::Director;
            else if (text == L"writer") role = Dto::CreditRole::Writer;
            if (role && std::find(roles.begin(), roles.end(), *role) == roles.end()) roles.push_back(*role);
        }
        return roles;
    }

    std::optional<Dto::PersonSummary> ParseSummary(IJsonValue const& item)
    {
        if (item.ValueType() != JsonValueType::Object) return std::nullopt;
        auto const object = item.GetObject();
        auto const tmdbId = SafePositiveInteger(object, L"tmdbId");
        auto const name = BoundedText(object, L"name", MaximumName);
        if (!tmdbId || !name) return std::nullopt;
        Dto::PersonSummary person{
            .TmdbId = *tmdbId,
            .Name = *name,
            .KnownFor = Department(object),
            .Photo = TmdbImage(object, L"photo"),
            .Popularity = Popularity(object),
        };
        if (auto const titles = Field(object, L"knownForTitles", JsonValueType::Array))
        {
            for (auto const& title : titles->GetArray())
            {
                if (person.KnownForTitles.size() == MaximumKnownForTitles) break;
                if (title.ValueType() != JsonValueType::String) continue;
                auto text = Trimmed(title.GetString());
                if (text.empty() || text.size() > MaximumName) continue;
                std::replace_if(text.begin(), text.end(), [](wchar_t character) { return std::iswcntrl(character) != 0; }, L' ');
                person.KnownForTitles.emplace_back(text);
            }
        }
        return person;
    }

    std::optional<Dto::PersonCredit> ParseCredit(IJsonValue const& item)
    {
        if (item.ValueType() != JsonValueType::Object) return std::nullopt;
        auto const object = item.GetObject();
        auto const type = TitleType(object);
        auto const imdbId = ImdbTitleId(object, L"imdbId");
        auto const name = BoundedText(object, L"name", MaximumName);
        auto roles = Roles(object);
        // Without an IMDb id it could be neither opened nor played; without a
        // role it would not belong on the page.
        if (!type || !imdbId || !name || roles.empty()) return std::nullopt;
        return Dto::PersonCredit{
            .Type = *type,
            .ImdbId = *imdbId,
            .TmdbId = SafePositiveInteger(object, L"tmdbId").value_or(0),
            .Name = *name,
            .Year = Year(object),
            .Poster = TmdbImage(object, L"poster"),
            .Roles = std::move(roles),
            .Character = BoundedText(object, L"character", MaximumName),
            .Popularity = Popularity(object),
        };
    }
}

namespace HaloDesktop::Api::Mappers
{
    bool IsTmdbImageUrl(std::wstring_view url) noexcept
    {
        if (url.size() > MaximumImageUrl || !url.starts_with(TmdbImagePrefix)) return false;
        auto const path = url.substr(TmdbImagePrefix.size());
        if (path.empty() || path.find(L"..") != std::wstring_view::npos) return false;
        return std::all_of(path.begin(), path.end(), [](wchar_t character)
        {
            return (character >= L'a' && character <= L'z') || (character >= L'A' && character <= L'Z')
                || (character >= L'0' && character <= L'9') || character == L'/' || character == L'_'
                || character == L'-' || character == L'.';
        });
    }

    Dto::Capabilities ParseCapabilities(IJsonValue const& value)
    {
        auto const object = RequireObject(value, "The capabilities response must be an object.");
        Dto::Capabilities capabilities{ .People = Boolean(object, L"people"), .Lookup = Boolean(object, L"lookup") };
        // An older server has no `magnets` at all, which reads as off.
        if (auto const magnets = Field(object, L"magnets", JsonValueType::Object))
        {
            auto const settings = magnets->GetObject();
            capabilities.Magnets = Boolean(settings, L"enabled");
            // Only an explicit false says uncached torrents may be fetched.
            auto const cachedOnly = Field(settings, L"cachedOnly", JsonValueType::Boolean);
            capabilities.MagnetsCachedOnly = !cachedOnly || cachedOnly->GetBoolean();
        }
        return capabilities;
    }

    std::vector<Dto::PersonSummary> ParsePeopleSearch(IJsonValue const& value)
    {
        auto const object = RequireObject(value, "The people search response must be an object.");
        std::vector<Dto::PersonSummary> people;
        auto const array = Field(object, L"people", JsonValueType::Array);
        if (!array) return people;
        for (auto const& item : array->GetArray())
        {
            if (people.size() == MaximumPeople) break;
            auto person = ParseSummary(item);
            if (!person) continue;
            // One card per person, should the server ever repeat one.
            auto const repeated = std::any_of(people.begin(), people.end(), [&person](Dto::PersonSummary const& other)
            {
                return other.TmdbId == person->TmdbId;
            });
            if (!repeated) people.push_back(std::move(*person));
        }
        return people;
    }

    Dto::PersonDetail ParsePerson(IJsonValue const& value)
    {
        auto const root = RequireObject(value, "The person response must be an object.");
        auto const personValue = Field(root, L"person", JsonValueType::Object);
        if (!personValue)
        {
            throw std::invalid_argument{ "The person response has no person." };
        }
        auto const person = personValue->GetObject();
        auto const tmdbId = SafePositiveInteger(person, L"tmdbId");
        auto const name = BoundedText(person, L"name", MaximumName);
        if (!tmdbId || !name)
        {
            throw std::invalid_argument{ "The person response names nobody." };
        }
        Dto::PersonDetail detail{
            .Person = {
                .TmdbId = *tmdbId,
                .Name = *name,
                .Photo = TmdbImage(person, L"photo"),
                .KnownFor = Department(person),
                .Biography = Prose(person, L"biography", MaximumBiography),
            },
            .Partial = Boolean(root, L"partial"),
        };
        if (auto const credits = Field(root, L"credits", JsonValueType::Array))
        {
            for (auto const& item : credits->GetArray())
            {
                if (detail.Credits.size() == MaximumCredits) break;
                auto credit = ParseCredit(item);
                if (!credit) continue;
                auto const repeated = std::any_of(detail.Credits.begin(), detail.Credits.end(), [&credit](Dto::PersonCredit const& other)
                {
                    return other.Type == credit->Type && other.ImdbId == credit->ImdbId;
                });
                if (!repeated) detail.Credits.push_back(std::move(*credit));
            }
        }
        return detail;
    }

    Dto::IdLookup ParseIdLookup(IJsonValue const& value)
    {
        auto const object = RequireObject(value, "The lookup response must be an object.");
        auto const kind = Field(object, L"kind", JsonValueType::String);
        auto const name = BoundedText(object, L"name", MaximumName);
        if (!kind || !name)
        {
            throw std::invalid_argument{ "The lookup response names nothing." };
        }
        if (kind->GetString() == L"person")
        {
            auto const tmdbId = SafePositiveInteger(object, L"tmdbId");
            if (!tmdbId)
            {
                throw std::invalid_argument{ "The lookup response names nobody." };
            }
            return {
                .Kind = Dto::IdLookupKind::Person,
                .Name = *name,
                .TmdbId = *tmdbId,
                .Photo = TmdbImage(object, L"photo"),
            };
        }
        if (kind->GetString() != L"title")
        {
            throw std::invalid_argument{ "The lookup response is of an unknown kind." };
        }
        auto const type = TitleType(object);
        auto const id = ImdbTitleId(object, L"id");
        if (!type || !id)
        {
            throw std::invalid_argument{ "The lookup response names no title." };
        }
        return {
            .Kind = Dto::IdLookupKind::Title,
            .Name = *name,
            .Type = *type,
            .Id = *id,
            .Year = Year(object),
            .Poster = TmdbImage(object, L"poster"),
        };
    }
}
