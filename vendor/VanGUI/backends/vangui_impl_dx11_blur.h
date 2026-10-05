// VanGUI: backdrop blur for the DirectX 11 renderer backend.
//
// Makes DrawBackdropBlur() (misc/vangui_effects) blur for real: the region behind the
// panel is copied out of the render target being drawn to, blurred on the GPU
// (downsampled, separable Gaussian) and drawn back inside the panel's rounded
// rectangle, then the panel's tint goes on top. Multisampled targets are resolved
// first. Without this, DrawBackdropBlur() falls back to a denser tint.
//
//   VanGui_ImplDX11_Init(device, context);
//   VanGui_ImplDX11_InitBlur(device);        // installs the handler (SetBlurHandler)
//   ...
//   VanGui_ImplDX11_ShutdownBlur();          // before VanGui_ImplDX11_Shutdown()
//
// Needs the effects module (VANGUI_ENABLE_EFFECTS); without it both calls do nothing.

#pragma once
#include <vangui/vangui.h>      // VANGUI_IMPL_API
#ifndef VANGUI_DISABLE

struct ID3D11Device;

VANGUI_IMPL_API bool VanGui_ImplDX11_InitBlur(ID3D11Device* device);
VANGUI_IMPL_API void VanGui_ImplDX11_ShutdownBlur();

#endif // #ifndef VANGUI_DISABLE
