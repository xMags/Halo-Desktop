#pragma once

#include "Services/Downloads/DownloadTypes.h"

#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace HaloDesktop::Services::Downloads
{
    // How the downloads show outside the app: the taskbar button's progress
    // and a notification when one finishes. No UI or WinRT types here, so the
    // tests compile this by path.

    enum class TaskbarProgressState
    {
        // Nothing left to download: the button is plain.
        None,
        // Sizes still unknown: the bar runs without a value.
        Indeterminate,
        Normal,
        // Everything left is paused.
        Paused,
        // Only failed downloads are left.
        Error,
    };

    struct TaskbarProgress final
    {
        TaskbarProgressState State{ TaskbarProgressState::None };
        // Bytes done and to do across what is left; Total is zero unless the
        // state carries a value.
        std::uint64_t Completed{};
        std::uint64_t Total{};

        bool operator==(TaskbarProgress const&) const = default;
    };

    // The button's progress for these records: every download not finished
    // and not on its way out counts. Running or queued work wins over paused,
    // which wins over failed; the bar is the bytes of the downloads the state
    // is about, and runs without a value while any of them has no known size.
    [[nodiscard]] TaskbarProgress SummarizeTaskbarProgress(std::span<DownloadRecord const> records);

    // The downloads that finished since `unfinished` (the jobs last seen not
    // done) was taken, which is then brought up to date: a download only
    // counts as newly finished if it was seen unfinished first, so the ones
    // already done when the app starts, or when an account's list arrives,
    // never announce themselves.
    [[nodiscard]] std::vector<std::wstring> TakeNewlyFinished(
        std::span<DownloadRecord const> records,
        std::set<std::wstring, std::less<>>& unfinished);

    // The notification for a finished download, as the toast XML Windows
    // reads: "Ready to watch", the title, and a Play button. Clicking the
    // notification itself opens Downloads. The title is escaped for XML and
    // the job is percent-encoded into the arguments, so neither can change
    // the notification's shape.
    [[nodiscard]] std::wstring ReadyToWatchToastXml(std::wstring_view title, std::wstring_view jobId);

    // What a click on that notification asked for: Play, or just to open
    // Downloads, with the job it was about. Empty for arguments it did not
    // write.
    struct ToastAnswer final
    {
        bool Play{};
        std::wstring JobId;

        bool operator==(ToastAnswer const&) const = default;
    };

    [[nodiscard]] std::optional<ToastAnswer> ParseToastArguments(std::wstring_view arguments);
}
