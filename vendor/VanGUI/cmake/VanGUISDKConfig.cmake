# VanGUISDKConfig.cmake — drop-in package for the prebuilt VanGUI SDK.
#
#   list(APPEND CMAKE_PREFIX_PATH "path/to/VanGUISDK/cmake")
#   find_package(VanGUISDK REQUIRED)
#   target_link_libraries(my_app PRIVATE VanGUI::suite)
#
# VanGUI::suite transitively links VanGUI::core AND applies the frozen
# VANGUI_ENABLE_* macro set, so the public headers expose their real
# declarations instead of the zero-cost shims. Link VanGUI::core alone only if
# you deliberately want the bare immediate-mode core with no enhancement suite
# (note: the prebuilt core still contains the guarded anim NewFrame hook, whose
# symbol the suite provides — so core-only linking requires the anim symbol to
# be otherwise unused, which is the default when you never call an Anim function).

cmake_minimum_required(VERSION 3.28)

# SDK root = parent of this cmake/ directory.
get_filename_component(_VANGUI_SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(_VANGUI_INCLUDE "${_VANGUI_SDK_ROOT}/include")

if(NOT EXISTS "${_VANGUI_INCLUDE}/vangui/vangui.h")
    message(FATAL_ERROR "VanGUISDK: headers not found at ${_VANGUI_INCLUDE}. Is the SDK layout intact?")
endif()

# ---- architecture -> lib subdir ----
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_VANGUI_ARCH "win-x64")
else()
    set(_VANGUI_ARCH "win-x86")
endif()
set(_VANGUI_LIBDIR "${_VANGUI_SDK_ROOT}/lib/${_VANGUI_ARCH}")

# The frozen macro set (must match how the .libs were compiled).
set(_VANGUI_SUITE_DEFS
    VANGUI_ENABLE_ANIM            VANGUI_ENABLE_LOADING
    VANGUI_ENABLE_NOTIFY          VANGUI_ENABLE_COMMAND_PALETTE
    VANGUI_ENABLE_THEMES          VANGUI_ENABLE_NODE_GRAPH
    VANGUI_ENABLE_LAYOUT          VANGUI_ENABLE_DIALOGS
    VANGUI_ENABLE_STYLESHEET      VANGUI_ENABLE_SIGNALS
    VANGUI_ENABLE_VIEWS           VANGUI_ENABLE_WIDGETS_EXT
    VANGUI_ENABLE_CHARTS          VANGUI_ENABLE_FEEDBACK
    VANGUI_ENABLE_FORMS           VANGUI_ENABLE_PANELS
    VANGUI_ENABLE_SHORTCUTS       VANGUI_ENABLE_VUI
    VANGUI_ENABLE_THEME_ENGINE    VANGUI_ENABLE_THEME_EDITOR
    VANGUI_ENABLE_THREAD          VANGUI_ENABLE_ACTIONS
    VANGUI_ENABLE_TOOLBAR         VANGUI_ENABLE_DROPZONE
    VANGUI_ENABLE_WIZARD
    # Tier 1-3 additions
    VANGUI_ENABLE_CONSOLE         VANGUI_ENABLE_PROPERTY_GRID
    VANGUI_ENABLE_CURVE_EDITOR    VANGUI_ENABLE_DATETIME
    VANGUI_ENABLE_MARKDOWN        VANGUI_ENABLE_HEX_EDITOR
    VANGUI_ENABLE_PLOT            VANGUI_ENABLE_TIMELINE
    VANGUI_ENABLE_DATA_GRID       VANGUI_ENABLE_BANNERS
    VANGUI_ENABLE_REPL            VANGUI_ENABLE_RICHTEXT
    VANGUI_ENABLE_PERF_HUD        VANGUI_ENABLE_VECTOR
    # Effects & icons pass
    VANGUI_ENABLE_RASTER          VANGUI_ENABLE_SVG
    VANGUI_ENABLE_EFFECTS         VANGUI_ENABLE_ICONS
    VANGUI_ENABLE_GALLERY
    # Lightweight-features passes
    VANGUI_ENABLE_EXTRAS          VANGUI_ENABLE_TOUCH
    # Gizmos
    VANGUI_ENABLE_GIZMO)

# ---- VanGUI::core ----
if(NOT TARGET VanGUI::core)
    add_library(VanGUI::core STATIC IMPORTED)
    set_target_properties(VanGUI::core PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_VANGUI_INCLUDE}"
        INTERFACE_COMPILE_FEATURES    "cxx_std_23"
        IMPORTED_LOCATION             "${_VANGUI_LIBDIR}/Release/vangui.lib"
        IMPORTED_LOCATION_RELEASE     "${_VANGUI_LIBDIR}/Release/vangui.lib"
        IMPORTED_LOCATION_DEBUG       "${_VANGUI_LIBDIR}/Debug/vangui.lib")
endif()

# ---- VanGUI::suite ----
if(NOT TARGET VanGUI::suite)
    add_library(VanGUI::suite STATIC IMPORTED)
    set_target_properties(VanGUI::suite PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES  "${_VANGUI_INCLUDE}"
        INTERFACE_COMPILE_FEATURES     "cxx_std_23"
        INTERFACE_COMPILE_DEFINITIONS  "${_VANGUI_SUITE_DEFS}"
        INTERFACE_LINK_LIBRARIES       "VanGUI::core"
        IMPORTED_LOCATION              "${_VANGUI_LIBDIR}/Release/vangui_suite.lib"
        IMPORTED_LOCATION_RELEASE      "${_VANGUI_LIBDIR}/Release/vangui_suite.lib"
        IMPORTED_LOCATION_DEBUG        "${_VANGUI_LIBDIR}/Debug/vangui_suite.lib")
    if(WIN32)
        find_package(Threads QUIET)
        if(TARGET Threads::Threads)
            set_property(TARGET VanGUI::suite APPEND PROPERTY INTERFACE_LINK_LIBRARIES Threads::Threads)
        endif()
    endif()
endif()

set(VanGUISDK_FOUND TRUE)
set(VanGUISDK_INCLUDE_DIR "${_VANGUI_INCLUDE}")
message(STATUS "VanGUISDK: ${_VANGUI_ARCH} @ ${_VANGUI_SDK_ROOT}")
