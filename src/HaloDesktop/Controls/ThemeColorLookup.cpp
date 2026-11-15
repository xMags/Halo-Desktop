#include "pch.h"
#include "Controls/ThemeColorLookup.h"

namespace
{
    std::optional<winrt::Windows::UI::Color> FindInDictionary(
        winrt::Microsoft::UI::Xaml::ResourceDictionary const& dictionary,
        winrt::hstring const& theme,
        winrt::Windows::Foundation::IInspectable const& key,
        int depth)
    {
        // Merged dictionaries nest only a level or two here; the bound keeps a
        // cycle from ever recursing without end.
        if (!dictionary || depth > 8)
        {
            return std::nullopt;
        }
        auto const themes = dictionary.ThemeDictionaries();
        if (themes && themes.HasKey(winrt::box_value(theme)))
        {
            if (auto const themed = themes.Lookup(winrt::box_value(theme)).try_as<winrt::Microsoft::UI::Xaml::ResourceDictionary>())
            {
                if (themed.HasKey(key))
                {
                    if (auto const color = themed.Lookup(key)
                            .try_as<winrt::Windows::Foundation::IReference<winrt::Windows::UI::Color>>())
                    {
                        return color.Value();
                    }
                }
            }
        }
        for (auto const& merged : dictionary.MergedDictionaries())
        {
            if (auto const found = FindInDictionary(merged, theme, key, depth + 1))
            {
                return found;
            }
        }
        return std::nullopt;
    }
}

namespace HaloDesktop::Controls
{
    std::optional<winrt::Windows::UI::Color> ThemeColor(
        winrt::Microsoft::UI::Xaml::FrameworkElement const& scope,
        wchar_t const* key)
    {
        if (!scope || !key)
        {
            return std::nullopt;
        }
        try
        {
            return ThemeColor(scope.ActualTheme(), key);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::optional<winrt::Windows::UI::Color> ThemeColor(
        winrt::Microsoft::UI::Xaml::ElementTheme theme,
        wchar_t const* key)
    {
        if (!key)
        {
            return std::nullopt;
        }
        try
        {
            auto const dictionary = theme == winrt::Microsoft::UI::Xaml::ElementTheme::Light
                ? winrt::hstring{ L"Light" }
                : winrt::hstring{ L"Default" };
            return FindInDictionary(
                winrt::Microsoft::UI::Xaml::Application::Current().Resources(),
                dictionary,
                winrt::box_value(winrt::hstring{ key }),
                0);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }
}
