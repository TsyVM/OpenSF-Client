package org.teamvanilla.legacysf;

import android.app.NativeActivity;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.net.wifi.WifiManager;
import android.text.Editable;
import android.text.InputFilter;
import android.text.InputType;
import android.text.TextWatcher;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.SocketTimeoutException;
import java.net.URL;
import java.net.UnknownHostException;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import javax.net.ssl.SSLException;

// The game: liblegacysf.so (Client/Android/AndroidMain.cpp) on a full-screen surface, the system's
// bars hidden until swiped in, the screen kept on while it runs.
public class GameActivity extends NativeActivity {
    private WifiManager.MulticastLock multicast;

    @Override
    protected void onCreate(Bundle state) {
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        super.onCreate(state);
        immersive();
        // Servers on this Wi-Fi answer the list's broadcast; without the lock the phone drops them.
        try {
            WifiManager wifi = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            if (wifi != null) {
                multicast = wifi.createMulticastLock("legacysf-lan");
                multicast.setReferenceCounted(false);
                multicast.acquire();
            }
        } catch (Exception e) {
            multicast = null;
        }
    }

    @Override
    protected void onDestroy() {
        if (multicast != null && multicast.isHeld()) multicast.release();
        super.onDestroy();
    }

    // Typing (a name, an invite code, a chat line): the phone's keyboard over the game, typing into
    // a field nobody sees; the game draws the line itself, in its own look. Each change goes to the
    // native side as it happens (Window_android.cpp), and the end once: Done confirms, closing the
    // keyboard (Back) does not.
    private EditText typing;
    private boolean typingOpen, keyboardShown, quiet;

    public void startTyping(final String initial, final int maxLength, final boolean capitals) {
        runOnUiThread(() -> {
            if (typing == null) {
                typing = new EditText(this);
                typing.setSingleLine(true);
                typing.setBackground(null);
                typing.setTextColor(Color.TRANSPARENT);
                typing.setCursorVisible(false);
                typing.setAlpha(0.0f);
                addContentView(typing, new ViewGroup.LayoutParams(1, 1));
                typing.addTextChangedListener(new TextWatcher() {
                    @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
                    @Override public void onTextChanged(CharSequence s, int start, int before, int count) {}
                    @Override public void afterTextChanged(Editable s) {
                        if (typingOpen && !quiet) nativeTextChanged(s.toString());
                    }
                });
                typing.setOnEditorActionListener((v, action, event) -> {
                    if (action == EditorInfo.IME_ACTION_DONE || action == EditorInfo.IME_ACTION_SEND || action == EditorInfo.IME_NULL) {
                        finishTyping(true);
                        return true;
                    }
                    return false;
                });
                // The keyboard closed without Done (Back, or its own close key): the end of it.
                getWindow().getDecorView().setOnApplyWindowInsetsListener((v, insets) -> {
                    if (Build.VERSION.SDK_INT >= 30) {
                        final boolean shown = insets.isVisible(WindowInsets.Type.ime());
                        if (typingOpen && keyboardShown && !shown) finishTyping(false);
                        keyboardShown = shown;
                    }
                    return v.onApplyWindowInsets(insets);
                });
            }
            int type = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS | InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD;
            if (capitals) type |= InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS;
            typing.setInputType(type);
            typing.setImeOptions(EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_FLAG_NO_FULLSCREEN);
            typing.setFilters(maxLength > 0 ? new InputFilter[] {new InputFilter.LengthFilter(maxLength)} : new InputFilter[0]);
            quiet = true;
            typing.setText(initial);
            typing.setSelection(typing.getText().length());
            quiet = false;
            typingOpen = true;
            keyboardShown = false;
            typing.setFocusable(true);
            typing.setFocusableInTouchMode(true);
            typing.requestFocus();
            InputMethodManager imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
            if (imm != null) imm.restartInput(typing);
            // Asked for once the field has its focus (a field just added has none yet), and again a
            // moment later: a keyboard asked for too early does not come.
            typing.post(this::showKeyboard);
            typing.postDelayed(this::showKeyboard, 250);
        });
    }

    private void showKeyboard() {
        if (!typingOpen || typing == null) return;
        typing.requestFocus();
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController c = getWindow().getInsetsController();
            if (c != null) c.show(WindowInsets.Type.ime());
        }
        InputMethodManager imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
        if (imm != null) imm.showSoftInput(typing, 0);
    }

    // A key from a real keyboard (or a test's), which the game's input queue takes before any
    // view sees it: into the line as the phone's keyboard would type it. 0 letters, 1 rub out
    // the last, 2 Done.
    public void typeKeys(final String text, final int action) {
        runOnUiThread(() -> {
            if (!typingOpen || typing == null) return;
            if (action == 2) {
                finishTyping(true);
                return;
            }
            Editable e = typing.getText();
            if (action == 1) {
                if (e.length() > 0) e.delete(e.length() - 1, e.length());
            } else {
                e.append(text);
            }
        });
    }

    // The game is done with the line (it moved on): the keyboard goes, nothing is answered.
    public void stopTyping() {
        runOnUiThread(() -> {
            typingOpen = false;
            hideKeyboard();
        });
    }

    private void finishTyping(boolean confirmed) {
        if (!typingOpen) return;
        typingOpen = false;
        nativeTextEntered(typing.getText().toString(), confirmed);
        hideKeyboard();
    }

    private void hideKeyboard() {
        if (typing == null) return;
        InputMethodManager imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
        if (imm != null) imm.hideSoftInputFromWindow(typing.getWindowToken(), 0);
        typing.clearFocus();
        immersive();
    }

    // Registered by the native side when it first asks for typing (the library is loaded by
    // NativeActivity, not System.loadLibrary, so Java cannot find them by their names).
    private static native void nativeTextChanged(String text);
    private static native void nativeTextEntered(String text, boolean confirmed);

    @Override
    public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (focused) immersive();
    }

    @SuppressWarnings("deprecation")
    private void immersive() {
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController c = getWindow().getInsetsController();
            if (c != null) {
                c.hide(WindowInsets.Type.systemBars());
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
        }
    }

    // One web request for the native side (Shared/Engine/Net/Https.cpp), made on its own worker
    // thread: Team Vanilla's account service over the phone's HTTPS, its certificates checked by the
    // system. headers: name, value, name, value... The answer: the body (null when there was none),
    // and in `answer` the status, the headers ("name: value" lines, names lower case) and, when
    // nothing came back, why.
    public static byte[] webRequest(String method, String url, String[] headers, byte[] body, int timeoutMs, String[] answer) {
        HttpURLConnection c = null;
        try {
            c = (HttpURLConnection) new URL(url).openConnection();
            c.setInstanceFollowRedirects(false);
            c.setUseCaches(false);
            c.setConnectTimeout(timeoutMs);
            c.setReadTimeout(timeoutMs);
            c.setRequestMethod(method);
            for (int i = 0; i + 1 < headers.length; i += 2) c.setRequestProperty(headers[i], headers[i + 1]);
            if (body != null) {
                c.setDoOutput(true);
                c.setFixedLengthStreamingMode(body.length);
                try (OutputStream out = c.getOutputStream()) {
                    out.write(body);
                }
            }
            final int status = c.getResponseCode();
            final ByteArrayOutputStream reply = new ByteArrayOutputStream();
            final InputStream in = status >= 400 ? c.getErrorStream() : c.getInputStream();
            if (in != null) {
                try (InputStream s = in) {
                    final byte[] buffer = new byte[16384];
                    for (int n; (n = s.read(buffer)) > 0;) {
                        reply.write(buffer, 0, n);
                        if (reply.size() > (16 << 20)) throw new IOException("the answer is too large");
                    }
                }
            }
            final StringBuilder lines = new StringBuilder();
            for (Map.Entry<String, List<String>> h : c.getHeaderFields().entrySet()) {
                if (h.getKey() == null) continue;   // the status line
                for (String v : h.getValue()) lines.append(h.getKey().toLowerCase(Locale.ROOT)).append(": ").append(v).append('\n');
            }
            answer[0] = Integer.toString(status);
            answer[1] = lines.toString();
            return reply.toByteArray();
        } catch (SSLException e) {
            answer[2] = "the server's certificate did not check out (" + e.getMessage() + ")";
        } catch (SocketTimeoutException e) {
            answer[2] = "Timeout was reached";
        } catch (UnknownHostException e) {
            answer[2] = "Couldn't resolve host name";
        } catch (Exception e) {
            answer[2] = e.getMessage() != null ? e.getMessage() : e.getClass().getSimpleName();
        } finally {
            if (c != null) c.disconnect();
        }
        return null;
    }

    // Called by the native side when it finds no game data: back to the setup screen.
    public void returnToSetup() {
        runOnUiThread(() -> {
            startActivity(new Intent(this, SetupActivity.class).putExtra("setup", true));
            finish();
        });
    }
}
