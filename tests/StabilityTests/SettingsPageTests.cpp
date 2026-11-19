#include "SettingsPageTests.h"

#include "Services/AddonRowPolicy.h"
#include "Services/BuildInfoPolicy.h"
#include "Services/CatalogLanguagePolicy.h"

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
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

    namespace Services = HaloDesktop::Services;
    using Services::AddonRowContext;
    using Services::AddonRowFacts;
    using Services::AddonScope;

    constexpr std::wstring_view Cinemeta = L"https://v3-cinemeta.strem.io/manifest.json";
    constexpr std::wstring_view Torrentio = L"https://torrentio.example/manifest.json";

    // The card reserves the widest row's buttons on every row, so a card with
    // no buttons at all puts its switches at the edge.
    void TestAddonActionsWidth()
    {
        using Services::AddonActionsWidth;
        using Services::AddonRowActions;
        Require(AddonActionsWidth(AddonRowActions{}) == 0.0, "a row with no buttons reserved room for one");
        Require(AddonActionsWidth(AddonRowActions{ .ShowRemove = true }) == 28.0, "one button did not take one button's width");
        Require(AddonActionsWidth(AddonRowActions{ .ShowInstallForEveryone = true }) == 28.0, "install for everyone did not take one button's width");
        Require(AddonActionsWidth(AddonRowActions{ .ShowInstallForEveryone = true, .ShowRemove = true }) == 58.0, "two buttons did not take two widths and their gap");
    }

    void TestAddonRowActions()
    {
        using Services::AddonGripHint;
        using Services::EvaluateAddonRow;

        AddonRowFacts own{ .Scope = AddonScope::Yours, .TransportUrl = Torrentio, .CatalogCount = 2 };
        AddonRowContext user{ .IsAdmin = false, .ListBusy = false, .ScopeListComplete = true };
        auto actions = EvaluateAddonRow(own, user);
        Require(actions.ShowRemove && actions.ListActionsEnabled, "an own addon could not be removed");
        Require(!actions.ShowInstallForEveryone, "a non-admin was offered to install for every user");
        Require(actions.Editable && actions.Movable, "an own addon could not be moved");
        Require(actions.ShowCatalogSwitch && actions.CatalogSwitchEnabled, "an own catalog switch was missing");
        Require(actions.Grip == AddonGripHint::DragToChangePriority, "the own grip said the wrong thing");

        // Built-ins stay in your own list: no remove button at all.
        AddonRowFacts builtIn{ .Scope = AddonScope::Yours, .TransportUrl = Cinemeta, .CatalogCount = 3 };
        Require(!EvaluateAddonRow(builtIn, user).ShowRemove, "a built-in addon offered remove");
        Require(EvaluateAddonRow(builtIn, user).BuiltIn, "Cinemeta was not built in");
        Require(!Services::IsBuiltInAddon(AddonScope::Global, Cinemeta), "a global Cinemeta was called built in");
        Require(!Services::IsBuiltInAddon(AddonScope::Yours, std::nullopt), "an addon without a URL was called built in");

        // Global rows are managed on the server: no remove, even for an admin.
        AddonRowFacts global{ .Scope = AddonScope::Global, .CatalogCount = 1 };
        auto const globalAsUser = EvaluateAddonRow(global, user);
        Require(!globalAsUser.ShowRemove && !globalAsUser.ShowInstallForEveryone, "a global row offered an action");
        Require(!globalAsUser.Editable && !globalAsUser.Movable && !globalAsUser.CatalogSwitchEnabled,
            "a non-admin could change a global row");
        Require(globalAsUser.ShowCatalogSwitch, "a global row with catalogs drew no switch");
        Require(globalAsUser.Grip == AddonGripHint::ManagedByServerAdmin, "the global grip said the wrong thing");
        AddonRowContext admin{ .IsAdmin = true, .ListBusy = false, .ScopeListComplete = true };
        auto const globalAsAdmin = EvaluateAddonRow(global, admin);
        Require(!globalAsAdmin.ShowRemove, "an admin was offered to remove a global row");
        Require(globalAsAdmin.Editable && globalAsAdmin.Movable && globalAsAdmin.CatalogSwitchEnabled,
            "an admin could not change a global row");
        Require(globalAsAdmin.Grip == AddonGripHint::DragToChangePriority, "the admin grip said the wrong thing");

        Require(EvaluateAddonRow(own, admin).ShowInstallForEveryone, "an admin's own row offered no install for everyone");

        // A shadowed copy: greyed, no switch, no share, cannot move, can still be removed.
        AddonRowFacts shadowed{ .Scope = AddonScope::Yours, .TransportUrl = Torrentio, .ProvidedGlobally = true, .CatalogCount = 2 };
        auto const shadowedActions = EvaluateAddonRow(shadowed, admin);
        Require(shadowedActions.Shadowed, "a copy the global entry answers for was not shadowed");
        Require(!shadowedActions.ShowCatalogSwitch && !shadowedActions.ShowInstallForEveryone && !shadowedActions.Movable,
            "a shadowed copy kept its switch, share or order");
        Require(shadowedActions.ShowRemove, "a shadowed copy could not be removed");
        Require(shadowedActions.Grip == AddonGripHint::NotUsedWhileGlobal, "the shadowed grip said the wrong thing");
        AddonRowFacts globalFlagged{ .Scope = AddonScope::Global, .ProvidedGlobally = true, .CatalogCount = 1 };
        Require(!EvaluateAddonRow(globalFlagged, admin).Shadowed, "a global row was shadowed by itself");

        // One save at a time.
        AddonRowContext busy{ .IsAdmin = true, .ListBusy = true, .ScopeListComplete = true };
        auto const busyActions = EvaluateAddonRow(own, busy);
        Require(!busyActions.Movable && !busyActions.ListActionsEnabled, "a row could act while a save ran");
        Require(busyActions.ShowRemove, "the remove button vanished while a save ran instead of disabling");
        AddonRowContext incomplete{ .IsAdmin = false, .ListBusy = false, .ScopeListComplete = false };
        Require(!EvaluateAddonRow(own, incomplete).Movable, "a list without every URL could be reordered");

        // The switch shows for catalogs, or for a hidden addon whose manifest came back stripped.
        AddonRowFacts noCatalogs{ .Scope = AddonScope::Yours, .TransportUrl = Torrentio };
        Require(!EvaluateAddonRow(noCatalogs, user).ShowCatalogSwitch, "an addon without catalogs drew a switch");
        AddonRowFacts hidden{ .Scope = AddonScope::Yours, .TransportUrl = Torrentio, .HideCatalogs = true };
        Require(EvaluateAddonRow(hidden, user).ShowCatalogSwitch, "a hidden addon lost its switch");

        Require(Services::AddonGripToolTip(AddonGripHint::DragToChangePriority) == L"Drag to change priority"
                && Services::AddonGripToolTip(AddonGripHint::ManagedByServerAdmin) == L"Managed by the server admin"
                && Services::AddonGripToolTip(AddonGripHint::NotUsedWhileGlobal) == L"Not used while the global copy is installed",
            "a grip tooltip changed");
    }

    void TestAddonTextLines()
    {
        std::vector<std::wstring> const resources{ L"catalog", L"meta" };
        Require(Services::AddonProvidesLine(3, resources, L"ignored") == L"3 catalogs · catalog · meta",
            "the provides line lost its catalogs or resources");
        std::vector<std::wstring> const stream{ L"stream" };
        Require(Services::AddonProvidesLine(0, stream, {}) == L"stream", "a resource-only addon was described wrongly");
        Require(Services::AddonProvidesLine(0, {}, L"Streams from torrents") == L"Streams from torrents",
            "the description did not stand in");
        Require(Services::AddonProvidesLine(0, {}, {}) == L"No declared resources", "the fallback line changed");

        Require(Services::AddonInitials(L"Cinemeta") == L"CI", "a one-word name did not give its first two letters");
        Require(Services::AddonInitials(L"The Movie Database") == L"TM", "two words did not give two initials");
        Require(Services::AddonInitials(L"open-subtitles") == L"OS", "a hyphen did not split words");
        Require(Services::AddonInitials(L"torrentio.lite") == L"TL", "a dot did not split words");
        Require(Services::AddonInitials(L"x") == L"X", "a one-letter name failed");
        Require(Services::AddonInitials(L"") == L"?" && Services::AddonInitials(L" _ ") == L"?", "a wordless name was not ?");

        Require(Services::AddonCatalogSwitchLabel(L"Cinemeta") == L"Show Cinemeta catalogs on Home", "the switch label changed");
        Require(Services::ShadowedAddonLine == L"Provided globally, so this copy is not used", "the shadowed line changed");

        using Services::AddonListErrorLine;
        using Services::AddonListFailure;
        Require(AddonListErrorLine(AddonListFailure::Remove, L"Torrentio") == L"“Torrentio” could not be removed. Try again.",
            "the remove failure line changed");
        Require(AddonListErrorLine(AddonListFailure::Reorder, {}) == L"The new addon order could not be saved. Try again.",
            "the reorder failure line changed");
        Require(AddonListErrorLine(AddonListFailure::Catalogs, L"TMDB") == L"“TMDB” catalogs could not be changed. Try again.",
            "the catalogs failure line changed");
        Require(AddonListErrorLine(AddonListFailure::InstallForEveryone, L"TMDB") == L"“TMDB” could not be installed for every user. Try again.",
            "the install failure line changed");
        Require(AddonListErrorLine(AddonListFailure::OwnCopyKept, L"TMDB")
                == L"“TMDB” is now global, but your own copy could not be removed. Remove it below.",
            "the own-copy line changed");
        Require(AddonListErrorLine(AddonListFailure::GlobalListIncomplete, {})
                == L"The global addon list could not be read in full. Refresh and try again.",
            "the incomplete-list line changed");

        auto const removeOwn = Services::RemoveAddonConfirmation(AddonScope::Yours, L"Torrentio");
        Require(removeOwn.Title == L"Remove addon?" && removeOwn.Body == L"“Torrentio” will be removed from your addons."
                && removeOwn.Primary == L"Remove" && removeOwn.Close == L"Cancel" && removeOwn.Destructive,
            "the remove dialog changed");
        auto const removeGlobal = Services::RemoveAddonConfirmation(AddonScope::Global, L"Torrentio");
        Require(removeGlobal.Title == L"Remove for every user?"
                && removeGlobal.Body == L"“Torrentio” will be removed for everyone on this server." && removeGlobal.Destructive,
            "the global remove dialog changed");
        auto const share = Services::InstallForEveryoneConfirmation(L"Torrentio");
        Require(share.Title == L"Install for every user?"
                && share.Body == L"“Torrentio” will be added to every account on this server, and your own copy will be replaced by it."
                && share.Primary == L"Install for everyone" && share.Close == L"Cancel" && !share.Destructive,
            "the install dialog changed");
        auto const signOut = Services::SignOutConfirmation();
        Require(signOut.Title == L"Sign out?" && signOut.Body == L"You will need to sign in again to use this server."
                && signOut.Primary == L"Sign out" && signOut.Close == L"Cancel" && signOut.Destructive,
            "the sign-out dialog changed");
    }

    void TestAddField()
    {
        using Services::AddFieldState;
        using Services::CanSubmitAddon;
        Require(CanSubmitAddon(AddFieldState{ .ListLoaded = true, .Url = L"https://a.example/manifest.json" }),
            "a loaded list refused a URL");
        Require(!CanSubmitAddon(AddFieldState{ .ListLoaded = false, .Url = L"https://a.example/manifest.json" }),
            "an addon could be added before the list loaded");
        Require(!CanSubmitAddon(AddFieldState{ .Adding = true, .ListLoaded = true, .Url = L"https://a.example" }),
            "an addon could be added twice at once");
        Require(!CanSubmitAddon(AddFieldState{ .ListBusy = true, .ListLoaded = true, .Url = L"https://a.example" }),
            "an addon could be added while another save ran");
        Require(!CanSubmitAddon(AddFieldState{ .ListLoaded = true, .Url = L"   " }), "a blank URL could be added");
        Require(Services::AddButtonLabel(false) == L"Add" && Services::AddButtonLabel(true) == L"Adding…",
            "the add button label changed");
        Require(Services::TrimAddonUrl(L"  https://a.example/m.json \t") == L"https://a.example/m.json", "the URL was not trimmed");
        Require(Services::TrimAddonUrl(L"   ").empty(), "a blank URL was not empty");

        std::vector<std::optional<std::wstring>> const installed{ std::nullopt, std::wstring{ Cinemeta } };
        Require(Services::IsAddonInstalled(installed, Cinemeta), "an installed URL was not found");
        Require(!Services::IsAddonInstalled(installed, Torrentio), "a new URL was called installed");
        Require(Services::AlreadyInstalledLine == L"Already installed.", "the already-installed line changed");
    }

    void TestReordering()
    {
        using Services::AddonListEntry;
        std::vector<AddonListEntry> const own{
            { L"a", L"https://a/manifest.json" },
            { L"b", L"https://b/manifest.json" },
            { L"c", L"https://c/manifest.json" },
        };
        std::vector<std::wstring> const moved{ L"c", L"a", L"b" };
        auto const urls = Services::ReorderedTransportUrls(own, moved);
        Require(urls && *urls == std::vector<std::wstring>{ L"https://c/manifest.json", L"https://a/manifest.json", L"https://b/manifest.json" },
            "a reorder did not send the list in its new order");
        std::vector<std::wstring> const duplicate{ L"a", L"a", L"b" };
        Require(!Services::ReorderedTransportUrls(own, duplicate), "a row sent twice was accepted");
        std::vector<std::wstring> const missing{ L"a", L"b" };
        Require(!Services::ReorderedTransportUrls(own, missing), "a reorder that drops a row was accepted");
        std::vector<std::wstring> const stranger{ L"a", L"b", L"z" };
        Require(!Services::ReorderedTransportUrls(own, stranger), "a row from elsewhere was accepted");
        std::vector<AddonListEntry> const redacted{ { L"a", std::nullopt }, { L"b", L"https://b/manifest.json" } };
        std::vector<std::wstring> const both{ L"b", L"a" };
        Require(!Services::ReorderedTransportUrls(redacted, both), "a list without every URL was sent");

        std::vector<std::wstring> const ids{ L"a", L"b", L"c" };
        Require(Services::MoveAddonId(ids, L"b", -1) == std::optional<std::vector<std::wstring>>{ { L"b", L"a", L"c" } },
            "move up failed");
        Require(Services::MoveAddonId(ids, L"b", 1) == std::optional<std::vector<std::wstring>>{ { L"a", L"c", L"b" } },
            "move down failed");
        Require(!Services::MoveAddonId(ids, L"a", -1) && !Services::MoveAddonId(ids, L"c", 1),
            "a row moved off the end of its list");
        Require(!Services::MoveAddonId(ids, L"z", 1) && !Services::MoveAddonId(ids, L"a", 0), "a missing row or no move moved");
    }

    void TestInstallForEveryone()
    {
        using Services::AddonListEntry;
        using Services::InstallForEveryoneCheck;
        using Services::PlanInstallForEveryone;
        std::vector<AddonListEntry> const global{ { L"g1", L"https://g/manifest.json" } };
        std::vector<AddonListEntry> const own{
            { L"o1", std::wstring{ Cinemeta } },
            { L"o2", std::wstring{ Torrentio } },
        };

        auto const plan = PlanInstallForEveryone(global, own, L"o2", true);
        Require(plan.Check == InstallForEveryoneCheck::Proceed, "an admin could not share an own addon");
        Require(plan.GlobalUrls == std::vector<std::wstring>{ L"https://g/manifest.json", std::wstring{ Torrentio } },
            "the shared addon did not join the end of the global list");
        Require(plan.OwnUrlsAfter == std::optional<std::vector<std::wstring>>{ { std::wstring{ Cinemeta } } },
            "the own copy was not removed afterwards");
        Require(!plan.OwnListIncomplete, "a complete own list was called incomplete");

        auto const builtIn = PlanInstallForEveryone(global, own, L"o1", true);
        Require(builtIn.Check == InstallForEveryoneCheck::Proceed && !builtIn.OwnUrlsAfter,
            "a built-in left the own list when shared");

        Require(PlanInstallForEveryone(global, own, L"o2", false).Check == InstallForEveryoneCheck::NotAllowed,
            "a non-admin could share");
        Require(PlanInstallForEveryone(global, own, L"g1", true).Check == InstallForEveryoneCheck::NotAllowed,
            "a global row could be shared again");

        std::vector<AddonListEntry> const sharedAlready{ { L"g1", std::wstring{ Torrentio } } };
        Require(PlanInstallForEveryone(sharedAlready, own, L"o2", true).Check == InstallForEveryoneCheck::AlreadyGlobal,
            "an addon already global was shared twice");

        std::vector<AddonListEntry> const redactedGlobal{ { L"g1", std::nullopt } };
        auto const incomplete = PlanInstallForEveryone(redactedGlobal, own, L"o2", true);
        Require(incomplete.Check == InstallForEveryoneCheck::GlobalListIncomplete && incomplete.GlobalUrls.empty(),
            "a global list without its URLs was sent");

        std::vector<AddonListEntry> const redactedOwn{ { L"o1", std::nullopt }, { L"o2", std::wstring{ Torrentio } } };
        auto const ownIncomplete = PlanInstallForEveryone(global, redactedOwn, L"o2", true);
        Require(ownIncomplete.Check == InstallForEveryoneCheck::Proceed && ownIncomplete.OwnListIncomplete && !ownIncomplete.OwnUrlsAfter,
            "an own list without every URL was sent after sharing");
    }

    void TestCatalogLanguageRow()
    {
        using Services::CatalogExtraDeclaration;
        std::vector<std::wstring> const languages{ L"English", L"Hindi", L"Tamil", L"Telugu", L"French", L"German", L"Japanese" };
        std::vector<std::vector<CatalogExtraDeclaration>> const catalogs{
            { { L"genre", false, { L"Action", L"Drama", L"Comedy" } }, { L"skip", false, {} } },
            { { L"genre", false, { L"2026", L"2025" } } },
            { { L"genre", false, languages } },
        };
        auto const choices = Services::FirstLanguageChoices(catalogs);
        Require(choices && *choices == languages, "the language catalog's options were not offered");
        std::vector<std::vector<CatalogExtraDeclaration>> const none{ { { L"genre", false, { L"Action" } } } };
        Require(!Services::FirstLanguageChoices(none), "a genre list was offered as languages");
        Require(!Services::FirstLanguageChoices({}), "no catalogs offered languages");

        Require(Services::CatalogLanguageIndex(languages, std::nullopt) == 0, "an unset language was not the addon default");
        Require(Services::CatalogLanguageIndex(languages, L"Tamil") == 3, "a chosen language was not selected");
        Require(Services::CatalogLanguageIndex(languages, L"Klingon") == 0, "a language the row lacks was selected");
        Require(!Services::CatalogLanguageForIndex(languages, 0), "Addon default stored a language");
        Require(Services::CatalogLanguageForIndex(languages, 1) == std::optional<std::wstring>{ L"English" },
            "the first language was not stored");
        Require(!Services::CatalogLanguageForIndex(languages, 99) && !Services::CatalogLanguageForIndex(languages, -1),
            "an index past the list stored a language");
        Require(Services::AddonDefaultLanguageLabel == L"Addon default", "the default label changed");
    }

    std::vector<std::uint8_t> VersionBlock(std::uint32_t most, std::uint32_t least)
    {
        std::vector<std::uint8_t> block(40, 0x20);
        auto const put = [&block](std::uint32_t value)
        {
            for (int shift = 0; shift < 32; shift += 8)
            {
                block.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
            }
        };
        put(0xFEEF04BDu);
        put(0x00010000u);
        put(most);
        put(least);
        put(0);
        put(0);
        return block;
    }

    void TestBuildInfo()
    {
        auto const release = VersionBlock(0x00010002u, 0x00030000u);
        Require(Services::ParseFixedFileVersion(release) == std::optional<std::wstring>{ L"1.2.3" },
            "the app version was not read from the resource");
        auto const revised = VersionBlock(0x00010002u, 0x00030004u);
        Require(Services::ParseFixedFileVersion(revised) == std::optional<std::wstring>{ L"1.2.3.4" },
            "a revision was dropped");
        std::vector<std::uint8_t> const empty(64, 0);
        Require(!Services::ParseFixedFileVersion(empty), "a block without fixed info gave a version");
        auto truncated = VersionBlock(0x00010002u, 0x00030000u);
        truncated.resize(48);
        Require(!Services::ParseFixedFileVersion(truncated), "a cut-off record gave a version");

        Require(Services::MpvRelease(std::string_view{ "mpv v0.41.0-878-g94335ab87" }) == std::optional<std::wstring>{ L"0.41.0" },
            "the mpv release was not taken from its version string");
        Require(Services::MpvRelease(std::string_view{ "mpv 0.39.0" }) == std::optional<std::wstring>{ L"0.39.0" },
            "a plain release was not read");
        Require(Services::MpvRelease(std::string_view{ "mpv git-abc123" }) == std::optional<std::wstring>{ L"git-abc123" },
            "an unusual build lost its name");
        Require(Services::MpvRelease(std::string_view{ "v1.2" }) == std::optional<std::wstring>{ L"v1.2" },
            "an unrecognisable string was not passed through");
        Require(!Services::MpvRelease(std::nullopt) && !Services::MpvRelease(std::string_view{}), "no version gave one");

        Require(Services::BuildInfoLine({}) == L"v… · mpv …", "the loading line changed");
        Require(Services::BuildInfoLine({ .Loaded = true, .App = L"1.0.0", .Mpv = L"0.41.0" }) == L"v1.0.0 · mpv 0.41.0",
            "the version line changed");
        Require(Services::BuildInfoLine({ .Loaded = true, .App = L"1.0.0" }) == L"v1.0.0 · mpv unknown",
            "an unreadable mpv version was not said");
    }
}

void RunSettingsPageTests()
{
    TestAddonRowActions();
    TestAddonActionsWidth();
    TestAddonTextLines();
    TestAddField();
    TestReordering();
    TestInstallForEveryone();
    TestCatalogLanguageRow();
    TestBuildInfo();
}
