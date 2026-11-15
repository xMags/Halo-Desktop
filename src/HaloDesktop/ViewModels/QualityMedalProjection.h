#pragma once

#include "ViewModels/QualityMedalPolicy.h"

#include <winrt/HaloDesktop.h>

namespace HaloDesktop::ViewModels
{
    // The policy's finish as the medal control's projected enum. Kept apart from the
    // policy so the policy stays free of generated WinRT types for the tests.
    [[nodiscard]] inline winrt::HaloDesktop::QualityMedalFinish ToQualityMedalFinish(MedalFinish finish) noexcept
    {
        switch (finish)
        {
        case MedalFinish::Gold: return winrt::HaloDesktop::QualityMedalFinish::Gold;
        case MedalFinish::Silver: return winrt::HaloDesktop::QualityMedalFinish::Silver;
        case MedalFinish::Bronze: return winrt::HaloDesktop::QualityMedalFinish::Bronze;
        case MedalFinish::Plain: break;
        }
        return winrt::HaloDesktop::QualityMedalFinish::Plain;
    }
}
