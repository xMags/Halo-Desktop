#include "Services/Downloads/DownloadAttentionPolicy.h"

#include <utility>

namespace HaloDesktop::Services::Downloads
{
    namespace
    {
        struct Tally final
        {
            std::size_t Count{};
            std::uint64_t Completed{};
            std::uint64_t Total{};
            bool UnknownSize{};

            void Add(DownloadRecord const& record) noexcept
            {
                ++Count;
                if (record.TotalBytes == 0)
                {
                    UnknownSize = true;
                    return;
                }
                Total += record.TotalBytes;
                Completed += record.DownloadedBytes < record.TotalBytes ? record.DownloadedBytes : record.TotalBytes;
            }
        };

        TaskbarProgress FromTally(Tally const& tally, TaskbarProgressState state) noexcept
        {
            if (tally.UnknownSize || tally.Total == 0)
            {
                // A paused or failed bar with no value would read as nothing;
                // it shows full in its colour instead.
                if (state != TaskbarProgressState::Normal) return { state, 1, 1 };
                return { TaskbarProgressState::Indeterminate, 0, 0 };
            }
            return { state, tally.Completed, tally.Total };
        }
    }

    TaskbarProgress SummarizeTaskbarProgress(std::span<DownloadRecord const> records)
    {
        Tally running;
        Tally paused;
        Tally failed;
        for (auto const& record : records)
        {
            if (record.PendingDeletion) continue;
            switch (record.Status)
            {
            case DownloadStatus::Queued:
            case DownloadStatus::Downloading:
                running.Add(record);
                break;
            case DownloadStatus::Paused:
                paused.Add(record);
                break;
            case DownloadStatus::Failed:
                failed.Add(record);
                break;
            case DownloadStatus::Done:
                break;
            }
        }
        if (running.Count > 0)
        {
            // The bar covers everything still to come, paused work included,
            // so pausing one of two does not make the bar jump ahead.
            Tally left = running;
            left.Count += paused.Count;
            left.Completed += paused.Completed;
            left.Total += paused.Total;
            left.UnknownSize = left.UnknownSize || paused.UnknownSize;
            return FromTally(left, TaskbarProgressState::Normal);
        }
        if (paused.Count > 0) return FromTally(paused, TaskbarProgressState::Paused);
        if (failed.Count > 0) return FromTally(failed, TaskbarProgressState::Error);
        return {};
    }

    namespace
    {
        std::wstring EscapeXml(std::wstring_view text)
        {
            std::wstring escaped;
            escaped.reserve(text.size());
            for (auto const character : text)
            {
                switch (character)
                {
                case L'&': escaped += L"&amp;"; break;
                case L'<': escaped += L"&lt;"; break;
                case L'>': escaped += L"&gt;"; break;
                case L'"': escaped += L"&quot;"; break;
                case static_cast<wchar_t>(0x27): escaped += L"&apos;"; break;
                default:
                    // Control characters other than tab are not allowed in XML.
                    if (character >= 0x20 || character == static_cast<wchar_t>(0x09)) escaped.push_back(character);
                    break;
                }
            }
            return escaped;
        }

        bool Unreserved(wchar_t character) noexcept
        {
            return (character >= L'A' && character <= L'Z') || (character >= L'a' && character <= L'z')
                || (character >= L'0' && character <= L'9') || character == L'-' || character == L'_'
                || character == L'.' || character == L'~';
        }

        // Each UTF-16 unit as %XXXX: job ids are ASCII, and this only has to
        // round-trip through ParseToastArguments.
        std::wstring PercentEncode(std::wstring_view text)
        {
            constexpr wchar_t Hex[] = L"0123456789ABCDEF";
            std::wstring encoded;
            for (auto const character : text)
            {
                if (Unreserved(character))
                {
                    encoded.push_back(character);
                    continue;
                }
                auto const value = static_cast<unsigned>(character);
                encoded.push_back(L'%');
                for (int shift = 12; shift >= 0; shift -= 4) encoded.push_back(Hex[(value >> shift) & 0xF]);
            }
            return encoded;
        }

        std::optional<std::wstring> PercentDecode(std::wstring_view text)
        {
            std::wstring decoded;
            for (std::size_t index = 0; index < text.size(); ++index)
            {
                if (text[index] != L'%')
                {
                    if (!Unreserved(text[index])) return std::nullopt;
                    decoded.push_back(text[index]);
                    continue;
                }
                if (index + 4 >= text.size()) return std::nullopt;
                unsigned value = 0;
                for (std::size_t digit = 1; digit <= 4; ++digit)
                {
                    auto const character = text[index + digit];
                    value <<= 4;
                    if (character >= L'0' && character <= L'9') value |= static_cast<unsigned>(character - L'0');
                    else if (character >= L'A' && character <= L'F') value |= static_cast<unsigned>(character - L'A' + 10);
                    else return std::nullopt;
                }
                decoded.push_back(static_cast<wchar_t>(value));
                index += 4;
            }
            return decoded;
        }

        std::wstring Arguments(wchar_t const* action, std::wstring_view jobId)
        {
            return std::wstring{ L"action=" } + action + L"&job=" + PercentEncode(jobId);
        }
    }

    std::wstring ReadyToWatchToastXml(std::wstring_view title, std::wstring_view jobId)
    {
        auto const shown = title.empty() ? std::wstring_view{ L"A download has finished." } : title;
        return L"<toast launch=\"" + EscapeXml(Arguments(L"open", jobId)) + L"\">"
            L"<visual><binding template=\"ToastGeneric\">"
            L"<text>Ready to watch</text>"
            L"<text>" + EscapeXml(shown) + L"</text>"
            L"</binding></visual>"
            L"<actions><action content=\"Play\" activationType=\"foreground\" arguments=\""
            + EscapeXml(Arguments(L"play", jobId)) + L"\" /></actions>"
            L"</toast>";
    }

    std::optional<ToastAnswer> ParseToastArguments(std::wstring_view arguments)
    {
        constexpr std::wstring_view PlayPrefix = L"action=play&job=";
        constexpr std::wstring_view OpenPrefix = L"action=open&job=";
        ToastAnswer answer;
        std::wstring_view encoded;
        if (arguments.starts_with(PlayPrefix))
        {
            answer.Play = true;
            encoded = arguments.substr(PlayPrefix.size());
        }
        else if (arguments.starts_with(OpenPrefix))
        {
            encoded = arguments.substr(OpenPrefix.size());
        }
        else
        {
            return std::nullopt;
        }
        auto job = PercentDecode(encoded);
        if (!job) return std::nullopt;
        answer.JobId = std::move(*job);
        return answer;
    }

    std::vector<std::wstring> TakeNewlyFinished(
        std::span<DownloadRecord const> records,
        std::set<std::wstring, std::less<>>& unfinished)
    {
        std::vector<std::wstring> finished;
        std::set<std::wstring, std::less<>> stillUnfinished;
        for (auto const& record : records)
        {
            if (record.PendingDeletion) continue;
            if (record.Status == DownloadStatus::Done)
            {
                if (unfinished.contains(record.JobId)) finished.push_back(record.JobId);
            }
            else
            {
                stillUnfinished.insert(record.JobId);
            }
        }
        unfinished = std::move(stillUnfinished);
        return finished;
    }
}
