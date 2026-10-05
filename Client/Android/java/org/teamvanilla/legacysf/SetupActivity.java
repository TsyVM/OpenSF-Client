package org.teamvanilla.legacysf;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.res.AssetManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Build;
import android.os.Bundle;
import android.os.StatFs;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

// The first screen. The app carries the Soldier Front game data in its package (assets/data,
// listed in assets/data.list as "path<TAB>bytes" a line); the game reads it from the app's own
// folder, Android/data/org.teamvanilla.legacysf/files/data. This copies it there the first time,
// and again after an update brings other data, then passes on to the game. Once it is there, this
// screen only passes straight on.
public class SetupActivity extends Activity {
    private static final int LIME = Color.rgb(170, 220, 70);
    private static final int TEXT = Color.rgb(221, 219, 207);
    private static final int DIM = Color.rgb(163, 161, 148);

    private TextView status;
    private ProgressBar bar;
    private Button retry;

    private File dataDir() {
        File root = getExternalFilesDir(null);
        if (root == null) root = getFilesDir();
        return new File(root, "data");
    }

    // The package's version: the data copied is marked with it, so an update copies again.
    private String stamp() {
        try {
            PackageInfo p = getPackageManager().getPackageInfo(getPackageName(), 0);
            long code = Build.VERSION.SDK_INT >= 28 ? p.getLongVersionCode() : p.versionCode;
            return Long.toString(code);
        } catch (Exception e) {
            return "0";
        }
    }

    private boolean copied() {
        File mark = new File(dataDir(), ".copied");
        if (!mark.isFile()) return false;
        try (InputStream in = new java.io.FileInputStream(mark)) {
            byte[] b = new byte[64];
            int n = in.read(b);
            return n > 0 && new String(b, 0, n, "UTF-8").trim().equals(stamp());
        } catch (Exception e) {
            return false;
        }
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        boolean forced = getIntent().getBooleanExtra("setup", false);
        if (!forced && copied()) {
            play();
            return;
        }
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setGravity(Gravity.CENTER);
        page.setBackgroundColor(Color.rgb(14, 15, 13));
        int pad = dp(32);
        page.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText("Soldier Front Legacy");
        title.setTextColor(Color.WHITE);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 26);
        title.setGravity(Gravity.CENTER);
        page.addView(title);

        status = new TextView(this);
        status.setTextColor(TEXT);
        status.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        status.setGravity(Gravity.CENTER);
        status.setPadding(0, dp(18), 0, dp(14));
        page.addView(status);

        bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        bar.setMax(1000);
        bar.getProgressDrawable().setTint(LIME);
        page.addView(bar, new LinearLayout.LayoutParams(dp(420), dp(10)));

        retry = new Button(this);
        retry.setText("Try again");
        retry.setOnClickListener(v -> copy());
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.topMargin = dp(18);
        page.addView(retry, lp);

        TextView note = new TextView(this);
        note.setText("The Soldier Front game data is set up in the app's own folder, once.");
        note.setTextColor(DIM);
        note.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        note.setGravity(Gravity.CENTER);
        note.setPadding(0, dp(18), 0, 0);
        page.addView(note);

        setContentView(page);
        copy();
    }

    private int dp(int v) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics()));
    }

    private void say(final String text, final int permille, final boolean failed) {
        runOnUiThread(() -> {
            status.setText(text);
            if (permille >= 0) bar.setProgress(permille);
            retry.setVisibility(failed ? android.view.View.VISIBLE : android.view.View.GONE);
        });
    }

    private void copy() {
        retry.setVisibility(android.view.View.GONE);
        new Thread(() -> {
            AssetManager assets = getAssets();
            List<String> paths = new ArrayList<>();
            List<Long> sizes = new ArrayList<>();
            long total = 0;
            try (BufferedReader r = new BufferedReader(new InputStreamReader(assets.open("data.list"), "UTF-8"))) {
                for (String line; (line = r.readLine()) != null;) {
                    int tab = line.indexOf('\t');
                    if (tab <= 0) continue;
                    paths.add(line.substring(0, tab));
                    long n = Long.parseLong(line.substring(tab + 1).trim());
                    sizes.add(n);
                    total += n;
                }
            } catch (Exception e) {
                // A package without the data (a test build, the data copied over by USB): the
                // game plays from the folder when the data is already there.
                File here = dataDir();
                if (new File(here, "area").isDirectory() && new File(here, "lobby").isDirectory()) {
                    runOnUiThread(this::play);
                    return;
                }
                say("This package carries no game data. Copy the Soldier Front data folder (area, lobby, ...) to " + here.getPath() + ".", 0, true);
                return;
            }
            File dest = dataDir();
            dest.mkdirs();
            long free = new StatFs(dest.getPath()).getAvailableBytes();
            if (free < total + 64L * 1024 * 1024) {
                say(String.format(Locale.US, "Not enough room: the game data needs %.1f GB and the phone has %.1f GB free.",
                        total / 1e9, free / 1e9), 0, true);
                return;
            }
            new File(dest, ".copied").delete();
            long done = 0;
            byte[] buf = new byte[1 << 20];
            for (int i = 0; i < paths.size(); i++) {
                String rel = paths.get(i);
                File out = new File(dest, rel);
                File parent = out.getParentFile();
                if (parent != null) parent.mkdirs();
                if (out.isFile() && out.length() == sizes.get(i)) {
                    done += sizes.get(i);
                    continue;
                }
                try (InputStream in = assets.open("data/" + rel); OutputStream o = new FileOutputStream(out)) {
                    for (int n; (n = in.read(buf)) > 0;) {
                        o.write(buf, 0, n);
                        done += n;
                        if (total > 0)
                            say(String.format(Locale.US, "Setting up the game data: %.0f of %.0f MB", done / 1048576.0, total / 1048576.0),
                                (int) (done * 1000 / total), false);
                    }
                } catch (Exception e) {
                    say("Setting up the game data stopped at " + rel + ": " + e.getMessage(), (int) (total > 0 ? done * 1000 / total : 0), true);
                    return;
                }
            }
            try (OutputStream o = new FileOutputStream(new File(dest, ".copied"))) {
                o.write(stamp().getBytes("UTF-8"));
            } catch (Exception e) {
                say("The game data could not be marked as set up: " + e.getMessage(), 1000, true);
                return;
            }
            say("Ready.", 1000, false);
            runOnUiThread(this::play);
        }).start();
    }

    private void play() {
        startActivity(new Intent(this, GameActivity.class).putExtras(getIntent()));
        finish();
    }
}
