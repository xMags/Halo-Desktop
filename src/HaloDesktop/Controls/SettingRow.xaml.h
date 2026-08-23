#pragma once

#include "SettingRow.g.h"

#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    struct SettingRow : SettingRowT<SettingRow>
    {
        SettingRow();
        [[nodiscard]] winrt::hstring Label() const;
        void Label(winrt::hstring const& value);
        [[nodiscard]] winrt::hstring Hint() const;
        void Hint(winrt::hstring const& value);
        [[nodiscard]] winrt::Windows::Foundation::IInspectable SettingContent() const;
        void SettingContent(winrt::Windows::Foundation::IInspectable const& value);

    private:
        winrt::hstring m_label;
        winrt::hstring m_hint;
        winrt::Windows::Foundation::IInspectable m_settingContent{ nullptr };
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct SettingRow : SettingRowT<SettingRow, implementation::SettingRow> {};
}
