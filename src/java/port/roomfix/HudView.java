package port.roomfix;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.os.Build;
import android.view.DisplayCutout;
import android.view.View;
import android.view.WindowInsets;

/**
 * Touch HUD drawn over the game. It never receives touches itself: RoomFix.onTouch hit-tests it and sends
 * presses to libroomfix. Visibility follows the game's own input gates (state flags from native code).
 * Round 5: the position gauge in the top right corner - one track per axis of the view (forward, right, up) while two
 * fingers move the eyes along it inside the location (rf_view.c): the location's span, the reachable part, its centre
 * and the eyes, with a stop flash where a move was held back.
 * Round 6: the hold-distance slider under the pause button while an item is carried (rf_interact.c) - up is farther,
 * down nearer; the track fills from the near end to the knob. RoomFix routes a finger on it here, never to the game.
 */
final class HudView extends View {
    // native HUD_* flags (src/roomfix/rf.h)
    static final int S_VISIBLE = 1, S_TELEPORT = 2, S_TELEPORT_ON = 4, S_INVENTORY = 8, S_INV_OPEN = 16,
            S_GOGGLES = 32, S_GOGGLES_ON = 64, S_SNAP = 128, S_MENU = 256;
    // native HB_* buttons
    static final int B_TELEPORT = 1, B_INVENTORY = 2, B_GOGGLES = 3, B_PAUSE = 4, B_TURN_L = 5, B_TURN_R = 6;
    private static final int NB = 7;
    // native RF_GAUGE_* (rf.h): [0] axes to draw (bit 0 right, 1 up, 2 forward); per axis alpha, eyes, reach low,
    // reach high, stop side, stop strength - positions in units of the location's half span
    static final int GAUGE_STRIDE = 6, GAUGE_N = 1 + 3 * GAUGE_STRIDE;
    private static final int AX_RIGHT = 0, AX_UP = 1, AX_FORWARD = 2;

    private final float[] cx = new float[NB], cy = new float[NB], rad = new float[NB];
    private final boolean[] pressed = new boolean[NB];
    private int state;
    private final float dp;
    private final Paint bg = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint ring = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint glyph = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint solid = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();
    private final RectF rect = new RectF();
    private final float[] gauge = new float[GAUGE_N];
    private int gaugeAxes;
    private float gRight, gTop;  // top right corner of the gauge
    private final Paint gPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    // native RF_HOLD_N (rf.h): [0] shown, [1] the item's distance from the eyes, [2] near end, [3] far end (metres)
    static final int HOLD_N = 4;
    private final float[] hold = new float[HOLD_N];
    private boolean holdOn;     // an item is carried: the slider shows
    private float holdAlpha;    // fades in and out
    private boolean holdDrag;   // a finger moves the knob
    private float holdGrabDy;   // knob centre minus the finger (taking the knob itself does not make it jump)
    private float holdFingerY;  // the dragging finger
    private float holdValue;    // metres the knob stands for while dragged
    private float hX, hTop, hBotFull, hBotSnap;  // track x, far end (top), near end (bottom; above the turn buttons)

    HudView(Context c) {
        super(c);
        dp = c.getResources().getDisplayMetrics().density;
        setFocusable(false);
        setClickable(false);
        bg.setStyle(Paint.Style.FILL);
        ring.setStyle(Paint.Style.STROKE);
        ring.setStrokeWidth(1.5f * dp);
        glyph.setStyle(Paint.Style.STROKE);
        glyph.setStrokeCap(Paint.Cap.ROUND);
        glyph.setStrokeJoin(Paint.Join.ROUND);
        glyph.setColor(0xF2F3EBDD);
        solid.setStyle(Paint.Style.FILL);
        solid.setColor(0xF2F3EBDD);
        gPaint.setStrokeCap(Paint.Cap.ROUND);
        gPaint.setStrokeJoin(Paint.Join.ROUND);
    }

    /** Gauge data from RoomFix.nativeGauge; returns whether any track is drawn (the caller keeps polling then). */
    boolean setGauge(float[] g, int axes) {
        boolean changed = axes != gaugeAxes;
        for (int i = 0; i < GAUGE_N && !changed && axes != 0; i++) changed = g[i] != gauge[i];
        System.arraycopy(g, 0, gauge, 0, GAUGE_N);
        gaugeAxes = axes;
        if (changed) invalidate();
        return axes != 0;
    }

    /**
     * Round 6: hold-distance data from RoomFix.nativeHold, every display frame while polled; returns whether the slider
     * is drawn (shown or fading out) and the caller keeps polling.
     */
    boolean setHold(float[] h, boolean on) {
        boolean changed = on != holdOn;
        for (int i = 1; i < HOLD_N && !changed && on; i++) changed = h[i] != hold[i];
        if (on) System.arraycopy(h, 0, hold, 0, HOLD_N);
        // a finger still on the slider from an earlier hold takes the new item from where that item is
        if (on && !holdOn && holdDrag) holdGrabDy = holdY(hold[1]) - holdFingerY;
        holdOn = on;
        float a = Math.max(0f, Math.min(1f, holdAlpha + (on ? 0.2f : -0.14f)));
        if (a != holdAlpha) changed = true;
        holdAlpha = a;
        if (changed) invalidate();
        return on || holdAlpha > 0f;
    }

    void setState(int s) {
        if (s != state) {
            state = s;
            invalidate();
        }
    }

    private float holdBottom() {
        return (state & S_SNAP) != 0 ? hBotSnap : hBotFull;
    }

    // knob position for a distance, and the distance at a position (up is farther)
    private float holdY(float metres) {
        float lo = hold[2], hi = hold[3], top = hTop, bot = holdBottom();
        float t = hi > lo ? (metres - lo) / (hi - lo) : 0f;
        return bot - Math.max(0f, Math.min(1f, t)) * (bot - top);
    }

    private float holdMetres(float y) {
        float top = hTop, bot = holdBottom();
        float t = bot > top ? (bot - y) / (bot - top) : 0f;
        return hold[2] + Math.max(0f, Math.min(1f, t)) * (hold[3] - hold[2]);
    }

    /** The hold-distance slider is under a point in this view's coordinates (only while it shows). */
    boolean holdHit(float x, float y) {
        if (!holdOn || holdAlpha < 0.5f || (state & S_VISIBLE) == 0) return false;
        return Math.abs(x - hX) <= 34 * dp && y >= hTop - 30 * dp && y <= holdBottom() + 30 * dp;
    }

    /** A finger lands on the slider: returns the distance it asks for (the knob comes to it unless it took the knob). */
    float holdDown(float y) {
        float ky = holdY(hold[1]);
        holdGrabDy = Math.abs(y - ky) <= 26 * dp ? ky - y : 0f;
        holdDrag = true;
        return holdMove(y);
    }

    float holdMove(float y) {
        holdFingerY = y;
        float v = holdMetres(y + holdGrabDy);
        if (v != holdValue) {
            holdValue = v;
            invalidate();
        }
        return v;
    }

    void holdUp() {
        if (holdDrag) {
            holdDrag = false;
            invalidate();
        }
    }

    int state() {
        return state;
    }

    boolean visible(int b) {
        if ((state & S_VISIBLE) == 0) return false;
        switch (b) {
            case B_TELEPORT: return (state & S_TELEPORT) != 0;
            case B_INVENTORY: return (state & (S_INVENTORY | S_INV_OPEN)) != 0;
            case B_GOGGLES: return (state & (S_GOGGLES | S_GOGGLES_ON)) != 0;
            case B_PAUSE: return true;
            case B_TURN_L:
            case B_TURN_R: return (state & S_SNAP) != 0;
            default: return false;
        }
    }

    private boolean active(int b) {
        switch (b) {
            case B_TELEPORT: return (state & S_TELEPORT_ON) != 0;
            case B_INVENTORY: return (state & S_INV_OPEN) != 0;
            case B_GOGGLES: return (state & S_GOGGLES_ON) != 0;
            default: return false;
        }
    }

    /** Button under a point in this view's coordinates (0: none). */
    int hit(float x, float y) {
        float slop = 10 * dp;
        for (int b = 1; b < NB; b++) {
            if (!visible(b)) continue;
            float dx = x - cx[b], dy = y - cy[b], r = rad[b] + slop;
            if (dx * dx + dy * dy <= r * r) return b;
        }
        return 0;
    }

    void setPressed(int b, boolean p) {
        if (b > 0 && b < NB && pressed[b] != p) {
            pressed[b] = p;
            invalidate();
        }
    }

    void clearPressed() {
        for (int b = 0; b < NB; b++) pressed[b] = false;
        holdDrag = false;
        invalidate();
    }

    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        super.onSizeChanged(w, h, ow, oh);
        layoutButtons(w, h);
    }

    @Override
    public WindowInsets onApplyWindowInsets(WindowInsets insets) {
        WindowInsets r = super.onApplyWindowInsets(insets);
        layoutButtons(getWidth(), getHeight());
        invalidate();
        return r;
    }

    private void layoutButtons(int w, int h) {
        if (w <= 0 || h <= 0) return;
        float insetL = 0, insetR = 0, insetT = 0, insetB = 0;
        if (Build.VERSION.SDK_INT >= 28) {
            WindowInsets wi = getRootWindowInsets();
            DisplayCutout dc = wi != null ? wi.getDisplayCutout() : null;
            if (dc != null) {
                insetL = dc.getSafeInsetLeft();
                insetR = dc.getSafeInsetRight();
                insetT = dc.getSafeInsetTop();
                insetB = dc.getSafeInsetBottom();
            }
        }
        float m = 22 * dp;
        float right = w - insetR - m, left = insetL + m, bottom = h - insetB - m, top = insetT + 16 * dp;
        float rT = 33 * dp, rS = 27 * dp, gap = 14 * dp;
        set(B_TELEPORT, right - rT, bottom - rT, rT);
        set(B_INVENTORY, right - rT, bottom - 2 * rT - gap - rS, rS);
        set(B_GOGGLES, right - rT, bottom - 2 * rT - 2 * gap - 3 * rS, rS);
        set(B_PAUSE, left + 21 * dp, top + 21 * dp, 21 * dp);
        set(B_TURN_L, left + rS, bottom - rS, rS);
        set(B_TURN_R, left + 3 * rS + gap, bottom - rS, rS);
        gRight = right;  // the gauge: top right, above the button column
        gTop = top;
        // the hold-distance slider: under the pause button, down to the bottom (above the turn buttons while they show);
        // its ends leave room for the far (top) and near (bottom) marks
        hX = cx[B_PAUSE];
        hTop = cy[B_PAUSE] + rad[B_PAUSE] + 50 * dp;
        hBotFull = bottom - 30 * dp;
        hBotSnap = bottom - 2 * rS - gap - 30 * dp;
    }

    private void set(int b, float x, float y, float r) {
        cx[b] = x;
        cy[b] = y;
        rad[b] = r;
    }

    @Override
    protected void onDraw(Canvas c) {
        if ((state & S_VISIBLE) == 0) return;
        for (int b = 1; b < NB; b++) {
            if (!visible(b)) continue;
            float x = cx[b], y = cy[b], r = rad[b];
            boolean on = active(b), p = pressed[b];
            bg.setColor(on ? 0xA0A07A35 : (p ? 0x99000000 : 0x5C000000));
            c.drawCircle(x, y, r, bg);
            ring.setColor(on || p ? 0xF0E6C27A : 0xB3D9B36C);
            c.drawCircle(x, y, r - ring.getStrokeWidth() * 0.5f, ring);
            glyph.setStrokeWidth(Math.max(2f * dp, r * 0.085f));
            drawGlyph(c, b, x, y, r * 0.52f);
        }
        if (gaugeAxes != 0) drawGauge(c);
        if (holdAlpha > 0f) drawHold(c);
    }

    // ---- round 6: hold-distance slider ----
    private static final int H_BACK = 0x8C000000, H_TRACK = 0x59F3EBDD, H_FILL = 0xEBF3EBDD, H_MARK = 0xCCF3EBDD,
            H_KNOB = 0xFFE6C27A, H_EDGE = 0xE6000000, H_RING = 0xF0E6C27A;

    // A column under the pause button: a small item mark at the far end (top), a large one at the near end (bottom), the
    // track filled from the near end up to the knob.
    private void drawHold(Canvas c) {
        float a = holdAlpha, x = hX, top = hTop, bot = holdBottom(), hw = 17 * dp;
        gPaint.setStyle(Paint.Style.FILL);
        gPaint.setColor(fade(H_BACK, a));
        rect.set(x - hw, top - 36 * dp, x + hw, bot + 36 * dp);
        c.drawRoundRect(rect, hw, hw, gPaint);
        gPaint.setColor(fade(H_MARK, a));
        c.drawCircle(x, top - 19 * dp, 3.5f * dp, gPaint);
        c.drawCircle(x, bot + 19 * dp, 7f * dp, gPaint);
        float y = holdY(holdDrag ? holdValue : hold[1]);
        gPaint.setStyle(Paint.Style.STROKE);
        gPaint.setStrokeWidth(3f * dp);
        gPaint.setColor(fade(H_TRACK, a));
        c.drawLine(x, top, x, bot, gPaint);
        gPaint.setStrokeWidth(4f * dp);
        gPaint.setColor(fade(H_FILL, a));
        c.drawLine(x, y, x, bot, gPaint);
        float r = (holdDrag ? 13f : 11f) * dp;
        gPaint.setStyle(Paint.Style.FILL);
        gPaint.setColor(fade(H_KNOB, a));
        c.drawCircle(x, y, r, gPaint);
        gPaint.setStyle(Paint.Style.STROKE);
        gPaint.setStrokeWidth(1.5f * dp);
        gPaint.setColor(fade(holdDrag ? H_RING : H_EDGE, a));
        c.drawCircle(x, y, holdDrag ? r + 3 * dp : r, gPaint);
    }

    // ---- position gauge ----
    private static final int G_BACK = 0x8C000000, G_SPAN = 0x66F3EBDD, G_REACH = 0xEBF3EBDD, G_CENTRE = 0xFFE6C27A,
            G_EYES = 0xFFE6C27A, G_EDGE = 0xE6000000, G_STOP = 0xFFFF6A3D;
    private static final float G_LEN = 50, G_HIGH = 120, G_X_WIDE = 172, G_Z_WIDE = 46, G_Y_WIDE = 24, G_GAP = 12;

    private static int fade(int argb, float a) {
        return ((int) ((argb >>> 24) * Math.max(0f, Math.min(1f, a))) << 24) | (argb & 0xFFFFFF);
    }

    private static float clampUnit(float v) {
        return Math.max(-1.12f, Math.min(1.12f, v));
    }

    // Tracks right to left: up (a vertical bar), right (a horizontal bar), forward (a lane narrowing into the distance,
    // the far end on top). Each one only while native code draws its axis; a track is G_LEN dp per half span.
    private void drawGauge(Canvas c) {
        float top = gTop, mid = top + G_HIGH * dp * 0.5f, len = G_LEN * dp;
        float yR = gRight, yL = yR - G_Y_WIDE * dp;
        float xR = yL - G_GAP * dp, xL = xR - G_X_WIDE * dp;
        float zR = xL - G_GAP * dp, zL = zR - G_Z_WIDE * dp;
        // up: x fixed, y = mid - v * len
        if ((gaugeAxes & (1 << AX_UP)) != 0) {
            int o = 1 + AX_UP * GAUGE_STRIDE;
            float a = gauge[o], x = (yL + yR) * 0.5f;
            backdrop(c, yL, top, yR, top + G_HIGH * dp, a);
            track(c, x, mid + len, x, mid - len, gauge, o, a, 0, true);
            chevron(c, x, mid - len - 6 * dp, 0, -1, a);
            chevron(c, x, mid + len + 6 * dp, 0, 1, a);
        }
        // right: y fixed, x = centre + v * len
        if ((gaugeAxes & (1 << AX_RIGHT)) != 0) {
            int o = 1 + AX_RIGHT * GAUGE_STRIDE;
            float a = gauge[o], x = (xL + xR) * 0.5f, h = 15 * dp;
            backdrop(c, xL, mid - h, xR, mid + h, a);
            track(c, x - len, mid, x + len, mid, gauge, o, a, 0, true);
            chevron(c, x - len - 8 * dp, mid, -1, 0, a);
            chevron(c, x + len + 8 * dp, mid, 1, 0, a);
        }
        // forward: a lane on the floor seen from behind, x fixed, y = mid - v * len, narrower towards the far end
        if ((gaugeAxes & (1 << AX_FORWARD)) != 0) {
            int o = 1 + AX_FORWARD * GAUGE_STRIDE;
            float a = gauge[o], x = (zL + zR) * 0.5f, near = 16 * dp, far = 7 * dp;
            backdrop(c, zL, top, zR, top + G_HIGH * dp, a);
            gPaint.setStyle(Paint.Style.STROKE);
            gPaint.setStrokeWidth(1.2f * dp);
            gPaint.setColor(fade(G_SPAN, a));
            path.reset();
            path.moveTo(x - near, mid + len);
            path.lineTo(x - far, mid - len);
            path.moveTo(x + near, mid + len);
            path.lineTo(x + far, mid - len);
            for (int i = -2; i <= 2; i += 1) {  // rungs every half of the half span
                if (i == 0) continue;
                float t = i * 0.5f, y = mid - t * len, w = near + (far - near) * (t + 1) * 0.5f;
                path.moveTo(x - w, y);
                path.lineTo(x + w, y);
            }
            c.drawPath(path, gPaint);
            track(c, x, mid + len, x, mid - len, gauge, o, a, (near + far) * 0.5f, false);
        }
    }

    private void backdrop(Canvas c, float l, float t, float r, float b, float a) {
        gPaint.setStyle(Paint.Style.FILL);
        gPaint.setColor(fade(G_BACK, a));
        rect.set(l, t, r, b);
        float rr = Math.min(r - l, b - t) * 0.5f;
        c.drawRoundRect(rect, rr, rr, gPaint);
    }

    // a track from (x0, y0) at -1 to (x1, y1) at +1: its span, the reachable part, the centre tick, the eyes and the
    // stop flash; tick: half length of the centre tick (0: default)
    private void track(Canvas c, float x0, float y0, float x1, float y1, float[] g, int o, float a, float tick,
                       boolean span) {
        float mx = (x0 + x1) * 0.5f, my = (y0 + y1) * 0.5f, hx = (x1 - x0) * 0.5f, hy = (y1 - y0) * 0.5f;
        float l = (float) Math.hypot(hx, hy), nx = -hy / l, ny = hx / l;  // unit normal of the track
        gPaint.setStyle(Paint.Style.STROKE);
        if (span) {
            gPaint.setStrokeWidth(2.5f * dp);
            gPaint.setColor(fade(G_SPAN, a));
            c.drawLine(x0, y0, x1, y1, gPaint);
        }
        float lo = clampUnit(g[o + 2]), hi = clampUnit(g[o + 3]);
        gPaint.setStrokeWidth(3.5f * dp);
        gPaint.setColor(fade(G_REACH, a));
        if (hi > lo) c.drawLine(mx + lo * hx, my + lo * hy, mx + hi * hx, my + hi * hy, gPaint);
        float t = tick > 0 ? tick : 7 * dp;
        gPaint.setStrokeWidth(2.5f * dp);
        gPaint.setColor(fade(G_CENTRE, a));
        c.drawLine(mx - nx * t, my - ny * t, mx + nx * t, my + ny * t, gPaint);
        float side = g[o + 4], stop = g[o + 5];
        float v = clampUnit(g[o + 1]), ex = mx + v * hx, ey = my + v * hy, er = 6.5f * dp;
        if (side != 0 && stop > 0) {  // where the move stopped: just past the eyes, on the side they were pushed to
            float k = side * (er + 3 * dp) / l, sx = ex + k * hx, sy = ey + k * hy, sl = 10 * dp;
            gPaint.setStrokeWidth(3.5f * dp);
            gPaint.setColor(fade(G_STOP, a * stop));
            c.drawLine(sx - nx * sl, sy - ny * sl, sx + nx * sl, sy + ny * sl, gPaint);
        }
        gPaint.setStyle(Paint.Style.FILL);
        gPaint.setColor(fade(G_EYES, a));
        c.drawCircle(ex, ey, er, gPaint);
        gPaint.setStyle(Paint.Style.STROKE);
        gPaint.setStrokeWidth(1.5f * dp);
        gPaint.setColor(side != 0 && stop > 0 ? fade(G_STOP, a * stop) : fade(G_EDGE, a));
        c.drawCircle(ex, ey, er, gPaint);
    }

    // a small arrowhead at (x, y) pointing along (dx, dy)
    private void chevron(Canvas c, float x, float y, float dx, float dy, float a) {
        float s = 4 * dp;
        gPaint.setStyle(Paint.Style.STROKE);
        gPaint.setStrokeWidth(1.8f * dp);
        gPaint.setColor(fade(G_SPAN, a));
        path.reset();
        path.moveTo(x - dx * s - dy * s, y - dy * s - dx * s);
        path.lineTo(x, y);
        path.lineTo(x - dx * s + dy * s, y - dy * s + dx * s);
        c.drawPath(path, gPaint);
    }

    // glyphs in a unit box [-1, 1] scaled by s around (x, y)
    private void drawGlyph(Canvas c, int b, float x, float y, float s) {
        path.reset();
        switch (b) {
            case B_TELEPORT: {
                // arc jumping onto a location marker
                path.moveTo(x - 0.95f * s, y + 0.35f * s);
                path.quadTo(x - 0.2f * s, y - 1.25f * s, x + 0.45f * s, y + 0.2f * s);
                c.drawPath(path, glyph);
                path.reset();
                path.moveTo(x + 0.45f * s, y + 0.2f * s);
                path.lineTo(x + 0.08f * s, y + 0.02f * s);
                path.moveTo(x + 0.45f * s, y + 0.2f * s);
                path.lineTo(x + 0.55f * s, y - 0.2f * s);
                c.drawPath(path, glyph);
                rect.set(x + 0.05f * s, y + 0.5f * s, x + 0.95f * s, y + 0.85f * s);
                c.drawOval(rect, glyph);
                break;
            }
            case B_INVENTORY: {
                float q = 0.42f * s, g = 0.12f * s, rr = 0.12f * s;
                for (int i = 0; i < 2; i++) {
                    for (int j = 0; j < 2; j++) {
                        float l = x - g / 2 - q + i * (q + g), t = y - g / 2 - q + j * (q + g);
                        rect.set(l, t, l + q, t + q);
                        c.drawRoundRect(rect, rr, rr, glyph);
                    }
                }
                break;
            }
            case B_GOGGLES: {
                // monocle: lens, rim highlight and handle
                float lx = x - 0.15f * s, ly = y - 0.15f * s, lr = 0.62f * s;
                c.drawCircle(lx, ly, lr, glyph);
                rect.set(lx - lr * 0.62f, ly - lr * 0.62f, lx + lr * 0.62f, ly + lr * 0.62f);
                c.drawArc(rect, 200, 70, false, glyph);
                path.moveTo(lx + lr * 0.72f, ly + lr * 0.72f);
                path.lineTo(x + 0.95f * s, y + 0.95f * s);
                c.drawPath(path, glyph);
                break;
            }
            case B_PAUSE: {
                float bw = 0.28f * s, bh = 0.8f * s, g = 0.18f * s;
                rect.set(x - g - bw, y - bh, x - g, y + bh);
                c.drawRoundRect(rect, bw * 0.3f, bw * 0.3f, solid);
                rect.set(x + g, y - bh, x + g + bw, y + bh);
                c.drawRoundRect(rect, bw * 0.3f, bw * 0.3f, solid);
                break;
            }
            case B_TURN_L:
            case B_TURN_R: {
                // arc over the top (left to right for a right turn), arrowhead at the end of the motion
                boolean left = b == B_TURN_L;
                float ar = 0.78f * s, oy = 0.25f * s;
                rect.set(x - ar, y + oy - ar, x + ar, y + oy + ar);
                float start = left ? -20 : 200, sweep = left ? -140 : 140;
                c.drawArc(rect, start, sweep, false, glyph);
                double a = Math.toRadians(start + sweep);
                float ex = x + (float) Math.cos(a) * ar, ey = y + oy + (float) Math.sin(a) * ar;
                // tangent of the motion at the end point (screen y down; clockwise = increasing angle)
                float tx = (float) -Math.sin(a), ty = (float) Math.cos(a);
                if (left) {
                    tx = -tx;
                    ty = -ty;
                }
                float hl = 0.48f * s, ca = 0.82f, sa = 0.57f;  // head length, cos/sin 35 degrees
                path.moveTo(ex - hl * (tx * ca - ty * sa), ey - hl * (tx * sa + ty * ca));
                path.lineTo(ex, ey);
                path.lineTo(ex - hl * (tx * ca + ty * sa), ey - hl * (-tx * sa + ty * ca));
                c.drawPath(path, glyph);
                break;
            }
            default:
                break;
        }
    }
}
