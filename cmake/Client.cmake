# legacysf.exe: the game for Windows (Client/PC/main.cpp over Client/Source), with the server's
# core linked in so a player can host on their own machine. Included by the top CMakeLists.txt.
enable_language(RC)

# --- VanGUI (prebuilt SDK in vendor/, x64 libraries built /MT) -------------------
set(VanGUISDK_DIR "${CMAKE_SOURCE_DIR}/vendor/VanGUI/cmake" CACHE PATH "" FORCE)
find_package(VanGUISDK REQUIRED CONFIG)
set(VANGUI_BACKENDS "${CMAKE_SOURCE_DIR}/vendor/VanGUI/backends")
add_library(lsf_vangui_backends STATIC
    "${VANGUI_BACKENDS}/vangui_impl_dx11.cpp"
    "${VANGUI_BACKENDS}/vangui_impl_dx12.cpp"
    "${VANGUI_BACKENDS}/vangui_impl_win32.cpp"
    "${VANGUI_BACKENDS}/vangui_impl_opengl3.cpp")
target_link_libraries(lsf_vangui_backends PUBLIC VanGUI::suite)
target_include_directories(lsf_vangui_backends PUBLIC
    "${VANGUI_BACKENDS}"
    "${CMAKE_SOURCE_DIR}/vendor/VanGUI/include/vangui"
    "${CMAKE_SOURCE_DIR}/vendor/Khronos/include")
target_compile_definitions(lsf_vangui_backends PRIVATE VANGUI_IMPL_OPENGL_ES3)
target_compile_options(lsf_vangui_backends PRIVATE /W0 /utf-8)

# --- OpenGL through ANGLE: the phone's OpenGL ES renderer on the card's OpenGL driver ---
add_library(lsf_angle INTERFACE)
target_include_directories(lsf_angle SYSTEM INTERFACE "${CMAKE_SOURCE_DIR}/vendor/Khronos/include")
target_link_libraries(lsf_angle INTERFACE "${CMAKE_SOURCE_DIR}/vendor/ANGLE/lib/libEGL.lib" "${CMAKE_SOURCE_DIR}/vendor/ANGLE/lib/libGLESv2.lib")
target_link_options(lsf_angle INTERFACE /DELAYLOAD:libEGL.dll /DELAYLOAD:libGLESv2.dll)

# --- Engine presentation: window, input, graphics, audio, interface -------------------
add_library(lsf_engine_client STATIC ${LSF_ENGINE_CLIENT_SOURCES} ${LSF_ENGINE_WINDOWS_SOURCES})
target_include_directories(lsf_engine_client PUBLIC "${CMAKE_SOURCE_DIR}/Client/Source")
target_include_directories(lsf_engine_client PRIVATE "${CMAKE_SOURCE_DIR}/vendor/minimp3" "${CMAKE_SOURCE_DIR}/vendor/stb")
target_link_libraries(lsf_engine_client PUBLIC lsf_shared lsf_vangui_backends lsf_angle
    d3d12 d3d11 dxgi d3dcompiler dxguid xaudio2 gdi32 user32 shell32 imm32 dwmapi xinput hid setupapi)
target_compile_definitions(lsf_engine_client PRIVATE ENG_GLES_ON_WINDOWS)

# --- The game --------------------------------------------------------------------------
add_executable(legacysf WIN32 Client/PC/main.cpp ${LSF_CLIENT_SOURCES})
target_link_libraries(legacysf PRIVATE lsf_engine_client lsf_server_core delayimp)
# The game's own server ("This PC", SL-9): on Windows and Linux only (PF-1, PF-2).
target_compile_definitions(legacysf PRIVATE LSF_WITH_SERVER=1)
include(cmake/Shaders.cmake)
lsf_embed_shaders(legacysf)
# ANGLE's two DLLs ride inside the exe, packed, for the OpenGL renderer.
set(LSF_ANGLE_DLLS "${CMAKE_SOURCE_DIR}/vendor/ANGLE/bin/libEGL.dll" "${CMAKE_SOURCE_DIR}/vendor/ANGLE/bin/libGLESv2.dll")
set(LSF_ANGLE_PACK "${CMAKE_BINARY_DIR}/angle.pack")
add_custom_command(OUTPUT "${LSF_ANGLE_PACK}"
    COMMAND packres "${LSF_ANGLE_PACK}" ${LSF_ANGLE_DLLS}
    DEPENDS packres ${LSF_ANGLE_DLLS}
    COMMENT "Packing ANGLE (OpenGL) for the exe")
set(LSF_ICON "${CMAKE_SOURCE_DIR}/Client/PC/legacysf.ico")
configure_file(Client/PC/legacysf.rc.in "${CMAKE_BINARY_DIR}/legacysf.rc" @ONLY)
target_sources(legacysf PRIVATE "${CMAKE_BINARY_DIR}/legacysf.rc")
set_source_files_properties("${CMAKE_BINARY_DIR}/legacysf.rc" PROPERTIES OBJECT_DEPENDS "${LSF_ANGLE_PACK};${LSF_ICON}")
add_custom_target(lsf_packs DEPENDS "${LSF_ANGLE_PACK}")
add_dependencies(legacysf lsf_packs)
