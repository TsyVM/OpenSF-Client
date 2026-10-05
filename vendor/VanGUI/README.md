<div align="center">

<img src="vangui-logo.png" width="600" alt="VanGUI"/>

<p><em>Immediate-Mode GUI with an Opt-In, Qt-Flavored Application Layer — for C++23</em></p>

<a href="#">
<img src="https://readme-typing-svg.demolab.com/?lines=Immediate-mode+core.+Qt-flavored+suite+on+top.;38+modules.+Every+one+zero-cost+when+off.;All+toggles+off+%3D+the+bare+core.;.vss+stylesheets.+Live+theme+transitions.;Signals+and+slots.+No+moc.+No+codegen.;Dialogs+that+return+std%3A%3Aexpected.;A+command+palette+in+your+own+app.;Rebuild+every+frame.+No+widget+tree.&font=Fira%20Code&center=true&width=700&height=45&color=E4C694&vCenter=true&size=20&pause=1800"/>
</a>

<br/>

[![License: MIT](https://img.shields.io/badge/License-MIT-E4C694?style=for-the-badge&labelColor=3F2B16)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-E4C694?style=for-the-badge&labelColor=3F2B16&logo=cplusplus&logoColor=E4C694)](https://en.cppreference.com/w/cpp/23)
[![Windows](https://img.shields.io/badge/Windows-x86%20%7C%20x64-E4C694?style=for-the-badge&labelColor=3F2B16&logo=windows&logoColor=E4C694)](lib/)
[![TeamVanilla](https://img.shields.io/badge/Team-TeamVanilla-E4C694?style=for-the-badge&labelColor=3F2B16)](https://www.teamvanilla.org/)

<br/>

[![Stars](https://img.shields.io/github/stars/tsyvm/vangui?style=for-the-badge&color=E4C694&labelColor=3F2B16)](../../stargazers)
[![Issues](https://img.shields.io/github/issues/tsyvm/vangui?style=for-the-badge&color=E4C694&labelColor=3F2B16)](../../issues)
[![Last Commit](https://img.shields.io/github/last-commit/tsyvm/vangui?style=for-the-badge&color=E4C694&labelColor=3F2B16)](../../commits)
[![Downloads](https://img.shields.io/github/downloads/tsyvm/vangui/total?style=for-the-badge&color=E4C694&labelColor=3F2B16)](../../releases)

<br/>

[![Immediate mode](https://img.shields.io/badge/Paradigm-immediate%20mode-E4C694?style=flat-square&labelColor=3F2B16)](#-features-at-a-glance)
[![Modules](https://img.shields.io/badge/Suite-38%20modules-E4C694?style=flat-square&labelColor=3F2B16)](#-the-suite)
[![Backends](https://img.shields.io/badge/Backends-21%20%C2%B7%20shipped%20as%20source-E4C694?style=flat-square&labelColor=3F2B16)](#-backends)
[![Theming](https://img.shields.io/badge/Theming-.vss%20stylesheets-E4C694?style=flat-square&labelColor=3F2B16)](#-the-suite)
[![Zero-cost](https://img.shields.io/badge/Unused-compiles%20to%20nothing-E4C694?style=flat-square&labelColor=3F2B16)](#-features-at-a-glance)
[![Exception-free](https://img.shields.io/badge/Extras-std%3A%3Aexpected-E4C694?style=flat-square&labelColor=3F2B16)](#-error-handling)

</div>

<img width="100%" src="https://capsule-render.vercel.app/api?type=rect&color=0:3F2B16,50:E4C694,100:3F2B16&height=3"/>

VanGUI is a C++23 immediate-mode GUI library. The interface is rebuilt every frame, there is no retained widget tree to keep in sync, the vertex output is optimized, and it draws inside whatever pipeline you already have. On top of that core sits a **strictly opt-in enhancement suite** that brings a finished application closer to Qt: animation and transitions, declarative theming (`.vss`), standard dialogs, signals and slots, model/view adapters, loading states, an actions registry, a command palette, and a fluent `van::` facade.

The defining constraint: **every enhancement is zero-cost when unused.** Each module is its own build switch; leave one off and its source compiles to nothing, and leave them all off and what remains is the bare immediate-mode core. This SDK ships the *everything-enabled* build.

VanGUI is distributed as **precompiled static libraries with public API headers**. Drop the headers and the matching `.lib` files into your project and link — no build system integration or source compilation required, apart from the one backend pair you choose.

<div align="center">

### 📑 Contents

[Features](#-features-at-a-glance) · [Where VanGUI Sits](#-where-vangui-sits) · [Requirements](#️-requirements) · [Installation](#-installation) · [Quick Start](#-quick-start)

[Frozen Config](#️-the-frozen-config-contract) · [The Suite](#-the-suite) · [Per-Frame Drivers](#-per-frame-drivers) · [Backends](#-backends)

[Error Handling](#-error-handling) · [Package Layout](#-package-layout) · [Known Limitations](#️-known-limitations) · [Build From Source](#-build-from-source)

</div>

<img width="100%" src="https://capsule-render.vercel.app/api?type=rect&color=0:3F2B16,50:E4C694,100:3F2B16&height=3"/>

## ✨ Features at a Glance

**The core**
- **Immediate mode** — the interface is rebuilt every frame, so there is no widget tree, no synchronisation step, and no state to leak between your data and the UI
- **Renderer agnostic** — VanGUI produces a `VanDrawData` vertex stream and never owns the device, the window, or the event loop; hand the stream to whichever backend you compiled
- **Zero-cost when off** — every enhancement is a build switch, and an unused module compiles to nothing
- **No third-party runtime dependencies** in the prebuilt libraries

**Application layer**
- **`van::` facade** — a fluent, designated-initializer API over the raw core: `window()`, `row()`, `button().on_click().tooltip()`
- **Animation** — tweens, springs and easing, advanced once per frame with no extra calls
- **Signals and slots** — RAII connections, header-only, **no moc and no code generator**
- **Model/view adapters** — virtualized lists that don't materialize what isn't on screen
- **Actions registry** — keyboard shortcuts and a **command palette** for your own app
- **Thread pool** — results delivered back on the main thread; without the module the same calls run synchronously

**Theming**
- **`.vss` stylesheets** — declarative theming with named themes and live reload
- **Theme engine and transitions** — themes cross-fade rather than snapping
- **Theme editor** — build a theme inside the running application

**Tooling widgets**
- Log console · property grid · curve and gradient editor · date-time picker · markdown renderer · hex editor · real-time plot · timeline and sequencer · data grid · node graph · charts · rich text · perf HUD · REPL

**Application chrome**
- Standard dialogs returning `std::expected` · toasts, banners and snackbars · loading spinners, progress rings and skeleton placeholders · layout helpers, forms and panels · a toolbar builder · drop zones · wizards

**`std::expected` on the extras** — the enhancement layer reports failure by value. No exceptions.

<img width="100%" src="https://capsule-render.vercel.app/api?type=rect&color=0:3F2B16,50:E4C694,100:3F2B16&height=3"/>

## 🧭 Where VanGUI Sits

| | Qt Widgets | **VanGUI** |
|---|---|---|
| **Paradigm** | Retained mode | **Immediate mode** |
| **Footprint** | Very large (100s of MB) | **Tiny core + à-la-carte extras** |
| **Renderer integration** | Owns the event loop | **Bring-your-own, any pipeline** |
| **Animation system** | ✓ | **✓ (opt-in `vangui_anim`)** |
| **Declarative theming** | QSS | **✓ `.vss` stylesheet** |
| **Signals / slots** | ✓ (moc) | **✓ header-only, RAII, no codegen** |
| **Model / view** | ✓ | **✓ virtualized adapters** |
| **Standard dialogs** | ✓ | **✓ `std::expected` results** |
| **Error handling** | exceptions | **`std::expected` on the extras** |
| **Build dependency** | qmake/CMake + moc | **none (CMake optional)** |
| **License** | LGPL / commercial | **MIT** |

The niche: *an immediate-mode toolkit that scales up to a real desktop application when you need it, and compiles back down to a tiny core when you don't.*

<img width="100%" src="https://capsule-render.vercel.app/api?type=rect&color=0:3F2B16,50:E4C694,100:3F2B16&height=3"/>

## 🛠️ Requirements

| | |
|---|---|
| **C++ standard** | C++23 |
| **MSVC** | 19.38+ (Visual Studio 2022 17.8+) |
| **CMake** | 3.28+ — optional; drop-in use needs no build system |
| **Windows target** | Windows 10 1903+ |
| **Platform** | Windows x86 / x64 for the prebuilt libraries |
| **Runtime dependencies** | None |

Backends ship as **source**, because each one links your graphics or platform SDK at *its* version (GLFW, SDL2/3, DirectX, Vulkan, …). Compile the one you need.

<img width="100%" src="https://capsule-render.vercel.app/api?type=rect&color=0:3F2B16,50:E4C694,100:3F2B16&height=3"/>

## 📦 Installation

Two static libraries, and you link **both**:

| Library | Contents |
|---|---|
| `vangui.lib` | The immediate-mode core, built with the guarded anim `NewFrame` hook. |
| `vangui_suite.lib` | The entire enhancement suite in one archive, plus the tooling and application modules. It provides the symbol the core's hook calls. |

| Target | Configuration | Path |
|---|---|---|
| Windows x64 | Release | `lib/win-x64/Release/` |
| Windows x64 | Debug | `lib/win-x64/Debug/` |
| Windows x86 | Release | `lib/win-x86/Release/` |
| Windows x86 | Debug | `lib/win-x86/Debug/` |

### Option A — CMake (recommended)

```cmake
list(APPEND CMAKE_PREFIX_PATH "path/to/VanGUISDK/cmake")
find_package(VanGUISDK REQUIRED)

target_link_libraries(my_app PRIVATE VanGUI::suite)   # pulls in VanGUI::core too
```

`VanGUI::suite` transitively links the core and, crucially, applies the frozen `VANGUI_ENABLE_*` compile-definition set so the public headers expose their real declarations rather than the zero-cost shims.

### Option B — MSVC project (manual)

1. **Additional Include Directories** → `include\`
2. **Additional Library Directories** → `lib\win-x64\Release\` (adjust arch/config)
3. **Additional Dependencies** → `vangui.lib;vangui_suite.lib`
4. **Preprocessor Definitions** → paste the `VANGUI_ENABLE_*` block from [`cmake/VanGUISDKConfig.cmake`](cmake/VanGUISDKConfig.cmake) so your translation units match the libraries
5. **Runtime Library** → match what the libs were built with: **`/MD`, `/MDd`** by default

> ⚠️ Step 4 is not optional. Without the macro set, `<vangui/van.h>` does not compile: its `van::` wrappers call suite functions unconditionally, and those functions are only declared when the macros are defined.

### Single include

```cpp
#include <vangui/van.h>        // fluent facade, pulls in the core and every enabled module
#include <vangui/vangui.h>     // the raw immediate-mode core on its own
```

The include root is `include/`, so the `vangui/` prefix is part of the path.

<img width="100%" src="https://capsule-render.vercel.app/api?type=rect&color=0:3F2B16,50:E4C694,100:3F2B16&height=3"/>

## ⚡ Quick Start

One include gives you the fluent facade:

```cpp
#include <vangui/van.h>
using namespace van;

// inside your frame, between VanGui::NewFrame() and VanGui::Render():
if (auto w = window("Settings", { .size = {420, 300}, .p_open = &open })) {
    heading("Display");
    row([&]{
        button("Save", { .primary = true }).on_click(save).tooltip("write to disk");
        button("Cancel").on_click([&]{ open = false; });
    });
    checkbox("V-Sync", cfg.vsync);
    slider("Volume", cfg.volume, 0.f, 100.f, "%.0f%%");
    toast_success("Saved");                 // lights up because notify is linked
}
```

Or drive the core directly:

```cpp
#include <vangui/vangui.h>

VanGui::CreateContext();
VanGuiIO& io = VanGui::GetIO();
io.DisplaySize = VanVec2(1280.0f, 720.0f);

VanGui::NewFrame();
VanGui::Begin("Hello");
VanGui::TextUnformatted("world");
VanGui::End();
VanGui::Render();

const VanDrawData* dd = VanGui::GetDrawData();   // hand this to your renderer backend
```

<img width="100%" src="https://capsule-render.vercel.app/api?type=rect&color=0:3F2B16,50:E4C694,100:3F2B16&height=3"/>

## ⚠️ The Frozen-Config Contract

VanGUI couples ABI to compile-time configuration: options in `vanconfig.h` (`VANGUI_USE_WCHAR32`, `VANGUI_USE_BGRA_PACKED_COLOR`, the disable-flags, `VANGUI_DEFINE_MATH_OPERATORS`) change the layout of structs shared through the *public* header. The prebuilt libraries here were compiled against the `vanconfig.h` shipped in this package.

**Do not edit layout-affecting `vanconfig.h` defines when linking these libraries.** The result is silent ABI corruption, not a link error. If you need a different core configuration, [build from source](#-build-from-source).

The 38 `VANGUI_ENABLE_*` macros are part of the same contract. They are supplied for you by `VanGUI::suite`; don't add or remove them by hand.

<img width="100%" src="https://capsule-render.vercel.app/api?type=rect&color=0:3F2B16,50:E4C694,100:3F2B16&height=3"/>

## 🧩 The Suite

38 modules, each its own build switch, all of them enabled in these binaries.

**Application building blocks**

| Macro | Module |
|---|---|
| `ANIM` | Tweens, springs and easing, advanced once per frame |
| `LOADING` | Spinners, progress rings, skeleton placeholders |
| `NOTIFY` | Toast notifications |
| `FEEDBACK` | Feedback prompts |
| `BANNERS` | Banners and snackbars |
| `DIALOGS` | Standard dialogs returning `std::expected` |
| `LAYOUT` | Layout helpers and application chrome |
| `FORMS` | Form construction |
| `PANELS` | Dockable panels |
| `VIEWS` | Model/view adapters with virtualized lists |
| `SIGNALS` | Signals and slots with RAII connections, no code generator |
| `ACTIONS` | An actions registry |
| `SHORTCUTS` | Keyboard shortcuts |
| `COMMAND_PALETTE` | A command palette |
| `TOOLBAR` | A toolbar builder |
| `DROPZONE` | Drag-and-drop targets |
| `WIZARD` | Multi-step wizards |
| `VUI` | `.vui` layout loader |
| `THREAD` | A thread pool whose results arrive on the main thread |

**Theming**

| Macro | Module |
|---|---|
| `THEMES` | Named themes |
| `THEME_ENGINE` | The theme engine and transitions |
| `STYLESHEET` | `.vss` stylesheets |
| `THEME_EDITOR` | An in-application theme editor |

**Tooling widgets**

| Macro | Module |
|---|---|
| `WIDGETS_EXT` | Extended widget pack |
| `CHARTS` | Charts |
| `PLOT` | Real-time plotting |
| `NODE_GRAPH` | A node graph |
| `CONSOLE` | A log console |
| `PROPERTY_GRID` | A property grid |
| `CURVE_EDITOR` | Curve and gradient editing |
| `DATETIME` | A date-time picker |
| `MARKDOWN` | A markdown renderer |
| `RICHTEXT` | Rich text |
| `HEX_EDITOR` | A hex editor |
| `DATA_GRID` | A data grid |
| `TIMELINE` | A timeline and sequencer |
| `REPL` | An interactive REPL |
| `PERF_HUD` | A performance HUD |

Header-only modules — signals, undo, settings, reflect, theme-gen, scoped guards, shorthand, the widgets pack, and the `enhance` drivers — have no `.cpp` and live entirely in the shipped headers.

<img width="100%" src="https://capsule-render.vercel.app/api?type=rect&color=0:3F2B16,50:E4C694,100:3F2B16&height=3"/>

## 🔁 Per-Frame Drivers

Some modules need a tick: theme transitions, toasts, the command palette, thread results. Two calls drive all of them, so you never hand-call each module.

```cpp
#include <vangui/misc/vangui_enhance.h>

VanGui::NewFrame();
VanGui::NewFrameExtras();     // advance transitions, reload stylesheets, drain thread results
// ... your UI ...
VanGui::RenderExtras();       // draw toasts and command-palette overlays
VanGui::Render();

if (!VanGui::EnhanceWantsRedraw()) {
    // nothing is animating: the backend may sleep until the next input event
}
```

<img width="100%" src="https://capsule-render.vercel.app/api?type=rect&color=0:3F2B16,50:E4C694,100:3F2B16&height=3"/>

## 🎛️ Backends

Pick one platform backend and one renderer backend from `backends/`, and add their `.cpp` to your build. They compile against the core headers already on your include path.

| | |
|---|---|
| **Platform** | `win32` · `glfw` · `sdl2` · `sdl3` · `osx` · `android` · `glut` |
| **Renderer** | `dx9` · `dx10` · `dx11` · `dx12` · `opengl2` · `opengl3` · `vulkan` · `metal` · `wgpu` · `sdlrenderer2` · `sdlrenderer3` · `sdlgpu3` · `allegro5` · `null` |

```
vangui_impl_win32.cpp + vangui_impl_dx11.cpp        // a Windows game overlay
vangui_impl_glfw.cpp  + vangui_impl_opengl3.cpp     // a portable desktop tool
```

They ship as source deliberately: a prebuilt backend would pin your GLFW, SDL or DirectX SDK to whichever version this package was built against.

<img width="100%" src="https://capsule-render.vercel.app/api?type=rect&color=0:3F2B16,50:E4C694,100:3F2B16&height=3"/>

## ❌ Error Handling

The immediate-mode core reports problems the way the core always has: asserts and booleans. The enhancement layer reports failure by value, with `std::expected`, so a dialog the user cancelled is not an exception:

```cpp
auto path = van::open_file_dialog({ .title = "Open", .filter = "*.png" });
if (!path) {
    // path.error() says why: cancelled, unavailable, or a platform failure
    return;
}
load_image(*path);
```

No exceptions are thrown across the library boundary.

<img width="100%" src="https://capsule-render.vercel.app/api?type=rect&color=0:3F2B16,50:E4C694,100:3F2B16&height=3"/>

## 📁 Package Layout

```
VanGUISDK/
├── include/vangui/
│   ├── vangui.h                    ← the immediate-mode core
│   ├── vanconfig.h                 ← frozen: do not edit layout-affecting defines
│   ├── van.h                       ← fluent facade, namespace van::
│   ├── van_kit.h                   ← amalgamated header-only utilities
│   ├── vanstb_*.h                  ← bundled stb (public domain)
│   └── misc/                       ← public header for every suite module
│       └── cpp/vangui_stdlib.h     ← std::string InputText overloads
├── backends/                       ← platform + renderer backends, as SOURCE
├── lib/
│   ├── win-x64/{Release,Debug}/    vangui.lib + vangui_suite.lib
│   └── win-x86/{Release,Debug}/    vangui.lib + vangui_suite.lib
├── cmake/
│   └── VanGUISDKConfig.cmake       ← IMPORTED targets + the frozen macro set
├── VanGUI_Suite_Guide.md
├── LICENSE
└── NOTICE
```

<img width="100%" src="https://capsule-render.vercel.app/api?type=rect&color=0:3F2B16,50:E4C694,100:3F2B16&height=3"/>

## ⚠️ Known Limitations

| Item | Status |
|---|---|
| **`van.h` requires the macro set** | Without the 38 `VANGUI_ENABLE_*` definitions the header does not compile: its `van::` wrappers call suite functions unconditionally, while the declarations are guarded. `VanGUI::suite` supplies them; a hand-rolled MSVC project has to paste the block. |
| **Both libraries are required** | The core carries a guarded anim `NewFrame` hook whose symbol lives in the suite. Linking `vangui.lib` alone works only if no Anim function is ever called. |
| **Editing `vanconfig.h` corrupts the ABI silently** | Layout-affecting defines change struct layouts shared through the public header. You get misread memory, not a link error. |
| **Dynamic CRT by default** | These libraries are `/MD` and `/MDd`, the usual choice for an application-linked toolkit. Mixing with a `/MT` target is an `LNK2038`. Rebuild from source with `CRT=MT` if you need static. |
| **Backends must be compiled** | They ship as source on purpose, so they bind to *your* SDK version rather than ours. |
| **Windows x86 / x64 only, prebuilt** | ARM64, Linux and macOS are supported by the source distribution; they are simply not in this package. |
| **No module target** | The C++20 `import vangui;` target is a source-build option and is not part of these binaries. |

<img width="100%" src="https://capsule-render.vercel.app/api?type=rect&color=0:3F2B16,50:E4C694,100:3F2B16&height=3"/>

## 🔨 Build From Source

These libraries are the everything-enabled convenience build. For à-la-carte features, a custom `vanconfig.h`, other architectures, or the C++20 module target, build from the VanGUI source repository, where every module is its own `VANGUI_MISC_*` CMake option:

```bat
:: à la carte — enable only what you need (each is zero-cost when off)
cmake -S VanGUI -B build -DVANGUI_MISC_ANIM=ON -DVANGUI_MISC_NOTIFY=ON

:: or the aggregate suite in one lib, which is what this package ships
cmake -S VanGUI -B build -DVANGUI_BUILD_SUITE=ON
cmake --build build --config Release --target vangui vangui_suite
```

See [`lib/README.md`](lib/README.md) for the library matrix and the CRT options.

<img width="100%" src="https://capsule-render.vercel.app/api?type=rect&color=0:3F2B16,50:E4C694,100:3F2B16&height=3"/>

## 📖 Documentation

Full API reference, every pillar, and the enable-macro each one rides on: **[VanGUI Suite Guide](VanGUI_Suite_Guide.md)**.

Third-party and provenance notices for the bundled components are in [`NOTICE`](NOTICE).

<div align="center">

<sub>Built and maintained by <a href="https://github.com/TsyVM">TsyVM</a> · <a href="https://www.teamvanilla.org/">TeamVanilla</a></sub>

<img width="100%" src="https://capsule-render.vercel.app/api?type=waving&color=0:E4C694,100:3F2B16&height=80&section=footer"/>

</div>
