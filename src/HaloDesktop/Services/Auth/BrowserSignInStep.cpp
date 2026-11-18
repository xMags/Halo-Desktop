#include "pch.h"
#include "Services/Auth/BrowserSignInStep.h"

#include "Services/Auth/LoopbackListener.h"
#include "Services/Auth/SignInFailurePolicy.h"

#include <chrono>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>

namespace
{
    std::wstring MessageOf(std::exception const& error)
    {
        return std::wstring{ winrt::to_hstring(error.what()) };
    }

    // The listener fails its own event on cancel; reading the outcome here
    // keeps PPL from reporting an unobserved exception.
    concurrency::task<void> DrainAsync(concurrency::task<winrt::hstring> callback)
    {
        try
        {
            static_cast<void>(co_await callback);
        }
        catch (...)
        {
        }
    }
}

namespace HaloDesktop::Services::Auth
{
    std::optional<winrt::hstring> BrowserCallback::Value(wchar_t const* name) const
    {
        std::wstring text{ Path };
        auto const query = text.find(L'?');
        if (query == std::wstring::npos || query + 1 >= text.size())
        {
            return std::nullopt;
        }
        try
        {
            winrt::Windows::Foundation::WwwFormUrlDecoder decoder{ winrt::hstring{ text.substr(query + 1) } };
            for (auto const& entry : decoder)
            {
                if (entry.Name() == name)
                {
                    return entry.Value();
                }
            }
        }
        catch (winrt::hresult_error const&)
        {
        }
        return std::nullopt;
    }

    concurrency::task<BrowserCallback> SignInInBrowserAsync(
        winrt::hstring authUrl,
        std::stop_token cancellation,
        SignInOpenedHandler onOpened,
        std::optional<winrt::hstring> expectedState)
    {
        co_await winrt::resume_background();
        if (cancellation.stop_requested())
        {
            throw BrowserSignInFailure(BrowserFailureReason::Cancelled, L"sign-in cancelled");
        }

        std::unique_ptr<LoopbackListener> listener;
        try
        {
            listener = std::make_unique<LoopbackListener>(
                std::chrono::seconds{ 300 },
                expectedState ? std::optional<std::wstring>{ std::wstring{ *expectedState } } : std::nullopt);
        }
        catch (std::exception const& error)
        {
            throw SignInFailed(MessageOf(error));
        }
        std::stop_callback const cancelListener{ cancellation, [&listener]() noexcept
        {
            listener->Cancel();
        } };
        auto callbackTask = listener->WaitAsync();

        bool launched{};
        std::wstring launchError;
        try
        {
            launched = co_await winrt::Windows::System::Launcher::LaunchUriAsync(
                winrt::Windows::Foundation::Uri{ authUrl });
        }
        catch (winrt::hresult_error const& error)
        {
            launchError = std::wstring{ error.message() };
        }
        if (!launched)
        {
            listener->Cancel();
            co_await DrainAsync(callbackTask);
            throw SignInFailed(launchError.empty() ? std::wstring{ L"The browser could not be opened." } : launchError);
        }
        if (cancellation.stop_requested())
        {
            listener->Cancel();
            co_await DrainAsync(callbackTask);
            throw BrowserSignInFailure(BrowserFailureReason::Cancelled, L"sign-in cancelled");
        }
        if (onOpened)
        {
            try
            {
                onOpened(authUrl);
            }
            catch (...)
            {
                // Only the "open the link again" offer depends on it.
            }
        }

        winrt::hstring path;
        std::optional<SignInErrorInfo> waitFailure;
        try
        {
            path = co_await callbackTask;
        }
        catch (std::exception const& error)
        {
            waitFailure = LoopbackWaitFailure(MessageOf(error));
        }
        catch (...)
        {
            waitFailure = LoopbackWaitFailure(L"The sign-in listener stopped unexpectedly.");
        }
        if (cancellation.stop_requested())
        {
            throw BrowserSignInFailure(BrowserFailureReason::Cancelled, L"sign-in cancelled");
        }
        if (waitFailure)
        {
            throw SignInError{ std::move(*waitFailure) };
        }

        BrowserCallback callback{ .Path = path };
        auto const error = callback.Value(L"error");
        auto const description = callback.Value(L"error_description");
        auto declined = CallbackFailure(
            error ? std::optional<std::wstring_view>{ std::wstring_view{ *error } } : std::nullopt,
            description ? std::optional<std::wstring_view>{ std::wstring_view{ *description } } : std::nullopt);
        if (declined)
        {
            throw SignInError{ std::move(*declined) };
        }
        co_return callback;
    }

    SignInErrorInfo DescribeSignInFailure(std::exception_ptr const& failure)
    {
        try
        {
            if (failure)
            {
                std::rethrow_exception(failure);
            }
        }
        catch (SignInError const& error)
        {
            return error.Info();
        }
        catch (winrt::hresult_error const& error)
        {
            return SignInErrorInfo{ .Message = SanitizedSignInText(std::wstring_view{ error.message() }) };
        }
        catch (std::exception const& error)
        {
            return SignInErrorInfo{ .Message = SanitizedSignInText(MessageOf(error)) };
        }
        catch (...)
        {
        }
        return SignInErrorInfo{ .Message = L"Unknown error" };
    }
}
