package port.roomfix;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.text.Layout;
import android.text.StaticLayout;
import android.text.TextPaint;
import android.view.View;

/**
 * Stage-8 gesture card (operator choice C3): one page listing the flat touch gestures, shown over the game the
 * first time the main menu opens and from Options → "view controls" (the authored button that opened the VR
 * controller map). Texts follow the game's current language (index from libroomfix, order of RF_LANG_NAMES).
 * A tap anywhere (or Back) closes it; while shown it takes every touch, so nothing reaches the game.
 */
final class HelpCard extends View {
    // English, French, German, Spanish, Italian, Russian, Japanese, Korean, Simplified Chinese, Traditional Chinese
    private static final String[][] TEXT = {
        {"Touch controls",
         "Drag on an empty spot to look around.",
         "Press an object and drag to take or move it: pull levers and drawers, lift bolts, turn handles. Circle around cranks and keys.",
         "Holding something, put a second finger down away from it to look around.",
         "Holding something, put a second finger right next to it to turn it in your hand: circle to roll it, swipe across to turn it over, up or down to tip it.",
         "Spread or pinch two fingers to lean in; slide two fingers together to move your head.",
         "Teleport button, then tap a glowing spot. Crystals seen through the eyepiece lead into miniatures the same way.",
         "Right side: inventory and eyepiece. Back: pause.",
         "Gyroscope (Options → Gyroscope): turn the phone or move it closer and away to move your eyes, or turn it to turn the view.",
         "Tap to close. Options → View controls shows this again."},
        {"Commandes tactiles",
         "Faites glisser sur un endroit vide pour regarder autour de vous.",
         "Appuyez sur un objet et faites glisser pour le prendre ou le déplacer : leviers, tiroirs, verrous, poignées. Tournez en cercle autour des manivelles et des clés.",
         "En tenant un objet, posez un deuxième doigt à l'écart pour regarder autour de vous.",
         "En tenant un objet, posez un deuxième doigt juste à côté pour le tourner dans la main : en cercle pour le pivoter, en travers pour le retourner, vers le haut ou le bas pour l'incliner.",
         "Écartez ou rapprochez deux doigts pour vous pencher ; faites glisser deux doigts ensemble pour déplacer la tête.",
         "Bouton de téléportation, puis touchez un point lumineux. Les cristaux vus à travers l'oculaire mènent aux miniatures de la même façon.",
         "À droite : inventaire et oculaire. Retour : pause.",
         "Gyroscope (Options → Gyroscope) : tournez le téléphone ou avancez-le et reculez-le pour déplacer les yeux, ou tournez-le pour tourner la vue.",
         "Touchez pour fermer. Options → Consulter les commandes pour revoir cette aide."},
        {"Touch-Steuerung",
         "Wische über eine freie Stelle, um dich umzusehen.",
         "Drücke auf einen Gegenstand und ziehe, um ihn zu nehmen oder zu bewegen: Hebel, Schubladen, Riegel, Griffe. Kurbeln und Schlüssel drehst du im Kreis.",
         "Hältst du etwas, setze einen zweiten Finger abseits auf, um dich umzusehen.",
         "Hältst du etwas, setze einen zweiten Finger direkt daneben auf, um es in der Hand zu drehen: im Kreis drehen, quer wischen zum Umdrehen, auf oder ab zum Kippen.",
         "Zwei Finger spreizen oder zusammenführen, um dich vorzubeugen; zwei Finger gemeinsam schieben, um den Kopf zu bewegen.",
         "Teleport-Schaltfläche, dann auf eine leuchtende Stelle tippen. Kristalle, die du durch das Okular siehst, führen so in Miniaturen.",
         "Rechts: Inventar und Okular. Zurück: Pause.",
         "Gyroskop (Optionen → Gyroskop): Drehe das Telefon oder bewege es vor und zurück, um die Augen zu bewegen, oder drehe es, um die Ansicht zu drehen.",
         "Zum Schließen tippen. Optionen → Steuerung ansehen zeigt diese Hilfe erneut."},
        {"Controles táctiles",
         "Desliza en un lugar vacío para mirar a tu alrededor.",
         "Pulsa un objeto y arrastra para cogerlo o moverlo: palancas, cajones, cerrojos, asas. Gira en círculo las manivelas y las llaves.",
         "Mientras sujetas algo, pon un segundo dedo lejos de él para mirar alrededor.",
         "Mientras sujetas algo, pon un segundo dedo justo al lado para girarlo en la mano: en círculo para rotarlo, de lado para darle la vuelta, arriba o abajo para inclinarlo.",
         "Separa o junta dos dedos para inclinarte; desliza dos dedos juntos para mover la cabeza.",
         "Botón de teletransporte y después toca un punto brillante. Los cristales que ves con el ocular llevan a las miniaturas del mismo modo.",
         "A la derecha: inventario y ocular. Atrás: pausa.",
         "Giroscopio (Opciones → Giroscopio): gira el teléfono o acércalo y aléjalo para mover los ojos, o gíralo para girar la vista.",
         "Toca para cerrar. Opciones → Ver controles muestra esta ayuda otra vez."},
        {"Controlli touch",
         "Trascina su un punto vuoto per guardarti intorno.",
         "Tocca un oggetto e trascina per prenderlo o spostarlo: leve, cassetti, chiavistelli, maniglie. Gira in cerchio manovelle e chiavi.",
         "Mentre tieni qualcosa, appoggia un secondo dito lontano per guardarti intorno.",
         "Mentre tieni qualcosa, appoggia un secondo dito proprio accanto per girarlo in mano: in cerchio per ruotarlo, di traverso per capovolgerlo, su o giù per inclinarlo.",
         "Allarga o avvicina due dita per sporgerti; trascina due dita insieme per spostare la testa.",
         "Pulsante del teletrasporto, poi tocca un punto luminoso. I cristalli visti con l'oculare portano allo stesso modo nelle miniature.",
         "A destra: inventario e oculare. Indietro: pausa.",
         "Giroscopio (Opzioni → Giroscopio): ruota il telefono o avvicinalo e allontanalo per spostare gli occhi, oppure ruotalo per girare la visuale.",
         "Tocca per chiudere. Opzioni → Visualizza comandi mostra di nuovo questo aiuto."},
        {"Сенсорное управление",
         "Проведите по свободному месту — осмотреться.",
         "Нажмите на предмет и тяните — взять или двигать: рычаги, ящики, засовы, ручки. Рукоятки и ключи вращайте пальцем по кругу.",
         "Держа предмет, коснитесь вторым пальцем в стороне — оглядеться, не отпуская его.",
         "Держа предмет, коснитесь вторым пальцем рядом — повернуть в руке: по кругу — повернуть, поперёк — перевернуть, вверх или вниз — наклонить.",
         "Разведите или сведите два пальца — наклониться ближе; сдвиньте два пальца вместе — сдвинуть голову.",
         "Кнопка телепорта, затем нажмите на светящуюся точку. Кристаллы, видимые в окуляр, так же ведут в миниатюры.",
         "Справа — инвентарь и окуляр. «Назад» — пауза.",
         "Гироскоп (Настройки → «Гироскоп»): поворачивайте телефон или двигайте его вперёд-назад, чтобы сдвигать глаза, либо поворачивайте, чтобы поворачивать камеру.",
         "Нажмите, чтобы закрыть. Снова: Настройки → «Просмотр управления»."},
        {"タッチ操作",
         "何もない所をドラッグして見回す。",
         "物を押さえてドラッグすると、取ったり動かしたりできる（レバー、引き出し、かんぬき、取っ手）。クランクや鍵は指で円を描いて回す。",
         "物を持ったまま、離れた所に2本目の指を置くと見回せる。",
         "物を持ったまま、すぐ横に2本目の指を置くと手の中で回せる：円を描くと回転、横にスワイプで裏返し、上下で傾ける。",
         "2本の指を広げる・狭めると身を乗り出す。2本の指をそろえて動かすと頭を動かす。",
         "テレポートボタンを押し、光る場所をタップ。接眼レンズで見える結晶も同じようにミニチュアへ入れる。",
         "右側：インベントリと接眼レンズ。戻る：ポーズ。",
         "ジャイロ（オプション → ジャイロ）：スマホを回したり前後に動かしたりして目の位置を動かすか、回して視点を回せます。",
         "タップで閉じる。オプション → ビューコントロールで再表示。"},
        {"터치 조작",
         "빈 곳을 드래그해 주위를 둘러봅니다.",
         "물건을 눌러 드래그하면 집거나 움직일 수 있습니다: 레버, 서랍, 빗장, 손잡이. 크랭크와 열쇠는 원을 그리며 돌립니다.",
         "물건을 든 채 떨어진 곳에 두 번째 손가락을 대면 주위를 둘러봅니다.",
         "물건을 든 채 바로 옆에 두 번째 손가락을 대면 손 안에서 돌립니다: 원을 그리면 회전, 가로로 밀면 뒤집기, 위아래로 밀면 기울이기.",
         "두 손가락을 벌리거나 모으면 몸을 기울입니다. 두 손가락을 함께 밀면 머리를 움직입니다.",
         "텔레포트 버튼을 누른 뒤 빛나는 곳을 누르세요. 접안렌즈로 보이는 수정도 같은 방법으로 미니어처에 들어갑니다.",
         "오른쪽: 인벤토리와 접안렌즈. 뒤로: 일시 정지.",
         "자이로(옵션 → 자이로): 휴대폰을 돌리거나 앞뒤로 움직여 눈 위치를 옮기거나, 돌려서 시점을 돌립니다.",
         "눌러서 닫기. 옵션 → 컨트롤 보기에서 다시 볼 수 있습니다."},
        {"触屏操作",
         "在空白处拖动即可环顾四周。",
         "按住物体并拖动即可拿起或移动：拉杆、抽屉、门闩、把手。曲柄和钥匙用手指画圈转动。",
         "拿着物体时，在远处放下第二根手指即可环顾四周。",
         "拿着物体时，在它旁边放下第二根手指即可在手中转动：画圈旋转，横向滑动翻转，上下滑动倾斜。",
         "双指张开或捏合即可凑近；双指一起滑动即可移动头部。",
         "点击瞬移按钮，再点击发光的位置。通过目镜看到的水晶也以同样方式进入微缩世界。",
         "右侧：物品栏和目镜。返回：暂停。",
         "体感（选项 → 体感）：转动手机或前后移动手机来移动眼睛位置，或转动手机来转动视角。",
         "点击关闭。在选项 → 查看操作中可再次查看。"},
        {"觸控操作",
         "在空白處拖曳即可環顧四周。",
         "按住物體並拖曳即可拿起或移動：拉桿、抽屜、門閂、把手。曲柄和鑰匙用手指畫圈轉動。",
         "拿著物體時，在遠處放下第二根手指即可環顧四周。",
         "拿著物體時，在它旁邊放下第二根手指即可在手中轉動：畫圈旋轉，橫向滑動翻轉，上下滑動傾斜。",
         "雙指張開或捏合即可湊近；雙指一起滑動即可移動頭部。",
         "按下瞬間移動按鈕，再按發光的位置。透過目鏡看到的水晶也以同樣方式進入微縮世界。",
         "右側：物品欄和目鏡。返回：暫停。",
         "體感（選項 → 體感）：轉動手機或前後移動手機來移動眼睛位置，或轉動手機來轉動視角。",
         "點擊關閉。在選項 → 查看控制中可再次查看。"},
    };

    private final float dp;
    private final Paint dim = new Paint();
    private final Paint card = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint border = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final TextPaint title = new TextPaint(Paint.ANTI_ALIAS_FLAG);
    private final TextPaint body = new TextPaint(Paint.ANTI_ALIAS_FLAG);
    private final TextPaint foot = new TextPaint(Paint.ANTI_ALIAS_FLAG);
    private final RectF rect = new RectF();
    private int lang = -1;

    HelpCard(Context c) {
        super(c);
        dp = c.getResources().getDisplayMetrics().density;
        setVisibility(GONE);
        setFocusable(false);
        dim.setColor(0xB0000000);
        card.setColor(0xF0161310);
        border.setStyle(Paint.Style.STROKE);
        border.setStrokeWidth(1.5f * dp);
        border.setColor(0xFFB59A62);  // the HUD's gold ring
        title.setColor(0xFFE9D9B0);
        title.setTypeface(Typeface.create(Typeface.SERIF, Typeface.BOLD));
        body.setColor(0xF2F3EBDD);
        body.setTypeface(Typeface.SERIF);
        foot.setColor(0xB0F3EBDD);
        foot.setTypeface(Typeface.create(Typeface.SERIF, Typeface.ITALIC));
    }

    void show(int language) {
        lang = language >= 0 && language < TEXT.length ? language : 0;
        setVisibility(VISIBLE);
        invalidate();
    }

    void hide() {
        setVisibility(GONE);
    }

    boolean showing() {
        return getVisibility() == VISIBLE;
    }

    private StaticLayout layout(CharSequence s, TextPaint p, int width) {
        return StaticLayout.Builder.obtain(s, 0, s.length(), p, width).setAlignment(Layout.Alignment.ALIGN_NORMAL)
                .setLineSpacing(0, 1.08f).build();
    }

    @Override
    protected void onDraw(Canvas c) {
        if (lang < 0) return;
        String[] t = TEXT[lang];
        float w = getWidth(), h = getHeight();
        c.drawRect(0, 0, w, h, dim);
        int cw = (int) Math.min(w * 0.72f, 640 * dp);
        int pad = (int) (18 * dp), inner = cw - 2 * pad;
        // shrink the text until the card fits the screen height
        for (float sp = 15; sp >= 9; sp -= 0.5f) {
            title.setTextSize(sp * 1.35f * dp);
            body.setTextSize(sp * dp);
            foot.setTextSize(sp * 0.85f * dp);
            float ch = 2 * pad + layout(t[0], title, inner).getHeight() + 10 * dp
                    + layout(t[t.length - 1], foot, inner).getHeight() + 8 * dp;
            for (int i = 1; i < t.length - 1; i++) ch += layout("•  " + t[i], body, inner).getHeight() + 6 * dp;
            if (ch <= h * 0.92f || sp <= 9) {
                float x = (w - cw) / 2f, y = (h - ch) / 2f;
                rect.set(x, y, x + cw, y + ch);
                c.drawRoundRect(rect, 10 * dp, 10 * dp, card);
                c.drawRoundRect(rect, 10 * dp, 10 * dp, border);
                float ty = y + pad;
                StaticLayout l = layout(t[0], title, inner);
                c.save();
                c.translate(x + pad, ty);
                l.draw(c);
                c.restore();
                ty += l.getHeight() + 10 * dp;
                for (int i = 1; i < t.length - 1; i++) {
                    l = layout("•  " + t[i], body, inner);
                    c.save();
                    c.translate(x + pad, ty);
                    l.draw(c);
                    c.restore();
                    ty += l.getHeight() + 6 * dp;
                }
                ty += 2 * dp;
                l = layout(t[t.length - 1], foot, inner);
                c.save();
                c.translate(x + pad, ty);
                l.draw(c);
                c.restore();
                return;
            }
        }
    }
}
