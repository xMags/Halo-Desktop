#include "pch.h"
#include "Views/DownloadsPage.xaml.h"
#if __has_include("DownloadsPage.g.cpp")
#include "DownloadsPage.g.cpp"
#endif

#include "App.xaml.h"

#include <iomanip>
#include <sstream>

namespace winrt::HaloDesktop::implementation
{
    DownloadsPage::DownloadsPage() = default;

    void DownloadsPage::OnLoaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        if (m_downloadChangedToken == 0)
        {
            m_downloadChangedToken = App::Services().Downloads->AddChangedHandler(
                [weak = get_weak()]()
                {
                    if (auto const self = weak.get())
                    {
                        self->UpdateProof();
                    }
                });
        }
        UpdateProof();
    }

    void DownloadsPage::OnUnloaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        if (m_downloadChangedToken != 0)
        {
            App::Services().Downloads->RemoveChangedHandler(m_downloadChangedToken);
            m_downloadChangedToken = 0;
        }
    }

    void DownloadsPage::UpdateProof()
    {
        std::wostringstream rate;
        rate << std::fixed << std::setprecision(1) << App::Services().Downloads->AggregateRate() << L" MB/s";
        RateText().Text(rate.str());
        QueueText().Text(App::Services().Downloads->QueueLine());
    }
}
