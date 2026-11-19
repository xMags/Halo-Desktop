#include "Services/AddonRowPolicy.h"

#include "Services/AddonListPolicy.h"

#include <algorithm>
#include <cwctype>
#include <unordered_set>

namespace
{
    bool IsInitialsSeparator(wchar_t character) noexcept
    {
        return std::iswspace(character) != 0 || character == L'.' || character == L'_' || character == L'-';
    }

    wchar_t Upper(wchar_t character) noexcept
    {
        return static_cast<wchar_t>(std::towupper(character));
    }

    std::wstring Quoted(std::wstring_view name)
    {
        std::wstring result{ L"“" };
        result.append(name);
        result.append(L"”");
        return result;
    }
}

namespace HaloDesktop::Services
{
    bool IsBuiltInAddon(AddonScope scope, std::optional<std::wstring_view> transportUrl) noexcept
    {
        return scope == AddonScope::Yours && transportUrl && IsDefaultAddonUrl(*transportUrl);
    }

    double AddonActionsWidth(AddonRowActions const& actions) noexcept
    {
        constexpr double ButtonWidth = 28.0;
        constexpr double ButtonSpacing = 2.0;
        auto const buttons = (actions.ShowInstallForEveryone ? 1 : 0) + (actions.ShowRemove ? 1 : 0);
        if (buttons == 0)
        {
            return 0.0;
        }
        return buttons * ButtonWidth + (buttons - 1) * ButtonSpacing;
    }

    AddonRowActions EvaluateAddonRow(AddonRowFacts const& facts, AddonRowContext const& context) noexcept
    {
        AddonRowActions actions;
        auto const yours = facts.Scope == AddonScope::Yours;
        actions.Shadowed = yours && facts.ProvidedGlobally;
        actions.BuiltIn = IsBuiltInAddon(facts.Scope, facts.TransportUrl);
        actions.Editable = yours || context.IsAdmin;
        actions.Movable = actions.Editable && !actions.Shadowed && !context.ListBusy && context.ScopeListComplete;
        // A hidden addon comes back with a stripped manifest, so the flag is
        // the only sign the switch should still be drawn.
        actions.ShowCatalogSwitch = !actions.Shadowed && (facts.CatalogCount > 0 || facts.HideCatalogs);
        actions.CatalogSwitchEnabled = actions.Editable;
        actions.ShowInstallForEveryone = yours && context.IsAdmin && !actions.Shadowed;
        // Only your own additions can be removed: the built-ins are what Halo
        // needs to browse, and global ones are managed on the server, so
        // neither offers the button rather than a disabled one.
        actions.ShowRemove = yours && !actions.BuiltIn;
        actions.ListActionsEnabled = !context.ListBusy;
        actions.Grip = actions.Shadowed
            ? AddonGripHint::NotUsedWhileGlobal
            : actions.Editable ? AddonGripHint::DragToChangePriority : AddonGripHint::ManagedByServerAdmin;
        return actions;
    }

    std::wstring_view AddonGripToolTip(AddonGripHint hint) noexcept
    {
        switch (hint)
        {
        case AddonGripHint::NotUsedWhileGlobal:
            return L"Not used while the global copy is installed";
        case AddonGripHint::ManagedByServerAdmin:
            return L"Managed by the server admin";
        case AddonGripHint::DragToChangePriority:
        default:
            return L"Drag to change priority";
        }
    }

    std::wstring AddonProvidesLine(
        std::size_t catalogCount,
        std::span<std::wstring const> resources,
        std::wstring_view description)
    {
        std::wstring line;
        auto const append = [&line](std::wstring_view part)
        {
            if (part.empty())
            {
                return;
            }
            if (!line.empty())
            {
                line.append(L" · ");
            }
            line.append(part);
        };
        if (catalogCount > 0)
        {
            append(std::to_wstring(catalogCount) + L" catalogs");
        }
        for (auto const& resource : resources)
        {
            append(resource);
        }
        if (!line.empty())
        {
            return line;
        }
        if (!description.empty())
        {
            return std::wstring{ description };
        }
        return L"No declared resources";
    }

    std::wstring AddonInitials(std::wstring_view name)
    {
        std::vector<std::wstring_view> words;
        std::size_t start = 0;
        for (std::size_t index = 0; index <= name.size(); ++index)
        {
            if (index == name.size() || IsInitialsSeparator(name[index]))
            {
                if (index > start)
                {
                    words.push_back(name.substr(start, index - start));
                    if (words.size() == 2)
                    {
                        break;
                    }
                }
                start = index + 1;
            }
        }
        if (words.empty())
        {
            return L"?";
        }
        std::wstring result;
        if (words.size() == 1)
        {
            for (auto const character : words.front().substr(0, 2))
            {
                result.push_back(Upper(character));
            }
            return result;
        }
        result.push_back(Upper(words[0].front()));
        result.push_back(Upper(words[1].front()));
        return result;
    }

    std::wstring AddonCatalogSwitchLabel(std::wstring_view name)
    {
        std::wstring label{ L"Show " };
        label.append(name);
        label.append(L" catalogs on Home");
        return label;
    }

    std::wstring AddonListErrorLine(AddonListFailure failure, std::wstring_view name)
    {
        switch (failure)
        {
        case AddonListFailure::Remove:
            return Quoted(name) + L" could not be removed. Try again.";
        case AddonListFailure::Reorder:
            return L"The new addon order could not be saved. Try again.";
        case AddonListFailure::Catalogs:
            return Quoted(name) + L" catalogs could not be changed. Try again.";
        case AddonListFailure::InstallForEveryone:
            return Quoted(name) + L" could not be installed for every user. Try again.";
        case AddonListFailure::OwnCopyKept:
            return Quoted(name) + L" is now global, but your own copy could not be removed. Remove it below.";
        case AddonListFailure::GlobalListIncomplete:
        default:
            return L"The global addon list could not be read in full. Refresh and try again.";
        }
    }

    ConfirmationCopy RemoveAddonConfirmation(AddonScope scope, std::wstring_view name)
    {
        if (scope == AddonScope::Global)
        {
            return ConfirmationCopy{
                .Title = L"Remove for every user?",
                .Body = Quoted(name) + L" will be removed for everyone on this server.",
                .Primary = L"Remove",
                .Close = L"Cancel",
                .Destructive = true,
            };
        }
        return ConfirmationCopy{
            .Title = L"Remove addon?",
            .Body = Quoted(name) + L" will be removed from your addons.",
            .Primary = L"Remove",
            .Close = L"Cancel",
            .Destructive = true,
        };
    }

    ConfirmationCopy InstallForEveryoneConfirmation(std::wstring_view name)
    {
        return ConfirmationCopy{
            .Title = L"Install for every user?",
            .Body = Quoted(name)
                + L" will be added to every account on this server, and your own copy will be replaced by it.",
            .Primary = L"Install for everyone",
            .Close = L"Cancel",
            .Destructive = false,
        };
    }

    ConfirmationCopy SignOutConfirmation()
    {
        return ConfirmationCopy{
            .Title = L"Sign out?",
            .Body = L"You will need to sign in again to use this server.",
            .Primary = L"Sign out",
            .Close = L"Cancel",
            .Destructive = true,
        };
    }

    std::wstring TrimAddonUrl(std::wstring_view url)
    {
        auto const isSpace = [](wchar_t character) { return std::iswspace(character) != 0; };
        auto const first = std::find_if_not(url.begin(), url.end(), isSpace);
        auto const last = std::find_if_not(url.rbegin(), url.rend(), isSpace).base();
        return first < last ? std::wstring{ first, last } : std::wstring{};
    }

    bool CanSubmitAddon(AddFieldState const& state)
    {
        return !state.Adding && !state.ListBusy && state.ListLoaded && !TrimAddonUrl(state.Url).empty();
    }

    std::wstring_view AddButtonLabel(bool adding) noexcept
    {
        return adding ? std::wstring_view{ L"Adding…" } : std::wstring_view{ L"Add" };
    }

    bool IsAddonInstalled(
        std::span<std::optional<std::wstring> const> installedUrls,
        std::wstring_view url)
    {
        return std::any_of(installedUrls.begin(), installedUrls.end(), [url](std::optional<std::wstring> const& installed)
        {
            return installed && *installed == url;
        });
    }

    std::optional<std::vector<std::wstring>> ReorderedTransportUrls(
        std::span<AddonListEntry const> scopeEntries,
        std::span<std::wstring const> orderedIds)
    {
        if (orderedIds.size() != scopeEntries.size())
        {
            return std::nullopt;
        }
        std::unordered_set<std::wstring> seen;
        std::vector<std::wstring> urls;
        urls.reserve(orderedIds.size());
        for (auto const& id : orderedIds)
        {
            if (!seen.insert(id).second)
            {
                return std::nullopt;
            }
            auto const entry = std::find_if(scopeEntries.begin(), scopeEntries.end(), [&id](AddonListEntry const& candidate)
            {
                return candidate.Id == id;
            });
            if (entry == scopeEntries.end() || !entry->TransportUrl)
            {
                return std::nullopt;
            }
            urls.push_back(*entry->TransportUrl);
        }
        return urls;
    }

    std::optional<std::vector<std::wstring>> MoveAddonId(
        std::span<std::wstring const> ids,
        std::wstring_view id,
        std::int32_t offset)
    {
        auto const found = std::find(ids.begin(), ids.end(), id);
        if (found == ids.end() || offset == 0)
        {
            return std::nullopt;
        }
        auto const from = static_cast<std::int64_t>(found - ids.begin());
        auto const to = from + offset;
        if (to < 0 || to >= static_cast<std::int64_t>(ids.size()))
        {
            return std::nullopt;
        }
        std::vector<std::wstring> result{ ids.begin(), ids.end() };
        auto moved = std::move(result[static_cast<std::size_t>(from)]);
        result.erase(result.begin() + from);
        result.insert(result.begin() + to, std::move(moved));
        return result;
    }

    InstallForEveryonePlan PlanInstallForEveryone(
        std::span<AddonListEntry const> global,
        std::span<AddonListEntry const> own,
        std::wstring_view id,
        bool isAdmin)
    {
        InstallForEveryonePlan plan;
        auto const entry = std::find_if(own.begin(), own.end(), [id](AddonListEntry const& candidate)
        {
            return candidate.Id == id;
        });
        if (!isAdmin || entry == own.end() || !entry->TransportUrl)
        {
            return plan;
        }
        auto const& url = *entry->TransportUrl;
        if (std::any_of(global.begin(), global.end(), [&url](AddonListEntry const& candidate)
            {
                return candidate.TransportUrl && *candidate.TransportUrl == url;
            }))
        {
            plan.Check = InstallForEveryoneCheck::AlreadyGlobal;
            return plan;
        }
        // The server sends global URLs to admins; without one of them the
        // list cannot be sent back whole.
        for (auto const& candidate : global)
        {
            if (!candidate.TransportUrl)
            {
                plan.Check = InstallForEveryoneCheck::GlobalListIncomplete;
                plan.GlobalUrls.clear();
                return plan;
            }
            plan.GlobalUrls.push_back(*candidate.TransportUrl);
        }
        plan.GlobalUrls.push_back(url);
        plan.Check = InstallForEveryoneCheck::Proceed;
        // A built-in stays in the own list, where it cannot be removed,
        // shadowed by the global copy until that goes.
        if (IsBuiltInAddon(AddonScope::Yours, std::wstring_view{ url }))
        {
            return plan;
        }
        std::vector<std::wstring> ownAfter;
        for (auto const& candidate : own)
        {
            if (!candidate.TransportUrl)
            {
                plan.OwnListIncomplete = true;
                return plan;
            }
            if (*candidate.TransportUrl != url)
            {
                ownAfter.push_back(*candidate.TransportUrl);
            }
        }
        plan.OwnUrlsAfter = std::move(ownAfter);
        return plan;
    }

    std::optional<std::vector<std::wstring>> FirstLanguageChoices(
        std::span<std::vector<CatalogExtraDeclaration> const> catalogs)
    {
        for (auto const& extras : catalogs)
        {
            if (auto choices = LanguageChoices(extras); choices && !choices->empty())
            {
                return choices;
            }
        }
        return std::nullopt;
    }

    std::int32_t CatalogLanguageIndex(
        std::span<std::wstring const> choices,
        std::optional<std::wstring_view> current)
    {
        if (!current || current->empty())
        {
            return 0;
        }
        auto const found = std::find(choices.begin(), choices.end(), *current);
        return found == choices.end() ? 0 : static_cast<std::int32_t>(found - choices.begin()) + 1;
    }

    std::optional<std::wstring> CatalogLanguageForIndex(
        std::span<std::wstring const> choices,
        std::int32_t index)
    {
        if (index <= 0 || static_cast<std::size_t>(index) > choices.size())
        {
            return std::nullopt;
        }
        return choices[static_cast<std::size_t>(index) - 1];
    }
}
