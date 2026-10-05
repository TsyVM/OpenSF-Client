// VanGUI: single-file wrapper include
// We use this to validate compiling all *.cpp files in a same compilation unit.
// Users of that technique (also called "Unity builds") can generally provide this themselves,
// so we don't really recommend you use this in your projects.

// Do this:
//    #define VANGUI_IMPLEMENTATION
// Before you include this file in *one* C++ file to create the implementation.
// Using this in your project will leak the contents of vangui_internal.h and VanVec2 operators in this compilation unit.

#ifdef VANGUI_IMPLEMENTATION
#define VANGUI_DEFINE_MATH_OPERATORS
#endif

#include "../../vangui.h"
#ifdef VANGUI_ENABLE_FREETYPE
#include "../../misc/freetype/vangui_freetype.h"
#endif

#ifdef VANGUI_IMPLEMENTATION
#include "../../vangui.cpp"
#include "../../vangui_demo.cpp"
#include "../../vangui_draw.cpp"
#include "../../vangui_tables.cpp"
#include "../../vangui_widgets.cpp"
#ifdef VANGUI_ENABLE_FREETYPE
#include "../../misc/freetype/vangui_freetype.cpp"
#endif
#endif
