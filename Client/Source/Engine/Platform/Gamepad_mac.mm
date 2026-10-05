// macOS: the pad, through Apple's GameController framework (Xbox, DualShock 4, DualSense and any
// other pad the Mac knows, by USB or Bluetooth), read into desktop_pad::State for Gamepad::poll
// (Gamepad.cpp). The pad last used is the one read, as on the other systems.
#include "Engine/Platform/Gamepad.hpp"

#include "Engine/Core/Log.hpp"

#import <GameController/GameController.h>

namespace eng::desktop_pad {

namespace {
NSString* g_last_name = nil;
}

void poll(State& out) {
    @autoreleasepool {
        GCController* pad = GCController.current;
        if (!pad || !pad.extendedGamepad) {
            pad = nil;
            for (GCController* c in GCController.controllers)
                if (c.extendedGamepad) {
                    pad = c;
                    break;
                }
        }
        if (!pad) {
            if (g_last_name) LOG_INFO("Pad: %s went away", g_last_name.UTF8String);
            g_last_name = nil;
            out.connected = false;
            return;
        }
        GCExtendedGamepad* g = pad.extendedGamepad;
        u32 b = 0;
        if (g.buttonA.pressed) b |= kPadA;
        if (g.buttonB.pressed) b |= kPadB;
        if (g.buttonX.pressed) b |= kPadX;
        if (g.buttonY.pressed) b |= kPadY;
        if (g.leftShoulder.pressed) b |= kPadLB;
        if (g.rightShoulder.pressed) b |= kPadRB;
        if (g.leftThumbstickButton.pressed) b |= kPadLThumb;
        if (g.rightThumbstickButton.pressed) b |= kPadRThumb;
        if (g.buttonMenu.pressed) b |= kPadStart;
        if (g.buttonOptions.pressed) b |= kPadBack;
        if (g.dpad.up.pressed) b |= kPadUp;
        if (g.dpad.down.pressed) b |= kPadDown;
        if (g.dpad.left.pressed) b |= kPadLeft;
        if (g.dpad.right.pressed) b |= kPadRight;
        // Sony's touch pad's click is Back, as on Windows.
        if ([g isKindOfClass:[GCDualSenseGamepad class]] && ((GCDualSenseGamepad*)g).touchpadButton.pressed) b |= kPadBack;
        if ([g isKindOfClass:[GCDualShockGamepad class]] && ((GCDualShockGamepad*)g).touchpadButton.pressed) b |= kPadBack;
        out.buttons = b;
        // GameController's y already points up, as the game's does.
        out.lx = g.leftThumbstick.xAxis.value, out.ly = g.leftThumbstick.yAxis.value;
        out.rx = g.rightThumbstick.xAxis.value, out.ry = g.rightThumbstick.yAxis.value;
        out.lt = g.leftTrigger.value, out.rt = g.rightTrigger.value;
        out.connected = true;
        NSString* name = pad.vendorName ?: @"Pad";
        out.name = name.UTF8String ? name.UTF8String : "Pad";
        const bool sony = [pad.productCategory isEqualToString:GCProductCategoryDualSense] || [pad.productCategory isEqualToString:GCProductCategoryDualShock4];
        out.vendor = sony ? 0x054C : 0;
        if (!g_last_name || ![g_last_name isEqualToString:name]) {
            LOG_INFO("Pad: %s (%s)", out.name.c_str(), pad.productCategory.UTF8String ? pad.productCategory.UTF8String : "?");
            g_last_name = name;
        }
    }
}

}  // namespace eng::desktop_pad
