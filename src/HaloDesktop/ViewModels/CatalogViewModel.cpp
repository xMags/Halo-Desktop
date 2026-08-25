#include "pch.h"
#include "ViewModels/CatalogViewModel.h"
#if __has_include("CatalogViewModel.g.cpp")
#include "CatalogViewModel.g.cpp"
#endif

#include "Models/Models.h"
#include "Services/NavigationService.h"
#include "ViewModels/ObservableHelper.h"

namespace
{
    auto EmptyItems()
    {
        return winrt::single_threaded_vector<winrt::HaloDesktop::MediaSummary>().GetView();
    }
}

namespace winrt::HaloDesktop::implementation
{
    CatalogViewModel::CatalogViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_navigation(services.Navigation), m_items(EmptyItems())
    {
    }

    winrt::hstring CatalogViewModel::Title() const
    {
        return m_title;
    }

    winrt::hstring CatalogViewModel::SourceLabel() const
    {
        return m_sourceLabel;
    }

    winrt::hstring CatalogViewModel::CountLabel() const
    {
        auto const count = m_items ? m_items.Size() : 0;
        return winrt::to_hstring(count) + (count == 1 ? L" ITEM" : L" ITEMS");
    }

    winrt::Windows::Foundation::IInspectable CatalogViewModel::Items() const
    {
        return m_items;
    }

    Microsoft::UI::Xaml::Visibility CatalogViewModel::SourceVisibility() const noexcept
    {
        return m_sourceLabel.empty()
            ? Microsoft::UI::Xaml::Visibility::Collapsed
            : Microsoft::UI::Xaml::Visibility::Visible;
    }

    Microsoft::UI::Xaml::Visibility CatalogViewModel::ContentVisibility() const noexcept
    {
        return m_items && m_items.Size() > 0
            ? Microsoft::UI::Xaml::Visibility::Visible
            : Microsoft::UI::Xaml::Visibility::Collapsed;
    }

    Microsoft::UI::Xaml::Visibility CatalogViewModel::EmptyVisibility() const noexcept
    {
        return ContentVisibility() == Microsoft::UI::Xaml::Visibility::Visible
            ? Microsoft::UI::Xaml::Visibility::Collapsed
            : Microsoft::UI::Xaml::Visibility::Visible;
    }

    void CatalogViewModel::Load(winrt::Windows::Foundation::IInspectable const& parameter)
    {
        m_title = L"Catalog";
        m_sourceLabel = {};
        m_items = EmptyItems();

        if (auto const shelf = parameter.try_as<winrt::HaloDesktop::Shelf>())
        {
            if (!shelf.Title().empty())
            {
                m_title = shelf.Title();
            }
            m_sourceLabel = shelf.SourceLabel();
            if (auto const items = shelf.Items())
            {
                m_items = items;
            }
        }

        RaiseState();
    }

    void CatalogViewModel::OpenDetail(winrt::Windows::Foundation::IInspectable const& item)
    {
        auto const media = item.try_as<winrt::HaloDesktop::MediaSummary>();
        if (!media || media.Type().empty() || media.Id().empty())
        {
            return;
        }

        m_navigation->GoTo(
            ::HaloDesktop::Services::Page::Detail,
            winrt::make<winrt::HaloDesktop::implementation::DetailNavParams>(
                media.Type(),
                media.Id(),
                media.Title(),
                media.Poster()));
    }

    winrt::event_token CatalogViewModel::PropertyChanged(
        Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }

    void CatalogViewModel::PropertyChanged(winrt::event_token const& token) noexcept
    {
        m_propertyChanged.remove(token);
    }

    void CatalogViewModel::RaiseState()
    {
        for (auto const property :
             { L"Title", L"SourceLabel", L"CountLabel", L"Items", L"SourceVisibility", L"ContentVisibility", L"EmptyVisibility" })
        {
            ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, property);
        }
    }
}
