# lib/

Pre-compiled static libraries for Windows, produced by `build_sdk.bat` in the
VanGUI source tree.

```
lib/
  win-x64/
    Debug/    vangui.lib  vangui_suite.lib   (MSVC x64)
    Release/  vangui.lib  vangui_suite.lib
  win-x86/
    Debug/    vangui.lib  vangui_suite.lib   (MSVC x86)
    Release/  vangui.lib  vangui_suite.lib
```

**Link both** `vangui.lib` and `vangui_suite.lib`. The core carries the guarded
`NewFrame` animation hook; the suite provides the symbol it calls, plus every
enhancement module in a single archive.

### CRT

By default the libraries are built with the **dynamic** CRT (`/MD`, `/MDd`) —
the CMake default and the most common choice for an application-linked toolkit.
Match your app's runtime library to the libs you link, or an
`RuntimeLibrary mismatch` (LNK2038) results.

To produce static-CRT libraries instead (`/MT`, `/MTd`) — e.g. for DLL injection
or redistributable-free deployment — rebuild with:

```bat
build_sdk.bat CRT=MT
```

### Other targets (ARM64, Linux, macOS) and custom configuration

These prebuilt libs are the Windows x86/x64 "everything-enabled" convenience
build against the SDK's frozen `vanconfig.h`. For other architectures, other
platforms, a custom `vanconfig.h`, à-la-carte feature selection, or the C++20
`import vangui;` module target, build from the VanGUI source repository:

```bat
:: à la carte — enable only what you need (each is zero-cost when off)
cmake -S VanGUI -B build -DVANGUI_MISC_ANIM=ON -DVANGUI_MISC_NOTIFY=ON ...

:: or the aggregate suite in one lib (what this SDK ships)
cmake -S VanGUI -B build -DVANGUI_BUILD_SUITE=ON
cmake --build build --config Release --target vangui vangui_suite
```
