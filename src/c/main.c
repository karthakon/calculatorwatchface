#include <pebble.h>

/*
 * Calculator Watchapp — Pebble SDK 3 (gabbro / emery)
 *
 * 80s CALCULATOR WATCH BEHAVIOUR
 * ─────────────────────────────────────────────────────────
 * SINGLE FIXED LAYOUT. Buttons are always visible and live.
 * Only the top display area changes:
 *   - Idle           -> shows the time (large green LECO).
 *   - Typing/result  -> shows digits / calculations.
 *
 * AC (on screen)  -> clear calculator to 0 (stays in calc view).
 * SELECT button   -> back to time.
 * BACK button     -> back to time; press again on time to exit.
 * 30 s idle       -> back to time.
 */

/* ─────────────────────────────────────────────────────────
 *  GLOBAL STATE
 * ───────────────────────────────────────────────────────── */
static Layer    *s_canvas      = NULL;
static AppTimer *s_idle_timer  = NULL;
static AppTimer *s_held_timer  = NULL;
static int       s_held        = -1;

/* ─────────────────────────────────────────────────────────
 *  SCREEN GEOMETRY
 * ───────────────────────────────────────────────────────── */
static int16_t SCR_W, SCR_H, DISP_H, BTN_W, BTN_H;

static void geo_init(GRect b) {
    SCR_W  = b.size.w;
    SCR_H  = b.size.h;
    DISP_H = SCR_H / 5;
    BTN_H  = (SCR_H - DISP_H) / 5;
    BTN_W  = SCR_W / 4;
}

/* ─────────────────────────────────────────────────────────
 *  CALCULATOR ENGINE
 * ───────────────────────────────────────────────────────── */
#define CALC_MAXLEN 13

static struct {
    char   str[CALC_MAXLEN + 1];
    double mem;
    char   op;
    bool   fresh;
    bool   done;
    bool   err;
    bool   active;   /* false -> show time, true -> show calc */
} g;

static void c_reset(void) {
    memset(&g, 0, sizeof(g));   /* active becomes false here */
    strcpy(g.str, "0");
}

static double c_val(void) {
    const char *s = g.str;
    double sign = 1.0;
    if (*s == '-') { sign = -1.0; s++; }
    double whole = 0.0;
    while (*s >= '0' && *s <= '9') {
        whole = whole * 10.0 + (*s - '0');
        s++;
    }
    double frac = 0.0, scale = 1.0;
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            scale *= 10.0;
            frac += (*s - '0') / scale;
            s++;
        }
    }
    return sign * (whole + frac);
}

static void c_fmt(double v) {
    if (v == 0.0) { strcpy(g.str, "0"); return; }
    bool neg = v < 0.0;
    if (neg) v = -v;
    long iv = (long)v;
    if ((double)iv == v && v < 9999999999.0) {
        snprintf(g.str, sizeof(g.str), "%s%ld", neg ? "-" : "", iv);
        g.str[CALC_MAXLEN] = '\0';
        return;
    }
    long ipart = (long)v;
    double rem = v - (double)ipart;
    char dec[8];
    int di = 0;
    for (int k = 0; k < 6; k++) {
        rem *= 10.0;
        int digit = (int)rem;
        if (digit > 9) digit = 9;
        dec[di++] = (char)('0' + digit);
        rem -= digit;
    }
    dec[di] = '\0';
    while (di > 0 && dec[di-1] == '0') dec[--di] = '\0';
    if (di == 0)
        snprintf(g.str, sizeof(g.str), "%s%ld", neg ? "-" : "", ipart);
    else
        snprintf(g.str, sizeof(g.str), "%s%ld.%s", neg ? "-" : "", ipart, dec);
    g.str[CALC_MAXLEN] = '\0';
}

static void c_digit(int d) {
    if (g.err) return;
    g.active = true;
    if (g.fresh || g.done) {
        snprintf(g.str, sizeof(g.str), "%d", d);
        g.fresh = g.done = false;
        return;
    }
    if (strcmp(g.str, "0") == 0) {
        snprintf(g.str, sizeof(g.str), "%d", d);
    } else {
        int n = (int)strlen(g.str);
        if (n < CALC_MAXLEN) { g.str[n] = (char)('0' + d); g.str[n+1] = '\0'; }
    }
}

static void c_dot(void) {
    if (g.err) return;
    g.active = true;
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

static void c_op(char op) {
    if (g.err) return;
    g.active = true;
    if (g.op && !g.fresh) c_compute();
    g.mem = c_val(); g.op = op;
    g.fresh = true; g.done = false;
}

static void c_eq(void) {
    if (g.err || !g.op) return;
    g.active = true;
    c_compute();
    g.done = true;
}

static void c_negate(void) {
    if (g.err || strcmp(g.str, "0") == 0) return;
    g.active = true;
    c_fmt(-c_val());
    g.done = false;
}

static void c_percent(void) {
    if (g.err) return;
    g.active = true;
    double v = c_val();
    c_fmt((g.op == '+' || g.op == '-') ? g.mem * v / 100.0 : v / 100.0);
    g.done = false;
}

/* ─────────────────────────────────────────────────────────
 *  BUTTON TABLE
 * ───────────────────────────────────────────────────────── */
typedef enum {
    B_0=0, B_1, B_2, B_3, B_4,
    B_5,   B_6, B_7, B_8, B_9,
    B_DOT, B_ADD, B_SUB, B_MUL, B_DIV,
    B_EQ, B_AC, B_NEG, B_PCT,
    B_COUNT
} BID;

typedef struct {
    GRect       rect;
    const char *label;
    BID         id;
    GColor      bg, fg;
} Btn;

static Btn s_btns[B_COUNT];

static void btns_init(void) {
    typedef struct {
        int8_t c, r, cs; BID id; const char *l; GColor bg, fg;
    } T;
    static const T D[] = {
        {0,0,1, B_AC,  "AC",  GColorLightGray,    GColorBlack  },
        {1,0,1, B_NEG, "+/-", GColorLightGray,    GColorBlack  },
        {2,0,1, B_PCT, "%",   GColorLightGray,    GColorBlack  },
        {3,0,1, B_DIV, "/",   GColorOrange,       GColorWhite  },
        {0,1,1, B_7,   "7",   GColorDarkGray,     GColorWhite  },
        {1,1,1, B_8,   "8",   GColorDarkGray,     GColorWhite  },
        {2,1,1, B_9,   "9",   GColorDarkGray,     GColorWhite  },
        {3,1,1, B_MUL, "x",   GColorOrange,       GColorWhite  },
        {0,2,1, B_4,   "4",   GColorDarkGray,     GColorWhite  },
        {1,2,1, B_5,   "5",   GColorDarkGray,     GColorWhite  },
        {2,2,1, B_6,   "6",   GColorDarkGray,     GColorWhite  },
        {3,2,1, B_SUB, "-",   GColorOrange,       GColorWhite  },
        {0,3,1, B_1,   "1",   GColorDarkGray,     GColorWhite  },
        {1,3,1, B_2,   "2",   GColorDarkGray,     GColorWhite  },
        {2,3,1, B_3,   "3",   GColorDarkGray,     GColorWhite  },
        {3,3,1, B_ADD, "+",   GColorOrange,       GColorWhite  },
        {0,4,2, B_0,   "0",   GColorDarkGray,     GColorWhite  },
        {2,4,1, B_DOT, ".",   GColorDarkGray,     GColorWhite  },
        {3,4,1, B_EQ,  "=",   GColorIslamicGreen, GColorWhite  },
    };
    memset(s_btns, 0, sizeof(s_btns));
    for (int i = 0; i < (int)(sizeof(D)/sizeof(D[0])); i++) {
        const T *t = &D[i];
        Btn     *b = &s_btns[t->id];
        b->id = t->id; b->label = t->l; b->bg = t->bg; b->fg = t->fg;
        b->rect = GRect(t->c * BTN_W + 1,
                        DISP_H + t->r * BTN_H + 1,
                        t->cs * BTN_W - 2,
                        BTN_H - 2);
    }
}

static int btn_hit(GPoint p) {
    for (int i = 0; i < B_COUNT; i++)
        if (grect_contains_point(&s_btns[i].rect, &p)) return i;
    return -1;
}

/* ─────────────────────────────────────────────────────────
 *  IDLE / HELD TIMERS
 * ───────────────────────────────────────────────────────── */
static void revert_to_time(void) {
    c_reset();   /* active -> false, display shows time */
    if (s_idle_timer) { app_timer_cancel(s_idle_timer); s_idle_timer = NULL; }
    if (s_canvas) layer_mark_dirty(s_canvas);
}

static void idle_timer_cb(void *context) {
    s_idle_timer = NULL;
    c_reset();
    if (s_canvas) layer_mark_dirty(s_canvas);
}

static void reset_idle_timer(void) {
    if (s_idle_timer) { app_timer_cancel(s_idle_timer); s_idle_timer = NULL; }
    s_idle_timer = app_timer_register(30000, idle_timer_cb, NULL);
}

static void clear_held_cb(void *context) {
    s_held_timer = NULL;
    s_held = -1;
    if (s_canvas) layer_mark_dirty(s_canvas);
}

/* ─────────────────────────────────────────────────────────
 *  BUTTON ACTIONS
 * ───────────────────────────────────────────────────────── */
static void btn_fire(BID id) {
    switch (id) {
        case B_0:   c_digit(0);  break;
        case B_1:   c_digit(1);  break;
        case B_2:   c_digit(2);  break;
        case B_3:   c_digit(3);  break;
        case B_4:   c_digit(4);  break;
        case B_5:   c_digit(5);  break;
        case B_6:   c_digit(6);  break;
        case B_7:   c_digit(7);  break;
        case B_8:   c_digit(8);  break;
        case B_9:   c_digit(9);  break;
        case B_DOT: c_dot();     break;
        case B_ADD: c_op('+');   break;
        case B_SUB: c_op('-');   break;
        case B_MUL: c_op('*');   break;
        case B_DIV: c_op('/');   break;
        case B_EQ:  c_eq();      break;
        case B_NEG: c_negate();  break;
        case B_PCT: c_percent(); break;
        case B_AC:
            c_reset();
            g.active = true;   /* clear to 0 but stay in calculator view */
            break;
        default: break;
    }
}

/* ─────────────────────────────────────────────────────────
 *  DRAWING
 * ───────────────────────────────────────────────────────── */
static void draw_display(GContext *ctx) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(0, 0, SCR_W, DISP_H), 0, GCornerNone);

    time_t     now  = time(NULL);
    struct tm *tm_p = localtime(&now);

    if (!g.active) {
        /* TIME: 80s green LCD look */
        char ts[6];
        snprintf(ts, sizeof(ts), "%02d:%02d",
                 tm_p->tm_hour, tm_p->tm_min);

        bool  big   = (DISP_H >= 40);
        GFont tfont = big
            ? fonts_get_system_font(FONT_KEY_LECO_32_BOLD_NUMBERS)
            : fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS);
        int   tfh   = big ? 36 : 22;

        bool show_date = (DISP_H >= 52);
        int  date_h    = show_date ? 16 : 0;
        int  yp        = (DISP_H - date_h - tfh) / 2;
        if (yp < 1) yp = 1;

        graphics_context_set_text_color(ctx, GColorGreen);
        graphics_draw_text(ctx, ts, tfont,
                           GRect(0, yp, SCR_W, tfh + 4),
                           GTextOverflowModeWordWrap,
                           GTextAlignmentCenter, NULL);

        if (show_date) {
            static const char * const WD[7] = {
                "Sun","Mon","Tue","Wed","Thu","Fri","Sat"
            };
            char ds[12];
            snprintf(ds, sizeof(ds), "%s %d/%d",
                     WD[tm_p->tm_wday],
                     tm_p->tm_mon + 1,
                     tm_p->tm_mday);
            graphics_context_set_text_color(ctx, GColorIslamicGreen);
            graphics_draw_text(ctx, ds,
                               fonts_get_system_font(FONT_KEY_GOTHIC_14),
                               GRect(0, DISP_H - 16, SCR_W, 14),
                               GTextOverflowModeWordWrap,
                               GTextAlignmentCenter, NULL);
        }

    } else {
        /* CALCULATOR NUMBERS */
        GFont small = fonts_get_system_font(FONT_KEY_GOTHIC_14);
        int   top_h = (DISP_H >= 40) ? 16 : 0;

        if (top_h && g.op && g.fresh) {
            char ob[2];
            ob[0] = (g.op == '*') ? 'x' : g.op;
            ob[1] = '\0';
            graphics_context_set_text_color(ctx, GColorOrange);
            graphics_draw_text(ctx, ob, small,
                               GRect(3, 1, 18, top_h - 1),
                               GTextOverflowModeWordWrap,
                               GTextAlignmentLeft, NULL);
        }

        int num_y = top_h;
        int num_h = DISP_H - top_h;
        int len   = (int)strlen(g.str);
        GFont nf; int nfh;
        if      (num_h >= 24 && len <=  9) {
            nf = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD); nfh = 24;
        } else if (num_h >= 18 && len <= 12) {
            nf = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD); nfh = 18;
        } else {
            nf = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD); nfh = 14;
        }
        int yp = (num_h - nfh) / 2;
        if (yp < 0) yp = 0;
        graphics_context_set_text_color(ctx, g.err ? GColorRed : GColorWhite);
        graphics_draw_text(ctx, g.str, nf,
                           GRect(2, num_y + yp, SCR_W - 4, nfh + 2),
                           GTextOverflowModeWordWrap,
                           GTextAlignmentRight, NULL);
    }

    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    graphics_draw_line(ctx, GPoint(0, DISP_H), GPoint(SCR_W, DISP_H));
}

static void draw_buttons(GContext *ctx) {
    GFont bf  = (BTN_H >= 28)
              ? fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD)
              : fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
    int   bfh = (BTN_H >= 28) ? 20 : 16;

    for (int i = 0; i < B_COUNT; i++) {
        Btn   *b    = &s_btns[i];
        bool   held = (i == s_held);
        GColor bg   = held ? b->fg : b->bg;
        GColor fg   = held ? b->bg : b->fg;

        graphics_context_set_fill_color(ctx, bg);
        graphics_fill_rect(ctx, b->rect, 4, GCornersAll);

        int yoff = (b->rect.size.h - bfh) / 2;
        if (yoff < 0) yoff = 0;
        GRect lr = GRect(b->rect.origin.x,
                         b->rect.origin.y + yoff,
                         b->rect.size.w, bfh + 2);
        graphics_context_set_text_color(ctx, fg);
        graphics_draw_text(ctx, b->label, bf, lr,
                           GTextOverflowModeWordWrap,
                           GTextAlignmentCenter, NULL);
    }
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);
    draw_display(ctx);
    draw_buttons(ctx);
}

/* ─────────────────────────────────────────────────────────
 *  TOUCH HANDLER
 * ───────────────────────────────────────────────────────── */
static void touch_handler(const TouchEvent *event, void *context) {
    reset_idle_timer();
    if (event->type != TouchEvent_Touchdown) return;

    GPoint pos = GPoint(event->x, event->y);
    int    hit = btn_hit(pos);

    if (hit >= 0) {
        if (s_held_timer) { app_timer_cancel(s_held_timer); s_held_timer = NULL; }
        s_held = hit;
        btn_fire((BID)hit);
        s_held_timer = app_timer_register(150, clear_held_cb, NULL);
        layer_mark_dirty(s_canvas);
    }
}

/* ─────────────────────────────────────────────────────────
 *  PHYSICAL BUTTONS
 * ───────────────────────────────────────────────────────── */
static void select_click_handler(ClickRecognizerRef rec, void *context) {
    revert_to_time();
}

static void back_click_handler(ClickRecognizerRef rec, void *context) {
    if (g.active) {
        revert_to_time();            /* showing numbers -> show time */
    } else {
        window_stack_pop_all(true);  /* already on time -> exit app */
    }
}

static void click_config_provider(void *context) {
    window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
    window_single_click_subscribe(BUTTON_ID_BACK,   back_click_handler);
}

/* ─────────────────────────────────────────────────────────
 *  TICK HANDLER
 * ───────────────────────────────────────────────────────── */
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    layer_mark_dirty(s_canvas);
}

/* ─────────────────────────────────────────────────────────
 *  WINDOW LIFECYCLE
 * ───────────────────────────────────────────────────────── */
static Window *s_window;

static void window_load(Window *win) {
    Layer *root   = window_get_root_layer(win);
    GRect  bounds = layer_get_bounds(root);
    geo_init(bounds);
    c_reset();
    btns_init();
    s_canvas = layer_create(bounds);
    layer_set_update_proc(s_canvas, canvas_update_proc);
    layer_add_child(root, s_canvas);
    window_set_click_config_provider(win, click_config_provider);
    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    touch_service_subscribe(touch_handler, NULL);
}

static void window_unload(Window *win) {
    touch_service_unsubscribe();
    tick_timer_service_unsubscribe();
    if (s_idle_timer) { app_timer_cancel(s_idle_timer); s_idle_timer = NULL; }
    if (s_held_timer) { app_timer_cancel(s_held_timer); s_held_timer = NULL; }
    layer_destroy(s_canvas);
    s_canvas = NULL;
}

/* ─────────────────────────────────────────────────────────
 *  ENTRY POINT
 * ───────────────────────────────────────────────────────── */
int main(void) {
    s_window = window_create();
    window_set_background_color(s_window, GColorBlack);
    window_set_window_handlers(s_window, (WindowHandlers){
        .load   = window_load,
        .unload = window_unload,
    });
    window_stack_push(s_window, true);
    app_event_loop();
    window_destroy(s_window);
    return 0;
}