cmake_minimum_required(VERSION 3.28)
project(LegacySF CXX)

# Soldier Front Legacy: the game (legacysf.exe; Client/PC, and Client/Android for phones), over the
# code it shares with the server (Shared/) and the server's core, which it hosts "This PC" with.
# The game reads the player's own Soldier Front client data (data/area, data/weapon, ...) in place.

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "SF-Rewritten builds x64 only. Configure with -A x64.")
endif()

option(LSF_BUILD_CLIENT "Build the game (needs the VanGUI SDK in vendor/)" ON)

# The C runtime is linked in (VanGUI's x64 libraries in vendor/ are built /MT to match), so the
# game is one exe: nothing of Visual C++ has to sit beside it.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(LSF_BIN "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY         "${LSF_BIN}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${LSF_BIN}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${LSF_BIN}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${LSF_BIN}/Debug")

add_library(lsf_options INTERFACE)
target_include_directories(lsf_options INTERFACE "${CMAKE_SOURCE_DIR}/Shared" "${CMAKE_SOURCE_DIR}/vendor/SFSDK/include")
target_compile_definitions(lsf_options INTERFACE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
target_compile_options(lsf_options INTERFACE
    /W4 /wd4100 /wd4201 /wd4244 /wd4267 /wd4324 /wd4458 /wd4459 /wd4505 /wd4996
    /MP /utf-8 /permissive- /Zc:__cplusplus /EHsc /Zi)
# Release builds keep their PDBs so a crash log names functions and lines.
target_link_options(lsf_options INTERFACE /DEBUG /OPT:REF /OPT:ICF)

include(cmake/Sources.cmake)

# --- Shared: engine core, network, Soldier Front loaders, rules and protocol ---------
add_library(lsf_shared STATIC ${LSF_SHARED_SOURCES})
target_include_directories(lsf_shared PRIVATE "${CMAKE_SOURCE_DIR}/vendor/stb")
target_link_libraries(lsf_shared PUBLIC lsf_options winmm windowscodecs ole32 shlwapi ws2_32 iphlpapi dbghelp winhttp bcrypt crypt32)
# The SDK's own sources are written to its conventions; keep their warnings to themselves.
set_source_files_properties(${LSF_SFSDK_SOURCES} PROPERTIES COMPILE_OPTIONS "/W3")

# --- Tools ---------------------------------------------------------------------
# Packs files (compressed) for the game to carry inside itself (Engine/Core/Embedded).
add_executable(packres Tools/packres/main.cpp)

# --- The server's core: "This PC", the server a player holds on their own machine --------
add_library(lsf_server_core STATIC ${LSF_SERVER_CORE_SOURCES})
target_include_directories(lsf_server_core PUBLIC "${CMAKE_SOURCE_DIR}/Server/Source")
target_link_libraries(lsf_server_core PUBLIC lsf_shared)

# --- The game ---------------------------------------------------------------------
if(LSF_BUILD_CLIENT AND DEFINED LSF_CLIENT_SOURCES)
    include(cmake/Client.cmake)
endif()
