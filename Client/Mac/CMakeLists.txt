# Soldier Front Legacy for macOS (Apple Silicon): "Soldier Front Legacy.app". Built on a Mac by
# Client/Mac/build_mac.sh (Client/Mac/README.md), which names the three things a Mac cannot make:
#
#   VANGUI_SRC    VanGUI's source (its core and suite are compiled here, as for Android and Linux)
#   ANGLE_DIR     libEGL.dylib and libGLESv2.dylib: ANGLE built for arm64 with its Metal back end
#   SHADERS_CPP   the shaders as GLSL ES, made on Windows by Tools/shaders_gen.py (DXC and
#                 SPIRV-Cross are Windows programs here)
#
# No server is linked in: a Mac plays, it does not host (PF-1; Server/Source/ServerOnly.hpp stops
# the compile if a server header is reached).
cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET 12.0 CACHE STRING "" FORCE)
project(legacysf C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 23)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

set(VANGUI_SRC "" CACHE PATH "VanGUI's source (include/, src/, misc/)")
set(ANGLE_DIR "" CACHE PATH "the folder with ANGLE's libEGL.dylib and libGLESv2.dylib (arm64, Metal)")
set(SHADERS_CPP "" CACHE FILEPATH "Shaders.cpp made by Tools/shaders_gen.py")
foreach(need VANGUI_SRC ANGLE_DIR SHADERS_CPP)
    if(NOT ${need} OR NOT EXISTS "${${need}}")
        message(FATAL_ERROR "${need} is not set or does not exist (build with Client/Mac/build_mac.sh)")
    endif()
endforeach()

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/Sources.cmake)
set(VANGUI "${LSF_ROOT}/vendor/VanGUI")
# The app's version: the game's build number (Shared/Game/Rules.hpp).
file(STRINGS "${LSF_ROOT}/Shared/Game/Rules.hpp" build_line REGEX "kBuildNumber = [0-9]+")
string(REGEX MATCH "[0-9]+" LSF_BUILD_NUMBER "${build_line}")

# VanGUI's frozen macro set: the same as every other build of the game (Client/Android/CMakeLists.txt).
set(VANGUI_DEFS
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
set(VANGUI_SUITE_MODULES
    anim loading notify command_palette themes node_graph layout dialogs style_sheet views widgets_ext charts
    feedback forms panels shortcuts vui theme_engine theme_editor thread actions toolbar dropzone wizard console
    property_grid curve_editor datetime markdown hex_editor plot timeline data_grid banners repl richtext perf_hud
    vector suite_selfcheck raster svg effects icons gallery extras touch gizmo)
set(VANGUI_COMPAT "${LSF_ROOT}/Client/Android/cmake/vangui_compat.h")
set(VANGUI_INCLUDES "${VANGUI_SRC}/include" "${VANGUI_SRC}/misc" "${VANGUI_SRC}/include/vangui" "${VANGUI_SRC}/src")

# VanGUI's core, with the animation hook only (as its own CMake builds it), and its suite.
add_library(vangui STATIC
    "${VANGUI_SRC}/src/vangui.cpp" "${VANGUI_SRC}/src/vangui_draw.cpp" "${VANGUI_SRC}/src/vangui_widgets.cpp"
    "${VANGUI_SRC}/src/vangui_tables.cpp" "${VANGUI_SRC}/src/vangui_docking.cpp")
target_include_directories(vangui PRIVATE "${VANGUI_SRC}" ${VANGUI_INCLUDES})
target_compile_definitions(vangui PRIVATE VANGUI_ENABLE_ANIM)
target_compile_options(vangui PRIVATE -include "${VANGUI_COMPAT}" -w)

set(suite_files "")
foreach(m IN LISTS VANGUI_SUITE_MODULES)
    list(APPEND suite_files "${VANGUI_SRC}/misc/vangui_${m}.cpp")
endforeach()
add_library(vangui_suite STATIC ${suite_files})
target_include_directories(vangui_suite PRIVATE ${VANGUI_INCLUDES})
target_compile_definitions(vangui_suite PRIVATE ${VANGUI_DEFS})
target_compile_options(vangui_suite PRIVATE -include "${VANGUI_COMPAT}" -w)

add_executable(legacysf MACOSX_BUNDLE
    ${LSF_SHARED_SOURCES}
    ${LSF_ENGINE_CLIENT_SOURCES}
    ${LSF_CLIENT_SOURCES}
    "${LSF_ROOT}/Client/Source/Engine/Platform/Window_mac.mm"
    "${LSF_ROOT}/Client/Source/Engine/Platform/Gamepad_mac.mm"
    "${LSF_ROOT}/Client/Desktop/main.cpp"
    "${VANGUI}/backends/vangui_impl_opengl3.cpp"
    "${SHADERS_CPP}"
    "${CMAKE_CURRENT_LIST_DIR}/legacysf.icns")
set_source_files_properties("${VANGUI}/backends/vangui_impl_opengl3.cpp" PROPERTIES COMPILE_DEFINITIONS VANGUI_IMPL_OPENGL_ES3)
set_source_files_properties("${LSF_ROOT}/Client/Source/Engine/Platform/Window_mac.mm" "${LSF_ROOT}/Client/Source/Engine/Platform/Gamepad_mac.mm"
                            PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/legacysf.icns" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

target_include_directories(legacysf PRIVATE
    "${LSF_ROOT}/Shared" "${LSF_ROOT}/Client/Source" "${LSF_ROOT}/vendor/SFSDK/include"
    "${LSF_ROOT}/vendor/stb" "${LSF_ROOT}/vendor/minimp3" "${VANGUI}/include" "${VANGUI}/include/vangui" "${VANGUI}/backends"
    "${LSF_ROOT}/vendor/Khronos/include")
target_compile_definitions(legacysf PRIVATE ${VANGUI_DEFS})
target_compile_options(legacysf PRIVATE -Wall -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
    -Wno-missing-braces -Wno-sign-compare -Wno-unused-private-field -Wno-reorder-ctor -Wno-deprecated-declarations
    -Wno-date-time -fno-strict-aliasing)
target_link_libraries(legacysf PRIVATE vangui_suite vangui
    "${ANGLE_DIR}/libEGL.dylib" "${ANGLE_DIR}/libGLESv2.dylib"
    "-framework Cocoa" "-framework QuartzCore" "-framework CoreGraphics" "-framework GameController"
    "-framework AudioToolbox" "-framework CoreFoundation")

set_target_properties(legacysf PROPERTIES
    OUTPUT_NAME legacysf
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME "Soldier Front Legacy"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.teamvanilla.legacysf"
    MACOSX_BUNDLE_ICON_FILE legacysf
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.${LSF_BUILD_NUMBER}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${LSF_BUILD_NUMBER}"
    BUILD_RPATH "@executable_path/../Frameworks"
    INSTALL_RPATH "@executable_path/../Frameworks")

# Into the app: ANGLE (Contents/Frameworks) and the game's own few files (Contents/Resources/Content).
# The Soldier Front data stays beside the app, never in it (DI-3).
add_custom_command(TARGET legacysf POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:legacysf>/Frameworks"
    COMMAND ${CMAKE_COMMAND} -E copy "${ANGLE_DIR}/libEGL.dylib" "${ANGLE_DIR}/libGLESv2.dylib" "$<TARGET_BUNDLE_CONTENT_DIR:legacysf>/Frameworks/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${LSF_ROOT}/Client/Content" "$<TARGET_BUNDLE_CONTENT_DIR:legacysf>/Resources/Content"
    COMMENT "ANGLE and Content into the app")
