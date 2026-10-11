package com.generalsx.generalszh;

import android.content.Context;
import android.content.res.AssetManager;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * SandboxRTS: game resources ship inside the APK (assets/GeneralsZH).
 * On first launch / after every APK update they are copied to /sdcard/GeneralsZH,
 * which is where the engine chdir()s. Options.ini of the player is kept.
 */
public final class AssetInstaller {
    public static final String TARGET = "/sdcard/GeneralsZH";
    private static final String ROOT = "GeneralsZH";
    private static final String MARKER = ".sbx_assets_version";

    private AssetInstaller() {}

    private static String versionStamp(Context c) {
        try {
            android.content.pm.PackageInfo pi = c.getPackageManager().getPackageInfo(c.getPackageName(), 0);
            return pi.lastUpdateTime + "";
        } catch (Exception e) {
            return "0";
        }
    }

    public static boolean isInstalled(Context c) {
        File m = new File(TARGET, MARKER);
        if (!m.exists()) return false;
        try (InputStream in = new java.io.FileInputStream(m)) {
            byte[] b = new byte[64];
            int n = in.read(b);
            return n > 0 && new String(b, 0, n).trim().equals(versionStamp(c));
        } catch (Exception e) {
            return false;
        }
    }

    /** Copies everything; returns null on success or an error text. */
    public static String install(Context c) {
        try {
            File dst = new File(TARGET);
            if (!dst.exists() && !dst.mkdirs()) return "не могу создать " + TARGET;
            copyDir(c.getAssets(), ROOT, dst);
            try (OutputStream o = new FileOutputStream(new File(dst, MARKER))) {
                o.write(versionStamp(c).getBytes());
            }
            return null;
        } catch (Exception e) {
            return e.toString();
        }
    }

    private static void copyDir(AssetManager am, String path, File dst) throws Exception {
        String[] list = am.list(path);
        if (list == null) return;
        if (!dst.exists()) dst.mkdirs();
        for (String name : list) {
            String sub = path + "/" + name;
            String[] children = am.list(sub);
            File out = new File(dst, name);
            if (children != null && children.length > 0) {
                copyDir(am, sub, out);
            } else {
                if (name.equals("Options.ini") && out.exists()) continue; // keep player settings
                copyFile(am, sub, out);
            }
        }
    }

    private static void copyFile(AssetManager am, String path, File out) throws Exception {
        byte[] buf = new byte[1 << 16];
        File tmp = new File(out.getPath() + ".tmp");
        try (InputStream in = am.open(path); OutputStream o = new FileOutputStream(tmp)) {
            int n;
            while ((n = in.read(buf)) > 0) o.write(buf, 0, n);
        }
        if (out.exists()) out.delete();
        if (!tmp.renameTo(out)) throw new java.io.IOException("rename " + out);
    }
}
