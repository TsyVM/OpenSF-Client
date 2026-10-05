#pragma once

#include <vangui/vangui.h>

//-----------------------------------------------------------------------------
// vangui_notify.h  --  Toast / notification overlay for VanGUI
//
// Usage:
//   1. Call VanGui::RenderNotifications() once per frame, after all other
//      windows, to draw active toasts.
//   2. Post a toast:
//        VanNotifyID id = VanGui::NotifyInfo("Loaded %d items", count);
//        VanGui::InsertNotification(VanNotifyType_Error, 5000.f, "Failed: %d", err);
//   3. Update a toast's progress bar (live):
//        VanGui::SetNotificationProgress(id, 0.75f);  // 0..1
//        VanGui::SetNotificationProgress(id, -1.f);   // hide bar
//   4. Dismiss programmatically:
//        VanGui::DismissNotification(id);
//
// Position:
//        VanGui::SetNotificationsPos(VanNotifyPos_TopRight);
//
// Type accent colors (left-border stripe):
//   Info    : rgb(66,  133, 244)   blue
//   Success : rgb(52,  168,  83)   green
//   Warning : rgb(251, 188,   4)   amber
//   Error   : rgb(234,  67,  53)   red
//-----------------------------------------------------------------------------

namespace VanGui
{
    //-------------------------------------------------------------------------
    // Types
    //-------------------------------------------------------------------------

    enum VanNotifyType
    {
        VanNotifyType_None    = 0,
        VanNotifyType_Info,
        VanNotifyType_Success,
        VanNotifyType_Warning,
        VanNotifyType_Error,
    };

    // Corner the toast stack is anchored to.
    enum VanNotifyPos
    {
        VanNotifyPos_BottomRight = 0,   // default
        VanNotifyPos_BottomLeft,
        VanNotifyPos_TopRight,
        VanNotifyPos_TopLeft,
    };

    // Opaque handle returned by Insert/Notify functions.
    // 0 is invalid (VanNotifyID_Invalid).
    typedef int VanNotifyID;
    static const VanNotifyID VanNotifyID_Invalid = 0;

    //-------------------------------------------------------------------------
    // Configuration (call before RenderNotifications each frame, or once at
    // startup — values are persistent until changed)
    //-------------------------------------------------------------------------

    // Set the corner where toasts stack (default: BottomRight).
    VANGUI_API void SetNotificationsPos(VanNotifyPos pos);

    // Maximum number of simultaneously visible toasts (1..64, default 8).
    // When the ring is full the oldest toast is displaced.
    VANGUI_API void SetNotificationsMaxCount(int max_count);

    //-------------------------------------------------------------------------
    // Core API
    //-------------------------------------------------------------------------

    // Render all active toasts.  Call once per frame after your regular windows.
    VANGUI_API void RenderNotifications();

    // Remove all active toasts immediately.
    VANGUI_API void ClearNotifications();

    // Post a new toast.  Returns an ID that can be used to update or dismiss it.
    //   type        -- accent/icon style
    //   duration_ms -- total visible time in ms (pass 0 for the default 3 s)
    //   fmt         -- printf-style format string
    VANGUI_API VanNotifyID InsertNotification(VanNotifyType type, float duration_ms,
                                              const char* fmt, ...) VAN_FMTARGS(3);

    // va_list variant (useful when forwarding variadic arguments).
    VANGUI_API VanNotifyID InsertNotificationV(VanNotifyType type, float duration_ms,
                                               const char* fmt, va_list args) VAN_FMTLIST(3);

    // Programmatically dismiss a toast before its timer expires.
    // Safe to call with VanNotifyID_Invalid or a stale ID.
    VANGUI_API void DismissNotification(VanNotifyID id);

    // Show or update a progress bar inside an existing toast.
    //   progress < 0  : hide the bar (default state)
    //   progress 0..1 : show the bar at that fraction
    // Safe to call with a stale ID (no-op).
    VANGUI_API void SetNotificationProgress(VanNotifyID id, float progress);

    //-------------------------------------------------------------------------
    // Convenience wrappers -- 3-second display time
    //-------------------------------------------------------------------------

    VANGUI_API VanNotifyID NotifyInfo   (const char* fmt, ...) VAN_FMTARGS(1);
    VANGUI_API VanNotifyID NotifySuccess(const char* fmt, ...) VAN_FMTARGS(1);
    VANGUI_API VanNotifyID NotifyWarning(const char* fmt, ...) VAN_FMTARGS(1);
    VANGUI_API VanNotifyID NotifyError  (const char* fmt, ...) VAN_FMTARGS(1);

    // Convenience wrappers with explicit display duration.
    VANGUI_API VanNotifyID NotifyInfoFor   (float duration_ms, const char* fmt, ...) VAN_FMTARGS(2);
    VANGUI_API VanNotifyID NotifySuccessFor(float duration_ms, const char* fmt, ...) VAN_FMTARGS(2);
    VANGUI_API VanNotifyID NotifyWarningFor(float duration_ms, const char* fmt, ...) VAN_FMTARGS(2);
    VANGUI_API VanNotifyID NotifyErrorFor  (float duration_ms, const char* fmt, ...) VAN_FMTARGS(2);

    // va_list convenience variants.
    VANGUI_API VanNotifyID NotifyInfoV   (const char* fmt, va_list args) VAN_FMTLIST(1);
    VANGUI_API VanNotifyID NotifySuccessV(const char* fmt, va_list args) VAN_FMTLIST(1);
    VANGUI_API VanNotifyID NotifyWarningV(const char* fmt, va_list args) VAN_FMTLIST(1);
    VANGUI_API VanNotifyID NotifyErrorV  (const char* fmt, va_list args) VAN_FMTLIST(1);

} // namespace VanGui
