#include "pch.h"
#include "ViewModels/SourcesViewModel.h"
#if __has_include("SourceDisplayItemViewModel.g.cpp")
#include "SourceDisplayItemViewModel.g.cpp"
#endif
#if __has_include("SourcesViewModel.g.cpp")
#include "SourcesViewModel.g.cpp"
#endif

#include "Services/NavigationService.h"
#include "Services/SampleData.h"
#include "ViewModels/ObservableHelper.h"

#include <sstream>
#include <utility>

namespace
{
    auto const Visible = winrt::Microsoft::UI::Xaml::Visibility::Visible;
    auto const Collapsed = winrt::Microsoft::UI::Xaml::Visibility::Collapsed;

    winrt::hstring StatusLabel(winrt::HaloDesktop::StreamStatus status)
    {
        switch (status)
        {
        case winrt::HaloDesktop::StreamStatus::Instant: return L"INSTANT";
        case winrt::HaloDesktop::StreamStatus::Caching: return L"CACHING";
        case winrt::HaloDesktop::StreamStatus::Uncached: return L"UNCACHED";
        case winrt::HaloDesktop::StreamStatus::OnDisk: return L"ON DISK";
        }
        return L"";
    }
}

namespace winrt::HaloDesktop::implementation
{
    SourceDisplayItemViewModel::SourceDisplayItemViewModel(winrt::hstring groupName, winrt::hstring groupNote, winrt::hstring groupCount)
        : m_groupName(std::move(groupName)), m_groupNote(std::move(groupNote)), m_groupCount(std::move(groupCount)), m_isHeader(true)
    {
    }
    SourceDisplayItemViewModel::SourceDisplayItemViewModel(winrt::HaloDesktop::StreamSource source)
        : m_source(std::move(source))
    {
    }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::HeaderVisibility() const noexcept { return m_isHeader ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::RowVisibility() const noexcept { return m_isHeader ? Collapsed : Visible; }
    winrt::hstring SourceDisplayItemViewModel::GroupName() const { return m_groupName; }
    winrt::hstring SourceDisplayItemViewModel::GroupNote() const { return m_groupNote; }
    winrt::hstring SourceDisplayItemViewModel::GroupCount() const { return m_groupCount; }
    winrt::hstring SourceDisplayItemViewModel::Quality() const { return m_source ? m_source.Quality() : L""; }
    winrt::hstring SourceDisplayItemViewModel::Range() const { return m_source ? m_source.Range() : L""; }
    winrt::hstring SourceDisplayItemViewModel::File() const { return m_source ? m_source.File() : L""; }
    winrt::hstring SourceDisplayItemViewModel::Codec() const { return m_source ? m_source.Codec() : L""; }
    winrt::hstring SourceDisplayItemViewModel::Audio() const { return m_source ? m_source.Audio() : L""; }
    winrt::hstring SourceDisplayItemViewModel::Languages() const { return m_source ? m_source.Languages() : L""; }
    winrt::hstring SourceDisplayItemViewModel::StatusLabel() const { return m_source ? ::StatusLabel(m_source.Status()) : L""; }
    winrt::hstring SourceDisplayItemViewModel::Size() const { return m_source ? m_source.Size() : L""; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::Quality2160Visibility() const noexcept { return m_source && m_source.Quality() == L"2160p" ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::Quality1080Visibility() const noexcept { return m_source && m_source.Quality() == L"1080p" ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::QualityOtherVisibility() const noexcept { return m_source && m_source.Quality() != L"2160p" && m_source.Quality() != L"1080p" ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::InstantVisibility() const noexcept { return m_source && m_source.Status() == winrt::HaloDesktop::StreamStatus::Instant ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::CachingVisibility() const noexcept { return m_source && m_source.Status() == winrt::HaloDesktop::StreamStatus::Caching ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::UncachedVisibility() const noexcept { return m_source && m_source.Status() == winrt::HaloDesktop::StreamStatus::Uncached ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SourceDisplayItemViewModel::OnDiskVisibility() const noexcept { return m_source && m_source.Status() == winrt::HaloDesktop::StreamStatus::OnDisk ? Visible : Collapsed; }

    SourcesViewModel::SourcesViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_sources(services.Sources),
          m_navigation(services.Navigation),
          m_sourceGroups(services.Sources->Groups()),
          m_items(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>())
    {
        if (m_sourceGroups.Size() > 0 && m_sourceGroups.GetAt(0).Sources().Size() > 0)
            m_bestSource = m_sourceGroups.GetAt(0).Sources().GetAt(0);
        Rebuild();
    }
    winrt::Windows::Foundation::IInspectable SourcesViewModel::Items() const { return m_items; }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> SourcesViewModel::ItemsView() const { return m_items; }
    winrt::hstring SourcesViewModel::BestQuality() const { return m_bestSource ? m_bestSource.Quality() : L""; }
    winrt::hstring SourcesViewModel::BestRange() const { return m_bestSource ? m_bestSource.Range() : L""; }
    winrt::hstring SourcesViewModel::BestFile() const { return m_bestSource ? m_bestSource.File() : L""; }
    winrt::hstring SourcesViewModel::BestCodec() const { return m_bestSource ? m_bestSource.Codec() : L""; }
    winrt::hstring SourcesViewModel::BestAudio() const { return m_bestSource ? m_bestSource.Audio() : L""; }
    winrt::hstring SourcesViewModel::BestLanguages() const { return m_bestSource ? m_bestSource.Languages() : L""; }
    winrt::hstring SourcesViewModel::BestSize() const { return m_bestSource ? m_bestSource.Size() : L""; }
    winrt::hstring SourcesViewModel::TeachingTipTitle() const { return ::HaloDesktop::Services::SampleData::Copy::SourceTeachingTitle; }
    winrt::hstring SourcesViewModel::TeachingTipBody() const { return ::HaloDesktop::Services::SampleData::Copy::SourceTeachingBody; }
    bool SourcesViewModel::TeachingTipOpen() const noexcept { return m_teachingTipOpen; }
    void SourcesViewModel::SetFilter(std::int32_t index)
    {
        if (index < 0 || index > 3 || index == m_filterIndex) return;
        m_filterIndex = index;
        Rebuild();
    }
    void SourcesViewModel::DismissTeachingTip()
    {
        if (!m_teachingTipOpen) return;
        m_teachingTipOpen = false;
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, L"TeachingTipOpen");
    }
    void SourcesViewModel::OpenPlayer() { m_navigation->ShowOverlay(::HaloDesktop::Services::Page::Player); }
    void SourcesViewModel::OpenSettings() { m_navigation->GoTo(::HaloDesktop::Services::Page::Settings); }
    winrt::event_token SourcesViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler) { return m_propertyChanged.add(handler); }
    void SourcesViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }
    void SourcesViewModel::Rebuild()
    {
        m_items.Clear();
        for (auto const& group : m_sourceGroups)
        {
            bool headerAdded{};
            for (auto const& source : group.Sources())
            {
                if (!MatchesFilter(source)) continue;
                if (!headerAdded)
                {
                    std::wostringstream count;
                    count << group.Count() << (group.Name() == L"LOCAL LIBRARY" ? L" FILES" : L" SOURCES");
                    m_items.Append(winrt::make<SourceDisplayItemViewModel>(group.Name(), group.Note(), winrt::hstring(count.str())));
                    headerAdded = true;
                }
                m_items.Append(winrt::make<SourceDisplayItemViewModel>(source));
            }
        }
    }
    bool SourcesViewModel::MatchesFilter(winrt::HaloDesktop::StreamSource const& source) const
    {
        if (m_filterIndex == 0) return true;
        if (m_filterIndex == 1) return source.Status() == winrt::HaloDesktop::StreamStatus::Instant || source.Status() == winrt::HaloDesktop::StreamStatus::OnDisk;
        if (m_filterIndex == 2) return source.Quality() == L"2160p";
        return source.Quality() == L"1080p";
    }
}
