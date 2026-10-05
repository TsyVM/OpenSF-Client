// The Android app the game runs in (android_native_app_glue): set once by android_main, read by
// the window, the pad and whatever needs Java (the activity's JNI environment).
#pragma once

struct android_app;
struct _JNIEnv;

namespace eng::android {

void set_app(android_app* app);
android_app* app();

// The main thread's JNI environment (attached on first use), for calls into the activity.
_JNIEnv* jni();

// Calls a no-argument void method on the activity (GameActivity.java), e.g. "finishToSetup".
void call_activity(const char* method);

}  // namespace eng::android
