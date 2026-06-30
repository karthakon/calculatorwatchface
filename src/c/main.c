/**
 * ══════════════════════════════════════════════════════════════
 *  Calculator Watch
 *  Target : Pebble Time 2 (rect) & Pebble Round 2 (round)
 *  IDE    : PebbleCloud  |  SDK 6.x (touch-enabled)
 *
 *  LAYOUT (rect example — auto-scales on round)
 *  ┌──────────────────────────────┐
 *  │ 12:45                    +   │  ← clock + pending-op badge
 *  │                      12345   │  ← calculator display
 *  ├───────┬───────┬───────┬──────┤
 *  │  AC   │  +/-  │   %   │  /   │  row 0 — function keys
 *  ├───────┼───────┼───────┼──────┤
 *  │   7   │   8   │   9   │  x   │  row 1
 *  ├───────┼───────┼───────┼──────┤
 *  │   4   │   5   │   6   │  -   │  row 2
 *  ├───────┼───────┼───────┼──────┤
 *  │   1   │   2   │   3   │  +   │  row 3
 *  ├───────────────┬───────┬──────┤
 *  │       0       │   .   │  =   │  row 4
 *  └───────────────┴───────┴──────┘
 *
 *  TOUCH API  (Pebble SDK 6.x assumed)
 *  ─────────────────────────────────
 *  Registration : window_set_touch_handler(win, handler, ctx)
 *  Handler type : void fn(GPoint pos, TouchPhase phase, void *ctx)
 *  TouchPhase   : TouchPhaseBegan | TouchPhaseMoved |
 *                 TouchPhaseEnded | TouchPhaseCancelled
 *
 *  If your SDK uses a different signature, see the
 *  "TOUCH API COMPATIBILITY" section near the bottom.
 * ══════════════════════════════════════════════════════════════
 */

#include <pebble.h>

/* ────────────────────────────────────────────────────────────
 *  1.  SCREEN GEOMETRY  (filled at runtime)
 * ──────────────────────────────────────────────────────────── */

static int16_t SCR_W, SCR_H;   /* screen width / height           */
static int16_t DISP_H;         /* display panel height (~20 % SH) */
static int16_t BTN_W, BTN_H;   /* button cell width / height      */

static void geo_init(GRect root_bounds) {
    SCR_W  = (int16_t)root_bounds.size.w;
    SCR_H  = (int16_t)root_bounds.size.h;
    DISP_H = SCR_H / 5;              /* display  = 20 % of screen */
    BTN_H  = (SCR_H - DISP_H) / 5;  /* 5 button rows              */
    BTN_W  = SCR_W / 4;              /* 4 button columns           */
}

/* ────────────────────────────────────────────────────────────
 *  2.  CALCULATOR ENGINE
 * ──────────────────────────────────────────────────────────── */

#define CALC_MAXLEN  13   /* max chars in display string */

static struct {
    char   str[CALC_MAXLEN + 1]; /* display string                     */
    double mem;                   /* stored left-hand operand           */
    char   op;                    /* '+' '-' '*' '/'  or  '\0' = none   */
    bool   fresh;  /* TRUE → next digit starts a fresh number entry     */
    bool   done;   /* TRUE → result just shown (= was last key)         */
    bool   err;    /* TRUE → error state                                */
} g;

/* ── reset ──────────────────────────────────────────────── */
static void c_reset(void) {
    memset(&g, 0, sizeof(g));
    strcpy(g.str, "0");
}

/* ── helpers ────────────────────────────────────────────── */
static double c_val(void) { return atof(g.str); }

static void c_fmt(double v) {
    if (v == 0.0) { strcpy(g.str, "0"); return; }
    long long iv = (long long)v;
    if ((double)iv == v && v > -9.9e12 && v < 9.9e12)
        snprintf(g.str, sizeof(g.str), "%lld", iv);
    else
        snprintf(g.str, sizeof(g.str), "%.7g", v);
    g.str[CALC_MAXLEN] = '\0';   /* hard-cap length */
}

/* ── digit 0–9 ──────────────────────────────────────────── */
static void c_digit(int d) {
    if (g.err) return;
    if (g.fresh || g.done) {
        snprintf(g.str, sizeof(g.str), "%d", d);
        g.fresh = g.done = false;
        return;
    }
    if (strcmp(g.str, "0") == 0) {
        snprintf(g.str, sizeof(g.str), "%d", d);
    } else {
        int n = (int)strlen(g.str);
        if (n < CALC_MAXLEN) { g.str[n] = (char)('0'+d); g.str[n+1] = '\0'; }
    }
}

/* ── decimal point ──────────────────────────────────────── */
static void c_dot(void) {
    if (g.err) return;
    if (g.fresh || g.done) {
        strcpy(g.str, "0.");
        g.fresh = g.done = false;
        return;
    }
    if (!strchr(g.str, '.')) {
        int n = (int)strlen(g.str);
        if (n < CALC_MAXLEN) { g.str[n] = '.'; g.str[n+1] = '\0'; }
    }
}

/* ── apply pending operator ─────────────────────────────── */
static void c_compute(void) {
    if (!g.op) return;
    double b = c_val();
    if (g.op == '/' && b == 0.0) {
        strcpy(g.str, "Err:Div0");
        g.err = true; g.op = '\0';
        return;
    }
    double r = 0.0;
    switch (g.op) {
        case '+': r = g.mem + b; break;
        case '-': r = g.mem - b; break;
        case '*': r = g.mem * b; break;
        case '/': r = g.mem / b; break;
    }
    c_fmt(r);
    g.op = '\0'; g.fresh = false;
}

/* ── operator key (+  -  *  /) ──────────────────────────── */
static void c_op(char op) {
    if (g.err) return;
    if (g.op && !g.fresh) c_compute();   /* chain-evaluate first */
    g.mem = c_val(); g.op = op;
    g.fresh = true;  g.done = false;
}

/* ── equals ─────────────────────────────────────────────── */
static void c_eq(void) {
    if (g.err || !g.op) return;
    c_compute();
    g.done = true;
}

/* ── AC (all clear) ─────────────────────────────────────── */
static void c_ac(void) { c_reset(); }

/* ── +/- sign toggle ────────────────────────────────────── */
static void c_negate(void) {
    if (g.err || strcmp(g.str, "0") == 0) return;
    c_fmt(-c_val());
    g.done = false;
}

/* ── percent ────────────────────────────────────────────── */
static void c_percent(void) {
    if (g.err) return;
    double v = c_val();
    /* iOS-style: x% of mem when adding/subtracting, else x/100 */
    c_fmt((g.op == '+' || g.op == '-') ? g.mem * v / 100.0 : v / 100.0);
    g.done = false;
}

/* ────────────────────────────────────────────────────────────
 *  3.  BUTTON TABLE
 * ──────────────────────────────────────────────────────────── */

typedef enum {
    B_0=0, B_1, B_2, B_3, B_4,
    B_5,   B_6, B_7, B_8, B_9,
    B_DOT,
    B_ADD, B_SUB, B_MUL, B_DIV,
    B_EQ,  B_AC,  B_NEG, B_PCT,
    B_COUNT    /* ← sentinel; keep last */
} BID;

typedef struct {
    GRect       rect;
    const char *label;
    BID         id;
    GColor      bg, fg;
} Btn;

static Btn s_btns[B_COUNT];
static int s_held = -1;   /* index of currently-pressed button */

static void btns_init(void) {
    /* col · row · colspan · id · label · bg · fg */
    typedef struct { int8_t c,r,cs; BID id; const char *l; GColor bg,fg; } T;

    static const T D[] = {
        /* row 0 — function keys */
        {0,0,1, B_AC,  "AC",  GColorLightGray,    GColorBlack       },
        {1,0,1, B_NEG, "+/-", GColorLightGray,    GColorBlack       },
        {2,0,1, B_PCT, "%",   GColorLightGray,    GColorBlack       },
        {3,0,1, B_DIV, "/",   GColorOrange,       GColorWhite       },
        /* row 1 */
        {0,1,1, B_7,  "7",    GColorDarkGray,     GColorWhite       },
        {1,1,1, B_8,  "8",    GColorDarkGray,     GColorWhite       },
        {2,1,1, B_9,  "9",    GColorDarkGray,     GColorWhite       },
        {3,1,1, B_MUL,"x",    GColorOrange,       GColorWhite       },
        /* row 2 */
        {0,2,1, B_4,  "4",    GColorDarkGray,     GColorWhite       },
        {1,2,1, B_5,  "5",    GColorDarkGray,     GColorWhite       },
        {2,2,1, B_6,  "6",    GColorDarkGray,     GColorWhite       },
        {3,2,1, B_SUB,"-",    GColorOrange,       GColorWhite       },
        /* row 3 */
        {0,3,1, B_1,  "1",    GColorDarkGray,     GColorWhite       },
        {1,3,1, B_2,  "2",    GColorDarkGray,     GColorWhite       },
        {2,3,1, B_3,  "3",    GColorDarkGray,     GColorWhite       },
        {3,3,1, B_ADD,"+",    GColorOrange,       GColorWhite       },
        /* row 4 */
        {0,4,2, B_0,  "0",    GColorDarkGray,     GColorWhite       },
        {2,4,1, B_DOT,".",    GColorDarkGray,     GColorWhite       },
        {3,4,1, B_EQ, "=",    GColorIslamicGreen, GColorWhite       },
    };

    memset(s_btns, 0, sizeof(s_btns));

    for (int i = 0; i < (int)(sizeof(D)/sizeof(D[0])); i++) {
        const T *t = &D[i];
        Btn *b     = &s_btns[t->id];
        b->id    = t->id;
        b->label = t->l;
        b->bg    = t->bg;
        b->fg    = t->fg;
        b->rect  = GRect(
            t->c  * BTN_W + 1,
            DISP_H + t->r * BTN_H + 1,
            t->cs * BTN_W - 2,
            BTN_H - 2
        );
    }
}

/* return button index at touch point p, or -1 */
static int btn_hit(GPoint p) {
    for (int i = 0; i < B_COUNT; i++)
        if (grect_contains_point(&s_btns[i].rect, &p)) return i;
    return -1;
}

/* execute action for button id */
static void btn_fire(BID id) {
    switch (id) {
        case B_0:  c_digit(0); break;   case B_1:  c_digit(1); break;
        case B_2:  c_digit(2); break;   case B_3:  c_digit(3); break;
        case B_4:  c_digit(4); break;   case B_5:  c_digit(5); break;
        case B_6:  c_digit(6); break;   case B_7:  c_digit(7); break;
        case B_8:  c_digit(8); break;   case B_9:  c_digit(9); break;
        case B_DOT: c_dot();             break;
        case B_ADD: c_op('+');           break;
        case B_SUB: c_op('-');           break;
        case B_MUL: c_op('*');           break;
        case B_DIV: c_op('/');           break;
        case B_EQ:  c_eq();              break;
        case B_AC:  c_ac();              break;
        case B_NEG: c_negate();          break;
        case B_PCT: c_percent();         break;
        default: break;
    }
}

/* ────────────────────────────────────────────────────────────
 *  4.  DRAWING
 * ──────────────────────────────────────────────────────────── */

static Layer *s_canvas;

/* ── display panel ──────────────────────────────────────── */
static void draw_display(GContext *ctx) {
    /* panel background */
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(0, 0, SCR_W, DISP_H), 0, GCornerNone);

    /* ── top row: clock (left) + operator badge (right) ── */
    GFont tiny = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    int   top_h = 17;   /* pixels reserved for the top row */

    /* clock */
    time_t now = time(NULL);
    struct tm *tm_p = localtime(&now);
    char ts[6];
    snprintf(ts, sizeof(ts), "%02d:%02d", tm_p->tm_hour, tm_p->tm_min);
    graphics_context_set_text_color(ctx, GColorDarkGray);
    graphics_draw_text(ctx, ts, tiny,
                       GRect(3, 1, 44, top_h - 2),
                       GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

    /* operator badge  (only while awaiting right operand) */
    if (g.op && g.fresh) {
        char ob[3] = { g.op == '*' ? 'x' : g.op, '\0' };
        graphics_context_set_text_color(ctx, GColorOrange);
        graphics_draw_text(ctx, ob, tiny,
                           GRect(SCR_W - 20, 1, 18, top_h - 2),
                           GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
    }

    /* ── number row ─────────────────────────────────────── */
    int num_y = top_h;
    int num_h = DISP_H - top_h;       /* available height  */
    int len   = (int)strlen(g.str);

    GFont nf;
    int   nfh;
    if      (num_h >= 24 && len <=  9) { nf = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD); nfh = 24; }
    else if (num_h >= 18 && len <= 12) { nf = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD); nfh = 18; }
    else                               { nf = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD); nfh = 14; }

    int yp = (num_h - nfh) / 2;
    if (yp < 0) yp = 0;

    graphics_context_set_text_color(ctx, g.err ? GColorRed : GColorWhite);
    graphics_draw_text(ctx, g.str, nf,
                       GRect(2, num_y + yp, SCR_W - 4, nfh + 2),
                       GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);

    /* separator line */
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    graphics_draw_line(ctx, GPoint(0, DISP_H), GPoint(SCR_W, DISP_H));
}

/* ── button grid ────────────────────────────────────────── */
static void draw_buttons(GContext *ctx) {
    GFont bf  = (BTN_H >= 28)
        ? fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD)
        : fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
    int   bfh = (BTN_H >= 28) ? 20 : 16;

    for (int i = 0; i < B_COUNT; i++) {
        Btn  *b    = &s_btns[i];
        bool  held = (i == s_held);

        /* invert colours while finger is down → visual press feedback */
        GColor bg = held ? b->fg : b->bg;
        GColor fg = held ? b->bg : b->fg;

        graphics_context_set_fill_color(ctx, bg);
        graphics_fill_rect(ctx, b->rect, 4, GCornersAll);

        int yoff = (b->rect.size.h - bfh) / 2;
        if (yoff < 0) yoff = 0;
        GRect lr = GRect(b->rect.origin.x,
                         b->rect.origin.y + yoff,
                         b->rect.size.w, bfh + 2);
        graphics_context_set_text_color(ctx, fg);
        graphics_draw_text(ctx, b->label, bf, lr,
                           GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }
}

/* ── layer update callback ─────────────────────────────── */
static void canvas_update_proc(Layer *layer, GContext *ctx) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);
    draw_display(ctx);
    draw_buttons(ctx);
}

/* ────────────────────────────────────────────────────────────
 *  5.  TOUCH HANDLER
 *
 *  Pebble SDK 6.x expected signature:
 *    void handler(GPoint pos, TouchPhase phase, void *context)
 *
 *  If TouchPhase is not defined by your SDK headers, add this
 *  above main():
 *
 *    typedef enum {
 *      TouchPhaseBegan, TouchPhaseMoved,
 *      TouchPhaseEnded, TouchPhaseCancelled
 *    } TouchPhase;
 *
 *  See TOUCH API COMPATIBILITY section at bottom for
 *  alternative signatures.
 * ──────────────────────────────────────────────────────────── */

static void touch_handler(GPoint pos, TouchPhase phase, void *context) {
    int hit = btn_hit(pos);

    switch (phase) {
        case TouchPhaseBegan:
            s_held = hit;               /* highlight the pressed button  */
            break;

        case TouchPhaseEnded:
            if (hit >= 0 && hit == s_held)
                btn_fire((BID)hit);     /* fire only on same-button lift */
            s_held = -1;
            break;

        case TouchPhaseCancelled:
            s_held = -1;               /* drag-cancelled — no action    */
            break;

        default:
            break;                     /* ignore TouchPhaseMoved        */
    }

    layer_mark_dirty(s_canvas);
}

/* ────────────────────────────────────────────────────────────
 *  6.  TICK HANDLER  — refreshes clock every minute
 * ──────────────────────────────────────────────────────────── */

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    layer_mark_dirty(s_canvas);
}

/* ────────────────────────────────────────────────────────────
 *  7.  WINDOW LIFECYCLE
 * ──────────────────────────────────────────────────────────── */

static Window *s_window;

static void window_load(Window *win) {
    Layer *root   = window_get_root_layer(win);
    GRect  bounds = layer_get_bounds(root);

    geo_init(bounds);    /* must come first — others depend on it */
    c_reset();
    btns_init();

    s_canvas = layer_create(bounds);
    layer_set_update_proc(s_canvas, canvas_update_proc);
    layer_add_child(root, s_canvas);

    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

    /* ── register touch input ────────────────────────────
     * Change this line if your SDK uses a different API.
     * See TOUCH API COMPATIBILITY at bottom of file.
     * ─────────────────────────────────────────────────── */
    window_set_touch_handler(win, touch_handler, NULL);
}

static void window_unload(Window *win) {
    tick_timer_service_unsubscribe();
    layer_destroy(s_canvas);
}

/* ────────────────────────────────────────────────────────────
 *  8.  ENTRY POINT
 * ──────────────────────────────────────────────────────────── */

int main(void) {
    s_window = window_create();
    window_set_background_color(s_window, GColorBlack);
    window_set_window_handlers(s_window, (WindowHandlers){
        .load   = window_load,
        .unload = window_unload,
    });
    window_stack_push(s_window, true /* animated */);
    app_event_loop();
    window_destroy(s_window);
    return 0;
}

/* ════════════════════════════════════════════════════════════
 *  TOUCH API COMPATIBILITY — try these if build fails
 * ════════════════════════════════════════════════════════════
 *
 *  OPTION A (phase enum — default above):
 *  ───────────────────────────────────────
 *    static void touch_handler(GPoint pos, TouchPhase phase, void *ctx)
 *    window_set_touch_handler(win, touch_handler, NULL);
 *
 *  OPTION B (boolean up/down):
 *  ─────────────────────────────
 *    static void touch_handler(GPoint pos, bool is_down, void *ctx) {
 *        int hit = btn_hit(pos);
 *        if (is_down) { s_held = hit; }
 *        else {
 *            if (hit >= 0 && hit == s_held) btn_fire((BID)hit);
 *            s_held = -1;
 *        }
 *        layer_mark_dirty(s_canvas);
 *    }
 *    window_set_touch_handler(win, touch_handler, NULL);
 *
 *  OPTION C (TouchEvent struct):
 *  ───────────────────────────────
 *    static void touch_handler(TouchEvent event, void *ctx) {
 *        GPoint pos   = event.position;
 *        TouchPhase p = event.phase;
 *        // same body as Option A
 *    }
 *    window_set_touch_event_handler(win, touch_handler, NULL);
 *
 *  OPTION D (global touch service):
 *  ──────────────────────────────────
 *    touch_event_service_subscribe(touch_handler, NULL);
 *    // remember to unsubscribe in window_unload
 * ═══════════════════════════════════════════════════════════ */