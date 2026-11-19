#include "ShellInputTests.h"

#include "Controls/ShelfScrollPolicy.h"
#include "Shell/PointerFocusPolicy.h"
#include "Shell/WindowBackdropPolicy.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
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

    void Require(bool condition, std::string const& message)
    {
        if (!condition)
        {
            throw std::runtime_error{ message };
        }
    }

    using HaloDesktop::Shell::ContentPressFacts;
    using HaloDesktop::Shell::FocusMoveFacts;

    void TestContentPressPlan()
    {
        using HaloDesktop::Shell::PlanContentPress;

        auto const intoBox = PlanContentPress({ .LandedInTextInput = true, .CaretInTextInput = true });
        Require(!intoBox.GuardTextFocus && !intoBox.ParkCaret, "a press into a text box was guarded against itself");

        auto const background = PlanContentPress({ .CaretInTextInput = true });
        Require(background.GuardTextFocus, "a press on empty content did not guard text focus");
        Require(background.ParkCaret, "a press on empty content left the caret in its box");

        auto const noCaret = PlanContentPress({});
        Require(noCaret.GuardTextFocus, "a press with no caret did not guard text focus");
        Require(!noCaret.ParkCaret, "a press with no caret moved focus to the sink");

        // A clear button or a suggestion row beside a box keeps the caret in it.
        auto const keeper = PlanContentPress({ .LandedOnFocusKeepingControl = true, .CaretInTextInput = true });
        Require(keeper.GuardTextFocus, "a press on a focus-keeping control did not guard text focus");
        Require(!keeper.ParkCaret, "a press on a focus-keeping control took the caret out of its box");
    }

    FocusMoveFacts PointerMoveIntoPageBox()
    {
        return FocusMoveFacts{
            .GuardActive = true,
            .ByPointer = true,
            .TargetIsTextInput = true,
            .TargetInShellContent = true,
            .TargetAlreadyFocused = false,
        };
    }

    void TestPointerFocusRefusal()
    {
        using HaloDesktop::Shell::ShouldRefusePointerFocus;

        // The bug: a press on background, and XAML settling focus on the page's
        // first tab stop, whichever page and whichever kind of text box it is.
        Require(ShouldRefusePointerFocus(PointerMoveIntoPageBox()), "pointer focus into a page's text box was allowed after a press that missed it");

        auto noPress = PointerMoveIntoPageBox();
        noPress.GuardActive = false;
        Require(!ShouldRefusePointerFocus(noPress), "a click straight into a text box was refused");

        auto keyboard = PointerMoveIntoPageBox();
        keyboard.ByPointer = false;
        Require(!ShouldRefusePointerFocus(keyboard), "Tab or programmatic focus into a text box was refused");

        auto button = PointerMoveIntoPageBox();
        button.TargetIsTextInput = false;
        Require(!ShouldRefusePointerFocus(button), "pointer focus onto a button was refused");

        auto dialog = PointerMoveIntoPageBox();
        dialog.TargetInShellContent = false;
        Require(!ShouldRefusePointerFocus(dialog), "a text box in a dialog, flyout or sheet was refused focus");

        auto same = PointerMoveIntoPageBox();
        same.TargetAlreadyFocused = true;
        Require(!ShouldRefusePointerFocus(same), "a box that already had the caret was refused it");
    }

    void TestSidewaysWheelRule()
    {
        using HaloDesktop::Controls::IsSidewaysWheel;
        Require(!IsSidewaysWheel(false, false), "a plain wheel was taken from the page");
        Require(IsSidewaysWheel(true, false), "a tilt wheel was left to pan a strip");
        Require(IsSidewaysWheel(false, true), "Shift with the wheel was left to pan a strip");
        Require(IsSidewaysWheel(true, true), "a shifted tilt wheel was left to pan a strip");
    }

    void TestWindowBackdrop()
    {
        using HaloDesktop::Shell::ResolveWindowBackdrop;
        using HaloDesktop::Shell::WindowBackdropInput;

        WindowBackdropInput const usual{ .PreferenceOn = true, .AcrylicSupported = true, .WindowActive = true, .PlayerShowing = false };
        auto const live = ResolveWindowBackdrop(usual);
        Require(live.UseAcrylic && !live.PaintGround && live.BlurLive, "an active translucent window did not draw its blur over a see-through ground");

        auto inactive = usual;
        inactive.WindowActive = false;
        auto const resting = ResolveWindowBackdrop(inactive);
        Require(resting.UseAcrylic && !resting.PaintGround && !resting.BlurLive, "an inactive window kept its blur live, or dropped the backdrop it rests on");

        auto player = usual;
        player.PlayerShowing = true;
        auto const playing = ResolveWindowBackdrop(player);
        Require(playing.UseAcrylic && !playing.BlurLive, "the blur stayed live under the player");

        auto off = usual;
        off.PreferenceOn = false;
        auto const opaque = ResolveWindowBackdrop(off);
        Require(!opaque.UseAcrylic && opaque.PaintGround && !opaque.BlurLive, "turning translucency off did not give the opaque ground back");

        auto unsupported = usual;
        unsupported.AcrylicSupported = false;
        auto const plain = ResolveWindowBackdrop(unsupported);
        Require(!plain.UseAcrylic && plain.PaintGround, "a window Windows cannot blur was left without its ground");

        for (auto const light : { false, true })
        {
            auto const strength = HaloDesktop::Shell::AcrylicStrengthFor(light);
            Require(strength.TintOpacity >= 0.5f && strength.TintOpacity <= 1.0f, "the palette tint was too weak to keep the window's colour");
            Require(strength.LuminosityOpacity >= 0.5f && strength.LuminosityOpacity <= 1.0f, "the backdrop's luminosity was too weak to keep text readable");
        }
    }

    // ---- XAML scan -------------------------------------------------------

    std::filesystem::path ExecutableDirectory()
    {
        std::wstring buffer(MAX_PATH, L'\0');
        while (true)
        {
            auto const length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0)
            {
                return {};
            }
            if (length < buffer.size())
            {
                buffer.resize(length);
                return std::filesystem::path{ buffer }.parent_path();
            }
            buffer.resize(buffer.size() * 2);
        }
    }

    std::optional<std::filesystem::path> FindAppSource()
    {
        std::error_code ignored;
        for (auto start : { std::filesystem::current_path(ignored), ExecutableDirectory() })
        {
            for (auto current = start; !current.empty(); current = current.parent_path())
            {
                auto const candidate = current / L"src" / L"HaloDesktop";
                if (std::filesystem::is_regular_file(candidate / L"HaloDesktop.vcxproj", ignored))
                {
                    return candidate;
                }
                if (current == current.root_path())
                {
                    break;
                }
            }
        }
        return std::nullopt;
    }

    std::string ReadText(std::filesystem::path const& path)
    {
        std::ifstream input{ path, std::ios::binary };
        if (!input)
        {
            throw std::runtime_error{ "a XAML file could not be read: " + path.string() };
        }
        return std::string{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
    }

    // One start tag: its name and its raw attribute text.
    struct StartTag final
    {
        std::string Name;
        std::string Attributes;
        std::size_t End{};
    };

    // Reads the start tag opening at `open` (a '<'), honouring quoted values.
    std::optional<StartTag> ReadStartTag(std::string const& xaml, std::size_t open)
    {
        auto position = open + 1;
        auto const nameEnd = xaml.find_first_of(" \t\r\n/>", position);
        if (nameEnd == std::string::npos)
        {
            return std::nullopt;
        }
        StartTag tag;
        tag.Name = xaml.substr(position, nameEnd - position);
        char quote = 0;
        for (position = nameEnd; position < xaml.size(); ++position)
        {
            auto const character = xaml[position];
            if (quote != 0)
            {
                if (character == quote)
                {
                    quote = 0;
                }
                continue;
            }
            if (character == '"' || character == '\'')
            {
                quote = character;
                continue;
            }
            if (character == '>')
            {
                tag.Attributes = xaml.substr(nameEnd, position - nameEnd);
                tag.End = position + 1;
                return tag;
            }
        }
        return std::nullopt;
    }

    std::optional<std::string> Attribute(std::string const& attributes, std::string_view name)
    {
        std::size_t position = 0;
        while ((position = attributes.find(name, position)) != std::string::npos)
        {
            auto const before = position == 0 ? ' ' : attributes[position - 1];
            auto after = position + name.size();
            auto const boundary = before == ' ' || before == '\t' || before == '\r' || before == '\n';
            while (after < attributes.size() && (attributes[after] == ' ' || attributes[after] == '\t'))
            {
                ++after;
            }
            if (boundary && after < attributes.size() && attributes[after] == '=')
            {
                auto const open = attributes.find_first_of("\"'", after);
                if (open == std::string::npos)
                {
                    return std::nullopt;
                }
                auto const close = attributes.find(attributes[open], open + 1);
                if (close == std::string::npos)
                {
                    return std::nullopt;
                }
                return attributes.substr(open + 1, close - open - 1);
            }
            position += name.size();
        }
        return std::nullopt;
    }

    // The first element inside the tag ending at `from`, skipping comments.
    std::optional<StartTag> FirstChildElement(std::string const& xaml, std::size_t from)
    {
        auto position = from;
        while (true)
        {
            position = xaml.find('<', position);
            if (position == std::string::npos)
            {
                return std::nullopt;
            }
            if (xaml.compare(position, 4, "<!--") == 0)
            {
                auto const close = xaml.find("-->", position + 4);
                if (close == std::string::npos)
                {
                    return std::nullopt;
                }
                position = close + 3;
                continue;
            }
            if (xaml.compare(position, 2, "</") == 0)
            {
                return std::nullopt;
            }
            return ReadStartTag(xaml, position);
        }
    }

    // Strips allowed to scroll sideways without the guard, each with its reason.
    struct AllowedStrip final
    {
        std::wstring_view File;
        std::string_view Name;
        std::string_view Reason;
    };

    constexpr std::array AllowedStrips{
        AllowedStrip{ L"LoginPage.xaml", "Stage", "the sign-in stage scrolls both ways so the card stays reachable on a small window" },
    };

    void TestSidewaysStripsCarryTheWheelGuard()
    {
        auto const root = FindAppSource();
        Require(root.has_value(), "the app's source folder (src/HaloDesktop) was not found from the test's folder");

        std::size_t guardedStrips = 0;
        std::size_t scannedFiles = 0;
        std::error_code error;
        auto iterator = std::filesystem::recursive_directory_iterator{ *root, error };
        Require(!error, "the app's source folder could not be listed");
        for (auto const end = std::filesystem::recursive_directory_iterator{}; iterator != end; iterator.increment(error))
        {
            Require(!error, "the app's source folder could not be listed");
            auto const& entry = *iterator;
            if (entry.is_directory())
            {
                auto const name = entry.path().filename().wstring();
                // Build output holds copies of the markup with its handlers removed.
                if (name == L"Generated Files" || name == L"obj" || name == L"bin" || name == L"x64" || name == L"ARM64")
                {
                    iterator.disable_recursion_pending();
                }
                continue;
            }
            if (entry.path().extension() != L".xaml")
            {
                continue;
            }
            ++scannedFiles;
            auto const xaml = ReadText(entry.path());
            auto const file = entry.path().filename().wstring();
            auto const label = entry.path().filename().string();
            std::size_t position = 0;
            while ((position = xaml.find("<ScrollViewer", position)) != std::string::npos)
            {
                auto const tag = ReadStartTag(xaml, position);
                Require(tag.has_value(), "a ScrollViewer tag in " + label + " could not be read");
                position = tag->End;
                if (tag->Name != "ScrollViewer")
                {
                    // A property element such as <ScrollViewer.Something>.
                    continue;
                }
                // Horizontal scrolling needs a horizontal extent, which a viewer
                // only gives its content when the bar is not Disabled (the default).
                auto const bar = Attribute(tag->Attributes, "HorizontalScrollBarVisibility").value_or("Disabled");
                if (bar == "Disabled")
                {
                    continue;
                }
                auto const name = Attribute(tag->Attributes, "x:Name").value_or("(unnamed)");
                auto const allowed = std::any_of(AllowedStrips.begin(), AllowedStrips.end(), [&file, &name](AllowedStrip const& strip)
                {
                    return strip.File == file && strip.Name == name;
                });
                if (allowed)
                {
                    continue;
                }
                auto const where = "the sideways ScrollViewer '" + name + "' in " + label;
                Require(
                    Attribute(tag->Attributes, "HorizontalScrollMode").value_or("") == "Disabled",
                    where + " lacks HorizontalScrollMode=\"Disabled\"; without it a plain mouse wheel pans the strip "
                        "instead of scrolling the page. Add it and move the strip with its arrow buttons.");
                auto handler = Attribute(tag->Attributes, "PointerWheelChanged");
                if (!handler)
                {
                    auto const content = FirstChildElement(xaml, tag->End);
                    if (content)
                    {
                        handler = Attribute(content->Attributes, "PointerWheelChanged");
                    }
                }
                Require(
                    handler.has_value(),
                    where + " has no sideways wheel guard. Wrap its content in <Grid Background=\"Transparent\" "
                        "PointerWheelChanged=\"OnStripPointerWheelChanged\"> and have that handler call "
                        "HaloDesktop::Controls::GuardSidewaysWheel (Controls/SidewaysWheelGuard.h); a tilt wheel or "
                        "Shift+wheel pans the strip otherwise. A strip meant to scroll sideways goes on the allow list "
                        "in this test with its reason.");
                auto codeBehind = entry.path();
                codeBehind += L".cpp";
                Require(
                    std::filesystem::is_regular_file(codeBehind, error) &&
                        ReadText(codeBehind).find("GuardSidewaysWheel") != std::string::npos,
                    where + " handles the wheel in code that does not call HaloDesktop::Controls::GuardSidewaysWheel; "
                        "use the shared guard instead of a copy of its rule.");
                ++guardedStrips;
            }
        }
        Require(scannedFiles > 0, "no XAML files were found under src/HaloDesktop");
        // The shelf strip, Home's Continue watching and the title page's cast and
        // similar rows at least.
        Require(guardedStrips >= 4, "fewer sideways strips were found than the app has; the XAML scan is not reading the markup");
    }
}

void RunShellInputTests()
{
    TestContentPressPlan();
    TestPointerFocusRefusal();
    TestSidewaysWheelRule();
    TestWindowBackdrop();
    TestSidewaysStripsCarryTheWheelGuard();
}
