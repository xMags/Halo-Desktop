#include "Services/SearchRankingPolicy.h"

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <utility>

namespace
{
    using HaloDesktop::Services::RankedTitle;
    using HaloDesktop::Services::SearchQueryParts;

    // NameRelevance's levels these rules read: some words of the query, none.
    constexpr int SomeWordsTier = 5;
    constexpr int UnrelatedTier = 6;
    // A name that is exactly the query.
    constexpr int ExactNameTier = 0;
    // Titles holding every word of the query that make the ones holding only some not worth showing.
    constexpr std::size_t EnoughNamed = 8;
    // Titles with a year and artwork that make the ones without artwork not worth showing.
    constexpr std::size_t EnoughComplete = 8;
    // The first film was shot in 1888; a year beyond the next two is part of a name.
    constexpr int EarliestYear = 1888;
    constexpr int YearsAhead = 2;

    bool IsAsciiDigit(wchar_t unit) noexcept { return unit >= L'0' && unit <= L'9'; }

    std::optional<int> YearOf(std::optional<std::wstring> const& releaseInfo)
    {
        auto const year = HaloDesktop::Services::ReleaseYear(releaseInfo);
        if (!year) return std::nullopt;
        int value = 0;
        for (auto const unit : *year) value = value * 10 + (unit - L'0');
        return value;
    }

    // Fills what is read from the title itself, after its copies are merged.
    void Score(RankedTitle& ranked, SearchQueryParts const& query)
    {
        auto const& name = ranked.Title.Name;
        auto tier = HaloDesktop::Services::NameRelevance(name, query.Full);
        if (query.Name != query.Full)
        {
            tier = (std::min)(tier, HaloDesktop::Services::NameRelevance(name, query.Name));
        }
        ranked.Tier = tier;
        ranked.YearMatches = query.Year.has_value() && YearOf(ranked.Title.ReleaseInfo) == query.Year;
        ranked.Complete = HaloDesktop::Services::ReleaseYear(ranked.Title.ReleaseInfo).has_value()
            && !HaloDesktop::Services::IsArtless(ranked.Title);
    }
}

namespace HaloDesktop::Services
{
    SearchQueryParts ParseSearchQuery(std::wstring_view query, int currentYear)
    {
        SearchQueryParts parts;
        parts.Full = TrimmedText(query);
        parts.Name = parts.Full;
        auto const words = WhitespaceWords(parts.Full);
        if (words.size() < 2) return parts;
        auto const& last = words.back();
        if (last.size() != 4 || !std::all_of(last.begin(), last.end(), IsAsciiDigit)) return parts;
        int year = 0;
        for (auto const unit : last) year = year * 10 + (unit - L'0');
        if (year < EarliestYear || year > currentYear + YearsAhead) return parts;
        // The last word is the trimmed query's last four units.
        auto name = TrimmedText(std::wstring_view{ parts.Full }.substr(0, parts.Full.size() - last.size()));
        if (name.empty()) return parts;
        parts.Name = std::move(name);
        parts.Year = year;
        return parts;
    }

    SearchQueryParts ParseSearchQuery(std::wstring_view query)
    {
        auto const today = std::chrono::year_month_day{ std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now()) };
        return ParseSearchQuery(query, static_cast<int>(today.year()));
    }

    bool IsArtless(SearchTitle const& title)
    {
        return HasNoPoster(title) && !IsImdbId(title.Id);
    }

    RankedTitle RankOne(
        SearchTitle title,
        SearchQueryParts const& query,
        std::size_t position,
        std::size_t addonRank)
    {
        RankedTitle ranked;
        ranked.Identities = TitleIdentities(title);
        ranked.Title = std::move(title);
        ranked.Copies = 1;
        ranked.BestPosition = position;
        ranked.AddonRank = addonRank;
        Score(ranked, query);
        return ranked;
    }

    RevealCutoffs ComputeCutoffs(std::span<RankedTitle const> ranked, SearchQueryMeaning meaning)
    {
        RevealCutoffs cutoffs;
        auto const person = meaning == SearchQueryMeaning::Person
            && std::none_of(ranked.begin(), ranked.end(), [](RankedTitle const& title) { return title.Tier == ExactNameTier; });
        cutoffs.DropUnrelated = std::any_of(ranked.begin(), ranked.end(), [](RankedTitle const& title)
        {
            return title.Tier < UnrelatedTier;
        });
        cutoffs.DropSomeWords = static_cast<std::size_t>(std::count_if(ranked.begin(), ranked.end(), [](RankedTitle const& title)
        {
            return title.Tier < SomeWordsTier;
        })) >= EnoughNamed || person;
        cutoffs.KeepCredited = person;
        // Counted among what the name rules keep, as the screen would list it.
        RevealCutoffs const byName{ cutoffs.DropUnrelated, cutoffs.DropSomeWords, false, cutoffs.KeepCredited };
        cutoffs.DropArtless = static_cast<std::size_t>(std::count_if(ranked.begin(), ranked.end(), [&byName](RankedTitle const& title)
        {
            return title.Complete && PassesCutoffs(title, byName);
        })) >= EnoughComplete;
        return cutoffs;
    }

    bool PassesCutoffs(RankedTitle const& title, RevealCutoffs const& cutoffs)
    {
        if (cutoffs.DropUnrelated && title.Tier >= UnrelatedTier && !(cutoffs.KeepCredited && title.Complete)) return false;
        if (cutoffs.DropSomeWords && title.Tier == SomeWordsTier) return false;
        if (!IsArtless(title.Title)) return true;
        auto const undated = !ReleaseYear(title.Title.ReleaseInfo).has_value();
        return !undated && !cutoffs.DropArtless;
    }

    std::vector<RankedTitle> RankForReveal(
        std::span<SearchAnswer const> answers,
        SearchQueryParts const& query)
    {
        // Answers by addon rank, the order they were passed in breaking a tie
        // (two catalogs of one addon).
        std::vector<std::size_t> order(answers.size());
        for (std::size_t index = 0; index < order.size(); ++index) order[index] = index;
        std::stable_sort(order.begin(), order.end(), [&answers](std::size_t a, std::size_t b)
        {
            return answers[a].AddonRank < answers[b].AddonRank;
        });
        std::size_t longest = 0;
        for (auto const& answer : answers) longest = (std::max)(longest, answer.Titles.size());

        std::vector<RankedTitle> merged;
        // Which answers listed each merged title, to count copies once per catalog.
        std::vector<std::vector<std::size_t>> listedBy;
        std::unordered_map<std::wstring, std::size_t> placed;
        auto const claim = [&placed, &merged](std::size_t index, std::vector<std::wstring> const& identities)
        {
            for (auto const& identity : identities)
            {
                if (placed.try_emplace(identity, index).second) merged[index].Identities.push_back(identity);
            }
        };

        // In turns: the copy at the better position,
        // then of the better addon, leads.
        for (std::size_t position = 0; position < longest; ++position)
        {
            for (auto const answerIndex : order)
            {
                auto const& answer = answers[answerIndex];
                if (position >= answer.Titles.size()) continue;
                if (IsProductCodeName(answer.Titles[position].Name)) continue;
                auto title = WithYearOutOfName(answer.Titles[position]);
                auto const identities = TitleIdentities(title);
                std::optional<std::size_t> kept;
                for (auto const& identity : identities)
                {
                    if (auto const found = placed.find(identity); found != placed.end())
                    {
                        kept = found->second;
                        break;
                    }
                }
                if (!kept)
                {
                    auto const index = merged.size();
                    merged.push_back(RankOne(std::move(title), query, position, answer.AddonRank));
                    merged[index].Identities.clear();
                    listedBy.push_back({ answerIndex });
                    claim(index, identities);
                    continue;
                }
                auto& entry = merged[*kept];
                entry.Title = MergeCopies(std::move(entry.Title), title);
                auto& listers = listedBy[*kept];
                if (std::find(listers.begin(), listers.end(), answerIndex) == listers.end())
                {
                    listers.push_back(answerIndex);
                    ++entry.Copies;
                }
                entry.BestPosition = (std::min)(entry.BestPosition, position);
                entry.AddonRank = (std::min)(entry.AddonRank, answer.AddonRank);
                claim(*kept, identities);
                // A copy that brought a year makes the merged title known by its name and year too.
                claim(*kept, TitleIdentities(merged[*kept].Title));
            }
        }

        for (auto& entry : merged) Score(entry, query);
        auto const hasYear = query.Year.has_value();
        std::stable_sort(merged.begin(), merged.end(), [hasYear](RankedTitle const& a, RankedTitle const& b)
        {
            if (a.Tier != b.Tier) return a.Tier < b.Tier;
            if (hasYear && a.YearMatches != b.YearMatches) return a.YearMatches;
            if (a.Complete != b.Complete) return a.Complete;
            if (a.Copies != b.Copies) return a.Copies > b.Copies;
            if (a.BestPosition != b.BestPosition) return a.BestPosition < b.BestPosition;
            return a.AddonRank < b.AddonRank;
        });
        return merged;
    }

    std::optional<std::size_t> ChooseRevealTopMatch(
        std::span<RankedTitle const> ranked,
        SearchQueryParts const& query)
    {
        if (ranked.empty()) return std::nullopt;
        auto const& first = ranked.front();
        if (first.Tier > 1) return std::nullopt;
        if (query.Year && !first.YearMatches) return std::nullopt;
        return std::size_t{ 0 };
    }
}
