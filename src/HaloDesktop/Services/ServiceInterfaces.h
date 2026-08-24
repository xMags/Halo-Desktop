#pragma once

#include <cstdint>
#include <functional>
#include <pplawait.h>
#include <ppltasks.h>
#include <winrt/HaloDesktop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace HaloDesktop::Services
{
    enum class AuthenticationMode
    {
        Unknown,
        Local,
        Oidc,
    };

    using DownloadChangedToken = std::uint64_t;
    using DownloadChangedHandler = std::function<void()>;

    class ICatalogService
    {
    public:
        virtual ~ICatalogService() = default;
        [[nodiscard]] virtual winrt::HaloDesktop::MediaSummary Hero() const = 0;
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::ContinueItem> ContinueWatching() const = 0;
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Shelf> Shelves() const = 0;
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary> LibraryItems() const = 0;
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::SearchGroup> Search(winrt::hstring const& query) const = 0;
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> RecentTerms() const = 0;
    };

    class IMetadataService
    {
    public:
        virtual ~IMetadataService() = default;
        [[nodiscard]] virtual winrt::HaloDesktop::MediaDetail Detail() const = 0;
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Episode> Episodes(std::int32_t season) const = 0;
    };

    class ISourceService
    {
    public:
        virtual ~ISourceService() = default;
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::SourceGroup> Groups() const = 0;
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::StreamSource> Filter(winrt::hstring const& quality) const = 0;
    };

    class IDownloadService
    {
    public:
        virtual ~IDownloadService() = default;
        virtual void Start() = 0;
        virtual void Stop() noexcept = 0;
        virtual void PauseAll() = 0;
        virtual void ResumeAll() = 0;
        virtual bool PauseTransfer(winrt::hstring const& id) = 0;
        virtual bool ResumeTransfer(winrt::hstring const& id) = 0;
        virtual bool StartNow(winrt::hstring const& id) = 0;
        virtual bool CancelTransfer(winrt::hstring const& id) = 0;
        virtual bool DeleteReady(winrt::hstring const& id) = 0;
        [[nodiscard]] virtual bool IsRunning() const noexcept = 0;
        [[nodiscard]] virtual bool IsPausedAll() const noexcept = 0;
        [[nodiscard]] virtual std::int32_t ActiveCount() const noexcept = 0;
        [[nodiscard]] virtual double AggregateRate() const noexcept = 0;
        [[nodiscard]] virtual winrt::hstring QueueLine() const = 0;
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::DownloadItem> Transfers() const = 0;
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::DownloadItem> Ready() const = 0;
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IObservableVector<double> Throughput() const = 0;
        virtual DownloadChangedToken AddChangedHandler(DownloadChangedHandler handler) = 0;
        virtual void RemoveChangedHandler(DownloadChangedToken token) noexcept = 0;
    };

    class IAddonService
    {
    public:
        virtual ~IAddonService() = default;
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::Addon> Items() const = 0;
        virtual bool Toggle(winrt::hstring const& name, bool enabled) = 0;
        virtual bool Remove(winrt::hstring const& name) = 0;
    };

    class ISessionService
    {
    public:
        virtual ~ISessionService() = default;
        [[nodiscard]] virtual winrt::hstring ServerUrl() const = 0;
        [[nodiscard]] virtual winrt::hstring UserName() const = 0;
        [[nodiscard]] virtual bool IsSignedIn() const noexcept = 0;
        [[nodiscard]] virtual bool IsAdmin() const noexcept = 0;
        [[nodiscard]] virtual AuthenticationMode Mode() const noexcept = 0;
        [[nodiscard]] virtual std::uint64_t Generation() const noexcept = 0;
        [[nodiscard]] virtual concurrency::task<void> RestoreAsync() = 0;
        [[nodiscard]] virtual concurrency::task<AuthenticationMode> DiscoverAuthenticationAsync() = 0;
        [[nodiscard]] virtual concurrency::task<winrt::HaloDesktop::SignInOutcome>
            SignInLocalAsync(winrt::hstring username, winrt::hstring password) = 0;
        [[nodiscard]] virtual concurrency::task<winrt::HaloDesktop::SignInOutcome>
            RequestBrowserSignInAsync() = 0;
        [[nodiscard]] virtual concurrency::task<void> SignOutAsync() = 0;
    };
}
