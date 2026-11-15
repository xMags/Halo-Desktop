#pragma once

#include <optional>
#include <winrt/base.h>

namespace HaloDesktop::Services
{
    // An addon's name as a page may print it: control characters dropped, and
    // anything that looks like a URL, a path or a token replaced outright, since
    // a misconfigured addon can report its manifest URL or a key as its name.
    [[nodiscard]] winrt::hstring SanitizeAddonName(std::optional<winrt::hstring> const& name);

    // Sentence fragments rather than badges: the sources sheet reads them as
    // "<provider> did not answer in time", so they have to complete that sentence.
    [[nodiscard]] winrt::hstring AddonFailureCopy(std::optional<winrt::hstring> const& code);
}
