#pragma once

#include "Services/ServiceInterfaces.h"

namespace HaloDesktop::Services
{
    // Mock services are UI-thread-only. They return immutable snapshots except for
    // observable collections explicitly named by their interfaces.
    class MockCatalogService final : public ICatalogService
    {
    public:
        MockCatalogService();

        [[nodiscard]] winrt::HaloDesktop::MediaSummary Hero() const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::ContinueItem> ContinueWatching() const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Shelf> Shelves() const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary> LibraryItems() const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::SearchGroup> Search(winrt::hstring const& query) const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> RecentTerms() const override;

    private:
        winrt::HaloDesktop::MediaSummary m_hero{ nullptr };
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::ContinueItem> m_continueWatching{ nullptr };
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Shelf> m_shelves{ nullptr };
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary> m_libraryItems{ nullptr };
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::SearchGroup> m_searchGroups{ nullptr };
        winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> m_recentTerms{ nullptr };
    };

    class MockMetadataService final : public IMetadataService
    {
    public:
        MockMetadataService();
        [[nodiscard]] winrt::HaloDesktop::MediaDetail Detail() const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Episode> Episodes(std::int32_t season) const override;

    private:
        winrt::HaloDesktop::MediaDetail m_detail{ nullptr };
    };

    class MockSourceService final : public ISourceService
    {
    public:
        MockSourceService();
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::SourceGroup> Groups() const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::StreamSource> Filter(winrt::hstring const& quality) const override;

    private:
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::SourceGroup> m_groups{ nullptr };
    };

    class MockAddonService final : public IAddonService
    {
    public:
        MockAddonService();
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::Addon> Items() const override;
        bool Toggle(winrt::hstring const& name, bool enabled) override;
        bool Remove(winrt::hstring const& name) override;

    private:
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::Addon> m_items{ nullptr };
    };

    class MockSessionService final : public ISessionService
    {
    public:
        [[nodiscard]] winrt::hstring ServerUrl() const override;
        [[nodiscard]] winrt::hstring UserName() const override;
        [[nodiscard]] bool IsSignedIn() const noexcept override;
        bool TestServer(winrt::hstring const& url) override;
        bool SignIn(winrt::hstring const& user, winrt::hstring const& password) override;
        void SignOut() noexcept override;

    private:
        winrt::hstring m_serverUrl;
        winrt::hstring m_userName;
        bool m_isSignedIn{};
    };
}
