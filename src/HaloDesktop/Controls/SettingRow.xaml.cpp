#include "pch.h"
#include "Controls/SettingRow.xaml.h"
#if __has_include("SettingRow.g.cpp")
#include "SettingRow.g.cpp"
#endif

namespace winrt::HaloDesktop::implementation
{
    SettingRow::SettingRow() = default;
    winrt::hstring SettingRow::Label() const { return m_label; }
    void SettingRow::Label(winrt::hstring const& value)
    {
        m_label = value;
        if (auto const text = FindName(L"LabelText").try_as<Microsoft::UI::Xaml::Controls::TextBlock>()) text.Text(value);
    }
    winrt::hstring SettingRow::Hint() const { return m_hint; }
    void SettingRow::Hint(winrt::hstring const& value)
    {
        m_hint = value;
        if (auto const text = FindName(L"HintText").try_as<Microsoft::UI::Xaml::Controls::TextBlock>()) text.Text(value);
    }
    winrt::Windows::Foundation::IInspectable SettingRow::SettingContent() const { return m_settingContent; }
    void SettingRow::SettingContent(winrt::Windows::Foundation::IInspectable const& value)
    {
        m_settingContent = value;
        if (auto const host = FindName(L"SettingContentHost").try_as<Microsoft::UI::Xaml::Controls::ContentPresenter>()) host.Content(value);
    }
}
