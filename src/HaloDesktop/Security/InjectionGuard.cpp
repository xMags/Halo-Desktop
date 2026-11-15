#include "Security/InjectionGuard.h"

#include <windows.h>

#include <cwchar>

namespace HaloDesktop::Security
{
    void RefuseExtensionPoints() noexcept
    {
        PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY policy{};
        policy.DisableExtensionPoints = 1;
        if (SetProcessMitigationPolicy(ProcessExtensionPointDisablePolicy, &policy, sizeof(policy)))
        {
            return;
        }
        wchar_t message[96]{};
        std::swprintf(
            message,
            sizeof(message) / sizeof(message[0]),
            L"Halo could not refuse extension-point DLLs: error %lu\n",
            GetLastError());
        OutputDebugStringW(message);
    }
}
