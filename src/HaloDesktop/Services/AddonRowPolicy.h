#pragma once

#include "Services/CatalogLanguagePolicy.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace HaloDesktop::Services
{
    // The rules of the Settings addon list, as the reference client's
    // `settings/Settings.tsx` draws and edits it. Pure, so the tests include it.

    enum class AddonScope
    {
        Global,
        Yours,
    };

    // What a row is made from.
    struct AddonRowFacts final
    {
        AddonScope Scope{ AddonScope::Yours };
        std::optional<std::wstring_view> TransportUrl;
        // The administrator installs this same addon for everyone, so the
        // server answers from the global copy and skips this one.
        bool ProvidedGlobally{};
        bool HideCatalogs{};
        std::size_t CatalogCount{};
    };

    struct AddonRowContext final
    {
        bool IsAdmin{};
        // A whole-list save is in flight; only one may run at a time.
        bool ListBusy{};
        // Every row of this row's scope carries its transport URL, so the
        // scope's list can be sent back whole.
        bool ScopeListComplete{};
    };

    enum class AddonGripHint
    {
        DragToChangePriority,
        ManagedByServerAdmin,
        NotUsedWhileGlobal,
    };

    struct AddonRowActions final
    {
        bool Shadowed{};
        bool BuiltIn{};
        // The row's scope is the account's own, or the caller administers the
        // server: its catalog switch can be used.
        bool Editable{};
        // The row can be dragged to a new place within its scope now.
        bool Movable{};
        bool ShowCatalogSwitch{};
        bool CatalogSwitchEnabled{};
        bool ShowInstallForEveryone{};
        bool ShowRemove{};
        // The row's list buttons (install for everyone, remove) can be pressed now.
        bool ListActionsEnabled{};
        AddonGripHint Grip{ AddonGripHint::DragToChangePriority };
    };

    // Cinemeta, OpenSubtitles and The Movie Database in the account's own list:
    // what Halo needs to browse at all, so they cannot be removed there.
    [[nodiscard]] bool IsBuiltInAddon(AddonScope scope, std::optional<std::wstring_view> transportUrl) noexcept;

    [[nodiscard]] AddonRowActions EvaluateAddonRow(AddonRowFacts const& facts, AddonRowContext const& context) noexcept;

    // The width a row's buttons (install for everyone, remove) take at its end:
    // 28 each, 2 apart, 0 with none. The addon card gives every row the widest
    // any row needs, so the catalog switches line up down the card and sit at
    // its edge when no row has a button.
    [[nodiscard]] double AddonActionsWidth(AddonRowActions const& actions) noexcept;

    [[nodiscard]] std::wstring_view AddonGripToolTip(AddonGripHint hint) noexcept;

    inline constexpr std::wstring_view ShadowedAddonLine = L"Provided globally, so this copy is not used";

    // `{n} catalogs · resource · resource`, else the manifest description,
    // else "No declared resources".
    [[nodiscard]] std::wstring AddonProvidesLine(
        std::size_t catalogCount,
        std::span<std::wstring const> resources,
        std::wstring_view description);

    // Two letters for the tile: the first two of a single word, else the first
    // letter of the first two words; "?" for a name with no word in it.
    [[nodiscard]] std::wstring AddonInitials(std::wstring_view name);

    [[nodiscard]] std::wstring AddonCatalogSwitchLabel(std::wstring_view name);

    enum class AddonListFailure
    {
        Remove,
        Reorder,
        Catalogs,
        InstallForEveryone,
        OwnCopyKept,
        GlobalListIncomplete,
    };

    [[nodiscard]] std::wstring AddonListErrorLine(AddonListFailure failure, std::wstring_view name);

    struct ConfirmationCopy final
    {
        std::wstring Title;
        std::wstring Body;
        std::wstring Primary;
        std::wstring Close;
        bool Destructive{};
    };

    [[nodiscard]] ConfirmationCopy RemoveAddonConfirmation(AddonScope scope, std::wstring_view name);
    [[nodiscard]] ConfirmationCopy InstallForEveryoneConfirmation(std::wstring_view name);
    [[nodiscard]] ConfirmationCopy SignOutConfirmation();

    // The add field.
    inline constexpr std::wstring_view AlreadyInstalledLine = L"Already installed.";
    inline constexpr std::wstring_view InvalidManifestLine = L"Invalid manifest URL";

    [[nodiscard]] std::wstring TrimAddonUrl(std::wstring_view url);

    struct AddFieldState final
    {
        bool Adding{};
        bool ListBusy{};
        // The account's list has been read: a save sends the whole list, so
        // one sent before it would leave only the new addon.
        bool ListLoaded{};
        std::wstring_view Url;
    };

    [[nodiscard]] bool CanSubmitAddon(AddFieldState const& state);
    [[nodiscard]] std::wstring_view AddButtonLabel(bool adding) noexcept;

    // Exactly this URL is already in either list.
    [[nodiscard]] bool IsAddonInstalled(
        std::span<std::optional<std::wstring> const> installedUrls,
        std::wstring_view url);

    struct AddonListEntry final
    {
        std::wstring Id;
        std::optional<std::wstring> TransportUrl;
    };

    // The scope's transport URLs in the given id order. Null unless the ids
    // are exactly the scope's ids, each once, and every row has its URL.
    [[nodiscard]] std::optional<std::vector<std::wstring>> ReorderedTransportUrls(
        std::span<AddonListEntry const> scopeEntries,
        std::span<std::wstring const> orderedIds);

    // The ids with `id` moved by `offset` places. Null when it is not there or
    // would leave the list.
    [[nodiscard]] std::optional<std::vector<std::wstring>> MoveAddonId(
        std::span<std::wstring const> ids,
        std::wstring_view id,
        std::int32_t offset);

    enum class InstallForEveryoneCheck
    {
        Proceed,
        // Not an admin, not one of the caller's own rows, or one without a URL.
        NotAllowed,
        // The global list already has this transport URL.
        AlreadyGlobal,
        // A global row came without its URL, so the list cannot be sent whole.
        GlobalListIncomplete,
    };

    // An admin shares one of their own addons: it joins the end of the global
    // list, then leaves their own list unless it is built in.
    struct InstallForEveryonePlan final
    {
        InstallForEveryoneCheck Check{ InstallForEveryoneCheck::NotAllowed };
        std::vector<std::wstring> GlobalUrls;
        // The own list to send afterwards; null when it is left alone.
        std::optional<std::vector<std::wstring>> OwnUrlsAfter;
        // The own copy should go but the own list cannot be sent whole.
        bool OwnListIncomplete{};
    };

    [[nodiscard]] InstallForEveryonePlan PlanInstallForEveryone(
        std::span<AddonListEntry const> global,
        std::span<AddonListEntry const> own,
        std::wstring_view id,
        bool isAdmin);

    // Settings' "Language rows on Home": the first catalog, in list order,
    // that offers language choices decides them.
    [[nodiscard]] std::optional<std::vector<std::wstring>> FirstLanguageChoices(
        std::span<std::vector<CatalogExtraDeclaration> const> catalogs);

    inline constexpr std::wstring_view AddonDefaultLanguageLabel = L"Addon default";

    // Index into "Addon default" followed by the choices. A value the choices
    // do not hold is the addon default, since Home would not send it either.
    [[nodiscard]] std::int32_t CatalogLanguageIndex(
        std::span<std::wstring const> choices,
        std::optional<std::wstring_view> current);

    // The value an index stands for; null for "Addon default" or out of range.
    [[nodiscard]] std::optional<std::wstring> CatalogLanguageForIndex(
        std::span<std::wstring const> choices,
        std::int32_t index);
}
