package com.generalsx.generalszh;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.TextView;

/**
 * SandboxRTS match intro drawn over the SDL surface:
 * black screen fades in, 3-2-1 countdown, then a top banner "РАЗВЕРТКА 0:40"
 * counting down to zero. Views are not clickable, so touches go to the game.
 */
public final class DeployOverlay {
    private static final int DEPLOY_SECONDS = 40;
    private static final int ORANGE = Color.rgb(255, 90, 31);

    private DeployOverlay() {}

    public static void attach(final Activity a) {
        final float d = a.getResources().getDisplayMetrics().density;
        final Handler h = new Handler(Looper.getMainLooper());

        final FrameLayout root = new FrameLayout(a);
        root.setClickable(false);
        root.setFocusable(false);

        final View black = new View(a);
        black.setBackgroundColor(Color.BLACK);
        black.setClickable(false);
        root.addView(black, new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        final TextView count = new TextView(a);
        count.setTextColor(Color.WHITE);
        count.setTextSize(96);
        count.setTypeface(Typeface.DEFAULT_BOLD);
        count.setShadowLayer(12f, 0f, 0f, Color.BLACK);
        count.setGravity(Gravity.CENTER);
        count.setClickable(false);
        count.setVisibility(View.GONE);
        root.addView(count, new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT, Gravity.CENTER));

        final TextView banner = new TextView(a);
        banner.setTextColor(Color.WHITE);
        banner.setTextSize(20);
        banner.setTypeface(Typeface.DEFAULT_BOLD);
        banner.setGravity(Gravity.CENTER);
        banner.setClickable(false);
        int px = (int) (18 * d), py = (int) (6 * d);
        banner.setPadding(px, py, px, py);
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(Color.argb(170, 20, 20, 22));
        bg.setStroke((int) (2 * d), ORANGE);
        bg.setCornerRadius(10 * d);
        banner.setBackground(bg);
        banner.setVisibility(View.GONE);
        FrameLayout.LayoutParams bl = new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT,
            Gravity.TOP | Gravity.CENTER_HORIZONTAL);
        bl.topMargin = (int) (12 * d);
        root.addView(banner, bl);

        a.addContentView(root, new ViewGroup.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        // 1) fade from black (game loads underneath)
        h.postDelayed(new Runnable() {
            @Override public void run() {
                black.animate().alpha(0f).setDuration(2500).start();
            }
        }, 1500);

        // 2) 3-2-1 countdown
        final String[] steps = { "3", "2", "1" };
        for (int i = 0; i < steps.length; i++) {
            final String s = steps[i];
            h.postDelayed(new Runnable() {
                @Override public void run() {
                    count.setText(s);
                    count.setVisibility(View.VISIBLE);
                    count.setAlpha(1f);
                    count.setScaleX(1.6f);
                    count.setScaleY(1.6f);
                    count.animate().scaleX(1f).scaleY(1f).alpha(0.2f).setDuration(900).start();
                }
            }, 4000 + i * 1000L);
        }

        // 3) deploy phase banner, 40 s
        final long deployStart = 7000;
        h.postDelayed(new Runnable() {
            @Override public void run() {
                count.setVisibility(View.GONE);
                black.setVisibility(View.GONE);
                banner.setVisibility(View.VISIBLE);
            }
        }, deployStart);
        for (int sec = DEPLOY_SECONDS; sec >= 0; sec--) {
            final int left = sec;
            h.postDelayed(new Runnable() {
                @Override public void run() {
                    banner.setText(String.format(java.util.Locale.ROOT, "РАЗВЕРТКА  0:%02d", left));
                    banner.setTextColor(left <= 10 ? ORANGE : Color.WHITE);
                }
            }, deployStart + (DEPLOY_SECONDS - sec) * 1000L);
        }

        // 4) battle starts
        final long end = deployStart + (DEPLOY_SECONDS + 1) * 1000L;
        h.postDelayed(new Runnable() {
            @Override public void run() {
                banner.setText("БОЙ!");
                banner.setTextColor(ORANGE);
            }
        }, end);
        h.postDelayed(new Runnable() {
            @Override public void run() {
                banner.animate().alpha(0f).setDuration(800).start();
            }
        }, end + 2000);
        h.postDelayed(new Runnable() {
            @Override public void run() {
                ViewGroup p = (ViewGroup) root.getParent();
                if (p != null) p.removeView(root);
            }
        }, end + 3000);
    }
}
