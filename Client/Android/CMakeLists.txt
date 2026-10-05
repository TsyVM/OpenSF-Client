# Soldier Front Legacy for Android: liblegacysf.so, the native side of GameActivity (a
# NativeActivity). Built per ABI with the NDK's toolchain file by Client/Android/build_apk.py.
# No server is linked in: a phone plays, it does not host (PF-1).
cmake_minimum_required(VERSION 3.22)
project(legacysf C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/Sources.cmake)
include(${LSF_ROOT}/cmake/Shaders.cmake)

set(VANGUI "${LSF_ROOT}/vendor/VanGUI")
set(GLUE "${ANDROID_NDK}/sources/android/native_app_glue")

add_library(native_app_glue STATIC "${GLUE}/android_native_app_glue.c")
target_include_directories(native_app_glue PUBLIC "${GLUE}")

# VanGUI's two platform pieces for Android: its OpenGL ES renderer and its Android input.
add_library(vangui_backends STATIC "${VANGUI}/backends/vangui_impl_opengl3.cpp" "${VANGUI}/backends/vangui_impl_android.cpp")
target_include_directories(vangui_backends PUBLIC "${VANGUI}/include" "${VANGUI}/include/vangui" "${VANGUI}/backends")
target_compile_definitions(vangui_backends PRIVATE VANGUI_IMPL_OPENGL_ES3)

add_library(legacysf SHARED
    ${LSF_SHARED_SOURCES}
    ${LSF_ENGINE_CLIENT_SOURCES}
    ${LSF_ROOT}/Client/Source/Engine/Platform/Window_android.cpp
    ${LSF_CLIENT_SOURCES}
    ${LSF_ROOT}/Client/Android/AndroidMain.cpp)
lsf_embed_shaders(legacysf)

target_include_directories(legacysf PRIVATE
    "${LSF_ROOT}/Shared" "${LSF_ROOT}/Client/Source" "${LSF_ROOT}/vendor/SFSDK/include"
    "${LSF_ROOT}/vendor/stb" "${LSF_ROOT}/vendor/minimp3" "${VANGUI}/include" "${VANGUI}/include/vangui" "${VANGUI}/backends")
# VanGUI's frozen macro set: the same as its libraries were built with (vendor/VanGUI/cmake).
target_compile_definitions(legacysf PRIVATE
    VANGUI_ENABLE_ANIM VANGUI_ENABLE_LOADING VANGUI_ENABLE_NOTIFY VANGUI_ENABLE_COMMAND_PALETTE
    VANGUI_ENABLE_THEMES VANGUI_ENABLE_NODE_GRAPH VANGUI_ENABLE_LAYOUT VANGUI_ENABLE_DIALOGS
    VANGUI_ENABLE_STYLESHEET VANGUI_ENABLE_SIGNALS VANGUI_ENABLE_VIEWS VANGUI_ENABLE_WIDGETS_EXT
    VANGUI_ENABLE_CHARTS VANGUI_ENABLE_FEEDBACK VANGUI_ENABLE_FORMS VANGUI_ENABLE_PANELS
    VANGUI_ENABLE_SHORTCUTS VANGUI_ENABLE_VUI VANGUI_ENABLE_THEME_ENGINE VANGUI_ENABLE_THEME_EDITOR
    VANGUI_ENABLE_THREAD VANGUI_ENABLE_ACTIONS VANGUI_ENABLE_TOOLBAR VANGUI_ENABLE_DROPZONE
    VANGUI_ENABLE_WIZARD VANGUI_ENABLE_CONSOLE VANGUI_ENABLE_PROPERTY_GRID VANGUI_ENABLE_CURVE_EDITOR
    VANGUI_ENABLE_DATETIME VANGUI_ENABLE_MARKDOWN VANGUI_ENABLE_HEX_EDITOR VANGUI_ENABLE_PLOT
    VANGUI_ENABLE_TIMELINE VANGUI_ENABLE_DATA_GRID VANGUI_ENABLE_BANNERS VANGUI_ENABLE_REPL
    VANGUI_ENABLE_RICHTEXT VANGUI_ENABLE_PERF_HUD VANGUI_ENABLE_VECTOR
    VANGUI_ENABLE_RASTER VANGUI_ENABLE_SVG VANGUI_ENABLE_EFFECTS VANGUI_ENABLE_ICONS VANGUI_ENABLE_GALLERY
    VANGUI_ENABLE_EXTRAS VANGUI_ENABLE_TOUCH VANGUI_ENABLE_GIZMO)
target_compile_options(legacysf PRIVATE -Wall -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
    -Wno-missing-braces -Wno-sign-compare -Wno-unused-private-field -Wno-reorder-ctor -Wno-deprecated-declarations
    -Wno-date-time -fno-strict-aliasing)
target_link_libraries(legacysf PRIVATE native_app_glue vangui_backends
    "${VANGUI}/lib/android-${ANDROID_ABI}/libvangui_suite.a" "${VANGUI}/lib/android-${ANDROID_ABI}/libvangui.a"
    android log EGL GLESv3 OpenSLES z m)
# The app glue's entry point is called by the system, so the linker must keep it.
target_link_options(legacysf PRIVATE -u ANativeActivity_onCreate -Wl,--gc-sections)
