// Force-included when VanGUI's own source (Projects/VanGUI) is compiled for Android: the few
// Microsoft C functions it calls, as their standard equivalents.
#pragma once

#include <cerrno>
#include <cstdio>

#define sscanf_s sscanf
inline int fopen_s(FILE** f, const char* path, const char* mode) {
    *f = std::fopen(path, mode);
    return *f ? 0 : errno;
}
