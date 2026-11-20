#pragma once

#include "TmdbKeyViewModel.g.h"

#include "Services/Tmdb/TmdbKeyStore.h"

#include <memory>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace winrt::HaloDesktop::implementation
{
    // Settings' TMDB key card, shown only for Halo without an account: paste a
    // key, have TMDB check it, save it, or remove the saved one. The key itself
    // is never shown back; a saved key reads as "Key saved".
    //
    // UI thread only. Attach after construction (it subscribes to the store,
    // which needs a weak reference the constructor cannot hand out yet).
    struct TmdbKeyViewModel : TmdbKeyViewModelT<TmdbKeyViewModel>
    {
        explicit TmdbKeyViewModel(std::shared_ptr<::HaloDesktop::Services::Tmdb::TmdbKeyStore> keys);
        ~TmdbKeyViewModel();

        TmdbKeyViewModel(TmdbKeyViewModel const&) = delete;
        TmdbKeyViewModel& operator=(TmdbKeyViewModel const&) = delete;

        void Attach();

        [[nodiscard]] winrt::hstring KeyText() const { return m_keyText; }
        void KeyText(winrt::hstring const& value);
        [[nodiscard]] bool CanSave() const noexcept;
        [[nodiscard]] winrt::hstring SaveButtonText() const;
        [[nodiscard]] winrt::hstring StatusText() const { return m_status; }
        [[nodiscard]] Microsoft::UI::Xaml::Visibility StatusOkVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility StatusProblemVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility EntryVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SavedVisibility() const noexcept;

        void Save();
        void Remove();

        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        winrt::fire_and_forget SaveAsync(winrt::hstring pasted);
        winrt::fire_and_forget RemoveAsync();
        // The status line for the store as it is, unless an attempt just said otherwise.
        void ShowStoreState();
        void SetStatus(winrt::hstring text, bool problem);
        void RaiseAll();

        std::shared_ptr<::HaloDesktop::Services::Tmdb::TmdbKeyStore> m_keys;
        ::HaloDesktop::Services::Tmdb::TmdbKeyChangedToken m_changedToken{};
        winrt::hstring m_keyText;
        winrt::hstring m_status;
        bool m_statusProblem{};
        bool m_busy{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
