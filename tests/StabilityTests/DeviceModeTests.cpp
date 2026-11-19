#include "DeviceModeTests.h"

#include "AddonTestFakes.h"

#include "Api/ApiClient.h"
#include "Api/ApiError.h"
#include "Api/BackendSelector.h"
#include "Api/HaloBackend.h"
#include "Api/HttpExecutor.h"
#include "Services/AddonListPolicy.h"
#include "Services/Auth/ITokenProvider.h"
#include "Services/Device/DeviceBackend.h"
#include "Services/Device/DeviceDocuments.h"
#include "Services/Device/DeviceModePolicy.h"
#include "Services/Device/DeviceMovePolicy.h"
#include "Services/Device/DeviceStore.h"
#include "Services/Downloads/DownloadAdoptionPolicy.h"
#include "Services/Downloads/DownloadIndexStore.h"
#include "Services/Downloads/DownloadTypes.h"
#include "Services/Downloads/TransferEngine.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <wil/resource.h>

namespace
{
    namespace Device = HaloDesktop::Services::Device;
    namespace Downloads = HaloDesktop::Services::Downloads;
    namespace Dto = HaloDesktop::Api::Dto;
    namespace Json = winrt::Windows::Data::Json;
    using AddonTests::Require;

    constexpr wchar_t Cinemeta[] = L"https://v3-cinemeta.strem.io/manifest.json";
    constexpr wchar_t OpenSubtitles[] = L"https://opensubtitles-v3.strem.io/manifest.json";

    Dto::AddonRecord Addon(std::wstring const& url, std::wstring const& name, bool hideCatalogs = false)
    {
        Dto::AddonRecord addon;
        addon.Id = winrt::hstring{ L"id:" + name };
        addon.TransportUrl = winrt::hstring{ url };
        addon.Name = winrt::hstring{ name };
        addon.Version = L"1";
        addon.HideCatalogs = hideCatalogs;
        return addon;
    }

    Dto::LibraryRow Library(std::wstring const& id, std::int64_t updatedAt)
    {
        return { winrt::hstring{ id }, L"movie", winrt::hstring{ id }, std::nullopt, 1, std::nullopt, updatedAt };
    }

    Dto::WatchEntry Watch(std::wstring const& videoId, std::int64_t updatedAt, std::optional<std::int64_t> removedAt = std::nullopt)
    {
        return {
            winrt::hstring{ videoId }, winrt::hstring{ L"movie:" + videoId }, 10, 100, false,
            std::nullopt, std::nullopt, updatedAt, removedAt,
        };
    }

    [[noreturn]] void Unexpected()
    {
        throw std::logic_error{ "The move asked for something it should not need." };
    }

    // The account side of the reference's FakeAccount: records the calls the
    // move makes, refuses some addon URLs (400) and fails on others as the
    // network would.
    class FakeAccount final : public HaloDesktop::Api::IHaloBackend
    {
    public:
        std::vector<std::wstring> Calls;
        std::vector<Dto::AddonRecord> Own;
        Dto::SettingsPayload Settings{ Json::JsonObject{}, 0 };
        std::set<std::wstring> Refused;
        std::set<std::wstring> Unreachable;
        bool LibraryUnreachable{};
        std::vector<Dto::WatchEntry> WatchSent;

        concurrency::task<Dto::AddonsPayload> GetAddonsAsync() override
        {
            return concurrency::task_from_result(Dto::AddonsPayload{ {}, Own });
        }

        concurrency::task<void> PutAddonsAsync(std::vector<winrt::hstring> transportUrls) override
        {
            std::wstring const added{ transportUrls.back() };
            Calls.push_back(L"addons:" + std::to_wstring(transportUrls.size()));
            if (Refused.contains(added))
            {
                throw winrt::hresult_error{ HaloDesktop::Api::ApiError::MakeHttpStatus(400), L"could not fetch a valid manifest" };
            }
            if (Unreachable.contains(added))
            {
                throw winrt::hresult_error{ HaloDesktop::Api::ApiError::Transport, L"The server could not be reached." };
            }
            std::vector<Dto::AddonRecord> next;
            for (auto const& url : transportUrls)
            {
                auto const found = std::find_if(Own.begin(), Own.end(), [&url](Dto::AddonRecord const& addon)
                {
                    return addon.TransportUrl && *addon.TransportUrl == url;
                });
                next.push_back(found != Own.end() ? *found : Addon(std::wstring{ url }, std::wstring{ url }));
            }
            Own = std::move(next);
            return concurrency::task_from_result();
        }

        concurrency::task<void> PatchAddonAsync(winrt::hstring addonId, bool hideCatalogs) override
        {
            Calls.push_back(L"patch:" + std::wstring{ addonId } + (hideCatalogs ? L":true" : L":false"));
            return concurrency::task_from_result();
        }

        concurrency::task<std::vector<Dto::MetaPreview>> GetCatalogAsync(
            winrt::hstring, winrt::hstring, winrt::hstring, std::vector<std::pair<winrt::hstring, winrt::hstring>>) override
        {
            Unexpected();
        }

        concurrency::task<Dto::MetaDetail> GetMetaAsync(winrt::hstring, winrt::hstring) override { Unexpected(); }
        concurrency::task<Dto::StreamsPayload> GetStreamsAsync(winrt::hstring, winrt::hstring) override { Unexpected(); }
        concurrency::task<Dto::NextEpisodePayload> GetNextEpisodeAsync(
            winrt::hstring, winrt::hstring, winrt::hstring, winrt::hstring, winrt::hstring) override
        {
            Unexpected();
        }
        concurrency::task<Dto::SubtitlesPayload> GetSubtitlesAsync(
            winrt::hstring, winrt::hstring, std::optional<winrt::hstring>, std::optional<std::uint64_t>, std::optional<winrt::hstring>) override
        {
            Unexpected();
        }
        concurrency::task<std::vector<Dto::LibraryRow>> GetLibraryAsync() override { Unexpected(); }

        concurrency::task<std::vector<Dto::LibraryRow>> PutLibraryAsync(std::vector<Dto::LibraryRow> rows) override
        {
            Calls.push_back(L"library:" + std::to_wstring(rows.size()));
            if (LibraryUnreachable)
            {
                throw winrt::hresult_error{ HaloDesktop::Api::ApiError::Transport, L"The server could not be reached." };
            }
            return concurrency::task_from_result(std::move(rows));
        }

        concurrency::task<std::vector<Dto::WatchEntry>> GetWatchStateAsync() override { Unexpected(); }

        concurrency::task<std::vector<Dto::WatchEntry>> PutWatchStateAsync(std::vector<Dto::WatchEntry> rows) override
        {
            Calls.push_back(L"watch:" + std::to_wstring(rows.size()));
            WatchSent.insert(WatchSent.end(), rows.begin(), rows.end());
            return concurrency::task_from_result(std::move(rows));
        }

        concurrency::task<Dto::SettingsPayload> GetSettingsAsync() override
        {
            return concurrency::task_from_result(Settings);
        }

        concurrency::task<Dto::SettingsPayload> PutSettingsAsync(Json::JsonObject value, std::int64_t updatedAt) override
        {
            Calls.push_back(L"settings:" + std::to_wstring(updatedAt));
            Settings = Dto::SettingsPayload{ value, updatedAt };
            return concurrency::task_from_result(Settings);
        }
    };

    // The reference's fakeDevice: only what the move reads.
    class FakeDevice final : public HaloDesktop::Api::IHaloBackend
    {
    public:
        std::vector<Dto::LibraryRow> LibraryRows;
        std::vector<Dto::WatchEntry> WatchStates;
        Dto::SettingsPayload Settings{ Json::JsonObject{}, 0 };
        std::vector<Dto::AddonRecord> Addons;

        concurrency::task<Dto::AddonsPayload> GetAddonsAsync() override
        {
            return concurrency::task_from_result(Dto::AddonsPayload{ {}, Addons });
        }
        concurrency::task<void> PutAddonsAsync(std::vector<winrt::hstring>) override { Unexpected(); }
        concurrency::task<void> PatchAddonAsync(winrt::hstring, bool) override { Unexpected(); }
        concurrency::task<std::vector<Dto::MetaPreview>> GetCatalogAsync(
            winrt::hstring, winrt::hstring, winrt::hstring, std::vector<std::pair<winrt::hstring, winrt::hstring>>) override
        {
            Unexpected();
        }
        concurrency::task<Dto::MetaDetail> GetMetaAsync(winrt::hstring, winrt::hstring) override { Unexpected(); }
        concurrency::task<Dto::StreamsPayload> GetStreamsAsync(winrt::hstring, winrt::hstring) override { Unexpected(); }
        concurrency::task<Dto::NextEpisodePayload> GetNextEpisodeAsync(
            winrt::hstring, winrt::hstring, winrt::hstring, winrt::hstring, winrt::hstring) override
        {
            Unexpected();
        }
        concurrency::task<Dto::SubtitlesPayload> GetSubtitlesAsync(
            winrt::hstring, winrt::hstring, std::optional<winrt::hstring>, std::optional<std::uint64_t>, std::optional<winrt::hstring>) override
        {
            Unexpected();
        }
        concurrency::task<std::vector<Dto::LibraryRow>> GetLibraryAsync() override
        {
            return concurrency::task_from_result(LibraryRows);
        }
        concurrency::task<std::vector<Dto::LibraryRow>> PutLibraryAsync(std::vector<Dto::LibraryRow>) override { Unexpected(); }
        concurrency::task<std::vector<Dto::WatchEntry>> GetWatchStateAsync() override { Unexpected(); }
        concurrency::task<std::vector<Dto::WatchEntry>> PutWatchStateAsync(std::vector<Dto::WatchEntry>) override { Unexpected(); }
        concurrency::task<Dto::SettingsPayload> GetSettingsAsync() override
        {
            return concurrency::task_from_result(Settings);
        }
        concurrency::task<Dto::SettingsPayload> PutSettingsAsync(Json::JsonObject, std::int64_t) override { Unexpected(); }
    };

    Device::StoredWatchStatesReader StoredOf(std::shared_ptr<FakeDevice> const& device)
    {
        return [device]() { return concurrency::task_from_result(device->WatchStates); };
    }

    std::vector<Dto::LibraryRow> ManyLibraryRows(std::size_t count)
    {
        std::vector<Dto::LibraryRow> rows;
        for (std::size_t index = 0; index < count; ++index)
        {
            rows.push_back(Library(L"movie:tt" + std::to_wstring(index), 1));
        }
        return rows;
    }

    /* The plan */

    void TestOnlyAddonsAddedHereMove()
    {
        std::vector<Dto::AddonRecord> const device{
            Addon(Cinemeta, L"Cinemeta"),
            Addon(L"https://a.test/manifest.json", L"A", true),
            Addon(OpenSubtitles, L"OpenSubtitles"),
            Addon(L"https://b.test/manifest.json", L"B"),
        };
        std::vector<Dto::AddonRecord> const account{ Addon(L"https://b.test/manifest.json", L"B") };
        auto const moves = Device::AddonsToMove(device, account);
        Require(moves == std::vector<Device::AddonMove>{ { L"https://a.test/manifest.json", L"A", true } },
            "a built-in or an addon the account has was moved, or one added here was not");
    }

    void TestSettingsMoveOnlyIntoAnAccountWithNone()
    {
        Require(Device::ShouldMoveSettings(5, 0), "this PC's settings did not move into an account that never saved any");
        Require(!Device::ShouldMoveSettings(5, 1), "this PC's settings replaced settings the account saved");
        Require(!Device::ShouldMoveSettings(0, 0), "settings never saved here were moved");
    }

    void TestRowsGoInBoundedChunks()
    {
        auto const chunks = Device::Chunked(std::vector<int>{ 1, 2, 3, 4, 5 }, 2);
        Require(chunks == std::vector<std::vector<int>>{ { 1, 2 }, { 3, 4 }, { 5 } }, "rows were not chunked in order");
        Require(Device::Chunked(std::vector<int>{}, 2).empty(), "no rows still made a chunk");
        Require(Device::MoveChunkSize == 500, "the chunk size is not the reference's");
    }

    /* The move itself, against fakes of both sides */

    void TestEverythingMovesAndADeadAddonIsNamed()
    {
        auto const account = std::make_shared<FakeAccount>();
        account->Own = { Addon(Cinemeta, L"Cinemeta") };
        account->Refused.insert(L"https://dead.test/manifest.json");
        auto const device = std::make_shared<FakeDevice>();
        device->LibraryRows = ManyLibraryRows(1200);
        device->WatchStates = { Watch(L"tt1", 3) };
        device->Settings = Dto::SettingsPayload{ Json::JsonObject::Parse(LR"({"playbackRate":1.25})"), 9 };
        device->Addons = {
            Addon(Cinemeta, L"Cinemeta"),
            Addon(L"https://dead.test/manifest.json", L"Dead"),
            Addon(L"https://alive.test/manifest.json", L"Alive", true),
        };

        auto const result = Device::MoveRecordsIntoAccountAsync(device, StoredOf(device), account).get();
        Require(result.FailedAddons == std::vector<std::wstring>{ L"Dead" }, "the dead addon was not the one named");
        Require(account->Calls == std::vector<std::wstring>{
                    L"library:500",
                    L"library:500",
                    L"library:200",
                    L"watch:1",
                    L"settings:9",
                    L"addons:2",
                    L"addons:2",
                    L"patch:id:https://alive.test/manifest.json:true",
                },
            "the move did not make the reference's calls in its order");
        Require(account->Own.size() == 2 && *account->Own[0].TransportUrl == Cinemeta
                    && *account->Own[1].TransportUrl == L"https://alive.test/manifest.json",
            "the account's own list is not the built-in followed by the addon that loaded");
    }

    void TestAnUnreachableAccountStopsTheMove()
    {
        auto const account = std::make_shared<FakeAccount>();
        account->Settings = Dto::SettingsPayload{ Json::JsonObject::Parse(LR"({"playbackRate":2})"), 4 };
        account->Unreachable.insert(L"https://a.test/manifest.json");
        auto const device = std::make_shared<FakeDevice>();
        device->Settings = Dto::SettingsPayload{ Json::JsonObject::Parse(LR"({"playbackRate":1})"), 9 };
        device->Addons = { Addon(L"https://a.test/manifest.json", L"A") };

        auto threw = false;
        try
        {
            static_cast<void>(Device::MoveRecordsIntoAccountAsync(device, StoredOf(device), account).get());
        }
        catch (winrt::hresult_error const& error)
        {
            threw = HaloDesktop::Api::ApiError::IsTransport(error.code());
        }
        Require(threw, "an account that could not be reached did not stop the move");
        // The account's own settings were kept: it already had some.
        Require(std::none_of(account->Calls.begin(), account->Calls.end(), [](std::wstring const& call)
                {
                    return call.starts_with(L"settings:");
                }),
            "this PC's settings replaced the account's");
    }

    void TestALibraryFailureSendsNothingAfterIt()
    {
        auto const account = std::make_shared<FakeAccount>();
        account->LibraryUnreachable = true;
        auto const device = std::make_shared<FakeDevice>();
        device->LibraryRows = ManyLibraryRows(3);
        device->WatchStates = { Watch(L"tt1", 3) };
        device->Settings = Dto::SettingsPayload{ Json::JsonObject::Parse(LR"({"playbackRate":1})"), 9 };
        device->Addons = { Addon(L"https://a.test/manifest.json", L"A") };

        auto threw = false;
        try
        {
            static_cast<void>(Device::MoveRecordsIntoAccountAsync(device, StoredOf(device), account).get());
        }
        catch (winrt::hresult_error const&)
        {
            threw = true;
        }
        Require(threw, "a library the account refused did not fail the move");
        Require(account->Calls == std::vector<std::wstring>{ L"library:3" }, "the move went on after the library failed");
    }

    // A sign-out and sign-in as someone else mid-move: the next request to the
    // account is refused before it is sent, so nothing more reaches the
    // second account, and the move fails for a later retry.
    void TestAnAccountSwitchStopsTheMoveAtTheNextRequest()
    {
        auto const account = std::make_shared<FakeAccount>();
        auto const device = std::make_shared<FakeDevice>();
        device->LibraryRows = ManyLibraryRows(1200);
        device->WatchStates = { Watch(L"tt1", 3) };
        device->Settings = Dto::SettingsPayload{ Json::JsonObject::Parse(LR"({"playbackRate":1})"), 9 };
        device->Addons = { Addon(L"https://a.test/manifest.json", L"A") };

        // The account changes once the first chunk has been sent.
        auto const checked = std::make_shared<Device::OwnerCheckedBackend>(
            account, [account]() { return account->Calls.empty(); });
        auto stopped = false;
        try
        {
            static_cast<void>(Device::MoveRecordsIntoAccountAsync(device, StoredOf(device), checked).get());
        }
        catch (Device::AccountChangedDuringMove const&)
        {
            stopped = true;
        }
        Require(stopped, "a move went on after the account changed");
        Require(account->Calls == std::vector<std::wstring>{ L"library:500" },
            "a request reached the account after it changed");

        // A switch just before the addons: the addon is not sent, and the stop
        // is not mistaken for an addon the account refused.
        auto const late = std::make_shared<FakeAccount>();
        auto const lateChecked = std::make_shared<Device::OwnerCheckedBackend>(
            late, [late]() { return late->Calls.size() < 2; });
        auto lateStopped = false;
        try
        {
            static_cast<void>(Device::MoveRecordsIntoAccountAsync(device, StoredOf(device), lateChecked).get());
        }
        catch (Device::AccountChangedDuringMove const&)
        {
            lateStopped = true;
        }
        Require(lateStopped && late->Calls == std::vector<std::wstring>{ L"library:500", L"library:500" },
            "the move did not stop at the request after the switch");

        // While the account stays the same the checked side changes nothing.
        auto const same = std::make_shared<FakeAccount>();
        auto const sameChecked = std::make_shared<Device::OwnerCheckedBackend>(same, []() { return true; });
        auto const result = Device::MoveRecordsIntoAccountAsync(device, StoredOf(device), sameChecked).get();
        Require(result.FailedAddons.empty() && same->Calls == std::vector<std::wstring>{
                    L"library:500", L"library:500", L"library:200", L"watch:1", L"settings:9", L"addons:1" },
            "the owner check changed a move into the same account");
    }

    void TestRemovalsReachTheAccount()
    {
        auto const account = std::make_shared<FakeAccount>();
        auto const device = std::make_shared<FakeDevice>();
        device->WatchStates = { Watch(L"tt1", 5, 5), Watch(L"tt2", 6) };

        static_cast<void>(Device::MoveRecordsIntoAccountAsync(device, StoredOf(device), account).get());
        Require(account->WatchSent.size() == 2 && account->WatchSent[0].RemovedAt == std::optional<std::int64_t>{ 5 },
            "a removal made on this PC did not reach the account");
    }

    // The account side as the server merges: a DeviceBackend applies the same
    // last-write-wins rules, so older rows from this PC never replace newer
    // ones the account already holds.
    void TestOlderDeviceRowsNeverReplaceNewerAccountRows()
    {
        auto const makeBackend = [](std::shared_ptr<AddonTests::MemoryStore> const& store)
        {
            return std::make_shared<Device::DeviceBackend>(Device::DeviceBackendOptions{
                .Store = store,
                .Fetcher = std::make_shared<AddonTests::FakeFetcher>(std::map<std::wstring, AddonTests::Route>{}),
            });
        };
        auto const accountStore = std::make_shared<AddonTests::MemoryStore>();
        auto const deviceStore = std::make_shared<AddonTests::MemoryStore>();
        auto const account = makeBackend(accountStore);
        auto const device = makeBackend(deviceStore);

        auto newer = Library(L"movie:tt1", 50);
        newer.Name = L"Account's";
        static_cast<void>(account->PutLibraryAsync({ newer }).get());
        static_cast<void>(account->PutWatchStateAsync({ Watch(L"tt1", 50) }).get());
        static_cast<void>(account->PutSettingsAsync(Json::JsonObject::Parse(LR"({"autoplayNextEpisode":false})"), 40).get());

        auto older = Library(L"movie:tt1", 10);
        older.Name = L"Device's";
        static_cast<void>(device->PutLibraryAsync({ older, Library(L"movie:tt2", 10) }).get());
        auto olderWatch = Watch(L"tt1", 10);
        olderWatch.PositionSec = 99;
        static_cast<void>(device->PutWatchStateAsync({ olderWatch }).get());
        static_cast<void>(device->PutSettingsAsync(Json::JsonObject::Parse(LR"({"autoplayNextEpisode":true})"), 20).get());

        auto const result = Device::MoveRecordsIntoAccountAsync(
            device, [device]() { return device->GetStoredWatchStatesAsync(); }, account).get();
        Require(result.FailedAddons.empty(), "a move without addons named one");

        auto const library = account->GetLibraryAsync().get();
        auto const first = std::find_if(library.begin(), library.end(), [](Dto::LibraryRow const& row) { return row.Id == L"movie:tt1"; });
        Require(first != library.end() && first->Name == L"Account's", "an older row from this PC replaced the account's newer one");
        Require(std::any_of(library.begin(), library.end(), [](Dto::LibraryRow const& row) { return row.Id == L"movie:tt2"; }),
            "a row only this PC had did not reach the account");
        auto const watch = account->GetWatchStateAsync().get();
        Require(watch.size() == 1 && watch[0].PositionSec == 10 && watch[0].UpdatedAt == 50,
            "older progress from this PC replaced the account's newer progress");
        auto const settings = account->GetSettingsAsync().get();
        Require(settings.UpdatedAt == 40 && !settings.Value.GetNamedBoolean(L"autoplayNextEpisode"),
            "this PC's settings replaced settings the account saved");
    }

    void TestFailedAddonsDialogCopy()
    {
        Require(Device::FailedAddonsTitle(1) == L"An addon was not moved" && Device::FailedAddonsTitle(2) == L"Some addons were not moved",
            "the dialog's title is not the reference's");
        Require(Device::FailedAddonsBody({ L"Dead" })
                    == L"Dead could not be added to your account because it did not load. You can add it again in Settings.",
            "the dialog's sentence for one addon is not the reference's");
        Require(Device::FailedAddonsBody({ L"A", L"B" })
                    == L"A, B could not be added to your account because they did not load. You can add them again in Settings.",
            "the dialog's sentence for several addons is not the reference's");
    }

    /* Startup, the top-up, the selector */

    void TestStartupDecisionTable()
    {
        using Device::ProfileRead;
        using Device::StartupDecision;
        using Device::StartupScreen;
        Require(Device::DecideStartup(true, ProfileRead::Found) == StartupDecision{ StartupScreen::SignedIn, true, false },
            "a restored session with a profile did not remember it for the move");
        Require(Device::DecideStartup(true, ProfileRead::None) == StartupDecision{ StartupScreen::SignedIn, false, false },
            "a restored session did not open signed in");
        Require(Device::DecideStartup(true, ProfileRead::Unreadable) == StartupDecision{ StartupScreen::SignedIn, false, false },
            "an unreadable profile stopped a restored session");
        Require(Device::DecideStartup(false, ProfileRead::Found) == StartupDecision{ StartupScreen::Device, false, false },
            "a profile without a session did not open Halo without an account");
        Require(Device::DecideStartup(false, ProfileRead::None) == StartupDecision{ StartupScreen::SignIn, false, false },
            "neither a session nor a profile did not open the sign-in screen");
        Require(Device::DecideStartup(false, ProfileRead::Unreadable) == StartupDecision{ StartupScreen::SignIn, false, true },
            "an unreadable profile did not open the sign-in screen with its error");
        Require(Device::DeviceSetupError(L"x") == L"Halo could not set up on this PC: x", "the setup error's copy is not the reference's");
    }

    void TestTopUpList()
    {
        std::vector<std::wstring> all;
        for (auto const url : HaloDesktop::Services::DefaultAddonUrls) all.emplace_back(url);
        Require(!Device::TopUpAddonList(all), "a list with every built-in was saved again");

        auto const fromNothing = Device::TopUpAddonList({});
        Require(fromNothing && *fromNothing == all, "an empty list was not given the built-ins in their order");

        std::vector<std::wstring> const own{ L"https://mine.test/manifest.json", std::wstring{ OpenSubtitles } };
        auto const topped = Device::TopUpAddonList(own);
        Require(topped && topped->size() == 4 && (*topped)[0] == own[0] && (*topped)[1] == own[1]
                    && (*topped)[2] == Cinemeta && (*topped)[3] == L"https://tmdb.elfhosted.com/manifest.json",
            "the missing built-ins did not follow this PC's own list");
    }

    class StillTokenProvider final : public HaloDesktop::Services::Auth::ITokenProvider
    {
    public:
        concurrency::task<std::optional<winrt::hstring>> AccessTokenAsync() override
        {
            return concurrency::task_from_result(std::optional<winrt::hstring>{});
        }
        concurrency::task<std::optional<winrt::hstring>> RefreshAccessTokenAsync() override
        {
            return concurrency::task_from_result(std::optional<winrt::hstring>{});
        }
        concurrency::task<void> RejectSessionAsync(std::uint64_t) override
        {
            return concurrency::task_from_result();
        }
        std::uint64_t SessionGeneration() const noexcept override { return 1; }
        HaloDesktop::Services::Auth::SessionTarget CurrentTarget() const override
        {
            return { .Generation = 1, .ServerUrl = std::nullopt };
        }
    };

    void TestSelectorSwitches()
    {
        auto const api = std::make_shared<HaloDesktop::Api::ApiClient>(
            L"http://127.0.0.1:9", std::make_shared<HaloDesktop::Api::HttpExecutor>(), std::make_shared<StillTokenProvider>());
        HaloDesktop::Api::BackendSelector selector{ api };
        auto const accountBackend = static_cast<HaloDesktop::Api::IHaloBackend*>(api.get());
        Require(!selector.UsesDevice() && selector.Current().get() == accountBackend, "the selector did not start on the account");

        auto const device = std::make_shared<FakeDevice>();
        selector.UseDevice(device);
        Require(selector.UsesDevice() && selector.Current() == device, "the selector did not switch to this PC's backend");
        selector.UseAccount();
        Require(!selector.UsesDevice() && selector.Current().get() == accountBackend, "the selector did not switch back to the account");

        auto refused = false;
        try
        {
            selector.UseDevice(nullptr);
        }
        catch (std::invalid_argument const&)
        {
            refused = true;
        }
        Require(refused && !selector.UsesDevice(), "the selector switched to no backend at all");
    }

    /* The downloads owner change */

    Downloads::DownloadRecord Owned(std::wstring jobId, std::wstring account, std::wstring videoId, Downloads::DownloadStatus status)
    {
        Downloads::DownloadRecord record;
        record.JobId = std::move(jobId);
        record.AccountKey = std::move(account);
        record.Media.VideoId = std::move(videoId);
        record.Status = status;
        return record;
    }

    void TestAdoptionMovesEveryDownloadWithoutADuplicate()
    {
        std::vector<Downloads::DownloadRecord> const records{
            Owned(L"d1", L"device", L"movie:1", Downloads::DownloadStatus::Done),
            Owned(L"d2", L"device", L"movie:2", Downloads::DownloadStatus::Paused),
            Owned(L"a1", L"account", L"movie:3", Downloads::DownloadStatus::Done),
            Owned(L"o1", L"other", L"movie:1", Downloads::DownloadStatus::Done),
        };
        auto const plan = Downloads::PlanAdoption(records, L"device", L"account");
        Require(plan.Rekey == std::vector<std::wstring>{ L"d1", L"d2" } && plan.Discard.empty(),
            "not every download of this PC moved, or one was discarded");
    }

    void TestAdoptionKeepsTheFinishedCopyOfADuplicate()
    {
        std::vector<Downloads::DownloadRecord> const records{
            // The device finished, the account did not: the device copy wins.
            Owned(L"d1", L"device", L"movie:1", Downloads::DownloadStatus::Done),
            Owned(L"a1", L"account", L"movie:1", Downloads::DownloadStatus::Paused),
            // Both finished: the account keeps its own.
            Owned(L"d2", L"device", L"movie:2", Downloads::DownloadStatus::Done),
            Owned(L"a2", L"account", L"movie:2", Downloads::DownloadStatus::Done),
            // Neither finished: the account keeps its own.
            Owned(L"d3", L"device", L"movie:3", Downloads::DownloadStatus::Failed),
            Owned(L"a3", L"account", L"movie:3", Downloads::DownloadStatus::Queued),
        };
        auto const plan = Downloads::PlanAdoption(records, L"device", L"account");
        Require(plan.Rekey == std::vector<std::wstring>{ L"d1" }, "the wrong copies changed owner");
        Require(plan.Discard == std::vector<std::wstring>{ L"a1", L"d2", L"d3" }, "the wrong duplicates were discarded");
    }

    void TestAdoptionDiscardsALosingDownloadsBackupWithIt()
    {
        auto replacing = Owned(L"d1", L"device", L"movie:1", Downloads::DownloadStatus::Paused);
        auto const backup = Owned(L"d0", L"device", L"movie:1", Downloads::DownloadStatus::Done);
        replacing.Replacement = Downloads::ReplacementBackup{ backup.JobId, {}, L"old.mkv", std::nullopt };
        std::vector<Downloads::DownloadRecord> const records{
            replacing,
            backup,
            Owned(L"a1", L"account", L"movie:1", Downloads::DownloadStatus::Done),
        };
        auto const plan = Downloads::PlanAdoption(records, L"device", L"account");
        Require(plan.Rekey.empty(), "a losing download or its backup changed owner");
        Require(plan.Discard == std::vector<std::wstring>{ L"d0", L"d1" }, "a losing download's backup was left behind");
    }

    // Every partition but the active one, once each, sorted: the owners whose
    // downloads the active account takes over.
    void TestOtherOwners()
    {
        std::vector<Downloads::DownloadRecord> const records{
            Owned(L"a1", L"account", L"movie:1", Downloads::DownloadStatus::Done),
            Owned(L"o1", L"old-address", L"movie:2", Downloads::DownloadStatus::Done),
            Owned(L"o2", L"old-address", L"movie:3", Downloads::DownloadStatus::Paused),
            Owned(L"d1", L"device", L"movie:4", Downloads::DownloadStatus::Done),
        };
        Require(Downloads::OtherOwners(records, L"account") == std::vector<std::wstring>{ L"device", L"old-address" },
            "the other owners were not each listed once, sorted, without the active one");
        Require(Downloads::OtherOwners(records, L"nobody").size() == 3, "an owner was missed");
        Require(Downloads::OtherOwners({}, L"account").empty(), "an empty index named an owner");
    }

    void TestAdoptingIntoTheSamePartitionChangesNothing()
    {
        std::vector<Downloads::DownloadRecord> const records{ Owned(L"d1", L"device", L"movie:1", Downloads::DownloadStatus::Done) };
        Require(Downloads::PlanAdoption(records, L"device", L"device") == Downloads::AdoptionPlan{},
            "adopting into the same partition changed something");
    }

    class TemporaryDirectory final
    {
    public:
        explicit TemporaryDirectory(std::wstring_view prefix)
        {
            GUID id{};
            winrt::check_hresult(CoCreateGuid(&id));
            std::array<wchar_t, 40> text{};
            if (StringFromGUID2(id, text.data(), static_cast<int>(text.size())) == 0)
            {
                throw std::runtime_error{ "The test could not create an identifier." };
            }
            m_path = std::filesystem::temp_directory_path() / (std::wstring{ prefix } + text.data());
            std::filesystem::create_directories(m_path);
        }

        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(m_path, ignored);
        }

        TemporaryDirectory(TemporaryDirectory const&) = delete;
        TemporaryDirectory& operator=(TemporaryDirectory const&) = delete;

        [[nodiscard]] std::filesystem::path const& Path() const noexcept { return m_path; }

    private:
        std::filesystem::path m_path;
    };

    void WriteFileBytes(std::filesystem::path const& path, std::string const& bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output{ path, std::ios::binary | std::ios::trunc };
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!output) throw std::runtime_error{ "The test could not write a file." };
    }

    Downloads::DownloadRecord Seeded(
        std::wstring jobId,
        std::wstring account,
        std::wstring videoId,
        std::filesystem::path const& root,
        Downloads::DownloadStatus status)
    {
        Downloads::DownloadRecord record;
        record.JobId = std::move(jobId);
        record.AccountKey = std::move(account);
        record.Media = { .VideoId = videoId, .ItemId = videoId, .MediaType = L"movie", .Title = L"Seeded" };
        record.FileName = record.JobId.substr(record.JobId.size() - 8) + L".mkv";
        record.RootPath = root;
        record.Status = status;
        record.TotalBytes = 5;
        record.DownloadedBytes = status == Downloads::DownloadStatus::Done ? 5 : 2;
        record.SourceFingerprint = Downloads::Sha256Hex(L"https://source.invalid/" + record.JobId);
        record.ExplicitPause = status == Downloads::DownloadStatus::Paused;
        record.CreatedAt = 1;
        record.UpdatedAt = 1;
        return record;
    }

    std::optional<Downloads::DownloadRecord> FindStored(std::filesystem::path const& root, std::wstring const& jobId)
    {
        Downloads::DownloadIndexStore store{ root };
        for (auto& record : store.Load(false))
        {
            if (record.JobId == jobId) return record;
        }
        return std::nullopt;
    }

    // An adoption on a real index in a temporary folder: owners change in the
    // index, the moved files stay where they were, the losing duplicate goes,
    // and a stale copy saved afterwards cannot give a record back.
    void TestAdoptionOnATemporaryIndex()
    {
        TemporaryDirectory temporary{ L"HaloDesktop-Adopt-" };
        auto const deviceKey = Device::DeviceDownloadOwner(Device::DeviceProfile{ L"3f8a1c2e-9b4d-4e6f-a1b2-c3d4e5f60718", 1 });
        auto const accountKey = Downloads::MakeAccountKey(L"https://account.invalid", L"user-a");

        std::filesystem::path folder;
        auto const moving = Seeded(L"00000000000000000000000000000001", deviceKey, L"movie:moving", {}, Downloads::DownloadStatus::Done);
        auto const paused = Seeded(L"00000000000000000000000000000002", deviceKey, L"movie:paused", {}, Downloads::DownloadStatus::Paused);
        auto const deviceDuplicate = Seeded(L"00000000000000000000000000000003", deviceKey, L"movie:both", {}, Downloads::DownloadStatus::Done);
        auto const accountCopy = Seeded(L"00000000000000000000000000000004", accountKey, L"movie:both", {}, Downloads::DownloadStatus::Done);
        std::vector<Downloads::DownloadRecord> seeded{ moving, paused, deviceDuplicate, accountCopy };
        {
            Downloads::DownloadIndexStore seed{ temporary.Path() };
            folder = seed.DownloadDirectory();
            for (auto& record : seeded) record.RootPath = folder;
            seed.Apply(seeded);
        }
        for (auto const& record : seeded)
        {
            WriteFileBytes(record.Status == Downloads::DownloadStatus::Done ? record.TargetPath() : record.PartialPath(), "12345");
        }

        {
            Downloads::TransferEngine engine{ temporary.Path() };
            engine.SetAccount(L"https://account.invalid", L"user-a");
            engine.AdoptAccount(deviceKey, accountKey);

            auto const listed = engine.List();
            auto const has = [&listed](std::wstring const& jobId)
            {
                return std::any_of(listed.begin(), listed.end(), [&jobId](Downloads::DownloadRecord const& record) { return record.JobId == jobId; });
            };
            Require(has(seeded[0].JobId) && has(seeded[1].JobId) && has(seeded[3].JobId) && !has(seeded[2].JobId) && listed.size() == 3,
                "the account does not show this PC's downloads once, beside its own copy");
            auto const pausedListed = std::find_if(listed.begin(), listed.end(), [&seeded](Downloads::DownloadRecord const& record)
            {
                return record.JobId == seeded[1].JobId;
            });
            Require(pausedListed != listed.end() && pausedListed->Status == Downloads::DownloadStatus::Paused,
                "a paused transfer did not move as it was");
        }

        auto const storedMoving = FindStored(temporary.Path(), seeded[0].JobId);
        auto const storedPaused = FindStored(temporary.Path(), seeded[1].JobId);
        Require(storedMoving && storedMoving->AccountKey == accountKey && storedPaused && storedPaused->AccountKey == accountKey,
            "the index does not name the account as the new owner");
        Require(!FindStored(temporary.Path(), seeded[2].JobId), "the losing duplicate is still in the index");
        auto const storedAccountCopy = FindStored(temporary.Path(), seeded[3].JobId);
        Require(storedAccountCopy && storedAccountCopy->AccountKey == accountKey, "the account's own copy changed");
        Require(std::filesystem::exists(seeded[0].TargetPath()) && std::filesystem::exists(seeded[1].PartialPath())
                    && std::filesystem::exists(seeded[3].TargetPath()),
            "a moved or kept download's file was touched");
        Require(!std::filesystem::exists(seeded[2].TargetPath()), "the losing duplicate's file was left behind");

        // A snapshot an engine copied before the rewrite and saves after it
        // must not give the record back to this PC.
        auto const stale = seeded[1];
        {
            Downloads::DownloadIndexStore back{ temporary.Path() };
            Require(back.AdoptAccount(accountKey, deviceKey, { stale.JobId }, 6).size() == 1,
                "a record could not be given back to this PC for the check");
        }
        Downloads::DownloadIndexStore store{ temporary.Path() };
        Require(store.AdoptAccount(deviceKey, accountKey, { seeded[0].JobId }, 5).empty(),
            "a record already moved was moved a second time");
        Require(store.AdoptAccount(deviceKey, accountKey, { stale.JobId }, 7).size() == 1, "a record of this PC did not move");
        store.Apply({ stale });
        auto const applied = FindStored(temporary.Path(), stale.JobId);
        Require(applied && applied->AccountKey == accountKey, "a copy with the old owner gave a moved record back to this PC");
        store.Save({ stale }, 1);
        auto const saved = FindStored(temporary.Path(), stale.JobId);
        Require(saved && saved->AccountKey == accountKey, "a whole-index save gave a moved record back to this PC");
    }

    // A changed server address (or a reinstall) signs the same viewer in under
    // a new account key. Their downloads saved under the old keys join the new
    // account instead of sitting hidden, one copy per video, files untouched.
    void TestDownloadsUnderAnotherAccountKeyJoinTheActiveOne()
    {
        TemporaryDirectory temporary{ L"HaloDesktop-AdoptOthers-" };
        auto const oldAddress = Downloads::MakeAccountKey(L"https://old.invalid", L"user-a");
        auto const otherUser = Downloads::MakeAccountKey(L"https://old.invalid", L"user-b");
        auto const current = Downloads::MakeAccountKey(L"https://new.invalid", L"user-a");

        auto const film = Seeded(L"00000000000000000000000000000011", oldAddress, L"movie:film", {}, Downloads::DownloadStatus::Done);
        auto const halfway = Seeded(L"00000000000000000000000000000012", oldAddress, L"movie:halfway", {}, Downloads::DownloadStatus::Paused);
        auto const elsewhere = Seeded(L"00000000000000000000000000000013", otherUser, L"movie:elsewhere", {}, Downloads::DownloadStatus::Done);
        auto const oldCopy = Seeded(L"00000000000000000000000000000014", oldAddress, L"movie:both", {}, Downloads::DownloadStatus::Done);
        auto const currentCopy = Seeded(L"00000000000000000000000000000015", current, L"movie:both", {}, Downloads::DownloadStatus::Done);
        std::vector<Downloads::DownloadRecord> seeded{ film, halfway, elsewhere, oldCopy, currentCopy };
        {
            Downloads::DownloadIndexStore seed{ temporary.Path() };
            auto const folder = seed.DownloadDirectory();
            for (auto& record : seeded) record.RootPath = folder;
            seed.Apply(seeded);
        }
        for (auto const& record : seeded)
        {
            WriteFileBytes(record.Status == Downloads::DownloadStatus::Done ? record.TargetPath() : record.PartialPath(), "12345");
        }

        {
            Downloads::TransferEngine engine{ temporary.Path() };
            engine.SetAccount(L"https://new.invalid", L"user-a");
            Require(engine.List().size() == 1, "the check needs the other keys' downloads hidden before adoption");
            engine.AdoptOtherOwners();
            auto const listed = engine.List();
            auto const has = [&listed](std::wstring const& jobId)
            {
                return std::any_of(listed.begin(), listed.end(), [&jobId](Downloads::DownloadRecord const& record) { return record.JobId == jobId; });
            };
            Require(has(film.JobId) && has(halfway.JobId) && has(elsewhere.JobId) && has(currentCopy.JobId) && listed.size() == 4,
                "a download saved under another account key is still hidden, or a video shows twice");
            // Nothing left over to adopt, and asking again changes nothing.
            engine.AdoptOtherOwners();
            Require(engine.List().size() == 4, "adopting a second time changed the list");
        }

        for (auto const& record : { film, halfway, elsewhere, currentCopy })
        {
            auto const stored = FindStored(temporary.Path(), record.JobId);
            Require(stored && stored->AccountKey == current, "the index does not name the active account as the owner");
        }
        Require(!FindStored(temporary.Path(), oldCopy.JobId), "the duplicate under the old key is still in the index");
        // `seeded` holds the copies with the folder set.
        Require(std::filesystem::exists(seeded[0].TargetPath()) && std::filesystem::exists(seeded[1].PartialPath())
                    && std::filesystem::exists(seeded[2].TargetPath()) && std::filesystem::exists(seeded[4].TargetPath()),
            "an adopted download's file was touched");
        Require(!std::filesystem::exists(seeded[3].TargetPath()), "the duplicate under the old key left its file behind");
    }

    void TestAdoptionThatCannotWriteChangesNothing()
    {
        TemporaryDirectory temporary{ L"HaloDesktop-AdoptRefused-" };
        Downloads::DownloadIndexStore store{ temporary.Path() };
        auto refused = false;
        try
        {
            static_cast<void>(store.AdoptAccount(L"same", L"same", {}, 1));
        }
        catch (std::invalid_argument const&)
        {
            refused = true;
        }
        Require(refused, "an adoption into the same owner was accepted");
    }

    /* The device store's clear */

    void TestClearRemovesTheProfileLast()
    {
        TemporaryDirectory temporary{ L"HaloDesktop-DeviceClear-" };
        Device::DeviceStore store{ temporary.Path() / L"device" };
        static_cast<void>(store.CreateProfileAsync(1).get());
        store.WriteAsync(Device::DeviceCollection::Library, R"({"version":1,"items":[]})").get();
        store.WriteAsync(Device::DeviceCollection::Settings, R"({"version":1,"value":{},"updatedAt":1})").get();

        // A document that cannot be removed now (another program holds it
        // open) stops the clear part way.
        auto const library = temporary.Path() / L"device" / std::wstring{ Device::CollectionFileName(Device::DeviceCollection::Library) };
        {
            wil::unique_hfile held{ CreateFileW(library.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) };
            Require(static_cast<bool>(held), "the test could not hold the library document open");
            auto failed = false;
            try
            {
                store.ClearAsync().get();
            }
            catch (...)
            {
                failed = true;
            }
            Require(failed, "a clear that could not remove a document reported success");
            Require(store.ReadProfileAsync().get().has_value(), "a clear cut short removed the profile, so the move would not run again");
        }

        store.ClearAsync().get();
        Require(!store.ReadProfileAsync().get().has_value(), "the profile survived a clear");
        Require(!store.ReadAsync(Device::DeviceCollection::Library).get() && !store.ReadAsync(Device::DeviceCollection::Settings).get(),
            "a document survived a clear");
    }
}

void RunDeviceModeTests()
{
    TestOnlyAddonsAddedHereMove();
    TestSettingsMoveOnlyIntoAnAccountWithNone();
    TestRowsGoInBoundedChunks();
    TestEverythingMovesAndADeadAddonIsNamed();
    TestAnUnreachableAccountStopsTheMove();
    TestALibraryFailureSendsNothingAfterIt();
    TestAnAccountSwitchStopsTheMoveAtTheNextRequest();
    TestRemovalsReachTheAccount();
    TestOlderDeviceRowsNeverReplaceNewerAccountRows();
    TestFailedAddonsDialogCopy();
    TestStartupDecisionTable();
    TestTopUpList();
    TestSelectorSwitches();
    TestAdoptionMovesEveryDownloadWithoutADuplicate();
    TestAdoptionKeepsTheFinishedCopyOfADuplicate();
    TestAdoptionDiscardsALosingDownloadsBackupWithIt();
    TestAdoptingIntoTheSamePartitionChangesNothing();
    TestOtherOwners();
    TestAdoptionOnATemporaryIndex();
    TestDownloadsUnderAnotherAccountKeyJoinTheActiveOne();
    TestAdoptionThatCannotWriteChangesNothing();
    TestClearRemovesTheProfileLast();
}
