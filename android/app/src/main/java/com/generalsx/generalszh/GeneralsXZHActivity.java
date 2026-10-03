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
        super.onCreate(savedInstanceState);
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.M) {
            if (checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
                requestPermissions(new String[]{
                    android.Manifest.permission.READ_EXTERNAL_STORAGE,
                    android.Manifest.permission.WRITE_EXTERNAL_STORAGE
                }, 1);
            }
        }
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }

    @Override
    protected String[] getArguments() {
        String args = getIntent() != null ? getIntent().getStringExtra("args") : null;
        if (args == null || args.trim().isEmpty()) {
            return new String[] {
                "-win", "-nologo", "-noshellmap", "-quickstart", "-startmap", "Maps\\Flat.map"
            };
        }
        return args.trim().split("\\s+");
    }
}
