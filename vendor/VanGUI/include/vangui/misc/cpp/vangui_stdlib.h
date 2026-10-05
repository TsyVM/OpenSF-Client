// VanGUI: wrappers for C++ standard library (STL) types (std::string, etc.)

// This is also an example of how you may wrap your own similar types.
// TL;DR; this is using the VanGuiInputTextFlags_CallbackResize facility,
// which also demonstrated in 'VanGUI Demo->Widgets->Text Input->Resize Callback'.

// Changelog:
// - v0.10: Initial version. Added InputText() / InputTextMultiline() calls with std::string

// Usage:
// {
//   #include "misc/cpp/vangui_stdlib.h"
//   #include "misc/cpp/vangui_stdlib.cpp" // <-- If you want to include implementation without messing with your project/build.
//   [...]
//   std::string my_string;
//   VanGui::InputText("my string", &my_string);
// }

#pragma once

#ifndef VANGUI_DISABLE

#include <string>

namespace VanGui
{
    // VanGui::InputText() with std::string
    // Because text input needs dynamic resizing, we need to setup a callback to grow the capacity
    VANGUI_API bool  InputText(const char* label, std::string* str, VanGuiInputTextFlags flags = 0, VanGuiInputTextCallback callback = nullptr, void* user_data = nullptr);
    VANGUI_API bool  InputTextMultiline(const char* label, std::string* str, const VanVec2& size = VanVec2(0, 0), VanGuiInputTextFlags flags = 0, VanGuiInputTextCallback callback = nullptr, void* user_data = nullptr);
    VANGUI_API bool  InputTextWithHint(const char* label, const char* hint, std::string* str, VanGuiInputTextFlags flags = 0, VanGuiInputTextCallback callback = nullptr, void* user_data = nullptr);
}

#endif // #ifndef VANGUI_DISABLE
