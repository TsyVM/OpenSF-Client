// vangui_forms.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — form layout + input validation.
// Qt's QFormLayout feel: aligned label/field rows, plus a tiny validation
// helper set (invalid-field tint + inline error messages).
//
//   * Opt-in / zero-cost-when-off via VANGUI_ENABLE_FORMS.
//
//   BeginForm("settings");
//     FormRow("Name");    InputText("##n", name, sizeof name);
//     if (name[0] == 0) FieldError("Name is required");
//     FormRow("Volume");  SliderFloat("##v", &vol, 0, 100);
//   EndForm();
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

namespace VanGui {

#ifdef VANGUI_ENABLE_FORMS

// Begin a form. label_width <= 0 picks ~35% of the available width.
VANGUI_API void BeginForm(const char* id, float label_width = 0.0f);
// Start a row: draws the label and positions the cursor for the field; the next
// item you submit fills the remaining row width.
VANGUI_API void FormRow(const char* label);
VANGUI_API void EndForm();

// Inline error / hint under the current field (indented to the field column).
VANGUI_API void FieldError(const char* msg);
VANGUI_API void FieldHint(const char* msg);

// Tint the next field as invalid (red frame + border). RAII guard provided.
VANGUI_API void PushInvalid();
VANGUI_API void PopInvalid();

struct VanInvalidScope
{
    bool on;
    explicit VanInvalidScope(bool invalid) : on(invalid) { if (on) PushInvalid(); }
    ~VanInvalidScope() { if (on) PopInvalid(); }
    VanInvalidScope(const VanInvalidScope&) = delete;
    VanInvalidScope& operator=(const VanInvalidScope&) = delete;
};

// Tiny validators (header-inline, no state).
inline bool ValidNotEmpty(const char* s) { return s && s[0] != '\0'; }
inline bool ValidInRange(float v, float lo, float hi) { return v >= lo && v <= hi; }
inline bool ValidInRange(int v, int lo, int hi) { return v >= lo && v <= hi; }

#else // ----------------------------- shims -----------------------------------

inline void BeginForm(const char*, float = 0.0f) {}
inline void FormRow(const char*) {}
inline void EndForm() {}
inline void FieldError(const char*) {}
inline void FieldHint(const char*) {}
inline void PushInvalid() {}
inline void PopInvalid() {}
struct VanInvalidScope { explicit VanInvalidScope(bool) {} ~VanInvalidScope() {} };
inline bool ValidNotEmpty(const char* s) { return s && s[0]; }
inline bool ValidInRange(float v, float lo, float hi) { return v >= lo && v <= hi; }
inline bool ValidInRange(int v, int lo, int hi) { return v >= lo && v <= hi; }

#endif // VANGUI_ENABLE_FORMS

} // namespace VanGui
