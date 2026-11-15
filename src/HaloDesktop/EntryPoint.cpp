#include "pch.h"

#include "Security/InjectionGuard.h"

// The XAML compiler's generated entry point, emitted under this name instead of
// wWinMain because the project defines DISABLE_XAML_GENERATED_MAIN. It starts
// the apartment and the XAML application, which constructs App.
int __stdcall wXamlGeneratedMain();

// The process entry point. The extension-point policy has to be in force
// before the first window exists, and the generated main creates the XAML
// framework's windows, so it is applied here rather than in App.
int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    ::HaloDesktop::Security::RefuseExtensionPoints();
    return wXamlGeneratedMain();
}
