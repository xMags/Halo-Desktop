#include "pch.h"
#include "Controls/QualityMedal.xaml.h"
#if __has_include("QualityMedal.g.cpp")
#include "QualityMedal.g.cpp"
#endif

#include <winrt/Microsoft.UI.Xaml.Automation.h>

namespace
{
    wchar_t const* FinishState(winrt::HaloDesktop::QualityMedalFinish finish) noexcept
    {
        switch (finish)
        {
        case winrt::HaloDesktop::QualityMedalFinish::Gold: return L"Gold";
        case winrt::HaloDesktop::QualityMedalFinish::Silver: return L"Silver";
        case winrt::HaloDesktop::QualityMedalFinish::Bronze: return L"Bronze";
        case winrt::HaloDesktop::QualityMedalFinish::Plain: break;
        }
        return L"Plain";
    }

    wchar_t const* SizeState(winrt::HaloDesktop::QualityMedalSize size) noexcept
    {
        switch (size)
        {
        case winrt::HaloDesktop::QualityMedalSize::Small: return L"Small";
        case winrt::HaloDesktop::QualityMedalSize::Large: return L"Large";
        case winrt::HaloDesktop::QualityMedalSize::Medium: break;
        }
        return L"Medium";
    }
}

namespace winrt::HaloDesktop::implementation
{
    void QualityMedal::InitializeComponent()
    {
        QualityMedalT::InitializeComponent();
        m_initialized = true;
        ApplyText();
        ApplyStates();
    }

    winrt::hstring QualityMedal::Plate() const { return m_plate; }
    void QualityMedal::Plate(winrt::hstring const& value)
    {
        m_plate = value;
        ApplyText();
    }

    winrt::hstring QualityMedal::TagText() const { return m_tag; }
    void QualityMedal::TagText(winrt::hstring const& value)
    {
        m_tag = value;
        ApplyText();
        ApplyStates();
    }

    winrt::HaloDesktop::QualityMedalFinish QualityMedal::Finish() const noexcept { return m_finish; }
    void QualityMedal::Finish(winrt::HaloDesktop::QualityMedalFinish value)
    {
        m_finish = value;
        ApplyStates();
    }

    winrt::HaloDesktop::QualityMedalSize QualityMedal::Size() const noexcept { return m_size; }
    void QualityMedal::Size(winrt::HaloDesktop::QualityMedalSize value)
    {
        m_size = value;
        ApplyStates();
    }

    // A recycled row is re-bound before it is loaded, and a visual state applied to
    // an unloaded control does not always stick, so the states are asserted again here.
    void QualityMedal::OnLoaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        ApplyStates();
    }

    void QualityMedal::ApplyText()
    {
        if (!m_initialized)
        {
            return;
        }
        PlateText().Text(m_plate);
        TagLabel().Text(m_tag);
        // Read as one label, "4K HDR", the way the medal is meant to be seen.
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(
            *this,
            m_tag.empty() ? m_plate : m_plate + L" " + m_tag);
    }

    void QualityMedal::ApplyStates()
    {
        if (!m_initialized)
        {
            return;
        }
        Microsoft::UI::Xaml::VisualStateManager::GoToState(*this, FinishState(m_finish), false);
        Microsoft::UI::Xaml::VisualStateManager::GoToState(*this, SizeState(m_size), false);
        Microsoft::UI::Xaml::VisualStateManager::GoToState(*this, m_tag.empty() ? L"Untagged" : L"Tagged", false);
    }
}
