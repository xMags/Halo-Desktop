#include "WatchOutboxTests.h"

#include "Api/Dto.h"
#include "Services/WatchOutbox.h"
#include "Services/WatchOutboxPolicy.h"
#include "Storage/FileStorage.h"

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
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

    using HaloDesktop::Api::Dto::WatchEntry;
    using HaloDesktop::Services::MergePendingWatchRows;
    using HaloDesktop::Services::WatchOutbox;
    using HaloDesktop::Services::WithoutSettledWatchRows;

    WatchEntry Row(wchar_t const* videoId, double position, std::int64_t updatedAt)
    {
        WatchEntry row;
        row.VideoId = videoId;
        row.ItemId = L"tt1";
        row.PositionSec = position;
        row.DurationSec = 3000;
        row.Watched = false;
        row.UpdatedAt = updatedAt;
        return row;
    }

    // A folder of its own under the temp directory, removed whatever happens.
    class ScratchFolder final
    {
    public:
        // Numbered, not timed: two folders made within one clock tick would share a
        // name, and a write still queued for the first would land in the second.
        ScratchFolder()
            : m_path(std::filesystem::temp_directory_path()
                / (L"halo-watch-outbox-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++s_count)))
        {
            std::filesystem::create_directories(m_path);
        }
        ~ScratchFolder()
        {
            std::error_code ignored;
            std::filesystem::remove_all(m_path, ignored);
        }
        ScratchFolder(ScratchFolder const&) = delete;
        ScratchFolder& operator=(ScratchFolder const&) = delete;

        [[nodiscard]] std::filesystem::path const& Path() const noexcept { return m_path; }
        [[nodiscard]] std::filesystem::path File() const { return m_path / L"watch-outbox.json"; }

    private:
        static inline std::uint64_t s_count{};
        std::filesystem::path m_path;
    };

    // Binds and reads the way the watch-state service does, waiting for the read.
    void BindAndLoad(WatchOutbox& outbox, std::wstring const& accountKey)
    {
        if (outbox.Bind(accountKey))
        {
            auto const epoch = outbox.Epoch();
            outbox.Adopt(epoch, outbox.ReadStoredAsync(accountKey).get());
        }
        // The adoption rewrites the file; a test must not leave that write queued
        // behind it for a folder that is about to go.
        outbox.FlushAsync().get();
    }

    void TestMergeKeepsNewestPerVideo()
    {
        // "staging keeps one row per video, the newest"
        auto const pending = MergePendingWatchRows(
            { Row(L"a", 10, 100), Row(L"b", 20, 100) },
            { Row(L"a", 30, 200), Row(L"b", 5, 50) });
        Require(pending.size() == 2 && pending[0].VideoId == L"b" && pending[0].PositionSec == 20
                && pending[1].VideoId == L"a" && pending[1].PositionSec == 30,
            "staging did not keep one row per video, the newest, oldest first");
        // A tie keeps what was there.
        auto const tie = MergePendingWatchRows({ Row(L"a", 10, 100) }, { Row(L"a", 99, 100) });
        Require(tie.size() == 1 && tie[0].PositionSec == 10, "a tie replaced the row already staged");
    }

    void TestSettleKeepsRowsStagedAgain()
    {
        // "a sent row is removed, and one staged again since is kept"
        auto const remaining = WithoutSettledWatchRows(
            { Row(L"a", 12, 150), Row(L"b", 20, 100), Row(L"c", 1, 90) },
            { Row(L"a", 10, 100), Row(L"b", 20, 100) });
        Require(remaining.size() == 2 && remaining[0].VideoId == L"a" && remaining[1].VideoId == L"c",
            "a settle dropped a row staged again since, or kept a sent one");
    }

    void TestOutboxIsBounded()
    {
        // "the outbox is bounded, dropping the oldest rows first"
        std::vector<WatchEntry> many;
        for (int index = 0; index < 250; ++index)
        {
            many.push_back(Row((L"v" + std::to_wstring(index)).c_str(), 10, index + 1));
        }
        auto const pending = MergePendingWatchRows({}, many);
        Require(pending.size() == 200 && pending.front().VideoId == L"v50", "the outbox was not bounded oldest first");
    }

    void TestAlreadyKnownRowsAreNotSent()
    {
        auto known = Row(L"a", 40, 300);
        auto const split = HaloDesktop::Services::SplitPendingWatchRows(
            { Row(L"a", 30, 200), Row(L"b", 10, 100), Row(L"c", 5, 500) },
            { known, Row(L"c", 1, 400) });
        Require(split.AlreadyKnown.size() == 1 && split.AlreadyKnown[0].VideoId == L"a",
            "a row older than the server's own was going to be sent");
        Require(split.ToSend.size() == 2 && split.ToSend[0].VideoId == L"b" && split.ToSend[1].VideoId == L"c",
            "a row newer than the server's, or one it does not have, was not sent");
    }

    void TestFileRoundTrip()
    {
        ScratchFolder folder;
        {
            WatchOutbox outbox{ folder.Path() };
            BindAndLoad(outbox, L"account-a");
            auto first = Row(L"a", 61.5, 1'700'000'000'000);
            first.Name = L"Show";
            first.Poster = L"https://img.test/p.jpg";
            outbox.Stage({ first, Row(L"b", 70, 1'700'000'000'500) });
            outbox.FlushAsync().get();
        }
        WatchOutbox reopened{ folder.Path() };
        BindAndLoad(reopened, L"account-a");
        auto const rows = reopened.Pending();
        Require(rows.size() == 2 && rows[0].VideoId == L"a" && rows[0].PositionSec == 61.5
                && rows[0].Name.value_or(L"") == L"Show" && rows[0].Poster.value_or(L"") == L"https://img.test/p.jpg"
                && rows[0].UpdatedAt == 1'700'000'000'000 && rows[1].VideoId == L"b",
            "staged rows did not survive a restart");

        // A launch that starts signed out leaves the last run's rows alone.
        WatchOutbox signedOut{ folder.Path() };
        Require(!signedOut.Bind(L""), "binding nobody asked for a read");
        signedOut.FlushAsync().get();
        Require(std::filesystem::exists(folder.File()), "starting signed out removed the last run's rows");
    }

    void TestCorruptAndOversizedFilesAreReplaced()
    {
        ScratchFolder folder;
        {
            std::ofstream file{ folder.File(), std::ios::binary | std::ios::trunc };
            file << "{ not json";
        }
        {
            WatchOutbox outbox{ folder.Path() };
            BindAndLoad(outbox, L"account-a");
            Require(outbox.Pending().empty(), "a corrupt file produced rows");
            outbox.FlushAsync().get();
            Require(!std::filesystem::exists(folder.File()), "a corrupt file was not replaced");
            outbox.Stage({ Row(L"a", 30, 100) });
            outbox.FlushAsync().get();
            Require(std::filesystem::exists(folder.File()), "a row staged after a corrupt file was not written");
        }
        {
            std::ofstream file{ folder.File(), std::ios::binary | std::ios::trunc };
            std::string const filler(static_cast<std::size_t>(HaloDesktop::Services::WatchOutboxMaximumBytes) + 16, ' ');
            file << filler;
        }
        WatchOutbox outbox{ folder.Path() };
        BindAndLoad(outbox, L"account-a");
        Require(outbox.Pending().empty(), "an oversized file produced rows");

        // Rows that are malformed one by one are dropped on their own.
        auto const text = std::string{ R"({"version":1,"account":"account-a","rows":[)" }
            + R"({"videoId":"a","itemId":"tt1","positionSec":10,"durationSec":3000,"watched":false,"updatedAt":100},)"
            + R"({"videoId":"","itemId":"tt1","positionSec":10,"durationSec":3000,"updatedAt":100},)"
            + R"({"videoId":"c","itemId":"tt1","positionSec":-5,"durationSec":3000,"updatedAt":100},)"
            + R"({"videoId":"d","itemId":"tt1","positionSec":10,"durationSec":3000,"updatedAt":"later"},)"
            + R"(7]})";
        auto const parsed = HaloDesktop::Services::ParseWatchOutbox(text, L"account-a");
        Require(parsed.size() == 1 && parsed[0].VideoId == L"a", "malformed rows were not dropped on their own");
        Require(HaloDesktop::Services::ParseWatchOutbox(text, L"account-b").empty(), "another account's rows were read");
        Require(HaloDesktop::Services::ParseWatchOutbox(R"({"version":2,"account":"account-a","rows":[]})", L"account-a").empty(),
            "an unknown version was read");
    }

    void TestAccountChangeClearsFirst()
    {
        ScratchFolder folder;
        WatchOutbox outbox{ folder.Path() };
        BindAndLoad(outbox, L"account-a");
        outbox.Stage({ Row(L"a", 30, 100), Row(L"b", 40, 110) });
        auto const oldEpoch = outbox.Epoch();
        auto const sentUnderOld = outbox.Pending();

        // The order that matters: once the new account is bound, before anything
        // has been awaited, no row of the old one can be read for a send.
        Require(outbox.Bind(L"account-b"), "a new account did not ask for its stored rows");
        Require(outbox.Pending().empty(), "a row of the old account was still pending under the new one");
        auto const read = outbox.ReadStoredAsync(L"account-b").get();
        Require(read.empty(), "the old account's rows were read for the new one");
        outbox.Adopt(outbox.Epoch(), read);

        // A send that began under the old account cannot settle the new one's rows.
        outbox.Stage({ Row(L"a", 5, 50) });
        outbox.Settle(oldEpoch, sentUnderOld);
        Require(outbox.Pending().size() == 1 && outbox.Pending()[0].UpdatedAt == 50,
            "a send from before the change of account settled the new account's rows");
        // Nor can a stale adoption bring the old rows back.
        outbox.Adopt(oldEpoch, sentUnderOld);
        Require(outbox.Pending().size() == 1, "rows read for the old account were adopted by the new one");
        outbox.FlushAsync().get();

        // On disk only the new account's row remains, under the new account.
        auto const stored = HaloDesktop::Storage::ReadUtf8File(folder.File(), HaloDesktop::Services::WatchOutboxMaximumBytes);
        Require(HaloDesktop::Services::ParseWatchOutbox(stored, L"account-a").empty(), "the old account's rows were still on disk");
        Require(HaloDesktop::Services::ParseWatchOutbox(stored, L"account-b").size() == 1, "the new account's row was not on disk");

        // Signing out removes them too, as the reference clears on every change.
        WatchOutbox reopenedNew{ folder.Path() };
        BindAndLoad(reopenedNew, L"account-b");
        Require(reopenedNew.Pending().size() == 1, "the new account's row was not kept");
        Require(!reopenedNew.Bind(L""), "signing out asked for a read");
        reopenedNew.FlushAsync().get();
        Require(!std::filesystem::exists(folder.File()), "signing out left the account's rows on disk");
    }

    void TestSettleDuringSendKeepsNewerStage()
    {
        ScratchFolder folder;
        WatchOutbox outbox{ folder.Path() };
        BindAndLoad(outbox, L"account-a");
        outbox.Stage({ Row(L"a", 30, 100), Row(L"b", 40, 100) });
        auto const epoch = outbox.Epoch();
        auto const sent = outbox.Pending();
        // The player stages again while the send is out.
        outbox.Stage({ Row(L"a", 32, 102) });
        outbox.Settle(epoch, sent);
        auto const left = outbox.Pending();
        Require(left.size() == 1 && left[0].VideoId == L"a" && left[0].PositionSec == 32,
            "a row staged again while its send was out was lost");

        // A failed send settles nothing: everything stays for the next one.
        outbox.FlushAsync().get();
        WatchOutbox reopened{ folder.Path() };
        BindAndLoad(reopened, L"account-a");
        Require(reopened.Pending().size() == 1 && reopened.Pending()[0].PositionSec == 32,
            "the row kept after a settle was not on disk");
    }
}

void RunWatchOutboxTests()
{
    TestMergeKeepsNewestPerVideo();
    TestSettleKeepsRowsStagedAgain();
    TestOutboxIsBounded();
    TestAlreadyKnownRowsAreNotSent();
    TestFileRoundTrip();
    TestCorruptAndOversizedFilesAreReplaced();
    TestAccountChangeClearsFirst();
    TestSettleDuringSendKeepsNewerStage();
}
