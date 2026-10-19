package port.roomfix;

import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.view.Gravity;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import java.util.Locale;

/** The startup screen stays responsive while Unity prepares its first scene and shader variants. */
final class LoadingView extends FrameLayout {
    // RF_LANG_NAMES order, as in HelpCard. Only the shader stage has a measured percentage.
    private static final String[][] TEXT = {
        {"Starting game", "Preparing graphics", "Preparing main menu"},
        {"Démarrage du jeu", "Préparation des graphismes", "Préparation du menu principal"},
        {"Spiel wird gestartet", "Grafik wird vorbereitet", "Hauptmenü wird vorbereitet"},
        {"Iniciando el juego", "Preparando los gráficos", "Preparando el menú principal"},
        {"Avvio del gioco", "Preparazione della grafica", "Preparazione del menu principale"},
        {"Запуск игры", "Подготовка графики", "Подготовка главного меню"},
        {"ゲームを起動中", "グラフィックを準備中", "メインメニューを準備中"},
        {"게임 시작 중", "그래픽 준비 중", "메인 메뉴 준비 중"},
        {"正在启动游戏", "正在准备图形", "正在准备主菜单"},
        {"正在啟動遊戲", "正在準備圖形", "正在準備主選單"}
    };
    private final TextView status;
    private final TextView percent;
    private final ProgressBar progress;
    private final int initialLanguage;
    private int state = -1;

    LoadingView(Context context) {
        super(context);
        setBackgroundColor(0xFF100F0E);
        float dp = getResources().getDisplayMetrics().density;
        initialLanguage = language(getResources().getConfiguration().getLocales().get(0));
        LinearLayout content = new LinearLayout(context);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setGravity(Gravity.CENTER);
        content.setPadding((int)(48 * dp), (int)(24 * dp), (int)(48 * dp), (int)(24 * dp));
        addView(content, new FrameLayout.LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT, Gravity.CENTER));
        TextView title = text(context, 30, 0xFFF0E9DB);
        title.setText("THE ROOM VR");
        title.setTypeface(Typeface.SERIF);
        content.addView(title);
        status = text(context, 16, 0xFFDDD2BF);
        LinearLayout.LayoutParams statusLayout = new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT);
        statusLayout.topMargin = (int)(24 * dp);
        content.addView(status, statusLayout);
        progress = new ProgressBar(context, null, android.R.attr.progressBarStyleHorizontal);
        progress.setMax(100);
        ColorStateList tint = ColorStateList.valueOf(0xFFDAC29A);
        progress.setProgressTintList(tint);
        progress.setIndeterminateTintList(tint);
        progress.setProgressBackgroundTintList(ColorStateList.valueOf(0xFF3B342A));
        LinearLayout.LayoutParams barLayout = new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, (int)(6 * dp));
        barLayout.topMargin = (int)(20 * dp);
        content.addView(progress, barLayout);
        percent = text(context, 14, 0xFFDDD2BF);
        LinearLayout.LayoutParams percentLayout = new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT);
        percentLayout.topMargin = (int)(12 * dp);
        content.addView(percent, percentLayout);
        setState(1 | (255 << 8) | (255 << 16));
    }

    private static TextView text(Context context, int sp, int color) {
        TextView text = new TextView(context);
        text.setGravity(Gravity.CENTER);
        text.setTextSize(sp);
        text.setTextColor(color);
        return text;
    }

    /** Returns true when the first menu accepts input; subsequent scene loads keep the game's own UI. */
    boolean setState(int value) {
        int stage = value & 255;
        if (stage == 0) return true;
        if (state == value) return false;
        state = value;
        int lang = (value >>> 16) & 255;
        if (lang >= TEXT.length) lang = initialLanguage;
        status.setText(TEXT[lang][Math.max(0, Math.min(2, stage - 1))]);
        int p = (value >>> 8) & 255;
        boolean measured = p <= 100;
        progress.setIndeterminate(!measured);
        if (measured) progress.setProgress(p);
        percent.setText(measured ? Integer.toString(p) + "%" : "");
        percent.setVisibility(measured ? View.VISIBLE : View.INVISIBLE);
        return false;
    }

    private static int language(Locale locale) {
        String lang = locale.getLanguage();
        if (lang.equals("fr")) return 1;
        if (lang.equals("de")) return 2;
        if (lang.equals("es")) return 3;
        if (lang.equals("it")) return 4;
        if (lang.equals("ru")) return 5;
        if (lang.equals("ja")) return 6;
        if (lang.equals("ko")) return 7;
        if (lang.equals("zh")) {
            String region = locale.getCountry();
            return locale.getScript().equals("Hant") || region.equals("TW") || region.equals("HK") || region.equals("MO") ? 9 : 8;
        }
        return 0;
    }
}
