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
            float py2 = card.Y + 52 + (card.Height - 132) * 0.62f;
            RectF panel(card.X + 22, py2, card.Width - 44, 104.0f);
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
        LinearGradientBrush pb(PointF(plate.X, plate.Y), PointF(plate.X, plate.Y + plate.Height),
                               Color(255, 30, 34, 43), Color(255, 18, 20, 26));
        GraphicsPath pp; rounded(&pp, plate, 14.0f);
        g->FillPath(&pb, &pp);
        Pen outer(Color(255, 48, 53, 66), 1.0f);
        g->DrawPath(&outer, &pp);
        Pen gloss(Color(22, 255, 255, 255), 1.0f);
        g->DrawLine(&gloss, PointF(plate.X + 14, plate.Y + 1.5f), PointF(plate.X + plate.Width - 14, plate.Y + 1.5f));
    }
    /* scale the board into the card, keeping its aspect ratio */
    float pad = 16.0f;
    float avail_w = area.Width - pad * 2, avail_h = area.Height - pad * 2;
    float sx = avail_w / BOARD_WIDTH, sy = avail_h / BOARD_HEIGHT;
    float s = sx < sy ? sx : sy;
    float ox = area.X + (area.Width - BOARD_WIDTH * s) / 2;
    float oy = area.Y + (area.Height - BOARD_HEIGHT * s) / 2;
    float gap = 2.0f;
    int i;
    Font key(g_face, 10.5f, FontStyleRegular, UnitPixel);
    SolidBrush textbrush(dim);
    for (i = 0; i < KEY_COUNT; i++) {
        RectF k(ox + KEYS[i].x * s + gap / 2, oy + KEYS[i].y * s + gap / 2,
                KEYS[i].w * s - gap, KEYS[i].h * s - gap);
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
        if (k.Width > 26.0f) {
            Color pc = preview_colour(i, KEYS[i].x, KEYS[i].y);
            int lum = pc.GetA() ? (pc.GetR() * 299 + pc.GetG() * 587 + pc.GetB() * 114) / 1000
                                : 0;
            SolidBrush tb(lum > 140 ? Color(255, 18, 20, 26) : Color(255, 205, 213, 226));
            StringFormat c;
            c.SetAlignment(StringAlignmentCenter);
            c.SetLineAlignment(StringAlignmentCenter);
            wchar_t lbl[32];
            MultiByteToWideChar(CP_UTF8, 0, KEYS[i].label, -1, lbl, 32);
            g->DrawString(lbl, -1, &key, k, &c, &tb);
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

        if (g_dbg) dbg("paint %dx%d", rc.right, rc.bottom);
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

        /* ---------------- effects ---------------- */
        {
            RectF ec((REAL)cx, (REAL)y, (REAL)leftW, (REAL)(cb - y));
            shadow(&g, ec, 18.0f);
            GraphicsPath p; rounded(&p, ec, 18.0f);
            card_skin(&g, &p, ec, cardTop, cardBot, line);
            {
                SolidBrush bar(accent);
                g.FillRectangle(&bar, (REAL)(ec.X + 22), ec.Y + 17, 3.0f, 12.0f);
                text(&g, "ON THE KEYBOARD", RectF(ec.X + 32, ec.Y + 16, 260, 18), 10.5f, dim, StringAlignmentNear, NULL, true);
            {
                Pen rule(Color(18, 255, 255, 255), 1.0f);
                g.DrawLine(&rule, PointF(ec.X + 22, ec.Y + 38), PointF(ec.X + ec.Width - 22, ec.Y + 38));
            }
            }
            text(&g, "17 firmware effects", RectF(ec.X + ec.Width - 180, ec.Y + 16, 158, 18), 10.5f,
                 Color(255, 96, 106, 124), StringAlignmentFar);

            int cols = 3, gap = 12;
            int bw = (int)((ec.Width - 44 - gap * (cols - 1)) / cols);
            int rows = (app_effect_count() + cols - 1) / cols;
            int bh = (int)((ec.Height - 44 - 14 - gap * (rows - 1)) / rows);
            if (bh > 46) bh = 46;
            if (bh < 28) bh = 28;
            int usedH = rows * bh + (rows - 1) * gap;
            int spare = (int)(ec.Height - 44 - usedH);
            int x0 = (int)ec.X + 22, y0 = (int)ec.Y + 44 + (spare > 0 ? spare / 3 : 0);
            int i;
            for (i = 0; i < app_effect_count(); i++) {
                int cx = x0 + (i % cols) * (bw + gap);
                int cy = y0 + (i / cols) * (bh + gap);
                int id = 100 + i;
                int active = (app_state()->effect == app_effect_id(i));
                RectF btn((REAL)cx, (REAL)cy, (REAL)bw, (REAL)bh);
                Color fillc = active ? accentSoft
                             : (g_hover == id ? Color(255, 36, 40, 50) : Color(255, 26, 29, 37));
                if (active) {
                    fillc = Color(255, (BYTE)(24 + accent.GetR() * 0.10f),
                                       (BYTE)(30 + accent.GetG() * 0.10f),
                                       (BYTE)(42 + accent.GetB() * 0.10f));
                }
                fill_round(&g, btn, 11.0f, fillc);
                if (active) {
                    Pen glow(Color(48, 74, 144, 255), 4.0f);
                    GraphicsPath gp; rounded(&gp, btn, 11.0f);
                    g.DrawPath(&glow, &gp);
                }
                stroke_round(&g, btn, 11.0f, active ? accent : lineSoft, active ? 1.6f : 1.0f);
                {
                    Color dot = SWATCH[i % 7];
                    SolidBrush db(active ? dot : Color(150, dot.GetR(), dot.GetG(), dot.GetB()));
                    g.FillEllipse(&db, btn.X + 14, btn.Y + btn.Height / 2 - 3, 6.0f, 6.0f);
                }
                text(&g, app_effect_name(i), RectF(btn.X + 10, btn.Y, btn.Width - 20, btn.Height), 13.0f,
                     active ? Color(255, 214, 231, 255) : ink, StringAlignmentCenter);
                add_hot(cx, cy, bw, bh, id, app_effect_name(i));
            }
        }

        /* ---------------- right column ---------------- */
        {
            int rx = cx + cw - rightW;
            RectF cc((REAL)rx, (REAL)y, (REAL)rightW, (REAL)(cb - y));
            shadow(&g, cc, 18.0f);
            GraphicsPath p; rounded(&p, cc, 18.0f);
            card_skin(&g, &p, cc, cardTop, cardBot, line);
            {
                SolidBrush bar(accent);
                g.FillRectangle(&bar, (REAL)(cc.X + 22), cc.Y + 17, 3.0f, 12.0f);
                text(&g, "COLOUR", RectF(cc.X + 32, cc.Y + 16, 160, 18), 10.5f, dim, StringAlignmentNear, NULL, true);
            {
                Pen rule(Color(18, 255, 255, 255), 1.0f);
                g.DrawLine(&rule, PointF(cc.X + 22, cc.Y + 38), PointF(cc.X + cc.Width - 22, cc.Y + 38));
            }
            }

            const Color *sw = SWATCH;
            static const char *hex[7] = {"#F0685C", "#F4B04C", "#ECD85C", "#60D08A", "#58B0F4", "#8A7AF6", "#EC7AC6"};
            int k;
            for (k = 0; k < 7; k++) {
                RectF b2((REAL)(cc.X + 22 + k * 38), cc.Y + 44, 32.0f, 32.0f);
                fill_round(&g, b2, 9.0f, sw[k]);
                if (app_state()->colour == k) {
                    RectF ring2(b2.X - 3, b2.Y - 3, b2.Width + 6, b2.Height + 6);
                    stroke_round(&g, ring2, 12.0f, Color(255, 255, 255, 255), 1.6f);
                    /* tick mark so the choice is unambiguous at a glance */
                    Pen tick(Color(255, 18, 20, 26), 2.2f);
                    g.DrawLine(&tick, PointF(b2.X + 10, b2.Y + 16.5f), PointF(b2.X + 14.5f, b2.Y + 21));
                    g.DrawLine(&tick, PointF(b2.X + 14.5f, b2.Y + 21), PointF(b2.X + 22, b2.Y + 11.5f));
                } else {
                    stroke_round(&g, b2, 9.0f, g_hover == 200 + k ? Color(120, 255, 255, 255)
                                                                : Color(50, 255, 255, 255), 1.0f);
                }
                add_hot((int)b2.X, (int)b2.Y, (int)b2.Width, (int)b2.Height, 200 + k, "swatch");
            }
            text(&g, hex[app_state()->colour < 7 ? app_state()->colour : 0],
                 RectF(cc.X + 22, cc.Y + 82, 120, 16), 11.0f, Color(255, 110, 120, 138), StringAlignmentNear);

            {
                char buf[16];
                RectF lab(cc.X + 22, cc.Y + 104, 160, 16);
                text(&g, "Brightness", lab, 11.5f, dim, StringAlignmentNear);
                snprintf(buf, sizeof(buf), "%d%%", (app_state()->brightness + 1) * 10);
                text(&g, buf, RectF(cc.X + cc.Width - 92, cc.Y + 104, 70, 16), 11.5f,
                     Color(255, 190, 200, 216), StringAlignmentFar);
            }
            {
                int by = (int)cc.Y + 128, bx = (int)cc.X + 22, bw = rightW - 44;
                fill_round(&g, RectF((REAL)bx, (REAL)(by - 2), (REAL)bw, 6.0f), 3.0f, Color(255, 31, 34, 42));
                int filled = bw * (app_state()->brightness + 1) / 10;
                fill_round(&g, RectF((REAL)bx, (REAL)(by - 2), (REAL)filled, 6.0f), 3.0f, accent);
                SolidBrush knob(Color(255, 248, 251, 255));
                g.FillEllipse(&knob, (REAL)(bx + filled - 8), (REAL)(by - 8), 16.0f, 16.0f);
                Pen ring(Color(255, 120, 170, 255), 1.0f);
                g.DrawEllipse(&ring, (REAL)(bx + filled - 8), (REAL)(by - 8), 16.0f, 16.0f);
                add_hot(bx - 8, by - 14, bw + 16, 30, 300, "brightness");
            }

            {
                char buf[16];
                text(&g, "Speed", RectF(cc.X + 22, cc.Y + 156, 160, 16), 11.5f, dim, StringAlignmentNear);
                snprintf(buf, sizeof(buf), "%dx", app_state()->speed + 1);
                text(&g, buf, RectF(cc.X + cc.Width - 92, cc.Y + 156, 70, 16), 11.5f,
                     Color(255, 190, 200, 216), StringAlignmentFar);
            }
            {
                int by = (int)cc.Y + 180, bx = (int)cc.X + 22, bw = rightW - 44;
                fill_round(&g, RectF((REAL)bx, (REAL)(by - 2), (REAL)bw, 6.0f), 3.0f, Color(255, 31, 34, 42));
                int filled = bw * (app_state()->speed + 1) / 4;
                fill_round(&g, RectF((REAL)bx, (REAL)(by - 2), (REAL)filled, 6.0f), 3.0f, accent);
                SolidBrush knob(Color(255, 248, 251, 255));
                g.FillEllipse(&knob, (REAL)(bx + filled - 8), (REAL)(by - 8), 16.0f, 16.0f);
                Pen ring(Color(255, 120, 170, 255), 1.0f);
                g.DrawEllipse(&ring, (REAL)(bx + filled - 8), (REAL)(by - 8), 16.0f, 16.0f);
                add_hot(bx - 8, by - 14, bw + 16, 30, 301, "speed");
            }

            /* quick presets: a warm look, a cool look, or dark */
            {
                int py = (int)cc.Y + 210;
                const char *names[3] = {"Warm", "Cool", "Off"};
                int ids[3] = {600, 601, 602};
                int j, bw3 = (rightW - 44 - 16) / 3;
                for (j = 0; j < 3; j++) {
                    RectF b((REAL)(cc.X + 22 + j * (bw3 + 8)), (REAL)py, (REAL)bw3, 30.0f);
                    fill_round(&g, b, 9.0f, g_hover == ids[j] ? Color(255, 34, 38, 48) : Color(255, 26, 29, 37));
                    stroke_round(&g, b, 9.0f, lineSoft, 1.0f);
                    text(&g, names[j], b, 11.5f, ink, StringAlignmentCenter);
                    add_hot((int)b.X, (int)b.Y, (int)b.Width, (int)b.Height, ids[j], names[j]);
                }
            }

            /* live summary (omitted when the card is short, rather than clipped) */
            if (cc.Height > 340.0f) {
                int sy = (int)cc.Y + 250;
                RectF box((REAL)(cc.X + 22), (REAL)sy, (REAL)(rightW - 44), 76.0f);
                fill_round(&g, box, 12.0f, Color(255, 20, 22, 28));
                stroke_round(&g, box, 12.0f, lineSoft, 1.0f);
                text(&g, "RUNNING", RectF(box.X + 14, box.Y + 10, 120, 16), 9.5f,
                     Color(255, 110, 120, 138), StringAlignmentNear);
                text(&g, app_effect_name(0) && app_state()->effect == 0 ? "Off" : "",
                     RectF(box.X + 14, box.Y + 26, 200, 18), 12.0f, ink, StringAlignmentNear);
                {
                    int i, found = -1;
                    for (i = 0; i < app_effect_count(); i++)
                        if (app_effect_id(i) == app_state()->effect) found = i;
                    if (found >= 0)
                        text(&g, app_effect_name(found), RectF(box.X + 14, box.Y + 26, 200, 18),
                             12.0f, ink, StringAlignmentNear);
                    else if (app_state()->effect == 0)
                        text(&g, "Off", RectF(box.X + 14, box.Y + 26, 200, 18), 12.0f, ink, StringAlignmentNear);
                }
                {
                    char line2[64];
                    snprintf(line2, sizeof(line2), "brightness %d%%   |   speed %dx",
                             (app_state()->brightness + 1) * 10, app_state()->speed + 1);
                    text(&g, line2, RectF(box.X + 14, box.Y + 50, rightW - 70, 16), 11.0f, dim,
                         StringAlignmentNear);
                }
                SolidBrush chip(app_state()->colour < 7 ? sw[app_state()->colour] : sw[0]);
                g.FillEllipse(&chip, (REAL)(box.X + box.Width - 40), (REAL)(box.Y + 22), 16.0f, 16.0f);
            }

            /* actions */
            int ay = (int)cc.Y + (int)cc.Height - 54;
            {
                int bw3 = (rightW - 20) / 3;
                const char *names[3] = {"Import", "Restore", "Rescan"};
                int ids[3] = {402, 400, 401};
                int j;
                for (j = 0; j < 3; j++) {
                    RectF b((REAL)(rx + j * (bw3 + 10)), (REAL)ay, (REAL)bw3, 38.0f);
                    fill_round(&g, b, 11.0f, g_hover == ids[j] ? Color(255, 34, 38, 48) : Color(255, 26, 29, 37));
                    stroke_round(&g, b, 11.0f, lineSoft, 1.0f);
                    icon(&g, 9 + j, b.X + 10, b.Y + b.Height / 2 - 6, dim);
                    text(&g, names[j], RectF(b.X + 26, b.Y, b.Width - 30, b.Height), 12.0f, ink,
                         StringAlignmentNear);
                    add_hot((int)b.X, (int)b.Y, (int)b.Width, (int)b.Height, ids[j], names[j]);
                }
            }

            if (InterlockedCompareExchange(&g_update, 0, 0)) {
                RectF up((REAL)rx, (REAL)(ay - 46), (REAL)rightW, 36.0f);
                fill_round(&g, up, 11.0f, accentSoft);
                stroke_round(&g, up, 11.0f, accent, 1.2f);
                char ub[64];
                if (app_latest_tag()[0]) snprintf(ub, sizeof(ub), "Update to %s", app_latest_tag());
                else lstrcpynA(ub, "Update available", (int)sizeof(ub));
                text(&g, ub, up, 12.5f, Color(255, 214, 231, 255), StringAlignmentCenter);
                add_hot((int)up.X, (int)up.Y, (int)up.Width, (int)up.Height, 403, "update");
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
    int right_w = 300, rx = rc.right - PAD - right_w;
    int bx = rx + 18, bw = right_w - 36;
    int v = (x - bx) * (id == 300 ? 10 : 4) / bw;
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
                } else if (isxdigit((unsigned char)ch) && strlen(g_code) < 2) {
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
        case WM_SIZE:
            InvalidateRect(h, NULL, FALSE);   /* layout depends on the client size */
            return 0;
        case WM_GETMINMAXINFO:
            {
                MINMAXINFO *mm = (MINMAXINFO *)l;
                mm->ptMinTrackSize.x = (LONG)(960 * g_scale);
                mm->ptMinTrackSize.y = (LONG)(900 * g_scale);
            }
            return 0;
        case WM_TIMER:
            if (w == 1) { InvalidateRect(h, NULL, FALSE); return 0; }
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
    app_init();
    /* the update probe is deferred until after the first paint */
    PostMessageW(g_hwnd, WM_APP + 1, 0, 0);

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
        g_hwnd = CreateWindowA("Hi75CApp", "LEOBOG Hi75C Pro", WS_OVERLAPPEDWINDOW,
                               wx, wy, ww, wh, NULL, NULL, inst, NULL);
    }
    if (!g_hwnd) return 1;
    dark_title_bar(g_hwnd);
    app_pump(g_hwnd);
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
