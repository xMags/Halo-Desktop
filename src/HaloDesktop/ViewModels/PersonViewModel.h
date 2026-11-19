#pragma once

#include "PersonViewModel.g.h"

#include "Api/Dto.h"
#include "Services/AppServices.h"
#include "Services/FilmographyPolicy.h"
#include "Services/ServerFeaturesService.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace winrt::HaloDesktop::implementation
{
    // A person page's state. Opened by TMDB id, or by name (from a cast entry
    // without an id), in which case the person is found by an exact-name
    // search first; nobody by that name falls back to IMDb's people search in
    // the browser, as cast cards did before.
    //
    // UI-thread only. The people service delivers only the newest call
    // (its generation counter), and reaches this through a weak reference.
    struct PersonViewModel : PersonViewModelT<PersonViewModel>
    {
        explicit PersonViewModel(::HaloDesktop::Services::AppServices const& services);
        ~PersonViewModel();
        PersonViewModel(PersonViewModel const&) = delete;
        PersonViewModel& operator=(PersonViewModel const&) = delete;

        [[nodiscard]] winrt::hstring Name() const { return m_name; }
        [[nodiscard]] winrt::hstring Photo() const { return m_photo; }
        [[nodiscard]] winrt::hstring Initials() const;
        [[nodiscard]] winrt::hstring Kicker() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility KickerVisibility() const;
        [[nodiscard]] winrt::hstring Biography() const { return m_biography; }
        [[nodiscard]] Microsoft::UI::Xaml::Visibility BiographyVisibility() const noexcept;

        [[nodiscard]] winrt::Windows::Foundation::IInspectable Items() const { return m_items; }
        [[nodiscard]] auto ItemsView() const { return m_items; }
        [[nodiscard]] winrt::hstring CountText() const;
        [[nodiscard]] bool Loading() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility LoadingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility FilmographyVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PartialVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ChipsVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ActingChipVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility DirectingChipVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility WritingChipVisibility() const noexcept;
        [[nodiscard]] std::int32_t FilterIndex() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility EmptyVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ErrorVisibility() const noexcept;
        [[nodiscard]] winrt::hstring ErrorTitle() const;
        [[nodiscard]] winrt::hstring ErrorText() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RetryVisibility() const noexcept;

        void Load(winrt::Windows::Foundation::IInspectable const& parameter);
        void SetFilter(std::int32_t index);
        void Retry();
        void OpenDetail(winrt::Windows::Foundation::IInspectable const& item);
        void GoBack();

        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        enum class State
        {
            // Waiting for the server's features, the name's search or the person.
            Loading,
            Loaded,
            // The server has no people pages (or Halo is used without an account).
            Unavailable,
            NotFound,
            Failed,
        };

        // Asks for the person once the server's features are known.
        void Start();
        void FindByName();
        void LoadById(std::int64_t tmdbId);
        void ShowPerson(::HaloDesktop::Api::Dto::PersonDetail detail);
        // Nobody by this name here: IMDb's search in the browser, and back.
        void FallBackToImdb();
        void SetState(State state);
        void RebuildGrid();
        [[nodiscard]] bool Offers(::HaloDesktop::Services::FilmographyFilter filter) const noexcept;
        void Raise(wchar_t const* name);
        void RaiseAll();

        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        std::shared_ptr<::HaloDesktop::Services::ServerFeaturesService> m_features;
        std::shared_ptr<::HaloDesktop::Services::PeopleService> m_people;
        ::HaloDesktop::Services::ServerFeaturesChangedToken m_featuresToken{};

        winrt::HaloDesktop::PersonNavParams m_params{ nullptr };
        // Set once the person is known: by the page's parameter, or by the
        // name's search.
        std::int64_t m_tmdbId{};
        State m_state{ State::Loading };
        winrt::hstring m_name;
        winrt::hstring m_photo;
        ::HaloDesktop::Api::Dto::PersonDepartment m_knownFor{ ::HaloDesktop::Api::Dto::PersonDepartment::Unknown };
        winrt::hstring m_biography;
        bool m_partial{};
        std::vector<::HaloDesktop::Api::Dto::PersonCredit> m_credits;
        std::vector<::HaloDesktop::Services::FilmographyFilter> m_filters;
        ::HaloDesktop::Services::FilmographyFilter m_filter{ ::HaloDesktop::Services::FilmographyFilter::All };
        std::size_t m_shownCount{};
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_items{ nullptr };
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
