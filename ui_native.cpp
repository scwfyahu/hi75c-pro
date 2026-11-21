/*
 * Hi75C Pro — native UI (GDI+). No browser engine: a full window costs a few
 * megabytes instead of a hundred. Everything is custom drawn so it does not
 * look like a 1990s dialog.
 */
#include <windows.h>
#ifdef __cplusplus
#endif
#include <objidl.h>
#include <gdiplus.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <string.h>
#include "app.h"
#include "board.h"

using namespace Gdiplus;

#define IDT_TICK 1

static ULONG_PTR g_gdiplus;
static HWND g_hwnd;
static const wchar_t *g_face = L"Tahoma";

/* ---- design tokens: near-black canvas, one accent, hierarchy by weight ---- */
static const Color C_CANVAS_TOP(255, 13, 14, 18);
static const Color C_CANVAS_BOT(255, 8, 9, 12);
static const Color C_SURFACE_TOP(255, 22, 24, 31);
static const Color C_SURFACE_BOT(255, 17, 19, 24);
static const Color C_HAIRLINE(255, 40, 44, 54);
static const Color C_HAIRLINE_SOFT(255, 30, 33, 41);
static const Color C_INK(255, 236, 238, 242);
static const Color C_INK_2(255, 155, 161, 174);
static const Color C_INK_3(255, 110, 116, 128);
static const Color C_ACCENT(255, 91, 140, 255);
static const Color C_ACCENT_DIM(255, 22, 35, 61);
static const Color C_OK(255, 62, 207, 142);
static const Color C_BAD(255, 240, 104, 92);

static const Color SWATCH[7] = {
    Color(255, 240, 104, 92), Color(255, 244, 176, 76), Color(255, 236, 216, 92),
    Color(255, 96, 208, 138), Color(255, 88, 176, 244), Color(255, 138, 122, 246),
    Color(255, 236, 122, 198)
};   /* Segoe UI where it exists */
static int g_hover = -1, g_press = -1;
static FILE *g_dbg;
static void dbg(const char *fmt, ...) {
    va_list ap;
    if (!g_dbg) return;
    va_start(ap, fmt); vfprintf(g_dbg, fmt, ap); va_end(ap);
    fputc('\n', g_dbg); fflush(g_dbg);
}
static int g_sel = -1;
static volatile LONG g_update = 0;          /* selected key index in the keymap */
static char g_code[8] = "";     /* hex code being typed */
static int g_code_cur = -1;     /* usage last read for the selected key */

/* layout */
#define W_INIT 1010
#define H_INIT 900
#define PAD 22
#define HEADER_H 64
#define CARD_R 14

typedef struct { int x, y, w, h, id; const char *label; } Hot;

static Hot g_hot[256];
static int g_hot_n;

static void add_hot(int x, int y, int w, int h, int id, const char *label) {
    if (g_hot_n >= 256) return;
    g_hot[g_hot_n].x = x; g_hot[g_hot_n].y = y;
    g_hot[g_hot_n].w = w; g_hot[g_hot_n].h = h;
    g_hot[g_hot_n].id = id; g_hot[g_hot_n].label = label;
    g_hot_n++;
}

static void rounded(GraphicsPath *p, RectF r, float rad) {
    float d = rad * 2;
    p->AddArc(r.X, r.Y, d, d, 180, 90);
    p->AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p->AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p->AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p->CloseFigure();
}

static void fill_round(Graphics *g, const RectF &r, float rad, const Color &c) {
    GraphicsPath p;
    rounded(&p, r, rad);
    SolidBrush b(c);
    g->FillPath(&b, &p);
}

static void stroke_round(Graphics *g, const RectF &r, float rad, const Color &c, float w) {
    GraphicsPath p;
    rounded(&p, r, rad);
    Pen pen(c, w);
    g->DrawPath(&pen, &p);
}

static void text(Graphics *g, const char *s, const RectF &r, float size,
                 const Color &c, StringAlignment align, const wchar_t *face = NULL,
                 bool bold = false) {
    Font f(face ? face : g_face, size, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    SolidBrush b(c);
    StringFormat fmt;
    fmt.SetAlignment(align);
    fmt.SetLineAlignment(StringAlignmentCenter);
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    wchar_t wide[256];
    MultiByteToWideChar(CP_UTF8, 0, s, -1, wide, 256);
    g->DrawString(wide, -1, &f, r, &fmt, &b);
}


/* ---- live lighting preview -------------------------------------------------
   The board shows what the keyboard is doing: per-key colour for the selected
   effect, animated by the repaint timer. This is the part that makes the app
   feel like the hardware instead of a settings dialog.
*/
static void hsv(double h, double s, double v, BYTE *r, BYTE *g, BYTE *b) {
    double c = v * s, x = c * (1 - fabs(fmod(h * 6, 2) - 1)), m = v - c;
    double rr = 0, gg = 0, bb = 0;
    h = h - floor(h);
    int seg = (int)(h * 6);
    switch (seg) {
        case 0: rr = c; gg = x; break;
        case 1: rr = x; gg = c; break;
        case 2: gg = c; bb = x; break;
        case 3: gg = x; bb = c; break;
        case 4: rr = x; bb = c; break;
        default: rr = c; bb = x; break;
    }
    *r = (BYTE)((rr + m) * 255);
    *g = (BYTE)((gg + m) * 255);
    *b = (BYTE)((bb + m) * 255);
}


/* when a key is too narrow for its full legend, use the abbreviation a real
   board would print rather than an ellipsis */
static const char *short_label(const char *label) {
    static const char *pairs[][2] = {
        {"Backspace", "Bksp"}, {"Enter", "Ent"}, {"PgUp", "PgUp"}, {"PgDn", "PgDn"},
        {"RCtrl", "RCtl"}, {"Right", "Rgt"}, {"Down", "Dn"}, {"Left", "Lft"},
        {"RShift", "RSft"}, {"LShift", "LSft"}, {"Delete", "Del"}, {"Space", "Space"},
        {"Knob", "Knob"}, {"Caps", "Caps"}, {"Tab", "Tab"}, {"Esc", "Esc"}
    };
    int i;
    for (i = 0; i < (int)(sizeof(pairs) / sizeof(pairs[0])); i++)
        if (!strcmp(label, pairs[i][0])) return pairs[i][1];
    return label;
}

static float g_anim_t;   /* seconds since start, set once per paint */

static Color preview_colour(int i, float x, float y)
{
    int fx = app_state()->effect;
    int br = app_state()->brightness;
    double lv = (br < 0 ? 0 : (br + 1) / 10.0);
    Color base = SWATCH[(app_state()->colour >= 0 && app_state()->colour < 7)
                        ? app_state()->colour : 4];
    double r = base.GetR(), g = base.GetG(), b = base.GetB();
    double t = g_anim_t;
    BYTE hr, hg, hb;
    double dist = sqrt((x - 8.0) * (x - 8.0) + (y - 3.0) * (y - 3.0));

    switch (fx) {
        case 0: return Color(0, 0, 0, 0);                 /* off: no tint */
        case 2: { double k = 0.40 + 0.60 * (0.5 + 0.5 * sin(t * 2.2));
                  r *= k; g *= k; b *= k; } break;
        case 3: hsv(fmod(t * 0.12 + i * 0.004, 1.0), 0.85, 1.0, &hr, &hg, &hb);
                r = hr; g = hg; b = hb; break;
        case 4: { double k = 0.35 + 0.65 * (0.5 + 0.5 * sin(t * 3.0 - y * 0.9));
                  r *= k; g *= k; b *= k; } break;
        case 5: { double h = sin(i * 12.9898) * 43758.5453; h = h - floor(h);
                  double k = 0.25 + 0.75 * pow(fabs(sin(t * 2.0 + h * 6.28)), 6);
                  r *= k; g *= k; b *= k; } break;
        case 6: { double k = 0.35 + 0.65 * (0.5 + 0.5 * sin(x * 0.55 - t * 2.0));
                  r *= k; g *= k; b *= k; } break;
        case 7: case 13: { double k = 0.30 + 0.70 * (0.5 + 0.5 * sin(dist * 0.8 - t * 2.4));
                           r *= k; g *= k; b *= k; } break;
        case 8: { double k = 0.20 + 0.80 * pow(fabs(sin(t * 2.6 + i * 1.7)), 8);
                  r *= k; g *= k; b *= k; } break;
        case 15: hsv(fmod(t * 0.08 + (atan2(y - 3.2, x - 8.1) / 6.2832) + 0.5, 1.0),
                     0.9, 1.0, &hr, &hg, &hb);
                 r = hr; g = hg; b = hb; break;
        case 16: { double k = 0.25 + 0.75 * (0.5 + 0.5 * sin(y * 1.6 - t * 3.2));
                   r *= k; g *= k; b *= k; } break;
        case 17: { double h = sin(i * 7.31 + t * 30.0);
                   double k = h > 0.4 ? 1.0 : 0.25;
                   r *= k; g *= k; b *= k; } break;
        case 32: hsv(fmod(i * 0.11 + t * 0.05, 1.0), 0.8, 1.0, &hr, &hg, &hb);
                 r = hr; g = hg; b = hb; break;
        default: break;                                    /* solid accent */
    }
    r *= lv; g *= lv; b *= lv;
    if (!app_connected() && fx != 0) { r *= 0.42; g *= 0.42; b *= 0.42; }
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    return Color(255, (BYTE)r, (BYTE)g, (BYTE)b);
}


/* 12x12 vector icons, drawn rather than shipped as resources */
static void icon(Graphics *g, int kind, float x, float y, const Color &c) {
    Pen pen(c, 1.4f);
    SolidBrush br(c);
    switch (kind) {
        case 0:   /* off: circle with a slash */
            g->DrawEllipse(&pen, x + 1, y + 1, 10.0f, 10.0f);
            g->DrawLine(&pen, x + 3, y + 9, x + 9, y + 3);
            break;
        case 1:   /* solid */
            g->FillEllipse(&br, x + 2, y + 2, 8.0f, 8.0f);
            break;
        case 2:   /* breathing: rings */
            g->DrawEllipse(&pen, x + 1, y + 1, 10.0f, 10.0f);
            g->DrawEllipse(&pen, x + 4, y + 4, 4.0f, 4.0f);
            break;
        case 3:   /* spectrum: three arcs */
            g->DrawArc(&pen, x + 1, y + 2, 10.0f, 10.0f, 200.0f, 140.0f);
            g->DrawArc(&pen, x + 2.5f, y + 3.5f, 7.0f, 7.0f, 200.0f, 140.0f);
            g->DrawArc(&pen, x + 4, y + 5, 4.0f, 4.0f, 200.0f, 140.0f);
            break;
        case 4:   /* rows */
            g->DrawLine(&pen, x + 1, y + 3, x + 11, y + 3);
            g->DrawLine(&pen, x + 1, y + 6, x + 11, y + 6);
            g->DrawLine(&pen, x + 1, y + 9, x + 11, y + 9);
            break;
        case 5:   /* drops */
            g->FillEllipse(&br, x + 2, y + 2, 3.0f, 3.0f);
            g->FillEllipse(&br, x + 7, y + 5, 3.0f, 3.0f);
            g->FillEllipse(&br, x + 4, y + 8, 2.4f, 2.4f);
            break;
        case 6:   /* wave */
            g->DrawArc(&pen, x + 0, y + 3, 6.0f, 6.0f, 180.0f, 180.0f);
            g->DrawArc(&pen, x + 6, y + 3, 6.0f, 6.0f, 0.0f, 180.0f);
            break;
        case 7:   /* ripples */
            g->DrawEllipse(&pen, x + 1, y + 3, 10.0f, 6.0f);
            g->DrawEllipse(&pen, x + 4, y + 5, 4.0f, 2.5f);
            break;
        case 8:   /* star */
            g->DrawLine(&pen, x + 6, y + 1, x + 6, y + 11);
            g->DrawLine(&pen, x + 1, y + 6, x + 11, y + 6);
            g->DrawLine(&pen, x + 3, y + 3, x + 9, y + 9);
            g->DrawLine(&pen, x + 9, y + 3, x + 3, y + 9);
            break;
        case 9:   /* folder (import) */
            g->DrawRectangle(&pen, x + 1, y + 3.5f, 10.0f - 1, 6.5f);
            g->DrawLine(&pen, x + 1, y + 3.5f, x + 5, y + 3.5f);
            g->DrawLine(&pen, x + 5, y + 3.5f, x + 5, y + 2);
            g->DrawLine(&pen, x + 5, y + 2, x + 9, y + 2);
            g->DrawLine(&pen, x + 9, y + 2, x + 9, y + 3.5f);
            break;
        case 10:  /* restore: counter-clockwise arrow */
            g->DrawArc(&pen, x + 1.5f, y + 1.5f, 9.0f, 9.0f, 40.0f, 280.0f);
            g->DrawLine(&pen, x + 1.5f, y + 4.5f, x + 1.5f, y + 1.5f);
            g->DrawLine(&pen, x + 1.5f, y + 1.5f, x + 4.5f, y + 1.5f);
            break;
        case 11:  /* rescan: magnifier */
            g->DrawEllipse(&pen, x + 1.5f, y + 1.5f, 7.0f, 7.0f);
            g->DrawLine(&pen, x + 8, y + 8, x + 11, y + 11);
            break;
        default:
            g->FillEllipse(&br, x + 4, y + 4, 4.0f, 4.0f);
            break;
    }
}

/* effect index -> icon */
static int effect_icon(int i, int hw) {
    switch (hw) {
        case 0: return 0;
        case 1: return 1;
        case 2: return 2;
        case 3: return 3;
        case 4: return 4;
        case 5: return 5;
        case 6: return 6;
        case 7: return 7;
        case 8: return 8;
        case 10: return 6;
        case 11: return 3;
        case 12: return 5;
        case 13: return 7;
        case 15: return 3;
        case 16: return 4;
        case 17: return 8;
        case 32: return 5;
    }
    (void)i;
    return 1;
}


static float g_scale = 1.0f;   /* physical pixels per logical pixel */
static int g_page;   /* 0 lighting, 1 keymap, 2 about */

static void draw_rail(Graphics *g, const RECT &rc, int w, const Color &accent,
                      const Color &ink, const Color &dim, const Color &faint) {
    SolidBrush bg(Color(255, 15, 17, 22));
    g->FillRectangle(&bg, 0, 0, w, rc.bottom);
    Pen edge(Color(255, 26, 29, 37), 1.0f);
    g->DrawLine(&edge, PointF((REAL)w, 0), PointF((REAL)w, (REAL)rc.bottom));

    text(g, "SECTIONS", RectF(24, 24, 140, 16), 9.5f, faint, StringAlignmentNear, NULL, true);
    {
        const char *names[3] = {"Lighting", "Keymap", "About"};
        int glyph[3] = {3, 4, 1};
        int i;
        for (i = 0; i < 3; i++) {
            int top = 48 + i * 42;
            RectF it(12, (REAL)top, (REAL)(w - 24), 34.0f);
            if (g_page == i) {
                fill_round(g, it, 10.0f, Color(255, 26, 31, 41));
                SolidBrush bar(accent);
                g->FillRectangle(&bar, 12.0f, (REAL)top + 8, 3.0f, 18.0f);
            }
            icon(g, glyph[i], 26, (REAL)top + 10, g_page == i ? ink : faint);
            text(g, names[i], RectF(48, (REAL)top, 120, 34), 13.0f,
                 g_page == i ? ink : dim, StringAlignmentNear);
            add_hot(12, top, w - 24, 34, 700 + i, names[i]);
        }
    }
    {
        SolidBrush dot(app_connected() ? C_OK : C_BAD);
        g->FillEllipse(&dot, 24.0f, (REAL)rc.bottom - 58, 8.0f, 8.0f);
        text(g, app_connected() ? "Keyboard ready" : "No keyboard",
             RectF(40, (REAL)rc.bottom - 64, 140, 20), 11.0f,
             app_connected() ? C_OK : faint, StringAlignmentNear);
        {
            HANDLE d = OpenMutexA(SYNCHRONIZE, FALSE, "Hi75CPro_Daemon_Singleton");
            if (d) {
                CloseHandle(d);
                text(g, "background driver active", RectF(24, (REAL)rc.bottom - 88, 160, 18),
                     10.0f, C_ACCENT, StringAlignmentNear);
            }
        }
        {
            char vb[32];
            snprintf(vb, sizeof(vb), "v%s", app_version());
            text(g, vb, RectF(24, (REAL)rc.bottom - 34, 120, 16), 10.0f, faint,
                 StringAlignmentNear);
        }
        if (0) text(g, "", RectF(0, 0, 0, 0), 10.0f, faint,
             StringAlignmentNear);
    }
}


/* forward declarations: this helper is defined before the paint utilities */
static void shadow(Graphics *g, const RectF &r, float rad);
static void card_skin(Graphics *g, GraphicsPath *path, const RectF &r,
                      const Color &top, const Color &bot, const Color &line);
static void draw_keyboard(Graphics *g, const RectF &area, const Color &line, const Color &dim);

/* secondary pages share one card; the board is reused so the keymap page is
   the same keyboard you configure on the lighting page */
static void draw_secondary(Graphics *g, const RECT &rc, int cx, int cw, int top, int bottom,
                           const Color &cardTop, const Color &cardBot, const Color &line,
                           const Color &ink, const Color &dim, const Color &accent) {
    RectF card((REAL)cx, (REAL)top, (REAL)cw, (REAL)(bottom - top));
    shadow(g, card, 18.0f);
    GraphicsPath p;
    rounded(&p, card, 18.0f);
    card_skin(g, &p, card, cardTop, cardBot, line);

    if (g_page == 1) {
        text(g, "KEYMAP", RectF(card.X + 22, card.Y + 16, 200, 18), 10.5f, dim,
             StringAlignmentNear, NULL, true);
        text(g, "click a key, then type a hex usage code", 
             RectF(card.X + 120, card.Y + 16, 420, 18), 10.5f, Color(255, 96, 106, 124),
             StringAlignmentNear);
        {
            /* the layer being remapped matters as much here as on the board */
            int layer = app_layer();
            RectF track(card.X + card.Width - 162, card.Y + 10, 148, 30);
            fill_round(g, track, 9.0f, Color(255, 18, 20, 26));
            stroke_round(g, track, 9.0f, line, 1.0f);
            RectF lb(card.X + card.Width - 159, card.Y + 13, 70, 24);
            RectF lf(card.X + card.Width - 85, card.Y + 13, 68, 24);
            if (layer == 0) fill_round(g, lb, 7.0f, Color(255, 22, 35, 61));
            else fill_round(g, lf, 7.0f, Color(255, 22, 35, 61));
            text(g, "Base", lb, 11.5f, layer == 0 ? ink : dim, StringAlignmentCenter);
            text(g, "FN", lf, 11.5f, layer == 1 ? ink : dim, StringAlignmentCenter);
            add_hot((int)lb.X, (int)lb.Y, (int)lb.Width, (int)lb.Height, 500, "base");
            add_hot((int)lf.X, (int)lf.Y, (int)lf.Width, (int)lf.Height, 501, "fn");
        }
        RectF panel;
        {
            float availW = card.Width - 44;
            float wantH = availW * (BOARD_HEIGHT / BOARD_WIDTH);
            float availH = card.Height - 132;
            if (wantH > availH) wantH = availH;
            float bx = card.X + 22 + (availW - wantH * (BOARD_WIDTH / BOARD_HEIGHT)) / 2;
            draw_keyboard(g, RectF(bx, card.Y + 52, wantH * (BOARD_WIDTH / BOARD_HEIGHT), wantH),
                          line, dim);
        }
        {
            /* detail panel under the board */
            {
                float availW = card.Width - 44;
                float wantH = availW * (BOARD_HEIGHT / BOARD_WIDTH);
                float availH = card.Height - 132;
                float bh = wantH > availH ? availH : wantH;
                float py2 = card.Y + 52 + bh + 10;
                if (py2 + 104 > card.Y + card.Height - 8)
                    py2 = card.Y + card.Height - 112;   /* keep it inside the card */
                panel = RectF(card.X + 22, py2, card.Width - 44, 104.0f);
            }
            fill_round(g, panel, 12.0f, Color(255, 22, 25, 32));
            stroke_round(g, panel, 12.0f, line, 1.0f);
            text(g, "SELECTED KEY", RectF(panel.X + 16, panel.Y + 10, 200, 16), 9.5f,
                 Color(255, 110, 120, 138), StringAlignmentNear, NULL, true);
            if (g_sel >= 0 && g_sel < KEY_COUNT) {
                wchar_t lbl[32];
                MultiByteToWideChar(CP_UTF8, 0, KEYS[g_sel].label, -1, lbl, 32);
                SolidBrush tb(ink);
                Font big(g_face, 18.0f, FontStyleBold, UnitPixel);
                g->DrawString(lbl, -1, &big, PointF(panel.X + 16, panel.Y + 28), &tb);
                {
                    char meta[128];
                    snprintf(meta, sizeof(meta), "index %d   |   position %.2f, %.2f",
                             KEYS[g_sel].idx, KEYS[g_sel].x, KEYS[g_sel].y);
                    text(g, meta, RectF(panel.X + 16, panel.Y + 58, 420, 18), 11.0f, dim,
                         StringAlignmentNear);
                }
                {
                    char typed[64];
                    if (g_code[0])
                        snprintf(typed, sizeof(typed), "0x%s", g_code);
                    else
                        snprintf(typed, sizeof(typed), "type a hex code");
                    text(g, typed, RectF(panel.X + panel.Width - 220, panel.Y + 40, 200, 24),
                         14.0f, g_code[0] ? accent : Color(255, 96, 106, 124),
                         StringAlignmentFar);
                    text(g, "Enter applies  |  Backspace clears",
                         RectF(panel.X + panel.Width - 260, panel.Y + 68, 240, 16), 10.0f,
                         Color(255, 96, 106, 124), StringAlignmentFar);
                }
            } else {
                text(g, "Click any key on the board above.",
                     RectF(panel.X + 16, panel.Y + 34, 420, 20), 12.5f, dim, StringAlignmentNear);
            }
        }
        {
            char info[220];
            if (g_sel >= 0 && g_sel < KEY_COUNT) {
                int cur = app_keymap_cached(KEYS[g_sel].idx);
                if (cur >= 0)
                    snprintf(info, sizeof(info),
                             "%s   |   index %d   |   usage 0x%02x   |   type a hex code and press Enter",
                             KEYS[g_sel].label, KEYS[g_sel].idx, cur);
                else
                    snprintf(info, sizeof(info), "%s   |   index %d   |   reading from the board...",
                             KEYS[g_sel].label, KEYS[g_sel].idx);
            } else {
                snprintf(info, sizeof(info), "No key selected.");
            }
            text(g, info, RectF(card.X + 22, card.Y + card.Height - 58, card.Width - 44, 20),
                 12.0f, ink, StringAlignmentNear);

            /* the hex-code workflow needs somewhere to look the codes up */
            {
                RectF ref(card.X + 22, card.Y + card.Height - 152, card.Width - 44, 78.0f);
                fill_round(g, ref, 12.0f, Color(255, 19, 21, 27));
                stroke_round(g, ref, 12.0f, Color(255, 30, 34, 42), 1.0f);
                text(g, "COMMON USAGE CODES", RectF(ref.X + 16, ref.Y + 10, 260, 16), 9.5f,
                     Color(255, 110, 120, 138), StringAlignmentNear, NULL, true);
                {
                    const char *codes[2][6] = {
                        {"0x04 A", "0x05 B", "0x28 Enter", "0x29 Esc", "0x2c Space", "0xe0 LCtrl"},
                        {"0x06 C", "0x07 D", "0x2b Tab", "0x4f Right", "0x50 Left", "0x52 Up"}
                    };
                    int r2, c2;
                    for (r2 = 0; r2 < 2; r2++) {
                        for (c2 = 0; c2 < 6; c2++) {
                            float x2 = ref.X + 16 + c2 * ((ref.Width - 32) / 6.0f);
                            text(g, codes[r2][c2], RectF(x2, ref.Y + 30 + r2 * 22, 120, 18), 11.0f,
                                 r2 ? Color(255, 120, 128, 144) : dim, StringAlignmentNear);
                        }
                    }
                }
            }
        }
    } else {
        text(g, "ABOUT", RectF(card.X + 22, card.Y + 16, 200, 18), 10.5f, dim,
             StringAlignmentNear, NULL, true);
        text(g, "LEOBOG Hi75C Pro", RectF(card.X + 22, card.Y + 54, 420, 32), 21.0f, ink,
             StringAlignmentNear, NULL, true);
        text(g, "A native Windows driver. No WebView, no Electron, no background service.",
             RectF(card.X + 22, card.Y + 96, 620, 20), 12.5f, dim, StringAlignmentNear);
        text(g, "https://github.com/scwfyahu/hi75c-pro",
             RectF(card.X + 22, card.Y + 126, 620, 20), 12.5f, accent, StringAlignmentNear);
        text(g, "17 firmware effects   |   7 colours   |   2 layers   |   per-key remap",
             RectF(card.X + 22, card.Y + 166, 620, 20), 12.5f, dim, StringAlignmentNear);
        {
            const char *keys[4] = {"Version", "Device", "Path", "Effects"};
            char vals[4][320];
            snprintf(vals[0], sizeof(vals[0]), "%s", app_version());
            snprintf(vals[1], sizeof(vals[1]), "%s", app_connected() ? "connected" : "not connected");
            snprintf(vals[2], sizeof(vals[2]), "%s",
                     app_device_path()[0] ? app_device_path() : "none");
            snprintf(vals[3], sizeof(vals[3]), "%d firmware effects available", app_effect_count());
            int r;
            for (r = 0; r < 4; r++) {
                float ry = card.Y + 208 + r * 44;
                text(g, keys[r], RectF(card.X + 22, ry, 120, 16), 9.5f,
                     Color(255, 110, 120, 138), StringAlignmentNear, NULL, true);
                text(g, vals[r], RectF(card.X + 22, ry + 17, card.Width - 60, 20), 12.0f,
                     r == 2 && !app_connected() ? dim : ink, StringAlignmentNear);
            }
        }
    }
}

static void draw_keyboard(Graphics *g, const RectF &area, const Color &line, const Color &dim)
{
    /* the plate the keys sit in */
    RectF plateRect;
    {
        RectF plate(area.X - 6, area.Y - 2, area.Width + 12, area.Height + 10);
        plateRect = plate;
        /* the board sits in a well, not on a plate: darker than the card, with
           the shadow pooling at the top edge (design #8's surface language) */
        LinearGradientBrush pb(PointF(plate.X, plate.Y), PointF(plate.X, plate.Y + plate.Height),
                               Color(255, 14, 16, 20), Color(255, 22, 25, 32));
        GraphicsPath pp; rounded(&pp, plate, 14.0f);
        g->FillPath(&pb, &pp);
        Pen outer(Color(255, 48, 53, 66), 1.0f);
        g->DrawPath(&outer, &pp);
        Pen gloss(Color(16, 255, 255, 255), 1.0f);
        g->DrawLine(&gloss, PointF(plate.X + 14, plate.Y + plate.Height - 1.0f),
                    PointF(plate.X + plate.Width - 14, plate.Y + plate.Height - 1.0f));
        {
            Pen shade(Color(120, 0, 0, 0), 2.0f);
            g->DrawLine(&shade, PointF(plate.X + 12, plate.Y + 1.0f),
                        PointF(plate.X + plate.Width - 12, plate.Y + 1.0f));
        }
    }
    /* Scale the board to fill the card. The two axes are allowed to differ a
       little (up to 30%) so the keyboard uses the width instead of floating in
       the middle of a wide card; beyond that it starts to look stretched. */
    float pad = 16.0f;
    float avail_w = area.Width - pad * 2, avail_h = area.Height - pad * 2;
    float sx = avail_w / BOARD_WIDTH, sy = avail_h / BOARD_HEIGHT;
    /* Fill the card on both axes. The keyboard's own aspect (2.5:1) is close
       enough to this card's that the resulting distortion is ~10-15%, which
       reads as a slightly wide keycap rather than a stretched one. */
    /* fill the width only as far as the caps stay believable: beyond ~1.35 the
       keycaps read as squashed rather than wide */
    float rx = (sx <= sy * 1.35f) ? sx : sy * 1.35f;
    float sy2 = sy;
    float boardW = BOARD_WIDTH * rx, boardH = BOARD_HEIGHT * sy2;
    float ox = area.X + (area.Width - boardW) / 2;
    float oy = area.Y + (area.Height - boardH) / 2;
    float gap = 2.0f;
    int i;
    /* the legend has to scale with the cap, or text overflows the key when the
       board is drawn small */
    float legendPx = (sy2 < rx ? sy2 : rx) * 0.22f;
    if (legendPx < 6.0f) legendPx = 6.0f;
    if (legendPx > 10.5f) legendPx = 10.5f;
    Font key(g_face, legendPx, FontStyleRegular, UnitPixel);
    SolidBrush textbrush(dim);
    for (i = 0; i < KEY_COUNT; i++) {
        RectF k(ox + KEYS[i].x * rx + gap / 2, oy + KEYS[i].y * sy2 + gap / 2,
                KEYS[i].w * rx - gap, KEYS[i].h * sy2 - gap);
        {
            int hot = (g_hover == 1000 + i);
            /* side wall first, then the lit top face: this is what makes a
               rectangle read as a keycap */
            /* physical keycap: wall -> top face -> specular edge -> inner shadow */
            RectF wall(k.X, k.Y + 3.2f, k.Width, k.Height);
            fill_round(g, wall, 5.0f, Color(255, 8, 9, 13));
            {
                RectF under(k.X + 0.5f, k.Y + k.Height - 1.2f, k.Width - 1.0f, 2.0f);
                fill_round(g, under, 1.0f, Color(90, 0, 0, 0));
            }
            {
                Color c = preview_colour(i, KEYS[i].x, KEYS[i].y);
                int mix = c.GetA() ? 150 : 0;   /* how much of the live colour the cap shows */
                Color topc = hot ? Color(255, 68, 76, 94) : Color(255, 52, 58, 72);
                Color botc = hot ? Color(255, 44, 50, 63) : Color(255, 34, 39, 49);
                topc = Color(255, (BYTE)((topc.GetR() * (255 - mix) + c.GetR() * mix) / 255),
                                  (BYTE)((topc.GetG() * (255 - mix) + c.GetG() * mix) / 255),
                                  (BYTE)((topc.GetB() * (255 - mix) + c.GetB() * mix) / 255));
                botc = Color(255, (BYTE)((botc.GetR() * (255 - mix) + c.GetR() * mix) / 255),
                                  (BYTE)((botc.GetG() * (255 - mix) + c.GetG() * mix) / 255),
                                  (BYTE)((botc.GetB() * (255 - mix) + c.GetB() * mix) / 255));
                {
                    LinearGradientBrush grad2(PointF(k.X, k.Y), PointF(k.X, k.Y + k.Height), topc, botc);
                    GraphicsPath kp2;
                    rounded(&kp2, k, 5.0f);
                    g->FillPath(&grad2, &kp2);
                    Pen gl(Color(46, 255, 255, 255), 1.0f);
                    g->DrawLine(&gl, PointF(k.X + 5, k.Y + 1.2f), PointF(k.X + k.Width - 5, k.Y + 1.2f));
                    {
                        /* specular streak, short and off-centre, like a real cap */
                        Pen spec(Color(30, 255, 255, 255), 1.0f);
                        float sw2 = k.Width * 0.34f;
                        if (sw2 > 10.0f) {
                            g->DrawLine(&spec, PointF(k.X + 4, k.Y + 3.0f),
                                        PointF(k.X + 4 + sw2, k.Y + 3.0f));
                        }
                    }
                    {
                        Pen inner(Color(70, 0, 0, 0), 1.0f);
                        GraphicsPath ip;
                        RectF ir(k.X + 1.0f, k.Y + 1.0f, k.Width - 2.0f, k.Height - 2.0f);
                        rounded(&ip, ir, 4.0f);
                        g->DrawPath(&inner, &ip);
                    }
                    {
                        Color c2 = preview_colour(i, KEYS[i].x, KEYS[i].y);
                        Pen under(Color(70, c2.GetR(), c2.GetG(), c2.GetB()), 2.0f);
                        g->DrawLine(&under, PointF(k.X + 4, k.Y + k.Height - 1.0f),
                                    PointF(k.X + k.Width - 4, k.Y + k.Height - 1.0f));
                    }
                    if (i == g_sel) {
                        Pen ring(Color(255, 91, 140, 255), 2.0f);
                        GraphicsPath rp; rounded(&rp, k, 5.0f);
                        g->DrawPath(&ring, &rp);
                    } else {
                        Pen edge(Color(255, 20, 23, 28), 1.0f);
                        g->DrawPath(&edge, &kp2);
                    }
                }
            }
        }
        add_hot((int)k.X, (int)k.Y, (int)k.Width, (int)k.Height, 1000 + i, KEYS[i].label);
        if (k.Width > 15.0f) {
            Color pc = preview_colour(i, KEYS[i].x, KEYS[i].y);
            int lum = pc.GetA() ? (pc.GetR() * 299 + pc.GetG() * 587 + pc.GetB() * 114) / 1000
                                : 0;
            SolidBrush tb(lum > 140 ? Color(255, 18, 20, 26) : Color(255, 205, 213, 226));
            StringFormat c;
            c.SetAlignment(StringAlignmentCenter);
            c.SetLineAlignment(StringAlignmentCenter);
            c.SetFormatFlags(StringFormatFlagsNoWrap);   /* a wrapped "Backspace" is unreadable */
            c.SetTrimming(StringTrimmingEllipsisCharacter);
            {
                const char *text = KEYS[i].label;
                float room = (k.Width - 6.0f) / (legendPx * 0.50f);   /* chars that fit */
                if ((float)strlen(text) > room) text = short_label(text);
                {
                    wchar_t lbl[32];
                    MultiByteToWideChar(CP_UTF8, 0, text, -1, lbl, 32);
                    g->DrawString(lbl, -1, &key, k, &c, &tb);
                }
            }
        }
    }
}



/* cheap soft shadow: a few offset rounded rects at low alpha */
static void shadow(Graphics *g, const RectF &r, float rad) {
    int i;
    for (i = 5; i >= 1; i -= 2) {
        RectF o(r.X - i * 0.5f, r.Y + i, r.Width + i, r.Height + i);
        GraphicsPath p;
        rounded(&p, o, rad + i);
        SolidBrush b(Color(26, 0, 0, 0));
        g->FillPath(&b, &p);
    }
}

static void card_skin(Graphics *g, GraphicsPath *path, const RectF &r,
                      const Color &top, const Color &bot, const Color &line) {
    LinearGradientBrush brush(PointF(r.X, r.Y), PointF(r.X, r.Y + r.Height), top, bot);
    g->FillPath(&brush, path);
    Pen edge(line, 1.0f);
    g->DrawPath(&edge, path);
    Pen hi(Color(18, 255, 255, 255), 1.0f);
    g->DrawLine(&hi, PointF(r.X + 16, r.Y + 1.5f), PointF(r.X + r.Width - 16, r.Y + 1.5f));
}


/* Windows 10/11: paint the non-client area dark so the window does not have a
   bright title bar bolted onto a dark app. Silently ignored elsewhere. */
static void dark_title_bar(HWND hwnd) {
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    typedef HRESULT (WINAPI *PfnDwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
    if (!dwm) return;
    {
        PfnDwmSetWindowAttribute set_attr =
            (PfnDwmSetWindowAttribute)(void *)GetProcAddress(dwm, "DwmSetWindowAttribute");
        if (set_attr) {
            BOOL on = TRUE;
            set_attr(hwnd, 20, &on, sizeof(on));   /* DWMWA_USE_IMMERSIVE_DARK_MODE */
            set_attr(hwnd, 19, &on, sizeof(on));   /* older builds */
        }
    }
}

static void paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rcPhys, rc;
    GetClientRect(hwnd, &rcPhys);
    rc = rcPhys;
    if (g_scale > 1.001f || g_scale < 0.999f) {
        /* lay out in logical pixels so every literal in this file keeps its
           intended physical size on a scaled display */
        rc.right = (LONG)(rcPhys.right / g_scale);
        rc.bottom = (LONG)(rcPhys.bottom / g_scale);
    }

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rcPhys.right, rcPhys.bottom);
    HBITMAP old = (HBITMAP)SelectObject(mem, bmp);
    {
        Graphics g(mem);
        if (g_scale > 1.001f || g_scale < 0.999f) {
            Matrix m(g_scale, 0.0f, 0.0f, g_scale, 0.0f, 0.0f);
            g.SetTransform(&m);
        }
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);

        const Color bgTop = C_CANVAS_TOP, bgBot = C_CANVAS_BOT;
        const Color cardTop = C_SURFACE_TOP, cardBot = C_SURFACE_BOT;
        const Color line = C_HAIRLINE, lineSoft = C_HAIRLINE_SOFT;
        const Color ink = C_INK, dim = C_INK_2;
        Color pick = SWATCH[app_state()->colour < 7 ? app_state()->colour : 4];
        const Color accent = app_state()->effect ? pick : C_ACCENT, accentSoft = C_ACCENT_DIM;
        const Color ok = C_OK, bad = C_BAD;

        {
            LinearGradientBrush bg(PointF(0, 0), PointF(0, (REAL)rc.bottom), bgTop, bgBot);
            g.FillRectangle(&bg, 0, 0, rc.right, rc.bottom);
        }
        {
            /* soft vignette; keeps the eye in the middle of the window */
            GraphicsPath vp;
            RectF vr(-(REAL)rc.right * 0.1f, -(REAL)rc.bottom * 0.1f,
                     (REAL)rc.right * 1.2f, (REAL)rc.bottom * 1.2f);
            vp.AddEllipse(vr);
            PathGradientBrush vg(&vp);
            vg.SetCenterPoint(PointF((REAL)rc.right / 2, (REAL)rc.bottom / 2));
            vg.SetCenterColor(Color(0, 0, 0, 0));
            Color edgeC[1] = { Color(150, 0, 0, 0) };
            int nEdge = 1;
            vg.SetSurroundColors(edgeC, &nEdge);
            g.FillRectangle(&vg, 0, 0, rc.right, rc.bottom);
        }
        {
            Color c = SWATCH[(app_state()->colour >= 0 && app_state()->colour < 7)
                             ? app_state()->colour : 4];
            LinearGradientBrush wash(PointF(0, 0), PointF((REAL)rc.right, (REAL)rc.bottom),
                                     Color(26, c.GetR(), c.GetG(), c.GetB()),
                                     Color(0, 0, 0, 0));
            g.FillRectangle(&wash, 0, 0, rc.right, rc.bottom);
        }

        if (g_dbg) dbg("paint %dx%d scale %.3f", rc.right, rc.bottom, g_scale);
        if (rc.right < 700 || rc.bottom < 560) {
            SolidBrush bb(Color(255, 13, 14, 18));
            g.FillRectangle(&bb, 0, 0, rc.right, rc.bottom);
            text(&g, "Window is too small to lay out", RectF(20, (REAL)rc.bottom / 2 - 10, rc.right - 40, 20),
                 12.0f, C_INK_3, StringAlignmentCenter);
            g_hot_n = 0;
            goto rendered;
        }
        g_anim_t = (float)(GetTickCount64() % 1000000ULL) / 1000.0f;
        const int pad = 26;
        const int headerH = 76;
        const int navW = 190;                       /* navigation rail */
        const int cx = navW + pad;                  /* content left */
        const int cw = rc.right - navW - pad * 2;   /* content width */
        const int cb = rc.bottom - 46;              /* content bottom */

        /* ---------------- header ---------------- */
        {
            RectF title((REAL)cx, 22.0f, 400.0f, 30.0f);
            text(&g, "LEOBOG Hi75C Pro", title, 25.0f, ink, StringAlignmentNear, NULL, true);
            RectF sub((REAL)cx, 48.0f, 400.0f, 18.0f);
            char subtext[96];
            snprintf(subtext, sizeof(subtext), "%s   |   v%s",
                     app_connected() ? "USB / 2.4 GHz" : "no keyboard", app_version());
            text(&g, subtext, sub, 11.5f, dim, StringAlignmentNear);

            /* actions live in the header, right of the title */
            {
                const char *names[3] = {"Import", "Restore", "Rescan"};
                int ids[3] = {402, 400, 401};
                int j, bwid = 88;
                for (j = 0; j < 3; j++) {
                    RectF b((REAL)(rc.right - pad - 200 - (3 - j) * (bwid + 8)), 30.0f,
                            (REAL)bwid, 30.0f);
                    fill_round(&g, b, 9.0f, g_hover == ids[j] ? Color(255, 36, 40, 50)
                                                              : Color(255, 24, 27, 34));
                    stroke_round(&g, b, 9.0f, lineSoft, 1.0f);
                    text(&g, names[j], b, 11.0f, ink, StringAlignmentCenter);
                    add_hot((int)b.X, (int)b.Y, (int)b.Width, (int)b.Height, ids[j], names[j]);
                }
            }
            const char *st = app_connected() ? "Connected" : "Not connected";
            float pw = 132.0f;
            RectF pill((REAL)(rc.right - pad - pw), 26.0f, pw, 34.0f);
            fill_round(&g, pill, 17.0f, app_connected() ? Color(255, 20, 44, 33) : Color(255, 44, 24, 24));
            stroke_round(&g, pill, 17.0f, app_connected() ? Color(90, 62, 207, 142) : Color(90, 240, 104, 92), 1.0f);
            SolidBrush dotc(app_connected() ? ok : bad);
            g.FillEllipse(&dotc, pill.X + 16, pill.Y + 13, 8.0f, 8.0f);
            text(&g, st, RectF(pill.X + 32, pill.Y, pw - 38, pill.Height), 12.5f,
                 app_connected() ? ok : bad, StringAlignmentNear);
        }

        g_hot_n = 0;
        int y = headerH;
        draw_rail(&g, rc, navW, accent, ink, dim, C_INK_3);
        if (g_page != 0) {
            draw_secondary(&g, rc, cx, cw, headerH, cb, cardTop, cardBot, line, ink, dim, accent);
            goto rendered;
        }
        int rightW = 320;
        int leftW = cw - rightW - 20;

        /* ---------------- keyboard ---------------- */
        int kbH = 250;
        {
            int availH = rc.bottom - headerH - 46;
            kbH = (int)(availH * 0.46);
            if (kbH > 420) kbH = 420;
            if (kbH < 250) kbH = 250;
            RectF kb((REAL)cx, (REAL)y, (REAL)cw, (REAL)kbH);
            {
                /* the room is lit by the colour on the board */
                Color c = SWATCH[(app_state()->colour >= 0 && app_state()->colour < 7)
                                 ? app_state()->colour : 4];
                float cx = kb.X + kb.Width / 2, cy = kb.Y + kb.Height * 0.55f;
                RectF halo(cx - kb.Width * 0.45f, cy - kb.Height * 0.75f,
                           kb.Width * 0.9f, kb.Height * 1.6f);
                GraphicsPath hp;
                hp.AddEllipse(halo);
                PathGradientBrush pg(&hp);
                pg.SetCenterPoint(PointF(cx, cy));
                pg.SetCenterColor(Color(34, c.GetR(), c.GetG(), c.GetB()));
                Color surround[1] = { Color(0, c.GetR(), c.GetG(), c.GetB()) };
                int n = 1;
                pg.SetSurroundColors(surround, &n);
                g.FillPath(&pg, &hp);
            }
            shadow(&g, kb, 18.0f);
            GraphicsPath p; rounded(&p, kb, 18.0f);
            card_skin(&g, &p, kb, cardTop, cardBot, line);
            {
                SolidBrush bar(accent);
                g.FillRectangle(&bar, (REAL)(kb.X + 22), kb.Y + 17, 3.0f, 12.0f);
                text(&g, "BOARD", RectF(kb.X + 32, kb.Y + 16, 200, 18), 10.5f, dim, StringAlignmentNear, NULL, true);
            if (!app_connected()) {
                RectF hint(kb.X + 84, kb.Y + 16, 320, 18);
                text(&g, "preview only \xE2\x80\x94 no keyboard attached",
                     hint, 10.5f, Color(255, 120, 126, 140), StringAlignmentNear);
            }
            }

            /* layer switch, top right of the card */
            {
                int layer = app_layer();
                RectF track(kb.X + kb.Width - 162, kb.Y + 12, 148, 30);
                fill_round(&g, track, 9.0f, Color(255, 18, 20, 26));
                stroke_round(&g, track, 9.0f, lineSoft, 1.0f);
                RectF lb(kb.X + kb.Width - 159, kb.Y + 15, 70, 24);
                RectF lf(kb.X + kb.Width - 85, kb.Y + 15, 68, 24);
                if (layer == 0) fill_round(&g, lb, 7.0f, accentSoft);
                else fill_round(&g, lf, 7.0f, accentSoft);
                text(&g, "Base", lb, 11.5f, layer == 0 ? Color(255, 210, 228, 255) : dim, StringAlignmentCenter);
                add_hot((int)lb.X, (int)lb.Y, (int)lb.Width, (int)lb.Height, 500, "base");
                fill_round(&g, lf, 8.0f, layer == 1 ? accentSoft : Color(255, 23, 25, 32));
                stroke_round(&g, lf, 8.0f, layer == 1 ? accent : lineSoft, 1.0f);
                text(&g, "FN", lf, 11.5f, layer == 1 ? Color(255, 210, 228, 255) : dim, StringAlignmentCenter);
                add_hot((int)lf.X, (int)lf.Y, (int)lf.Width, (int)lf.Height, 501, "fn");
            }

            {
                Pen rule(Color(20, 255, 255, 255), 1.0f);
                g.DrawLine(&rule, PointF(kb.X + 22, kb.Y + 38), PointF(kb.X + kb.Width - 22, kb.Y + 38));
            }
            draw_keyboard(&g, RectF(kb.X + 10, kb.Y + 46, kb.Width - 20, kb.Height - 56), line, dim);
        }
        y += kbH + 18;

        /* ---------------- effects + controls (one surface) ---------------- */
        {
            RectF ec((REAL)cx, (REAL)y, (REAL)cw, (REAL)(cb - y));
            shadow(&g, ec, 18.0f);
            GraphicsPath p; rounded(&p, ec, 18.0f);
            card_skin(&g, &p, ec, cardTop, cardBot, line);

            {
                SolidBrush bar(accent);
                g.FillRectangle(&bar, (REAL)(ec.X + 22), ec.Y + 17, 3.0f, 12.0f);
            }
            text(&g, "EFFECTS", RectF(ec.X + 32, ec.Y + 16, 220, 18), 10.5f, dim,
                 StringAlignmentNear, NULL, true);
            {
                char cnt[32];
                snprintf(cnt, sizeof(cnt), "%d effects", app_effect_count());
                text(&g, cnt, RectF(ec.X + ec.Width - 190, ec.Y + 16, 168, 18), 10.5f,
                     Color(255, 96, 106, 124), StringAlignmentFar);
            }
            {
                Pen rule(Color(18, 255, 255, 255), 1.0f);
                g.DrawLine(&rule, PointF(ec.X + 22, ec.Y + 38), PointF(ec.X + ec.Width - 22, ec.Y + 38));
            }

            const int ctrlW = 300;
            int gridW = (int)ec.Width - ctrlW - 46;
            int cols = 5, gap = 8, rows = (app_effect_count() + cols - 1) / cols;
            int bw = (gridW - gap * (cols - 1)) / cols;
            int bh = 38;
            int gx = (int)ec.X + 22, gy = (int)ec.Y + 50;
            int i;
            for (i = 0; i < app_effect_count(); i++) {
                int bx = gx + (i % cols) * (bw + gap);
                int by = gy + (i / cols) * (bh + gap);
                int id = 100 + i;
                int active = (app_state()->effect == app_effect_id(i));
                RectF btn((REAL)bx, (REAL)by, (REAL)bw, (REAL)bh);
                fill_round(&g, btn, 9.0f, active ? Color(255, 40, 45, 56)
                                        : (g_hover == id ? Color(255, 33, 37, 46)
                                                         : Color(255, 24, 27, 34)));
                stroke_round(&g, btn, 9.0f, active ? Color(80, 255, 255, 255) : lineSoft, 1.0f);
                if (active) {
                    Color tint = SWATCH[i % 7];
                    icon(&g, effect_icon(i, app_effect_id(i)), btn.X + 10,
                         btn.Y + btn.Height / 2 - 5, tint);
                }
                text(&g, app_effect_name(i), RectF(btn.X + (active ? 26 : 10), btn.Y,
                                                   btn.Width - (active ? 32 : 16), btn.Height),
                     11.5f, active ? Color(255, 246, 247, 250) : dim,
                     active ? StringAlignmentNear : StringAlignmentCenter, NULL, active);
                add_hot(bx, by, bw, bh, id, app_effect_name(i));
            }

            /* controls column */
            {
                int rx = (int)ec.X + (int)ec.Width - ctrlW - 22;
                int ry = (int)ec.Y + 50;
                text(&g, "COLOUR", RectF((REAL)rx, (REAL)ry, 120, 16), 9.5f,
                     Color(255, 110, 120, 138), StringAlignmentNear, NULL, true);
                {
                    static const char *hex[7] = {"#FF2B2B","#FF8C1A","#FFE11A","#2EE65C","#1AE5FF","#2E6BFF","#B833FF"};
                    int k;
                    for (k = 0; k < 7; k++) {
                        RectF b2((REAL)(rx + k * 34), (REAL)(ry + 20), 28.0f, 28.0f);
                        fill_round(&g, b2, 8.0f, SWATCH[k]);
                        if (app_state()->colour == k)
                            stroke_round(&g, b2, 8.0f, Color(255, 255, 255, 255), 2.0f);
                        else
                            stroke_round(&g, b2, 8.0f, g_hover == 200 + k ? Color(120,255,255,255)
                                                                          : Color(40,255,255,255), 1.0f);
                        add_hot((int)b2.X, (int)b2.Y, 28, 28, 200 + k, "swatch");
                    }
                    text(&g, hex[app_state()->colour < 7 ? app_state()->colour : 0],
                         RectF((REAL)rx, (REAL)(ry + 52), 120, 16), 10.5f,
                         Color(255, 110, 120, 138), StringAlignmentNear);
                }
                {
                    char buf[16];
                    int sy = ry + 80;
                    text(&g, "BRIGHTNESS", RectF((REAL)rx, (REAL)sy, 160, 16), 9.5f,
                         Color(255, 110, 120, 138), StringAlignmentNear, NULL, true);
                    snprintf(buf, sizeof(buf), "%d%%", (app_state()->brightness + 1) * 10);
                    text(&g, buf, RectF((REAL)(rx + ctrlW - 70), (REAL)sy, 70, 16), 10.5f,
                         Color(255, 190, 200, 216), StringAlignmentFar);
                    {
                        int by2 = sy + 22, bw2 = ctrlW - 8;
                        fill_round(&g, RectF((REAL)rx, (REAL)(by2 - 2), (REAL)bw2, 6.0f), 3.0f, Color(255, 40, 45, 56));
                        int filled = bw2 * (app_state()->brightness + 1) / 10;
                        fill_round(&g, RectF((REAL)rx, (REAL)(by2 - 2), (REAL)filled, 6.0f), 3.0f, accent);
                        SolidBrush knob(Color(255, 248, 251, 255));
                        g.FillEllipse(&knob, (REAL)(rx + filled - 7), (REAL)(by2 - 7), 14.0f, 14.0f);
                        add_hot(rx - 8, by2 - 14, bw2 + 16, 28, 300, "brightness");
                    }
                }
                {
                    char buf[16];
                    int sy = ry + 138;
                    text(&g, "SPEED", RectF((REAL)rx, (REAL)sy, 160, 16), 9.5f,
                         Color(255, 110, 120, 138), StringAlignmentNear, NULL, true);
                    snprintf(buf, sizeof(buf), "%dx", app_state()->speed + 1);
                    text(&g, buf, RectF((REAL)(rx + ctrlW - 70), (REAL)sy, 70, 16), 10.5f,
                         Color(255, 190, 200, 216), StringAlignmentFar);
                    {
                        int by2 = sy + 22, bw2 = ctrlW - 8;
                        fill_round(&g, RectF((REAL)rx, (REAL)(by2 - 2), (REAL)bw2, 6.0f), 3.0f, Color(255, 40, 45, 56));
                        int filled = bw2 * (app_state()->speed + 1) / 4;
                        fill_round(&g, RectF((REAL)rx, (REAL)(by2 - 2), (REAL)filled, 6.0f), 3.0f, accent);
                        SolidBrush knob(Color(255, 248, 251, 255));
                        g.FillEllipse(&knob, (REAL)(rx + filled - 7), (REAL)(by2 - 7), 14.0f, 14.0f);
                        add_hot(rx - 8, by2 - 14, bw2 + 16, 28, 301, "speed");
                    }
                }
                {
                    int py = ry + 196, j, bw3 = (ctrlW - 16) / 3;
                    text(&g, "QUICK PRESETS", RectF((REAL)rx, (REAL)(py - 18), 160, 16), 9.5f,
                         Color(255, 110, 120, 138), StringAlignmentNear, NULL, true);
                    for (j = 0; j < 3; j++) {
                        RectF b((REAL)(rx + j * (bw3 + 8)), (REAL)py, (REAL)bw3, 30.0f);
                        fill_round(&g, b, 9.0f, g_hover == 600 + j ? Color(255, 36, 40, 50) : Color(255, 24, 27, 34));
                        stroke_round(&g, b, 9.0f, lineSoft, 1.0f);
                        text(&g, j == 0 ? "Warm" : (j == 1 ? "Cool" : "Off"), b, 11.0f, ink,
                             StringAlignmentCenter);
                        add_hot((int)b.X, (int)b.Y, (int)b.Width, (int)b.Height, 600 + j, "preset");
                    }
                }
            }

            /* running strip */
            {
                int sy = (int)ec.Y + (int)ec.Height - 46;
                Pen rule(Color(18, 255, 255, 255), 1.0f);
                g.DrawLine(&rule, PointF(ec.X + 22, (REAL)sy), PointF(ec.X + ec.Width - 22, (REAL)sy));
                {
                    SolidBrush dot(accent);
                    g.FillEllipse(&dot, (REAL)(ec.X + 22), (REAL)(sy + 15), 7.0f, 7.0f);
                }
                {
                    char run[220];
                    int i2, found = -1;
                    for (i2 = 0; i2 < app_effect_count(); i2++)
                        if (app_effect_id(i2) == app_state()->effect) found = i2;
                    snprintf(run, sizeof(run), "RUNNING    %s    |    colour %d/7    |    brightness %d%%    |    speed %dx",
                             found >= 0 ? app_effect_name(found) : "Off",
                             app_state()->colour + 1, (app_state()->brightness + 1) * 10,
                             app_state()->speed + 1);
                    text(&g, run, RectF(ec.X + 38, (REAL)(sy + 10), ec.Width - 260, 20), 11.0f, ink,
                         StringAlignmentNear);
                }
                text(&g, app_connected() ? "written to board" : "preview only",
                     RectF(ec.X + ec.Width - 200, (REAL)(sy + 10), 178, 20), 10.5f,
                     app_connected() ? C_OK : Color(255, 110, 120, 138), StringAlignmentFar);
            }
        }

        /* ---------------- footer / status ---------------- */
        {
            const char *note = app_state()->note;
            RectF strip((REAL)cx, (REAL)(rc.bottom - 32), (REAL)cw, 18.0f);
            if (note[0])
                text(&g, note, strip, 11.5f, bad, StringAlignmentNear);
            else if (g_sel >= 0 && g_sel < KEY_COUNT) {
                char info[160];
                if (g_code[0])
                    snprintf(info, sizeof(info), "%s   |   index %d   |   typing 0x%s   |   Enter to apply",
                             KEYS[g_sel].label, KEYS[g_sel].idx, g_code);
                else
                    snprintf(info, sizeof(info), "%s   |   index %d   |   current usage 0x%02x   |   type a hex code",
                             KEYS[g_sel].label, KEYS[g_sel].idx, g_code_cur < 0 ? 0 : g_code_cur);
                text(&g, info, strip, 11.5f, dim, StringAlignmentNear);
            } else {
                text(&g, "Click a key to remap it, or pick an effect above.",
                     strip, 11.5f, Color(255, 96, 106, 124), StringAlignmentNear);
            }
        }
    }
rendered:
    if (g_dbg) {
        int hi;
        dbg("hotspots %d", g_hot_n);
        for (hi = 0; hi < g_hot_n; hi++)
            dbg("hot %d %d %d %d %d", g_hot[hi].id, g_hot[hi].x, g_hot[hi].y,
                g_hot[hi].w, g_hot[hi].h);
    }
    BitBlt(dc, 0, 0, rcPhys.right, rcPhys.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static int hit(int px, int py) {
    int x = g_scale > 0.001f ? (int)(px / g_scale) : px;
    int y = g_scale > 0.001f ? (int)(py / g_scale) : py;
    int i;
    for (i = 0; i < g_hot_n; i++)
        if (x >= g_hot[i].x && x < g_hot[i].x + g_hot[i].w &&
            y >= g_hot[i].y && y < g_hot[i].y + g_hot[i].h)
            return g_hot[i].id;
    return -1;
}

static int slider_value(int id, int x) {
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    /* the layout is in logical pixels; the mouse arrives in physical ones, so
       both sides have to be converted or the value drifts on a scaled display */
    int W = (int)(rc.right / (g_scale > 0.001f ? g_scale : 1.0f));
    int xl = (int)(x / (g_scale > 0.001f ? g_scale : 1.0f));
    int right_w = 300, rx = W - PAD - right_w;
    int bx = rx + 18, bw = right_w - 36;
    int v = (xl - bx) * (id == 300 ? 10 : 4) / bw;
    int max = id == 300 ? 9 : 3;
    if (v < 0) v = 0;
    if (v > max) v = max;
    return v;
}

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_PAINT:
            paint(h);
            return 0;
        case WM_MOUSEMOVE:
            {
                int id = hit((int)(short)LOWORD(l), (int)(short)HIWORD(l));
                if (id != g_hover) { g_hover = id; SetCursor(LoadCursor(NULL, id == -1 ? IDC_ARROW : IDC_HAND)); InvalidateRect(h, NULL, FALSE); }
                if (g_press == 300) app_set_brightness(slider_value(300, (int)(short)LOWORD(l)));
                if (g_press == 301) app_set_speed(slider_value(301, (int)(short)LOWORD(l)));
            }
            return 0;
        case WM_LBUTTONDOWN:
            {
                int x = (int)(short)LOWORD(l), y = (int)(short)HIWORD(l);
                int id = hit(x, y);
                dbg("click at %d,%d  hotspots=%d  hit=%d", x, y, g_hot_n, id);
                g_press = id;
                if (id == 300 || id == 301) SetCapture(h);
                if (id >= 100 && id < 207) app_stop_background_driver();   /* take over from the daemon */
                if (id >= 100 && id < 100 + app_effect_count()) app_set_effect(app_effect_id(id - 100));
                else if (id >= 200 && id < 207) app_set_colour(id - 200);
                else if (id == 300) app_set_brightness(slider_value(300, x));
                else if (id == 301) app_set_speed(slider_value(301, x));
                else if (id == 400) app_restore_keymap();
                else if (id == 401) app_rescan();
                else if (id == 402) app_import_effect(h);
                else if (id == 403) {
                    app_update_install();          /* worker downloads; UI stays live */
                    InvalidateRect(h, NULL, FALSE);
                }
                else if (id >= 700 && id <= 702) { g_page = id - 700; g_sel = -1; }
                else if (id == 600) { app_set_colour(0); app_set_effect(1); }
                else if (id == 601) { app_set_colour(4); app_set_effect(1); }
                else if (id == 602) { app_set_effect(0); }
                else if (id == 500) { app_set_layer(0); g_sel = -1; InvalidateRect(h, NULL, FALSE); }
                else if (id == 501) { app_set_layer(1); g_sel = -1; InvalidateRect(h, NULL, FALSE); }
                else if (id >= 1000) {
                    g_sel = id - 1000;
                    g_code[0] = 0;
                    if (app_keymap_cached(KEYS[g_sel].idx) < 0) app_keymap_request();
                    InvalidateRect(h, NULL, FALSE);
                }
                InvalidateRect(h, NULL, FALSE);
            }
            return 0;
        case WM_CAPTURECHANGED:
            g_press = -1;                 /* without this a cancelled drag keeps scrubbing */
            return 0;
        case WM_LBUTTONUP:
            if (g_press == 300 || g_press == 301) ReleaseCapture();
            g_press = -1;
            return 0;
        case WM_CHAR:
            dbg("char %d sel=%d", (int)w, g_sel);
            if (g_sel >= 0) {
                int ch = (int)w;
                if (ch == VK_RETURN || ch == '\r') {
                    if (strlen(g_code) > 0) {
                        int usage = (int)strtol(g_code, NULL, 16);
                        app_keymap_apply(KEYS[g_sel].idx, usage);   /* worker writes it */
                        g_code[0] = 0;
                        InvalidateRect(h, NULL, FALSE);
                    }
                } else if ((ch == '\b') || (ch == 8)) {
                    size_t n = strlen(g_code);
                    if (n) g_code[n - 1] = 0;
                    InvalidateRect(h, NULL, FALSE);
                } else if (ch == 'x' || ch == 'X') {
                    /* the page shows codes as "0x2b"; typing that used to drop the
                       tail and apply "02", so the prefix clears the leading zero */
                    if (strlen(g_code) == 1 && g_code[0] == '0') g_code[0] = 0;
                    InvalidateRect(h, NULL, FALSE);
                } else if (ch < 128 && isxdigit((unsigned char)ch) && strlen(g_code) < 2) {
                    size_t n = strlen(g_code);
                    g_code[n] = (char)toupper((unsigned char)ch);
                    g_code[n + 1] = 0;
                    InvalidateRect(h, NULL, FALSE);
                }
            }
            return 0;
        case WM_APP + 1:
            {
                HANDLE th = CreateThread(NULL, 0, [](LPVOID)->DWORD {
                    if (app_update_check()) InterlockedExchange(&g_update, 1);
                    return 0;
                }, NULL, 0, NULL);
                if (th) CloseHandle(th);
            }
            return 0;
        case WM_DPICHANGED:
            {
                /* the window was moved to a monitor with a different scale;
                   recompute it and let the layout re-derive from the client
                   size. The window itself is left alone - resizing here risks
                   jumping if the suggestion and our minimum disagree. */
                int dpi = (int)HIWORD(w);
                if (dpi >= 96 && dpi <= 480) g_scale = (float)dpi / 96.0f;
                if (l) {
                    /* Windows supplies the size that keeps the layout sane at the
                       new scale; ignoring it left the logical client too small */
                    RECT *sug = (RECT *)l;
                    SetWindowPos(h, NULL, sug->left, sug->top,
                                 sug->right - sug->left, sug->bottom - sug->top,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
                }
                InvalidateRect(h, NULL, FALSE);
            }
            return 0;
        case WM_SIZE:
            InvalidateRect(h, NULL, FALSE);   /* layout depends on the client size */
            return 0;
        case WM_GETMINMAXINFO:
            {
                MINMAXINFO *mm = (MINMAXINFO *)l;
                mm->ptMinTrackSize.x = (LONG)(900 * g_scale);
                mm->ptMinTrackSize.y = (LONG)(700 * g_scale);
            }
            return 0;
        case WM_TIMER:
            if (w == 1) {
                /* nothing to show while minimised; repainting 15 times a second
                   for an invisible window is pure waste */
                if (!IsIconic(h) && IsWindowVisible(h)) InvalidateRect(h, NULL, FALSE);
                return 0;
            }
            if (w == 2) { KillTimer(h, 2); PostMessageW(h, WM_CLOSE, 0, 0); return 0; }
            break;
            InvalidateRect(h, NULL, FALSE);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            {
                WINDOWPLACEMENT wp;
                RECT r;
                memset(&wp, 0, sizeof(wp));
                wp.length = sizeof(wp);
                GetWindowPlacement(h, &wp);
                r = wp.rcNormalPosition;
                /* persist logical units: the loader multiplies by the display
                   scale, so storing physical pixels would grow the window on
                   every restart at any scale above 100% */
                app_save_state((int)(r.left / g_scale), (int)(r.top / g_scale),
                               (int)((r.right - r.left) / g_scale),
                               (int)((r.bottom - r.top) / g_scale));
            }
            app_shutdown();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show) {
    GdiplusStartupInput gsi;
    MSG msg;
    WNDCLASSA wc;

    (void)prev;
    {
        /* without this the whole window is bitmap-stretched on a scaled display */
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        typedef BOOL (WINAPI *PfnSetCtx)(HANDLE);
        typedef BOOL (WINAPI *PfnSetAware)(void);
        if (user32) {
            PfnSetCtx set_ctx = (PfnSetCtx)(void *)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
            if (set_ctx) set_ctx((HANDLE)-4);          /* PER_MONITOR_AWARE_V2 */
            else {
                PfnSetAware set_aware = (PfnSetAware)(void *)GetProcAddress(user32, "SetProcessDPIAware");
                if (set_aware) set_aware();
            }
        }
    }
    if (cmdline && strstr(cmdline, "--selftest")) return app_self_test();
    {
        const char *eff = cmdline ? strstr(cmdline, "--effect ") : NULL;
        if (eff) return app_effect_test(eff + 9);
    }
    {
        /* headless background runner: no window, no GDI+, just HID traffic */
        const char *bg = cmdline ? strstr(cmdline, "--daemon ") : NULL;
        if (bg) return app_daemon(bg + 9);
    }

    {
        char env[8];
        if (GetEnvironmentVariableA("HI75C_DEBUG", env, sizeof(env)) > 0) {
            char p[MAX_PATH];
            GetModuleFileNameA(NULL, p, MAX_PATH);
            { char *sl = strrchr(p, '\\'); if (sl) sl[1] = 0; }
            strncat(p, "ui-debug.log", MAX_PATH - strlen(p) - 1);
            g_dbg = fopen(p, "w");
            dbg("Hi75C UI debug");
        }
    }
    GdiplusStartup(&g_gdiplus, &gsi, NULL);
    {
        FontFamily probe(L"Segoe UI");
        g_face = (probe.GetLastStatus() == Ok) ? L"Segoe UI" : L"Tahoma";
    }
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    app_init();
    /* the update probe is deferred until after the first paint */

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.lpszClassName = "Hi75CApp";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    {
        int wx = CW_USEDEFAULT, wy = CW_USEDEFAULT, ww = W_INIT, wh = H_INIT;
        int forced = 0;
        app_load_state(&wx, &wy, &ww, &wh);
        {   /* --size WxH overrides the remembered size, for testing the layout */
            const char *sz = cmdline ? strstr(cmdline, "--size ") : NULL;
            if (sz) {
                int w = 0, h = 0;
                if (sscanf(sz + 7, "%dx%d", &w, &h) == 2 && w > 400 && h > 300) {
                    ww = w; wh = h; wx = 60; wy = 60; forced = 1;
                }
            }
        }
        if (!forced && (ww < 900 || wh < 700)) { ww = W_INIT; wh = H_INIT; wx = CW_USEDEFAULT; wy = CW_USEDEFAULT; }
        else {
            /* a saved rect from a monitor that is no longer attached would open
               the window off-screen; fall back to the default position */
            RECT saved;
            MONITORINFO mi;
            HMONITOR mon;
            saved.left = wx; saved.top = wy; saved.right = wx + ww; saved.bottom = wy + wh;
            mon = MonitorFromRect(&saved, MONITOR_DEFAULTTONULL);
            mi.cbSize = sizeof(mi);
            if (!mon || !GetMonitorInfoW(mon, &mi)) { wx = CW_USEDEFAULT; wy = CW_USEDEFAULT; }
        }
        {
            HDC sdc = GetDC(NULL);
            int dpi = GetDeviceCaps(sdc, LOGPIXELSY);
            ReleaseDC(NULL, sdc);
            if (dpi >= 96 && dpi <= 480) g_scale = (float)dpi / 96.0f;
            {
                /* override for testing the scaled layout on a 96-dpi display */
                char sc[16];
                if (GetEnvironmentVariableA("HI75C_SCALE", sc, sizeof(sc)) > 0) {
                    float f = (float)atof(sc);
                    if (f >= 1.0f && f <= 3.0f) g_scale = f;
                }
            }
            /* the window is created in physical pixels; the layout stays logical */
            ww = (int)(ww * g_scale);
            wh = (int)(wh * g_scale);
        }
        {
            /* one window per machine: a second copy would write to the same
               handle as the first, with no cross-process lock to stop it */
            HANDLE once = CreateMutexA(NULL, TRUE, "Hi75CPro_GUI_Singleton");
            if (once && GetLastError() == ERROR_ALREADY_EXISTS) {
                HWND existing = FindWindowA("Hi75CApp", NULL);
                if (existing) {
                    ShowWindow(existing, SW_RESTORE);
                    SetForegroundWindow(existing);
                }
                CloseHandle(once);
                return 0;
            }
        }
        g_hwnd = CreateWindowA("Hi75CApp", "LEOBOG Hi75C Pro", WS_OVERLAPPEDWINDOW,
                               wx, wy, ww, wh, NULL, NULL, inst, NULL);
    }
    if (!g_hwnd) return 1;
    dark_title_bar(g_hwnd);
    app_pump(g_hwnd);
    app_cleanup_old_binary();
    PostMessageW(g_hwnd, WM_APP + 1, 0, 0);   /* window exists now; the check can report back */
    SetTimer(g_hwnd, 1, 66, NULL);   /* ~15 fps preview */
    {
        char ms[16];
        if (GetEnvironmentVariableA("HI75C_QUIT_MS", ms, sizeof(ms)) > 0) {
            UINT_PTR d = (UINT_PTR)atoi(ms);
            if (d >= 500 && d <= 60000) SetTimer(g_hwnd, 2, d, NULL);
        }
    }
    ShowWindow(g_hwnd, show);
    UpdateWindow(g_hwnd);

    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    GdiplusShutdown(g_gdiplus);
    return 0;
}
