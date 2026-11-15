#pragma once

namespace HaloDesktop::Security
{
    // Keeps other programs' hook DLLs out of this process.
    //
    // On-screen overlays (an FPS counter, say) load a DLL into every program
    // that draws with the GPU through a global window hook, then patch its
    // Direct3D calls. mpv draws every video frame in this process, so such a
    // hook runs on each frame, and a fault in it takes the whole app down in
    // the middle of playback. Windows lets a process refuse these legacy
    // extension points (global window-hook DLLs, AppInit DLLs and Winsock
    // LSPs).
    //
    // The policy only stops DLLs that have not loaded yet, and a hook DLL loads
    // the first time this process handles window messages, so it must be set
    // before any window exists. Windows does not let a process turn it off
    // again. What it costs: legacy (pre-TSF) input methods and tools that work
    // by injecting a hook DLL cannot load into this process; current input
    // methods use TSF, so typing is unaffected.
    //
    // A failure leaves the process as every earlier build ran, so it is
    // reported to the debugger output and startup goes on.
    void RefuseExtensionPoints() noexcept;
}
