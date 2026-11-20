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
#include <string.h>
#include "app.h"
#include "board.h"

using namespace Gdiplus;

#define IDT_TICK 1

static ULONG_PTR g_gdiplus;
static HWND g_hwnd;
static const wchar_t *g_face = L"Tahoma";   /* Segoe UI where it exists */
static int g_hover = -1, g_press = -1;

/* layout */
#define W_INIT 1040
#define H_INIT 860
#define PAD 22
#define HEADER_H 64
#define CARD_R 14

typedef struct { int x, y, w, h, id; const char *label; } Hot;

static Hot g_hot[64];
static int g_hot_n;

static void add_hot(int x, int y, int w, int h, int id, const char *label) {
    if (g_hot_n >= 64) return;
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
    Font key(L"Tahoma", 9.0f, FontStyleRegular, UnitPixel);
    SolidBrush textbrush(dim);
    for (i = 0; i < KEY_COUNT; i++) {
        RectF k(ox + KEYS[i].x * s + gap / 2, oy + KEYS[i].y * s + gap / 2,
                KEYS[i].w * s - gap, KEYS[i].h * s - gap);
        fill_round(g, k, 4.0f, Color(255, 32, 37, 47));
        stroke_round(g, k, 4.0f, line, 1.0f);
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
        const Color bg(255, 15, 17, 21), card(255, 23, 26, 33), line(255, 38, 43, 54);
        const Color txt(255, 232, 236, 244), dim(255, 139, 149, 167);
        const Color accent(255, 61, 139, 253);
        SolidBrush bgb(bg);
        g.FillRectangle(&bgb, 0, 0, rc.right, rc.bottom);

        /* header */
        {
            RectF t(0, 0, 1, 1);
            t = RectF((REAL)PAD, 18.0f, 420.0f, 28.0f);
            text(&g, "LEOBOG Hi75C Pro", t, 19.0f, txt, StringAlignmentNear);
            char st[64];
            int online = app_connected();
            sprintf(st, "%s", online ? "connected" : "not connected");
            RectF pill((REAL)(rc.right - PAD - 150), 18.0f, 150.0f, 30.0f);
            fill_round(&g, pill, 15.0f, Color(255, online ? 24 : 40, online ? 40 : 26, online ? 32 : 26));
            stroke_round(&g, pill, 15.0f, Color(120, online ? 53 : 224, online ? 192 : 87, online ? 122 : 76), 1.0f);
            SolidBrush dot(online ? Color(255, 53, 192, 122) : Color(255, 224, 87, 76));
            g.FillEllipse(&dot, (REAL)(rc.right - PAD - 132), 29.0f, 8.0f, 8.0f);
            text(&g, st, RectF((REAL)(rc.right - PAD - 116), 18.0f, 108.0f, 30.0f), 12.5f,
                 online ? Color(255, 53, 192, 122) : Color(255, 224, 87, 76), StringAlignmentNear);
        }

        g_hot_n = 0;
        int top = HEADER_H;
        int right_w = 300;
        int left_w = rc.right - PAD * 3 - right_w;

        /* keyboard card across the top */
        {
            RectF kb((REAL)PAD, (REAL)top, (REAL)(rc.right - PAD * 2), 340.0f);
            fill_round(&g, kb, CARD_R, card);
            stroke_round(&g, kb, CARD_R, line, 1.0f);
            text(&g, "KEYBOARD", RectF(kb.X + 18, kb.Y + 12, 200, 20), 11.0f, dim, StringAlignmentNear);
            draw_keyboard(&g, RectF(kb.X + 8, kb.Y + 30, kb.Width - 16, kb.Height - 38), line, dim);
        }
        top += 356;

        /* effect card */
        RectF lc((REAL)PAD, (REAL)top, (REAL)left_w, (REAL)(rc.bottom - top - PAD));
        fill_round(&g, lc, CARD_R, card);
        stroke_round(&g, lc, CARD_R, line, 1.0f);
        text(&g, "EFFECT", RectF(lc.X + 18, lc.Y + 12, 200, 20), 11.0f, dim, StringAlignmentNear);

        {
            int cols = 3, cw = (left_w - 18 * 2 - (cols - 1) * 10) / cols, ch = 34;
            int i, x0 = (int)lc.X + 18, y0 = (int)lc.Y + 38;
            for (i = 0; i < app_effect_count(); i++) {
                int cx = x0 + (i % cols) * (cw + 10);
                int cy = y0 + (i / cols) * (ch + 10);
                int id = 100 + i;
                int active = (app_state()->effect == app_effect_id(i));
                RectF btn((REAL)cx, (REAL)cy, (REAL)cw, (REAL)ch);
                fill_round(&g, btn, 10.0f,
                           active ? Color(255, 30, 48, 78)
                                  : (g_hover == id ? Color(255, 37, 42, 54) : Color(255, 30, 34, 44)));
                stroke_round(&g, btn, 10.0f, active ? accent : line, active ? 1.4f : 1.0f);
                text(&g, app_effect_name(i), RectF(btn.X, btn.Y, btn.Width, btn.Height), 13.5f,
                     active ? Color(255, 207, 225, 255) : txt, StringAlignmentCenter);
                add_hot(cx, cy, cw, ch, id, app_effect_name(i));
            }
        }

        /* right column */
        {
            int rx = rc.right - PAD - right_w;
            RectF rc2((REAL)rx, (REAL)top, (REAL)right_w, 232.0f);
            fill_round(&g, rc2, CARD_R, card);
            stroke_round(&g, rc2, CARD_R, line, 1.0f);
            text(&g, "COLOUR", RectF(rc2.X + 18, rc2.Y + 12, 160, 20), 11.0f, dim, StringAlignmentNear);

            static const Color sw[7] = {
                Color(255, 224, 87, 76), Color(255, 232, 163, 61), Color(255, 226, 211, 75),
                Color(255, 84, 192, 122), Color(255, 74, 168, 232), Color(255, 124, 108, 240),
                Color(255, 224, 108, 192)
            };
            int k;
            for (k = 0; k < 7; k++) {
                RECT box;
                box.left = (LONG)rc2.X + 18 + k * 36;
                box.top = (LONG)rc2.Y + 40;
                box.right = box.left + 30;
                box.bottom = box.top + 30;
                RectF b2((REAL)box.left, (REAL)box.top, 30.0f, 30.0f);
                fill_round(&g, b2, 8.0f, sw[k]);
                if (app_state()->colour == k) stroke_round(&g, b2, 8.0f, Color(255, 255, 255, 255), 2.0f);
                add_hot(box.left, box.top, 30, 30, 200 + k, "colour");
            }

            {
                char buf[32];
                RectF lab(rc2.X + 18, rc2.Y + 84, 160, 18);
                text(&g, "Brightness", lab, 12.0f, dim, StringAlignmentNear);
                sprintf(buf, "%d%%", (app_state()->brightness + 1) * 10);
                text(&g, buf, RectF(rc2.X + rc2.Width - 78, rc2.Y + 84, 60, 18), 12.0f, dim, StringAlignmentFar);
            }
            {
                int by = (int)rc2.Y + 116, bx = (int)rc2.X + 18, bw = right_w - 36;
                SolidBrush track(Color(255, 44, 49, 62));
                g.FillRectangle(&track, bx, by, bw, 4);
                int filled = bw * (app_state()->brightness + 1) / 10;
                SolidBrush fill(accent);
                g.FillRectangle(&fill, bx, by, filled, 4);
                SolidBrush knob(Color(255, 245, 248, 255));
                g.FillEllipse(&knob, (REAL)(bx + filled - 7), (REAL)(by - 5), 14.0f, 14.0f);
                add_hot(bx - 6, by - 12, bw + 12, 28, 300, "brightness");
            }

            {
                char buf[16];
                text(&g, "Speed", RectF(rc2.X + 18, rc2.Y + 148, 160, 18), 12.0f, dim, StringAlignmentNear);
                sprintf(buf, "%dx", app_state()->speed + 1);
                text(&g, buf, RectF(rc2.X + rc2.Width - 78, rc2.Y + 148, 60, 18), 12.0f, dim, StringAlignmentFar);
            }
            {
                int by = (int)rc2.Y + 180, bx = (int)rc2.X + 18, bw = right_w - 36;
                SolidBrush track(Color(255, 44, 49, 62));
                g.FillRectangle(&track, bx, by, bw, 4);
                int filled = bw * (app_state()->speed + 1) / 4;
                SolidBrush fill(accent);
                g.FillRectangle(&fill, bx, by, filled, 4);
                SolidBrush knob(Color(255, 245, 248, 255));
                g.FillEllipse(&knob, (REAL)(bx + filled - 7), (REAL)(by - 5), 14.0f, 14.0f);
                add_hot(bx - 6, by - 12, bw + 12, 28, 301, "speed");
            }
        }

        /* action row */
        {
            int rx = rc.right - PAD - right_w;
            int y = top + 262;
            RectF a((REAL)rx, (REAL)y, (REAL)((right_w - 10) / 2), 40.0f);
            RectF b2((REAL)(rx + (right_w - 10) / 2 + 10), (REAL)y, (REAL)((right_w - 10) / 2), 40.0f);
            fill_round(&g, a, 10.0f, g_hover == 400 ? Color(255, 37, 42, 54) : Color(255, 30, 34, 44));
            stroke_round(&g, a, 10.0f, line, 1.0f);
            text(&g, "Restore keymap", a, 12.5f, txt, StringAlignmentCenter);
            add_hot((int)a.X, (int)a.Y, (int)a.Width, (int)a.Height, 400, "restore");
            fill_round(&g, b2, 10.0f, g_hover == 401 ? Color(255, 37, 42, 54) : Color(255, 30, 34, 44));
            stroke_round(&g, b2, 10.0f, line, 1.0f);
            text(&g, "Rescan", b2, 12.5f, txt, StringAlignmentCenter);
            add_hot((int)b2.X, (int)b2.Y, (int)b2.Width, (int)b2.Height, 401, "rescan");
        }

        /* note */
        {
            const char *note = app_state()->note;
            if (note[0])
                text(&g, note, RectF((REAL)PAD, (REAL)(rc.bottom - 26), (REAL)(rc.right - PAD * 2), 18.0f),
                     12.0f, Color(255, 224, 87, 76), StringAlignmentNear);
        }
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
                g_press = id;
                if (id >= 100 && id < 100 + app_effect_count()) app_set_effect(app_effect_id(id - 100));
                else if (id >= 200 && id < 207) app_set_colour(id - 200);
                else if (id == 300) app_set_brightness(slider_value(300, x));
                else if (id == 301) app_set_speed(slider_value(301, x));
                else if (id == 400) app_restore_keymap();
                else if (id == 401) app_rescan();
                InvalidateRect(h, NULL, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
            g_press = -1;
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
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
    if (cmdline && strstr(cmdline, "--selftest")) return app_self_test();

    GdiplusStartup(&g_gdiplus, &gsi, NULL);
    {
        FontFamily probe(L"Segoe UI");
        g_face = (probe.GetLastStatus() == Ok) ? L"Segoe UI" : L"Tahoma";
    }
    app_init();

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.lpszClassName = "Hi75CApp";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    g_hwnd = CreateWindowA("Hi75CApp", "LEOBOG Hi75C Pro", WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT, W_INIT, H_INIT, NULL, NULL, inst, NULL);
    if (!g_hwnd) return 1;
    ShowWindow(g_hwnd, show);
    UpdateWindow(g_hwnd);

    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    GdiplusShutdown(g_gdiplus);
    return 0;
}
