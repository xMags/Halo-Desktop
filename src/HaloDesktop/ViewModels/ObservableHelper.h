#pragma once

#include <winrt/Microsoft.UI.Xaml.Data.h>

namespace HaloDesktop::detail
{
    template <typename Sender>
    void RaisePropertyChanged(
        winrt::event<winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventHandler>& handlers,
        Sender const& sender,
        wchar_t const* propertyName)
    {
        handlers(sender, winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventArgs{ propertyName });
    }
}
