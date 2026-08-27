Run `tools/Build-VulkanLoader.ps1` to produce the ignored `vulkan-1.dll`.

`libmpv-2.dll` carries a static import of `vulkan-1.dll`, so the loader has to
be present or libmpv cannot be loaded at all. `vulkan-1.dll` is not a Windows
component: it arrives with GPU drivers, so a clean install, a fresh VM, or
Windows Sandbox does not have one and Halo would fail to start playback there.
Shipping the loader in the application folder removes that dependency on the
target machine.

The loader is a small shim that enumerates installed drivers and forwards calls
to them. It is not a driver and does not provide Vulkan where no driver exists.
With none installed it simply reports none, mpv's Vulkan context creation fails,
and playback continues on D3D11, which is the path Halo uses anyway. Users who
do have a driver keep working Vulkan.

It is built from pinned Khronos sources rather than copied out of an installed
SDK so the redistributed binary has provenance the repository controls.
`VERSION.txt` records the pinned SDK tag; the licence is Apache 2.0, in
`licenses/third-party/KhronosGroup.Vulkan-Loader--LICENSE.txt`.
