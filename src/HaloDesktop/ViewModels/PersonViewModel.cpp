#include "pch.h"
#include "ViewModels/PersonViewModel.h"
#if __has_include("PersonViewModel.g.cpp")
#include "PersonViewModel.g.cpp"
#endif
#include "Controls/ArtworkUrlPolicy.h"
#include "Models/Models.h"
#include "Services/NavigationService.h"
#include "Services/PeopleRowPolicy.h"
#include "Services/PeopleService.h"
#include "ViewModels/DetailFactsPolicy.h"
#include "ViewModels/ObservableHelper.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <winrt/Windows.System.h>

namespace
{
    auto const Visible = winrt::Microsoft::UI::Xaml::Visibility::Visible;
    auto const Collapsed = winrt::Microsoft::UI::Xaml::Visibility::Collapsed;

    using ::HaloDesktop::Services::FilmographyFilter;

    // The page's photo is drawn far larger than the cast thumbnail the server's
    // URL is sized for.
    winrt::hstring LargePhoto(winrt::hstring const& url)
    {
        return winrt::hstring{ ::HaloDesktop::Controls::PortraitPhotoUrl(url) };
    }

    constexpr wchar_t FailedTitle[] = L"Couldn't load this page";
    constexpr wchar_t FailedText[] = L"The server did not answer. Try again in a moment.";
    constexpr wchar_t NotFoundTitle[] = L"This person isn't here";
    constexpr wchar_t NotFoundText[] = L"The server doesn't know this person.";
    constexpr wchar_t UnavailableTitle[] = L"This page can't be shown";
    constexpr wchar_t UnavailableText[] = L"This kind of page needs a Halo server that supports it.";

    winrt::Microsoft::UI::Xaml::Visibility VisibleIf(bool value) noexcept
    {
        return value ? Visible : Collapsed;
    }

    // Opens IMDb's people search, built from the name alone, in the browser.
    winrt::fire_and_forget OpenInBrowser(std::wstring url)
    {
        try
        {
            winrt::Windows::Foundation::Uri const uri{ url };
            co_await winrt::Windows::System::Launcher::LaunchUriAsync(uri);
        }
        catch (...)
        {
            // A refused launch is swallowed, as the title page's cast cards do.
        }
    }

    winrt::HaloDesktop::MediaSummary ToMedia(::HaloDesktop::Api::Dto::PersonCredit const& credit)
    {
        auto const kind = credit.Type == L"series"
            ? winrt::HaloDesktop::MediaKind::Series
            : winrt::HaloDesktop::MediaKind::Movie;
        return winrt::make<winrt::HaloDesktop::implementation::MediaSummary>(
            credit.ImdbId,
            credit.Name,
            winrt::hstring{ ::HaloDesktop::Services::CreditLine(credit) },
            kind,
            credit.Type,
            credit.Poster.value_or(winrt::hstring{}),
            winrt::hstring{},
            winrt::hstring{},
            winrt::hstring{},
            credit.Year.value_or(winrt::hstring{}),
            0,
            0,
            winrt::hstring{},
            winrt::hstring{});
    }
}

namespace winrt::HaloDesktop::implementation
{
    PersonViewModel::PersonViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_navigation(services.Navigation),
          m_features(services.ServerFeatures),
          m_people(services.People),
          m_items(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>())
    {
        if (!m_navigation || !m_features || !m_people)
        {
            throw winrt::hresult_invalid_argument{ L"A person page needs navigation, the server's features and people." };
        }
    }

    PersonViewModel::~PersonViewModel()
    {
        if (m_features && m_featuresToken != 0)
        {
            m_features->RemoveChangedHandler(m_featuresToken);
        }
        if (m_people && m_state == State::Loading)
        {
            m_people->CancelPerson();
        }
    }

    winrt::hstring PersonViewModel::Initials() const
    {
        return winrt::hstring{ ::HaloDesktop::ViewModels::Initials(m_name) };
    }

    winrt::hstring PersonViewModel::Kicker() const
    {
        return winrt::hstring{ ::HaloDesktop::Services::DepartmentLabel(m_knownFor) };
    }

    Microsoft::UI::Xaml::Visibility PersonViewModel::KickerVisibility() const
    {
        return VisibleIf(m_knownFor != ::HaloDesktop::Api::Dto::PersonDepartment::Unknown);
    }

    Microsoft::UI::Xaml::Visibility PersonViewModel::BiographyVisibility() const noexcept { return VisibleIf(!m_biography.empty()); }
    winrt::hstring PersonViewModel::CountText() const { return winrt::to_hstring(static_cast<std::uint64_t>(m_shownCount)); }
    bool PersonViewModel::Loading() const noexcept { return m_state == State::Loading; }
    Microsoft::UI::Xaml::Visibility PersonViewModel::LoadingVisibility() const noexcept { return VisibleIf(Loading()); }
    Microsoft::UI::Xaml::Visibility PersonViewModel::FilmographyVisibility() const noexcept { return VisibleIf(m_state == State::Loaded && !m_credits.empty()); }
    Microsoft::UI::Xaml::Visibility PersonViewModel::PartialVisibility() const noexcept { return VisibleIf(m_state == State::Loaded && m_partial); }
    // All and one role say the same thing; chips are worth showing from two roles on.
    Microsoft::UI::Xaml::Visibility PersonViewModel::ChipsVisibility() const noexcept { return VisibleIf(m_state == State::Loaded && m_filters.size() > 2); }
    Microsoft::UI::Xaml::Visibility PersonViewModel::ActingChipVisibility() const noexcept { return VisibleIf(Offers(FilmographyFilter::Acting)); }
    Microsoft::UI::Xaml::Visibility PersonViewModel::DirectingChipVisibility() const noexcept { return VisibleIf(Offers(FilmographyFilter::Directing)); }
    Microsoft::UI::Xaml::Visibility PersonViewModel::WritingChipVisibility() const noexcept { return VisibleIf(Offers(FilmographyFilter::Writing)); }
    std::int32_t PersonViewModel::FilterIndex() const noexcept { return static_cast<std::int32_t>(m_filter); }
    Microsoft::UI::Xaml::Visibility PersonViewModel::EmptyVisibility() const noexcept { return VisibleIf(m_state == State::Loaded && m_credits.empty()); }

    Microsoft::UI::Xaml::Visibility PersonViewModel::ErrorVisibility() const noexcept
    {
        return VisibleIf(m_state == State::Failed || m_state == State::NotFound || m_state == State::Unavailable);
    }

    winrt::hstring PersonViewModel::ErrorTitle() const
    {
        switch (m_state)
        {
        case State::Failed: return FailedTitle;
        case State::NotFound: return NotFoundTitle;
        case State::Unavailable: return UnavailableTitle;
        case State::Loading:
        case State::Loaded: break;
        }
        return {};
    }

    winrt::hstring PersonViewModel::ErrorText() const
    {
        switch (m_state)
        {
        case State::Failed: return FailedText;
        case State::NotFound: return NotFoundText;
        case State::Unavailable: return UnavailableText;
        case State::Loading:
        case State::Loaded: break;
        }
        return {};
    }

    Microsoft::UI::Xaml::Visibility PersonViewModel::RetryVisibility() const noexcept { return VisibleIf(m_state == State::Failed); }

    bool PersonViewModel::Offers(FilmographyFilter filter) const noexcept
    {
        return std::find(m_filters.begin(), m_filters.end(), filter) != m_filters.end();
    }

    void PersonViewModel::Load(winrt::Windows::Foundation::IInspectable const& parameter)
    {
        m_params = parameter.try_as<winrt::HaloDesktop::PersonNavParams>();
        if (!m_params || (m_params.TmdbId() <= 0 && m_params.Name().empty()))
        {
            m_params = nullptr;
            SetState(State::NotFound);
            return;
        }
        // What the page was opened with shows at once; the server's own
        // answer replaces it.
        m_tmdbId = m_params.TmdbId() > 0 ? m_params.TmdbId() : 0;
        m_name = m_params.Name();
        // Every caller passes a photo its mapper already held to https.
        m_photo = std::wstring_view{ m_params.Photo() }.starts_with(L"https://") ? LargePhoto(m_params.Photo()) : winrt::hstring{};
        if (m_featuresToken == 0)
        {
            m_featuresToken = m_features->AddChangedHandler([weak = get_weak()]()
            {
                if (auto const self = weak.get(); self && self->m_state == State::Loading) self->Start();
            });
        }
        m_state = State::Loading;
        RaiseAll();
        m_features->EnsureLoaded();
        Start();
    }

    void PersonViewModel::Start()
    {
        if (!m_params || m_state != State::Loading || !m_features->Settled())
        {
            return;
        }
        if (!m_features->People())
        {
            SetState(State::Unavailable);
            return;
        }
        if (m_tmdbId > 0)
        {
            LoadById(m_tmdbId);
            return;
        }
        FindByName();
    }

    void PersonViewModel::FindByName()
    {
        m_people->FindPerson(std::wstring{ m_name }, [weak = get_weak()](::HaloDesktop::Services::PeopleOutcome outcome, std::int64_t tmdbId)
        {
            auto const self = weak.get();
            if (!self || self->m_state != State::Loading) return;
            switch (outcome)
            {
            case ::HaloDesktop::Services::PeopleOutcome::Found:
                self->m_tmdbId = tmdbId;
                self->LoadById(tmdbId);
                return;
            case ::HaloDesktop::Services::PeopleOutcome::NotFound:
                self->FallBackToImdb();
                return;
            case ::HaloDesktop::Services::PeopleOutcome::Failed:
                self->SetState(State::Failed);
                return;
            }
        });
    }

    void PersonViewModel::LoadById(std::int64_t tmdbId)
    {
        m_people->LoadPerson(tmdbId, [weak = get_weak()](
            ::HaloDesktop::Services::PeopleOutcome outcome,
            std::optional<::HaloDesktop::Api::Dto::PersonDetail> detail)
        {
            auto const self = weak.get();
            if (!self || self->m_state != State::Loading) return;
            if (outcome == ::HaloDesktop::Services::PeopleOutcome::Found && detail)
            {
                self->ShowPerson(std::move(*detail));
                return;
            }
            self->SetState(outcome == ::HaloDesktop::Services::PeopleOutcome::NotFound ? State::NotFound : State::Failed);
        });
    }

    void PersonViewModel::ShowPerson(::HaloDesktop::Api::Dto::PersonDetail detail)
    {
        m_name = detail.Person.Name;
        if (detail.Person.Photo) m_photo = LargePhoto(*detail.Person.Photo);
        m_knownFor = detail.Person.KnownFor;
        m_biography = detail.Person.Biography.value_or(winrt::hstring{});
        m_partial = detail.Partial;
        m_credits = std::move(detail.Credits);
        m_filters = ::HaloDesktop::Services::FilmographyFilters(m_credits);
        if (!Offers(m_filter)) m_filter = FilmographyFilter::All;
        m_state = State::Loaded;
        RebuildGrid();
        RaiseAll();
    }

    void PersonViewModel::FallBackToImdb()
    {
        OpenInBrowser(::HaloDesktop::ViewModels::PersonSearchUrl(m_name));
        SetState(State::NotFound);
        // Only while this page is the one showing: a page left meanwhile has
        // nothing to go back from.
        if (m_navigation->CurrentPage() == ::HaloDesktop::Services::Page::Person)
        {
            m_navigation->GoBack();
        }
    }

    void PersonViewModel::SetState(State state)
    {
        m_state = state;
        RaiseAll();
    }

    void PersonViewModel::RebuildGrid()
    {
        auto const shown = ::HaloDesktop::Services::Filmography(m_credits, m_filter);
        std::vector<winrt::Windows::Foundation::IInspectable> media;
        media.reserve(shown.size());
        for (auto const& credit : shown)
        {
            media.push_back(ToMedia(credit));
        }
        m_items.ReplaceAll(media);
        m_shownCount = shown.size();
    }

    void PersonViewModel::SetFilter(std::int32_t index)
    {
        if (index < 0 || index > 3) return;
        auto const filter = static_cast<FilmographyFilter>(index);
        if (filter == m_filter || m_state != State::Loaded || !Offers(filter)) return;
        m_filter = filter;
        RebuildGrid();
        Raise(L"CountText");
        Raise(L"FilterIndex");
    }

    void PersonViewModel::Retry()
    {
        if (m_state != State::Failed || !m_params) return;
        m_state = State::Loading;
        RaiseAll();
        m_features->EnsureLoaded();
        Start();
    }

    void PersonViewModel::OpenDetail(winrt::Windows::Foundation::IInspectable const& item)
    {
        auto const media = item.try_as<winrt::HaloDesktop::MediaSummary>();
        if (!media || media.Type().empty() || media.Id().empty()) return;
        m_navigation->GoTo(
            ::HaloDesktop::Services::Page::Detail,
            winrt::make<DetailNavParams>(media.Type(), media.Id(), media.Title(), media.Poster()));
    }

    void PersonViewModel::GoBack()
    {
        m_navigation->GoBack();
    }

    winrt::event_token PersonViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }

    void PersonViewModel::PropertyChanged(winrt::event_token const& token) noexcept
    {
        m_propertyChanged.remove(token);
    }

    void PersonViewModel::Raise(wchar_t const* name)
    {
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, name);
    }

    void PersonViewModel::RaiseAll()
    {
        for (auto const name : {
                 L"Name", L"Photo", L"Initials", L"Kicker", L"KickerVisibility", L"Biography", L"BiographyVisibility",
                 L"CountText", L"Loading", L"LoadingVisibility", L"FilmographyVisibility", L"PartialVisibility",
                 L"ChipsVisibility", L"ActingChipVisibility", L"DirectingChipVisibility", L"WritingChipVisibility",
                 L"FilterIndex", L"EmptyVisibility", L"ErrorVisibility", L"ErrorTitle", L"ErrorText", L"RetryVisibility" })
        {
            Raise(name);
        }
    }
}
