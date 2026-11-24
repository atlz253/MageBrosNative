package com.magebros.game;

import android.app.Activity;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;

import org.libsdl.app.SDLActivity;

/**
 * The Android shell: landscape, fullscreen and edge-to-edge, with the system bars hidden and the
 * display cutout used. Everything else is SDL's own activity, which loads libSDL3.so and then
 * libmain.so and calls the game's main function on its render thread.
 */
public class MageBrosActivity extends SDLActivity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            getWindow().setDecorFitsSystemWindows(false);
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            getWindow().getAttributes().layoutInDisplayCutoutMode =
                    android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }
        hideSystemBars();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
        }
    }

    private static native void nativeBack();

    @Override
    public void onBackPressed() {
        // The game decides: open/close the options menu, or leave from the title.
        nativeBack();
    }

    @Override
    protected String[] getArguments() {
        java.util.ArrayList<String> args = new java.util.ArrayList<>();
        // The launcher's explicit override for an executable it does not recognise.
        if (getIntent().getBooleanExtra("allowUnverified", false)) args.add("--allow-unverified");
        // Test hook, used by the scripted acceptance runs on a device.
        String auto = getIntent().getStringExtra("autoArgs");
        if (auto != null && !auto.isEmpty()) {
            args.add("--auto");
            args.add(auto);
        }
        if (getIntent().getBooleanExtra("benchmark", false)) {
            args.add("--benchmark");
            String seconds = getIntent().getStringExtra("benchmarkSeconds");
            if (seconds != null) {
                args.add("--benchmark-seconds");
                args.add(seconds);
            }
            String report = getIntent().getStringExtra("benchmarkReport");
            if (report != null) {
                args.add("--benchmark-report");
                args.add(report);
            }
        }
        return args.toArray(new String[0]);
    }

    /** Called from the game's options menu through JNI: re-import without leaving the session. */
    public static void openGameFilesImport(Activity activity) {
        activity.runOnUiThread(() -> {
            Intent intent = new Intent(activity, GameFilesActivity.class);
            intent.putExtra("importOnly", true);
            activity.startActivity(intent);
        });
    }

    private void hideSystemBars() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                            | View.SYSTEM_UI_FLAG_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
    }
}
