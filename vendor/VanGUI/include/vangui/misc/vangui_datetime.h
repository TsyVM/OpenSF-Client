// vangui_datetime.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — date/time pickers + calendar.
//
// The Qt-style widget the immediate-mode core lacks. Pure calendar math (no
// <ctime> dependency for layout); values are caller-owned plain structs.
// Opt-in / zero-cost via VANGUI_ENABLE_DATETIME.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

namespace VanGui {

struct VanDate { int year = 2026; int month = 1; int day = 1; };   // month 1-12, day 1-31
struct VanTime { int hour = 0; int minute = 0; int second = 0; };

#ifdef VANGUI_ENABLE_DATETIME

// Inline month grid; click a day to select. Returns true when the date changes.
VANGUI_API bool CalendarWidget(const char* id, VanDate* date);

// Combo-style button showing the date; opens a calendar popup.
VANGUI_API bool DatePicker(const char* label, VanDate* date);

// Hour/minute/second steppers (wrapping). Returns true on change.
VANGUI_API bool TimePicker(const char* label, VanTime* t, bool with_seconds = true);

// Helpers (pure).
VANGUI_API int  VanDaysInMonth(int year, int month);
VANGUI_API int  VanDayOfWeek(int year, int month, int day);   // 0=Sunday .. 6=Saturday

#else // ------------------------------- shims ---------------------------------

inline bool CalendarWidget(const char*, VanDate*) { return false; }
inline bool DatePicker(const char*, VanDate*) { return false; }
inline bool TimePicker(const char*, VanTime*, bool = true) { return false; }
inline int  VanDaysInMonth(int, int) { return 30; }
inline int  VanDayOfWeek(int, int, int) { return 0; }

#endif // VANGUI_ENABLE_DATETIME

} // namespace VanGui
