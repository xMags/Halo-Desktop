#pragma once

#include "Services/ServiceInterfaces.h"

namespace HaloDesktop::Services
{
    // Mock services are UI-thread-only. They return immutable snapshots except for
    // observable collections explicitly named by their interfaces.
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

}
