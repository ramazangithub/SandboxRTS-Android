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
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * SandboxRTS MVP welcome screen: title, the two MVP factions, economy rules and a Play button.
 * Plain Android views (no resources) so it always renders Cyrillic and cannot break the engine.
 */
public class WelcomeActivity extends Activity {
    private static final int ORANGE = Color.rgb(255, 90, 31);
    private static final int WHITE = Color.rgb(241, 240, 236);
    private static final int GOLD = Color.rgb(230, 190, 70);
    private static final int ICE = Color.rgb(120, 200, 232);
    private static final int BG = Color.rgb(18, 19, 22);

    private int dp(float v) { return (int) (v * getResources().getDisplayMetrics().density + 0.5f); }

    private TextView text(String s, float size, int color, boolean bold) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(size);
        t.setTextColor(color);
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    private LinearLayout card(String title, int accent, String body) {
        LinearLayout c = new LinearLayout(this);
        c.setOrientation(LinearLayout.VERTICAL);
        c.setPadding(dp(14), dp(10), dp(14), dp(10));
        GradientDrawable g = new GradientDrawable();
        g.setColor(Color.rgb(30, 32, 36));
        g.setStroke(dp(2), accent);
        g.setCornerRadius(dp(10));
        c.setBackground(g);
        c.addView(text(title, 18, accent, true));
        c.addView(text(body, 12, WHITE, false));
        return c;
    }

    private LinearLayout.LayoutParams half() {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        p.setMargins(dp(6), dp(10), dp(6), dp(6));
        return p;
    }

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN, WindowManager.LayoutParams.FLAG_FULLSCREEN);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(BG);
        root.setPadding(dp(24), dp(14), dp(24), dp(14));

        TextView title = text("SANDBOX RTS", 34, ORANGE, true);
        title.setGravity(Gravity.CENTER_HORIZONTAL);
        root.addView(title);
        TextView sub = text("MVP \u00b7 Стихия против Стаи", 14, GOLD, false);
        sub.setGravity(Gravity.CENTER_HORIZONTAL);
        root.addView(sub);

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.addView(card("СТИХИЯ", ICE,
            "Пехота: Изморозь, Град\nТехника: Шквал \u2192 Буран \u2192 Цунами\nАрта: Тайфун \u00b7 Авиа: Бриз\nРабочие: Прораб, Муссон\nБаза: Эпицентр, Зарница, Водохранилище, Метеостанция, Кузня бурь, Высота, Мерзлота, Гроза"), half());
        row.addView(card("СТАЯ", ORANGE,
            "Пехота: Шакал, Волк\nТехника: Хорёк \u2192 Росомаха \u2192 Медоед\nАрта: Дикобраз \u00b7 Авиа: Стрекоза\nРабочие: Бобр, Пеликан\nБаза: Логово, Муравейник, Запасник, Стойбище, Берлога, Гнездо, Улей, Капкан"), half());
        root.addView(row);

        TextView eco = text("Экономика: собирай ресурсы на точках снабжения и захватывай нефтевышки (пассивный доход). "
            + "Лимит армии: 20 + 20 за каждый штаб (максимум 80). Энергия занимается, а не тратится.", 12, WHITE, false);
        eco.setPadding(dp(6), dp(4), dp(6), dp(8));
        root.addView(eco);

        Button play = new Button(this);
        play.setText("ИГРАТЬ");
        play.setTextSize(20);
        play.setTextColor(Color.WHITE);
        play.setTypeface(Typeface.DEFAULT_BOLD);
        GradientDrawable pg = new GradientDrawable();
        pg.setColor(ORANGE);
        pg.setCornerRadius(dp(12));
        play.setBackground(pg);
        LinearLayout.LayoutParams pl = new LinearLayout.LayoutParams(dp(260), dp(56));
        pl.gravity = Gravity.CENTER_HORIZONTAL;
        play.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                startActivity(new Intent(WelcomeActivity.this, GeneralsXZHActivity.class));
                finish();
            }
        });
        root.addView(play, pl);

        TextView hint = text("Модели MVP: распакуй SandboxRTS_MVP_models.zip в /sdcard/ (без них игра идёт на стандартных моделях)", 10, Color.GRAY, false);
        hint.setGravity(Gravity.CENTER_HORIZONTAL);
        hint.setPadding(0, dp(8), 0, 0);
        root.addView(hint);

        ScrollView sv = new ScrollView(this);
        sv.setBackgroundColor(BG);
        sv.addView(root);
        setContentView(sv);
    }
}
