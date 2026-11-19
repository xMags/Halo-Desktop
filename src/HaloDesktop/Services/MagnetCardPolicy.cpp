#include "pch.h"
#include "Services/MagnetCardPolicy.h"

#include "Services/MagnetSourcePolicy.h"
#include "Services/StreamInfo.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <string_view>

namespace
{
    namespace Dto = ::HaloDesktop::Api::Dto;
    using ::HaloDesktop::Services::MagnetCard;
    using ::HaloDesktop::Services::MagnetCardAction;
    using ::HaloDesktop::Services::MagnetCardTone;

    // The words of the approved mockup, except where marked as Halo's own.
    constexpr wchar_t NotAvailableTitle[] = L"Magnet links aren't available";
    constexpr wchar_t NotAvailableText[] = L"This server doesn't open magnet links for your account.";
    constexpr wchar_t CheckingTitle[] = L"Checking this torrent…";
    constexpr wchar_t CheckingText[] = L"Looking for a video file.";
    constexpr wchar_t ErrorTitle[] = L"Couldn't check this torrent";
    constexpr wchar_t ErrorText[] = L"The server did not answer. Try again in a moment.";
    constexpr wchar_t RetryLabel[] = L"Try again";
    constexpr wchar_t NotCachedTitle[] = L"This torrent isn't ready";
    constexpr wchar_t NotCachedText[] = L"The server's debrid account doesn't have it yet. Try another magnet for the same title.";
    constexpr wchar_t DownloadingTitle[] = L"Getting this torrent ready…";
    constexpr wchar_t DownloadingText[] = L"You can leave this page; paste the magnet again to check on it.";
    constexpr wchar_t CancelLabel[] = L"Cancel";
    constexpr wchar_t NoVideoTitle[] = L"No video in this torrent";
    constexpr wchar_t NoVideoText[] = L"Halo only plays video files. Nothing was added.";
    constexpr wchar_t TooLargeTitle[] = L"This torrent is too large";
    constexpr wchar_t TooLargeText[] = L"It is bigger than the 100 GB the server accepts.";
    constexpr wchar_t UnidentifiedTitle[] = L"Unidentified video";
    constexpr wchar_t UnidentifiedText[] = L"Halo could not tell which film or series this is. It won't be added to your history.";
    constexpr wchar_t PlayUnidentifiedLabel[] = L"Play as unidentified video";
    constexpr wchar_t RefusedTitle[] = L"Can't open this magnet";
    constexpr wchar_t RefusedText[] = L"Halo could not tell which film or series this is.";
    constexpr wchar_t SeriesText[] = L"Open the title to pick an episode.";
    constexpr wchar_t OpenTitleLabel[] = L"Open title";
    // Halo's own words: the mockup has no state for a cancelled download.
    constexpr wchar_t StoppedTitle[] = L"Stopped getting this torrent ready";
    constexpr wchar_t StoppedText[] = L"Nothing more is fetched for it.";
    constexpr wchar_t StartAgainLabel[] = L"Start again";

    std::wstring SizeText(std::uint64_t bytes)
    {
        return std::wstring{ ::HaloDesktop::Services::FormatStreamSize(bytes > 0 ? std::optional<std::uint64_t>{ bytes } : std::nullopt) };
    }

    MagnetCard Note(MagnetCardTone tone, wchar_t const* title, wchar_t const* text)
    {
        MagnetCard card;
        card.Tone = tone;
        card.Title = title;
        card.Secondary = text;
        return card;
    }

    MagnetCard Error()
    {
        auto card = Note(MagnetCardTone::Bad, ErrorTitle, ErrorText);
        card.Action = MagnetCardAction::Retry;
        card.ActionLabel = RetryLabel;
        return card;
    }

    std::wstring Shown(std::wstring_view name, std::wstring_view fallback)
    {
        return std::wstring{ name.empty() ? fallback : name };
    }

    MagnetCard Ready(Dto::MagnetRequest const& request)
    {
        auto const largest = ::HaloDesktop::Services::LargestMagnetFile(request.Files);
        // A ready answer always lists files (ParseMagnet refuses one without).
        if (!largest) return Error();
        auto const& file = request.Files[*largest];

        if (!request.Identified)
        {
            MagnetCard card;
            card.Tone = MagnetCardTone::Warn;
            card.Title = UnidentifiedTitle;
            card.Mono = std::wstring{ file.Name };
            card.Tail = SizeText(file.SizeBytes);
            card.Secondary = UnidentifiedText;
            card.Action = MagnetCardAction::PlayUnidentified;
            card.ActionLabel = PlayUnidentifiedLabel;
            card.UnidentifiedFile = file;
            return card;
        }

        auto const& identity = *request.Identified;
        MagnetCard card;
        card.ShowsPoster = true;
        card.Title = std::wstring{ identity.Name };
        card.Identity = identity;
        card.Action = MagnetCardAction::OpenTitle;
        card.ActionLabel = OpenTitleLabel;
        if (identity.Type != L"series")
        {
            card.Mono = L"MOVIE";
            card.Plain = std::wstring{ identity.Year.value_or(winrt::hstring{}) };
            card.Tail = SizeText(file.SizeBytes);
            card.Secondary = std::wstring{ file.Name };
            return card;
        }

        // Only files with a season and an episode can be offered, so only
        // those are counted.
        std::set<std::int32_t> seasons;
        std::size_t episodes{};
        std::uint64_t bytes{};
        for (auto const& candidate : request.Files)
        {
            if (!candidate.Season || !candidate.Episode) continue;
            seasons.insert(*candidate.Season);
            ++episodes;
            bytes += candidate.SizeBytes;
        }
        card.Mono = L"SERIES";
        if (seasons.size() == 1)
        {
            card.Plain = L"Season " + std::to_wstring(*seasons.begin());
        }
        else if (seasons.size() > 1)
        {
            card.Plain = std::to_wstring(seasons.size()) + L" seasons";
        }
        card.Tail = std::to_wstring(episodes) + (episodes == 1 ? L" episode" : L" episodes") + L" \x00B7 "
            + SizeText(episodes > 0 ? bytes : request.SizeBytes);
        card.Secondary = SeriesText;
        return card;
    }

    MagnetCard Answered(Dto::MagnetRequest const& request, std::wstring_view linkName)
    {
        switch (request.State)
        {
        case Dto::MagnetState::Ready: return Ready(request);
        case Dto::MagnetState::NotCached: return Note(MagnetCardTone::Warn, NotCachedTitle, NotCachedText);
        case Dto::MagnetState::NoVideo: return Note(MagnetCardTone::Bad, NoVideoTitle, NoVideoText);
        case Dto::MagnetState::TooLarge: return Note(MagnetCardTone::Bad, TooLargeTitle, TooLargeText);
        case Dto::MagnetState::Downloading:
        {
            MagnetCard card;
            card.Tone = MagnetCardTone::Accent;
            card.Title = DownloadingTitle;
            card.Mono = Shown(std::wstring_view{ request.Name }, linkName);
            auto const fraction = std::isfinite(request.Progress) ? std::clamp(request.Progress, 0.0, 1.0) : 0.0;
            card.Progress = fraction;
            card.Tail = std::to_wstring(static_cast<int>(std::floor(fraction * 100.0))) + L"%";
            card.Secondary = DownloadingText;
            card.Action = MagnetCardAction::Cancel;
            card.ActionLabel = CancelLabel;
            return card;
        }
        case Dto::MagnetState::Failed:
            if (request.Reason && *request.Reason == L"unidentified") return Note(MagnetCardTone::Bad, RefusedTitle, RefusedText);
            // Another failure (the debrid service refused, the torrent went
            // away) is tried again by asking again.
            return Error();
        }
        return Error();
    }
}

namespace HaloDesktop::Services
{
    MagnetCard BuildMagnetCard(MagnetCardInput const& input)
    {
        switch (input.Phase)
        {
        case MagnetPhase::Unavailable: return Note(MagnetCardTone::Mute, NotAvailableTitle, NotAvailableText);
        case MagnetPhase::Checking:
        {
            auto card = Note(MagnetCardTone::Accent, CheckingTitle, CheckingText);
            card.Mono = input.LinkName;
            return card;
        }
        case MagnetPhase::Failed: return Error();
        case MagnetPhase::Stopped:
        {
            auto card = Note(MagnetCardTone::Mute, StoppedTitle, StoppedText);
            card.Action = MagnetCardAction::Retry;
            card.ActionLabel = StartAgainLabel;
            return card;
        }
        case MagnetPhase::Answered:
            if (!input.Request) return Error();
            return Answered(*input.Request, input.LinkName);
        }
        return Error();
    }
}
