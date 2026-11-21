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
                 const Color &c, StringAlignment align, const wchar_t *face = NULL) {
    Font f(face ? face : g_face, size, FontStyleRegular, UnitPixel);
    SolidBrush b(c);
    StringFormat fmt;
    fmt.SetAlignment(align);
    fmt.SetLineAlignment(StringAlignmentCenter);
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    wchar_t wide[256];
    MultiByteToWideChar(CP_UTF8, 0, s, -1, wide, 256);
    g->DrawString(wide, -1, &f, r, &fmt, &b);
}

static void draw_keyboard(Graphics *g, const RectF &area, const Color &line, const Color &dim)
{
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
            Color topc = hot ? Color(255, 50, 56, 70) : Color(255, 38, 42, 53);
            Color botc = hot ? Color(255, 36, 40, 51) : Color(255, 27, 30, 38);
            LinearGradientBrush grad(PointF(k.X, k.Y), PointF(k.X, k.Y + k.Height), topc, botc);
            GraphicsPath kp;
            rounded(&kp, k, 4.0f);
            g->FillPath(&grad, &kp);
            Pen edge(i == g_sel ? Color(255, 61, 139, 253) : line, i == g_sel ? 1.8f : 1.0f);
            g->DrawPath(&edge, &kp);
        }
        add_hot((int)k.X, (int)k.Y, (int)k.Width, (int)k.Height, 1000 + i, KEYS[i].label);
        if (k.Width > 26.0f) {
            StringFormat c;
            c.SetAlignment(StringAlignmentCenter);
            c.SetLineAlignment(StringAlignmentCenter);
            wchar_t lbl[32];
            MultiByteToWideChar(CP_UTF8, 0, KEYS[i].label, -1, lbl, 32);
            g->DrawString(lbl, -1, &key, k, &c, &textbrush);
        }
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
    RECT rc;
    GetClientRect(hwnd, &rc);

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HBITMAP old = (HBITMAP)SelectObject(mem, bmp);
    {
        Graphics g(mem);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);

        const Color bgTop = C_CANVAS_TOP, bgBot = C_CANVAS_BOT;
        const Color cardTop = C_SURFACE_TOP, cardBot = C_SURFACE_BOT;
        const Color line = C_HAIRLINE, lineSoft = C_HAIRLINE_SOFT;
        const Color ink = C_INK, dim = C_INK_2;
        const Color accent = C_ACCENT, accentSoft = C_ACCENT_DIM;
        const Color ok = C_OK, bad = C_BAD;

        {
            LinearGradientBrush bg(PointF(0, 0), PointF(0, (REAL)rc.bottom), bgTop, bgBot);
            g.FillRectangle(&bg, 0, 0, rc.right, rc.bottom);
        }

        if (g_dbg) dbg("paint %dx%d", rc.right, rc.bottom);
        const int pad = 26;
        const int headerH = 76;

        /* ---------------- header ---------------- */
        {
            RectF title((REAL)pad, 22.0f, 400.0f, 30.0f);
            text(&g, "LEOBOG Hi75C Pro", title, 22.0f, ink, StringAlignmentNear);
            RectF sub((REAL)pad, 48.0f, 400.0f, 18.0f);
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
        int rightW = 320;
        int leftW = rc.right - pad * 2 - rightW - 20;

        /* ---------------- keyboard ---------------- */
        int kbH = 250;
        {
            int availH = rc.bottom - headerH - 46;
            kbH = (int)(availH * 0.46);
            if (kbH > 420) kbH = 420;
            if (kbH < 250) kbH = 250;
            RectF kb((REAL)pad, (REAL)y, (REAL)(rc.right - pad * 2), (REAL)kbH);
            GraphicsPath p; rounded(&p, kb, 18.0f);
            card_skin(&g, &p, kb, cardTop, cardBot, line);
            {
                SolidBrush bar(accent);
                g.FillRectangle(&bar, (REAL)(kb.X + 22), kb.Y + 17, 3.0f, 12.0f);
                text(&g, "BOARD", RectF(kb.X + 32, kb.Y + 16, 200, 18), 10.5f, dim, StringAlignmentNear);
            }

            /* layer switch, top right of the card */
            {
                int layer = app_layer();
                RectF lb(kb.X + kb.Width - 176, kb.Y + 14, 74, 26);
                RectF lf(kb.X + kb.Width - 96, kb.Y + 14, 74, 26);
                fill_round(&g, lb, 8.0f, layer == 0 ? accentSoft : Color(255, 23, 25, 32));
                stroke_round(&g, lb, 8.0f, layer == 0 ? accent : lineSoft, 1.0f);
                text(&g, "Base", lb, 11.5f, layer == 0 ? Color(255, 210, 228, 255) : dim, StringAlignmentCenter);
                add_hot((int)lb.X, (int)lb.Y, (int)lb.Width, (int)lb.Height, 500, "base");
                fill_round(&g, lf, 8.0f, layer == 1 ? accentSoft : Color(255, 23, 25, 32));
                stroke_round(&g, lf, 8.0f, layer == 1 ? accent : lineSoft, 1.0f);
                text(&g, "FN", lf, 11.5f, layer == 1 ? Color(255, 210, 228, 255) : dim, StringAlignmentCenter);
                add_hot((int)lf.X, (int)lf.Y, (int)lf.Width, (int)lf.Height, 501, "fn");
            }

            draw_keyboard(&g, RectF(kb.X + 10, kb.Y + 42, kb.Width - 20, kb.Height - 52), line, dim);
        }
        y += kbH + 18;

        /* ---------------- effects ---------------- */
        {
            RectF ec((REAL)pad, (REAL)y, (REAL)leftW, (REAL)(rc.bottom - y - 46));
            GraphicsPath p; rounded(&p, ec, 18.0f);
            card_skin(&g, &p, ec, cardTop, cardBot, line);
            {
                SolidBrush bar(accent);
                g.FillRectangle(&bar, (REAL)(ec.X + 22), ec.Y + 17, 3.0f, 12.0f);
                text(&g, "ON THE KEYBOARD", RectF(ec.X + 32, ec.Y + 16, 260, 18), 10.5f, dim, StringAlignmentNear);
            }
            text(&g, "stored in the board", RectF(ec.X + ec.Width - 180, ec.Y + 16, 158, 18), 10.5f,
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
                fill_round(&g, btn, 11.0f, fillc);
                if (active) {
                    Pen glow(Color(48, 74, 144, 255), 4.0f);
                    GraphicsPath gp; rounded(&gp, btn, 11.0f);
                    g.DrawPath(&glow, &gp);
                }
                stroke_round(&g, btn, 11.0f, active ? accent : lineSoft, active ? 1.6f : 1.0f);
                text(&g, app_effect_name(i), RectF(btn.X, btn.Y, btn.Width, btn.Height), 13.0f,
                     active ? Color(255, 214, 231, 255) : ink, StringAlignmentCenter);
                add_hot(cx, cy, bw, bh, id, app_effect_name(i));
            }
        }

        /* ---------------- right column ---------------- */
        {
            int rx = rc.right - pad - rightW;
            RectF cc((REAL)rx, (REAL)y, (REAL)rightW, (REAL)(rc.bottom - y - 46));
            GraphicsPath p; rounded(&p, cc, 18.0f);
            card_skin(&g, &p, cc, cardTop, cardBot, line);
            {
                SolidBrush bar(accent);
                g.FillRectangle(&bar, (REAL)(cc.X + 22), cc.Y + 17, 3.0f, 12.0f);
                text(&g, "COLOUR", RectF(cc.X + 32, cc.Y + 16, 160, 18), 10.5f, dim, StringAlignmentNear);
            }

            const Color *sw = SWATCH;
            static const char *hex[7] = {"#F0685C", "#F4B04C", "#ECD85C", "#60D08A", "#58B0F4", "#8A7AF6", "#EC7AC6"};
            int k;
            for (k = 0; k < 7; k++) {
                RectF b2((REAL)(cc.X + 22 + k * 38), cc.Y + 44, 32.0f, 32.0f);
                fill_round(&g, b2, 9.0f, sw[k]);
                if (app_state()->colour == k) {
                    stroke_round(&g, b2, 9.0f, Color(255, 255, 255, 255), 2.0f);
                } else {
                    stroke_round(&g, b2, 9.0f, Color(60, 255, 255, 255), 1.0f);
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

            /* live summary */
            {
                int sy = (int)cc.Y + 206;
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
                    text(&g, names[j], b, 12.0f, ink, StringAlignmentCenter);
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
            RectF strip((REAL)pad, (REAL)(rc.bottom - 32), (REAL)(rc.right - pad * 2), 18.0f);
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
    if (g_dbg) {
        int hi;
        dbg("hotspots %d", g_hot_n);
        for (hi = 0; hi < g_hot_n; hi++)
            dbg("hot %d %d %d %d %d", g_hot[hi].id, g_hot[hi].x, g_hot[hi].y,
                g_hot[hi].w, g_hot[hi].h);
    }
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static int hit(int x, int y) {
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
            g_press = -1;
            return 0;
        case WM_CHAR:
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
                mm->ptMinTrackSize.x = 940;
                mm->ptMinTrackSize.y = 820;
            }
            return 0;
        case WM_TIMER:
            if (w == 1) { InvalidateRect(h, NULL, FALSE); return 0; }
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
                app_save_state(r.left, r.top, r.right - r.left, r.bottom - r.top);
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
        {   /* --size WxH lets the layout be tested at other dimensions */
            const char *sz = cmdline ? strstr(cmdline, "--size ") : NULL;
            if (sz) {
                int w = 0, h = 0;
                if (sscanf(sz + 7, "%dx%d", &w, &h) == 2 && w > 600 && h > 500) { ww = w; wh = h; }
            }
        }
        app_load_state(&wx, &wy, &ww, &wh);
        if (ww < 900 || wh < 700) { ww = W_INIT; wh = H_INIT; wx = CW_USEDEFAULT; wy = CW_USEDEFAULT; }
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
        g_hwnd = CreateWindowA("Hi75CApp", "LEOBOG Hi75C Pro", WS_OVERLAPPEDWINDOW,
                               wx, wy, ww, wh, NULL, NULL, inst, NULL);
    }
    if (!g_hwnd) return 1;
    dark_title_bar(g_hwnd);
    SetTimer(g_hwnd, 1, 250, NULL);
    app_pump(g_hwnd);
    SetTimer(g_hwnd, 1, 150, NULL);
    ShowWindow(g_hwnd, show);
    UpdateWindow(g_hwnd);

    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    GdiplusShutdown(g_gdiplus);
    return 0;
}
