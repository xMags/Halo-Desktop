#include "pch.h"
#include "ViewModels/TmdbKeyViewModel.h"
#if __has_include("TmdbKeyViewModel.g.cpp")
#include "TmdbKeyViewModel.g.cpp"
#endif

#include "Services/Tmdb/TmdbCredential.h"
#include "ViewModels/ObservableHelper.h"

#include <pplawait.h>
#include <stdexcept>
#include <utility>

namespace
{
    using Visibility = winrt::Microsoft::UI::Xaml::Visibility;
    using ::HaloDesktop::Services::Tmdb::TmdbKeySave;

    Visibility VisibleIf(bool visible) noexcept
    {
        return visible ? Visibility::Visible : Visibility::Collapsed;
    }

    constexpr wchar_t WorkingText[] = L"Key works. People search and IMDb and TVDB links are on.";
    constexpr wchar_t RefusedLaterText[] = L"TMDB no longer accepts the saved key. Paste a new one to turn people search back on.";

    winrt::hstring SaveMessage(TmdbKeySave outcome)
    {
        switch (outcome)
        {
        case TmdbKeySave::Saved: return WorkingText;
        case TmdbKeySave::NotAKey: return L"That isn't a TMDB API key or read access token. Copy one from your TMDB account's API page.";
        case TmdbKeySave::Refused: return L"TMDB refused this key. Check it and try again.";
        case TmdbKeySave::Unreachable: return L"Couldn't reach TMDB to check the key. Try again when you're online.";
        case TmdbKeySave::NotStored: return L"TMDB accepted the key, but Halo couldn't save it on this PC.";
        }
        return L"The key could not be saved.";
    }
}

namespace winrt::HaloDesktop::implementation
{
    TmdbKeyViewModel::TmdbKeyViewModel(std::shared_ptr<::HaloDesktop::Services::Tmdb::TmdbKeyStore> keys)
        : m_keys(std::move(keys))
    {
        if (!m_keys)
        {
            throw std::invalid_argument{ "TmdbKeyViewModel requires the TMDB key store." };
        }
        ShowStoreState();
    }

    TmdbKeyViewModel::~TmdbKeyViewModel()
    {
        if (m_changedToken != 0) m_keys->RemoveChangedHandler(m_changedToken);
    }

    void TmdbKeyViewModel::Attach()
    {
        if (m_changedToken != 0) return;
        m_changedToken = m_keys->AddChangedHandler([weak = get_weak()]()
        {
            auto const self = weak.get();
            if (!self || self->m_busy) return;
            self->ShowStoreState();
            self->RaiseAll();
        });
    }

    void TmdbKeyViewModel::KeyText(winrt::hstring const& value)
    {
        if (m_keyText == value) return;
        m_keyText = value;
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, L"KeyText");
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, L"CanSave");
    }

    bool TmdbKeyViewModel::CanSave() const noexcept
    {
        return !m_busy && !m_keyText.empty();
    }

    winrt::hstring TmdbKeyViewModel::SaveButtonText() const
    {
        return m_busy ? L"Checking…" : L"Save";
    }

    Visibility TmdbKeyViewModel::StatusOkVisibility() const noexcept
    {
        return VisibleIf(!m_status.empty() && !m_statusProblem);
    }

    Visibility TmdbKeyViewModel::StatusProblemVisibility() const noexcept
    {
        return VisibleIf(!m_status.empty() && m_statusProblem);
    }

    Visibility TmdbKeyViewModel::EntryVisibility() const noexcept
    {
        return VisibleIf(!m_keys->HasKey() || m_keys->IsRefused());
    }

    Visibility TmdbKeyViewModel::SavedVisibility() const noexcept
    {
        return VisibleIf(m_keys->HasKey() && !m_keys->IsRefused());
    }

    void TmdbKeyViewModel::Save()
    {
        if (!CanSave()) return;
        m_busy = true;
        RaiseAll();
        SaveAsync(m_keyText);
    }

    void TmdbKeyViewModel::Remove()
    {
        if (m_busy) return;
        m_busy = true;
        RaiseAll();
        RemoveAsync();
    }

    winrt::fire_and_forget TmdbKeyViewModel::SaveAsync(winrt::hstring pasted)
    {
        auto const self = get_strong();
        winrt::apartment_context const uiThread;
        auto outcome = TmdbKeySave::Unreachable;
        try
        {
            outcome = co_await m_keys->SaveAsync(std::wstring{ pasted });
        }
        catch (...)
        {
        }
        co_await uiThread;
        m_busy = false;
        if (outcome == TmdbKeySave::Saved)
        {
            // The key is never shown again once it is saved.
            m_keyText = {};
        }
        SetStatus(SaveMessage(outcome), outcome != TmdbKeySave::Saved);
        RaiseAll();
    }

    winrt::fire_and_forget TmdbKeyViewModel::RemoveAsync()
    {
        auto const self = get_strong();
        winrt::apartment_context const uiThread;
        try
        {
            co_await m_keys->RemoveAsync();
        }
        catch (...)
        {
        }
        co_await uiThread;
        m_busy = false;
        SetStatus({}, false);
        RaiseAll();
    }

    void TmdbKeyViewModel::ShowStoreState()
    {
        if (m_keys->IsRefused())
        {
            SetStatus(RefusedLaterText, true);
            return;
        }
        if (m_keys->HasKey())
        {
            SetStatus(WorkingText, false);
            return;
        }
        // No key: whatever an attempt last said stays until the next one.
    }

    void TmdbKeyViewModel::SetStatus(winrt::hstring text, bool problem)
    {
        m_status = std::move(text);
        m_statusProblem = problem;
    }

    void TmdbKeyViewModel::RaiseAll()
    {
        for (auto const name : {
                 L"KeyText", L"CanSave", L"SaveButtonText", L"StatusText", L"StatusOkVisibility",
                 L"StatusProblemVisibility", L"EntryVisibility", L"SavedVisibility" })
        {
            ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, name);
        }
    }

    winrt::event_token TmdbKeyViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }

    void TmdbKeyViewModel::PropertyChanged(winrt::event_token const& token) noexcept
    {
        m_propertyChanged.remove(token);
    }
}
