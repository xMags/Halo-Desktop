#pragma once

#include "Services/DevicePreferencesStore.h"

#include <filesystem>
#include <optional>

namespace HaloDesktop::Storage
{
    struct LegacyPackageData final
    {
        std::filesystem::path LocalState;
        ::HaloDesktop::Services::DevicePreferences Preferences;
    };

    class LegacyPackageDataSource
    {
    public:
        virtual ~LegacyPackageDataSource() = default;
        [[nodiscard]] virtual std::optional<LegacyPackageData> Read() = 0;
    };

    // Reads only the prior Halo MSIX identity. It never changes or deletes the
    // package, its registration, its settings, or any file in its data store.
    class InstalledLegacyPackageDataSource final : public LegacyPackageDataSource
    {
    public:
        [[nodiscard]] std::optional<LegacyPackageData> Read() override;
    };
}
