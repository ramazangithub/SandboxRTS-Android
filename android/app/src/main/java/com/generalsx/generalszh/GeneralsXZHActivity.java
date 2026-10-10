package com.generalsx.generalszh;

import org.libsdl.app.SDLActivity;

/**
 * GeneralsX @feature FadiLabib 06/07/2026 Thin SDLActivity shell.
 * getArguments() forwards an intent string extra "args" as engine argv,
 * enabling headless runs: adb shell am start -n <pkg>/.GeneralsXZHActivity
 *   --es args "-headless -replay 00000000.rep"
 */
public class GeneralsXZHActivity extends SDLActivity {
    @Override
    protected void onCreate(android.os.Bundle savedInstanceState) {
        setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.P) {
            android.view.WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.layoutInDisplayCutoutMode =
                android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(lp);
        }
        getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_FULLSCREEN
            | android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        super.onCreate(savedInstanceState);
        hideSystemBars();
        // mvp: fade-in, 3-2-1, "РАЗВЕРТКА 0:40" banner over the game
        if (getIntent() != null && getIntent().hasExtra(WelcomeActivity.EXTRA_FACTION)) {
            DeployOverlay.attach(this);
        }
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.M) {
            if (checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
                requestPermissions(new String[]{
                    android.Manifest.permission.READ_EXTERNAL_STORAGE,
                    android.Manifest.permission.WRITE_EXTERNAL_STORAGE
                }, 1);
            }
        }
    }

    @SuppressWarnings("deprecation")
    private void hideSystemBars() {
        getWindow().getDecorView().setSystemUiVisibility(
            android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            | android.view.View.SYSTEM_UI_FLAG_FULLSCREEN
            | android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            | android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            | android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            | android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
        }
    }

    // r019: SDL re-requests the orientation when the window is created; never
    // let it pick portrait. Always landscape (both sides, follows the sensor).
    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    @Override
    protected void onResume() {
        super.onResume();
        setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }

    @Override
    protected String[] getArguments() {
        String args = getIntent() != null ? getIntent().getStringExtra("args") : null;
        if (args != null && !args.trim().isEmpty()) {
            return args.trim().split("\\s+");
        }
        String map = getIntent() != null ? getIntent().getStringExtra("map") : null;
        if (map != null && !map.trim().isEmpty()) {
            if (!map.startsWith("Maps\\") && !map.startsWith("Maps/")) {
                map = "Maps\\" + map;
            }
            return new String[] {
                "-win", "-nologo", "-noshellmap", "-quickstart", "-startmap", map
            };
        }
        // r019: second launcher icon "SandboxRTS Showcase" starts the Campaign00 tour.
        String cls = (getIntent() != null && getIntent().getComponent() != null)
            ? getIntent().getComponent().getClassName() : "";
        String startMap = cls.endsWith("ShowcaseAlias") ? "Maps\\Campaign00.map" : "Maps\\Volcano.map";
        return new String[] {
            "-win", "-nologo", "-noshellmap", "-quickstart", "-startmap", startMap
        };
    }
}
