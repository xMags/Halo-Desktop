#include "pch.h"
#include "Playback/WindowsAudioSession.h"

#include <algorithm>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

namespace
{
    using Microsoft::WRL::ComPtr;

    std::optional<ComPtr<IAudioSessionEnumerator>> Sessions() noexcept
    {
        ComPtr<IMMDeviceEnumerator> deviceEnumerator;
        if (FAILED(CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&deviceEnumerator))))
        {
            return std::nullopt;
        }
        ComPtr<IMMDevice> device;
        if (FAILED(deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device)))
        {
            return std::nullopt;
        }
        ComPtr<IAudioSessionManager2> manager;
        // IMMDevice::Activate exposes the queried COM interface through void**.
        // ReleaseAndGetAddressOf keeps the ComPtr empty before that ownership transfer.
        if (FAILED(device->Activate(
            __uuidof(IAudioSessionManager2),
            CLSCTX_INPROC_SERVER,
            nullptr,
            reinterpret_cast<void**>(manager.ReleaseAndGetAddressOf()))))
        {
            return std::nullopt;
        }
        ComPtr<IAudioSessionEnumerator> sessions;
        if (FAILED(manager->GetSessionEnumerator(&sessions)))
        {
            return std::nullopt;
        }
        return sessions;
    }

    template<typename Callback>
    bool ForCurrentProcessSession(Callback&& callback) noexcept
    {
        auto const sessions = Sessions();
        if (!sessions)
        {
            return false;
        }
        int count{};
        if (FAILED((*sessions)->GetCount(&count)))
        {
            return false;
        }
        auto const processId = GetCurrentProcessId();
        auto matched = false;
        for (int index = 0; index < count; ++index)
        {
            ComPtr<IAudioSessionControl> control;
            if (FAILED((*sessions)->GetSession(index, &control)))
            {
                continue;
            }
            ComPtr<IAudioSessionControl2> control2;
            DWORD ownerProcessId{};
            if (FAILED(control.As(&control2))
                || FAILED(control2->GetProcessId(&ownerProcessId))
                || ownerProcessId != processId)
            {
                continue;
            }
            ComPtr<ISimpleAudioVolume> volume;
            if (FAILED(control.As(&volume)))
            {
                continue;
            }
            matched = callback(volume.Get()) || matched;
        }
        return matched;
    }
}

namespace HaloDesktop::Playback
{
    std::optional<WindowsAudioSessionState> WindowsAudioSession::Read() noexcept
    {
        std::optional<WindowsAudioSessionState> result;
        ForCurrentProcessSession([&result](ISimpleAudioVolume* volume) noexcept
        {
            float level{};
            BOOL muted{};
            if (FAILED(volume->GetMasterVolume(&level)) || FAILED(volume->GetMute(&muted)))
            {
                return false;
            }
            result = WindowsAudioSessionState{
                .Volume = std::clamp(static_cast<double>(level), 0.0, 1.0),
                .Muted = muted != FALSE,
            };
            return true;
        });
        return result;
    }

    bool WindowsAudioSession::SetVolume(double volume, bool unmute) noexcept
    {
        auto const normalized = static_cast<float>(std::clamp(volume, 0.0, 1.0));
        return ForCurrentProcessSession([normalized, unmute](ISimpleAudioVolume* session) noexcept
        {
            if (FAILED(session->SetMasterVolume(normalized, nullptr)))
            {
                return false;
            }
            return !unmute || SUCCEEDED(session->SetMute(FALSE, nullptr));
        });
    }
}
