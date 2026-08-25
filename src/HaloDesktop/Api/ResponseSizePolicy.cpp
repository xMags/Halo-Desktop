#include "Api/ResponseSizePolicy.h"

#include <stdexcept>

namespace
{
    [[noreturn]] void ThrowResponseTooLarge()
    {
        throw std::length_error{ "The server response exceeded the size limit." };
    }
}

namespace HaloDesktop::Api
{
    void ValidateDeclaredResponseSize(
        std::optional<std::uint64_t> declaredBytes,
        std::size_t maximumBytes)
    {
        if (declaredBytes && *declaredBytes > maximumBytes)
        {
            ThrowResponseTooLarge();
        }
    }

    std::size_t CheckedResponseSize(
        std::size_t currentBytes,
        std::size_t incomingBytes,
        std::size_t maximumBytes)
    {
        if (currentBytes > maximumBytes || incomingBytes > maximumBytes - currentBytes)
        {
            ThrowResponseTooLarge();
        }
        return currentBytes + incomingBytes;
    }
}
