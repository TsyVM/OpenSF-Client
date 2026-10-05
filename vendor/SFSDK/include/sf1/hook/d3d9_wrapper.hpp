// SPDX-License-Identifier: MIT
// sf1/hook/d3d9_wrapper.hpp — D3D9 device wrapper + callback registrar.
//
// Ported from NiHooks' d3d9_wrapper.cpp. The device is wrapped, not the vtable —
// callbacks fire around Present and Reset so the hook layer can drive an ImGui
// overlay and content pipeline without touching XignCode's hook detection surface.
#pragma once

#include "../result.hpp"
#include <functional>

struct IDirect3DDevice9;

namespace sf1::hook {

using DeviceFn        = std::function<void(IDirect3DDevice9*)>;
using DevicelessFn    = std::function<void()>;

class D3D9Wrapper {
public:
    [[nodiscard]] static Result<D3D9Wrapper*> install() noexcept;

    D3D9Wrapper& on_pre_present (DeviceFn f) noexcept;
    D3D9Wrapper& on_pre_reset   (DevicelessFn f) noexcept;
    D3D9Wrapper& on_post_reset  (DevicelessFn f) noexcept;
};

}  // namespace sf1::hook
