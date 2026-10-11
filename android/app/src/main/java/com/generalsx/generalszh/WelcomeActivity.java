package com.generalsx.generalszh;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * SandboxRTS start menu: plain grey background, faction picker and one centre Play button.
 * The chosen faction is passed to the game as an intent extra ("sbx_faction").
 */
public class WelcomeActivity extends Activity {
    public static final String EXTRA_FACTION = "sbx_faction";
    public static final int FACTION_STIHIYA = 0;
    public static final int FACTION_STAYA = 1;

    private static final int GREY_BG = Color.rgb(74, 76, 80);
    private static final int ORANGE = Color.rgb(255, 90, 31);
    private static final int ICE = Color.rgb(120, 200, 232);
    private static final int WHITE = Color.rgb(241, 240, 236);

    private int faction = FACTION_STIHIYA;
    private Button playBtn;
    private TextView status;
    private boolean busy = false;

    private boolean hasStorage() {
        if (android.os.Build.VERSION.SDK_INT < android.os.Build.VERSION_CODES.M) return true;
        return checkSelfPermission(android.Manifest.permission.WRITE_EXTERNAL_STORAGE) == android.content.pm.PackageManager.PERMISSION_GRANTED;
    }

    private void launchGame() {
        Intent i = new Intent(WelcomeActivity.this, GeneralsXZHActivity.class);
        i.putExtra(EXTRA_FACTION, faction);
        startActivity(i);
        finish();
    }

    // resources are inside the APK; copy them to /sdcard/GeneralsZH once per APK version
    private void startWithAssets() {
        if (busy) return;
        if (!hasStorage()) {
            status.setText("Разреши доступ к памяти — туда распакуются ресурсы игры");
            requestPermissions(new String[]{
                android.Manifest.permission.READ_EXTERNAL_STORAGE,
                android.Manifest.permission.WRITE_EXTERNAL_STORAGE
            }, 7);
            return;
        }
        if (AssetInstaller.isInstalled(this)) { launchGame(); return; }
        busy = true;
        playBtn.setEnabled(false);
        status.setText("Распаковка ресурсов… (только первый запуск)");
        new Thread(new Runnable() {
            @Override public void run() {
                final String err = AssetInstaller.install(WelcomeActivity.this);
                runOnUiThread(new Runnable() {
                    @Override public void run() {
                        busy = false;
                        playBtn.setEnabled(true);
                        if (err == null) launchGame();
                        else status.setText("Ошибка распаковки: " + err);
                    }
                });
            }
        }).start();
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] perms, int[] res) {
        super.onRequestPermissionsResult(code, perms, res);
        if (code == 7) {
            if (hasStorage()) startWithAssets();
            else status.setText("Без доступа к памяти игра не запустится");
        }
    }
    private Button stihiyaBtn;
    private Button stayaBtn;

    private int dp(float v) { return (int) (v * getResources().getDisplayMetrics().density + 0.5f); }

    private GradientDrawable box(int color, int radius) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(color);
        g.setCornerRadius(dp(radius));
        return g;
    }

    private Button factionButton(String label, final int id, int color) {
        Button b = new Button(this);
        b.setText(label);
        b.setAllCaps(false);
        b.setTextSize(16);
        b.setTypeface(Typeface.DEFAULT_BOLD);
        b.setTextColor(Color.WHITE);
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(dp(150), dp(48));
        p.setMargins(dp(8), 0, dp(8), 0);
        b.setLayoutParams(p);
        b.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                faction = id;
                refreshFactionButtons();
            }
        });
        b.setTag(color);
        return b;
    }

    private void refreshFactionButtons() {
        stihiyaBtn.setBackground(box(faction == FACTION_STIHIYA ? ICE : Color.rgb(50, 52, 56), 10));
        stayaBtn.setBackground(box(faction == FACTION_STAYA ? ORANGE : Color.rgb(50, 52, 56), 10));
    }

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN, WindowManager.LayoutParams.FLAG_FULLSCREEN);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setBackgroundColor(GREY_BG);
        root.setPadding(dp(24), dp(24), dp(24), dp(24));

        TextView title = new TextView(this);
        title.setText("SANDBOX RTS");
        title.setTextSize(30);
        title.setTextColor(WHITE);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setGravity(Gravity.CENTER_HORIZONTAL);
        root.addView(title);

        TextView pick = new TextView(this);
        pick.setText("Выбери фракцию");
        pick.setTextSize(14);
        pick.setTextColor(WHITE);
        pick.setGravity(Gravity.CENTER_HORIZONTAL);
        pick.setPadding(0, dp(24), 0, dp(10));
        root.addView(pick);

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER);
        stihiyaBtn = factionButton("Стихия", FACTION_STIHIYA, ICE);
        stayaBtn = factionButton("Стая", FACTION_STAYA, ORANGE);
        row.addView(stihiyaBtn);
        row.addView(stayaBtn);
        root.addView(row);
        refreshFactionButtons();

        Button play = new Button(this);
        play.setText("ИГРАТЬ");
        play.setAllCaps(false);
        play.setTextSize(22);
        play.setTextColor(Color.WHITE);
        play.setTypeface(Typeface.DEFAULT_BOLD);
        play.setBackground(box(ORANGE, 12));
        LinearLayout.LayoutParams pl = new LinearLayout.LayoutParams(dp(260), dp(64));
        pl.gravity = Gravity.CENTER_HORIZONTAL;
        pl.topMargin = dp(40);
        play.setLayoutParams(pl);
        play.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                startWithAssets();
            }
        });
        root.addView(play);
        playBtn = play;

        status = new TextView(this);
        status.setTextSize(13);
        status.setTextColor(WHITE);
        status.setGravity(Gravity.CENTER_HORIZONTAL);
        status.setPadding(0, dp(14), 0, 0);
        root.addView(status);

        setContentView(root);
    }
}
