#pragma once

#include "QualityMedal.g.h"

namespace winrt::HaloDesktop::implementation
{
    // The quality medal shared by the title page, the sources sheet, Downloads and the
    // player. Pure view: what a quality is stamped as is decided by QualityMedalPolicy.
    struct QualityMedal : QualityMedalT<QualityMedal>
    {
        QualityMedal() = default;
        void InitializeComponent();

        [[nodiscard]] winrt::hstring Plate() const;
        void Plate(winrt::hstring const& value);
        [[nodiscard]] winrt::hstring TagText() const;
        void TagText(winrt::hstring const& value);
        [[nodiscard]] winrt::HaloDesktop::QualityMedalFinish Finish() const noexcept;
        void Finish(winrt::HaloDesktop::QualityMedalFinish value);
        [[nodiscard]] winrt::HaloDesktop::QualityMedalSize Size() const noexcept;
        void Size(winrt::HaloDesktop::QualityMedalSize value);

        void OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        void ApplyText();
        void ApplyStates();

        winrt::hstring m_plate;
        winrt::hstring m_tag;
        winrt::HaloDesktop::QualityMedalFinish m_finish{ winrt::HaloDesktop::QualityMedalFinish::Plain };
        winrt::HaloDesktop::QualityMedalSize m_size{ winrt::HaloDesktop::QualityMedalSize::Medium };
        bool m_initialized{};
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct QualityMedal : QualityMedalT<QualityMedal, implementation::QualityMedal> {};
}
