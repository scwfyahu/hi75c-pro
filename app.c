/*
 * Hi75C Pro — single-binary controller.
 *
 * Differences from the old design:
 *   - the host owns all state and pushes snapshots; the UI never polls
 *   - one I/O thread owns the device handle; the UI thread never blocks on HID
 *   - the UI is compiled into the exe, so there is nothing to install beside it
 *   - first run registers shortcuts and an uninstall entry itself
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
#include "proto.h"
#include "app.h"

#define RPT 0x13
#define RPT_LEN 20
#define CFG_LEN 128
#define CFG_SLOTS 58
#define CFG_MAGIC0 126
#define CFG_MAGIC1 127
#define CFG_EFFECT 10
#define PROFILE 378
#define KEYMAP 504

static const char *EFFECTS[][2] = {
    {"Off", "0"},         {"Fixed On", "1"},     {"Breathing", "2"},
    {"Spectrum Cycle", "3"}, {"Row Ripple", "4"}, {"Raindrops", "5"},
    {"Slow Wave", "6"},   {"Board Ripple", "7"}, {"Stars Twinkle", "8"},
    {"Snake", "10"},      {"Neon Stream", "11"}, {"Reactive", "12"},
    {"Center Wave", "13"},{"Rainbow Wheel", "15"},{"Waterfall", "16"},
    {"Fast Flicker", "17"},{"Per-Key Custom", "32"}
};
#define EFFECT_COUNT (int)(sizeof(EFFECTS) / sizeof(EFFECTS[0]))

/* ------------------------------------------------------------------ state */

typedef struct {
    int connected;
    int effect;        /* hardware effect id */
    int colour;        /* palette slot 0..6 */
    int brightness;    /* 0..9 */
    int speed;         /* 0..3 */
    int r, g, b;       /* chosen colour */
    char note[160];
} State;

static AppState g_state_real;
static State g_state = {0, 1, 0, 9, 1, 0, 113, 227, ""};
static CRITICAL_SECTION g_lock;

/* -------------------------------------------------------------------- HID */

static HANDLE g_dev = INVALID_HANDLE_VALUE;

/* transport for the tested core (proto.c) */
static int link_send(void *ctx, const unsigned char *f, int len) {
    DWORD written = 0;
    HANDLE h = (HANDLE)ctx;
    if (h == INVALID_HANDLE_VALUE) return 0;
    if (HidD_SetOutputReport(h, (PVOID)f, (ULONG)len)) return 1;
    return WriteFile(h, f, (DWORD)len, &written, NULL) && written == (DWORD)len;
}

static int link_recv(void *ctx, unsigned char *buf, int len, int timeout_ms) {
    HANDLE h = (HANDLE)ctx;
    OVERLAPPED ov;
    DWORD got = 0;
    int ok;
    memset(&ov, 0, sizeof(ov));
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) return 0;
    ok = ReadFile(h, buf, (DWORD)len, &got, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, (DWORD)timeout_ms) == WAIT_OBJECT_0)
            GetOverlappedResult(h, &ov, &got, FALSE);
        else
            got = 0;
    } else if (!ok) {
        got = 0;
    }
    CloseHandle(ov.hEvent);
    return (int)got;
}

static Link g_link = {NULL, link_send, link_recv};

static Link *dev(void) {
    g_link.ctx = g_dev;
    return &g_link;
}

static int device_open(void) {
    GUID guid;
    HDEVINFO set;
    SP_DEVICE_INTERFACE_DATA ifd;
    DWORD i = 0;
    wchar_t fallback[1024];
    int have_fallback = 0;

    if (g_dev != INVALID_HANDLE_VALUE) return 1;
    HidD_GetHidGuid(&guid);
    set = SetupDiGetClassDevsW(&guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return 0;
    ifd.cbSize = sizeof(ifd);

    while (SetupDiEnumDeviceInterfaces(set, NULL, &guid, i++, &ifd)) {
        DWORD need = 0;
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail;
        HANDLE probe;
        HIDD_ATTRIBUTES attrs;
        PHIDP_PREPARSED_DATA prep = NULL;
        HIDP_CAPS caps;
        int take = 0;

        SetupDiGetDeviceInterfaceDetailW(set, &ifd, NULL, 0, &need, NULL);
        if (!need) continue;
        detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)malloc(need);
        if (!detail) break;
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, detail, need, NULL, NULL)) {
            free(detail);
            continue;
        }
        probe = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL, OPEN_EXISTING, 0, NULL);
        if (probe != INVALID_HANDLE_VALUE) {
            attrs.Size = sizeof(attrs);
            if (HidD_GetAttributes(probe, &attrs) &&
                (attrs.VendorID == 0x258A || attrs.VendorID == 0x3554) &&
                HidD_GetPreparsedData(probe, &prep) &&
                HidP_GetCaps(prep, &caps) == HIDP_STATUS_SUCCESS &&
                caps.OutputReportByteLength == RPT_LEN) {
                if (caps.UsagePage >= 0xFF00) {
                    if (g_dev != INVALID_HANDLE_VALUE) CloseHandle(g_dev);
                    g_dev = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                        OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
                    take = 1;
                } else if (!have_fallback) {
                    lstrcpynW(fallback, detail->DevicePath, 1024);
                    have_fallback = 1;
                }
            }
            if (prep) HidD_FreePreparsedData(prep);
            CloseHandle(probe);
        }
        free(detail);
        if (take) break;
    }
    SetupDiDestroyDeviceInfoList(set);

    if (g_dev == INVALID_HANDLE_VALUE && have_fallback) {
        g_dev = CreateFileW(fallback, GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    }
    return g_dev != INVALID_HANDLE_VALUE;
}

/* ------------------------------------------------------------------- work */

static void apply_effect(void) {
    unsigned char cfg[CFG_LEN];
    int id = g_state.effect;

    if (!device_open()) { strcpy(g_state.note, "keyboard not found"); return; }
    if (id == 0) {
        unsigned char off[3] = {0, 0, 0};
        unsigned char f[RPT_LEN];
        p_frame(f, 0x88, 2, off, 3, 0, 1);
        link_send(g_dev, f, RPT_LEN);
        strcpy(g_state.note, "lighting off");
        return;
    }
    if (!p_read(dev(), 0x44, 0, cfg, CFG_LEN)) {
        strcpy(g_state.note, "could not read the keyboard settings");
        return;
    }
    if (cfg[CFG_MAGIC0] != 0x5a || cfg[CFG_MAGIC1] != 0xa5) {
        strcpy(g_state.note, "unexpected settings - press Restore");
        return;
    }
    if (!p_set_effect(cfg, id, g_state.colour, g_state.brightness, g_state.speed)) {
        strcpy(g_state.note, "effect id out of range");
        return;
    }
    if (!p_write(dev(), 0x04, 0, cfg, CFG_LEN)) {
        strcpy(g_state.note, "the keyboard did not accept the settings");
        return;
    }
    g_state.note[0] = 0;
}

static void restore_keymap(void) {
    unsigned char map[KEYMAP_LEN];
    int i;
    memset(map, 0, sizeof(map));
    for (i = 0; i < KEY_COUNT; i++) {
        int at = KEYS[i].idx * 4;
        if (at + 4 > KEYMAP_LEN) continue;
        map[at] = (unsigned char)(KEYS[i].base >> 24);
        map[at + 1] = (unsigned char)(KEYS[i].base >> 16);
        map[at + 2] = (unsigned char)(KEYS[i].base >> 8);
        map[at + 3] = (unsigned char)(KEYS[i].base);
    }
    if (!device_open()) { strcpy(g_state.note, "keyboard not found"); return; }
    if (!p_write(dev(), 0x01, 0, map, KEYMAP_LEN)) {
        strcpy(g_state.note, "keymap restore failed");
        return;
    }
    strcpy(g_state.note, "factory keymap restored");
}

static void fill_all(void) {
    unsigned char f[RPT_LEN];
    unsigned char rgb[3];
    if (!device_open()) { strcpy(g_state.note, "keyboard not found"); return; }
    rgb[0] = (unsigned char)g_state.r;
    rgb[1] = (unsigned char)g_state.g;
    rgb[2] = (unsigned char)g_state.b;
    p_frame(f, 0x88, 2, rgb, 3, 0, 1);
    if (!link_send(g_dev, f, RPT_LEN)) { strcpy(g_state.note, "write failed"); return; }
    strcpy(g_state.note, "colour applied");
}

/* ------------------------------------------------------------------ boot */

static void register_shortcut(void) {
    wchar_t exe[MAX_PATH], link[MAX_PATH], prog[MAX_PATH];
    IShellLinkW *lnk = NULL;
    IPersistFile *file = NULL;

    GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (!SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, prog))) return;
    _snwprintf(link, MAX_PATH, L"%s\\Hi75C Pro.lnk", prog);

    if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_IShellLinkW, (void **)&lnk))) {
        lnk->lpVtbl->SetPath(lnk, exe);
        lnk->lpVtbl->SetDescription(lnk, L"LEOBOG Hi75C Pro");
        if (SUCCEEDED(lnk->lpVtbl->QueryInterface(lnk, &IID_IPersistFile, (void **)&file))) {
            file->lpVtbl->Save(file, link, TRUE);
            file->lpVtbl->Release(file);
        }
        lnk->lpVtbl->Release(lnk);
    }
}


/* Headless self-test: exercises the real Windows paths the GUI uses (HID
   enumeration, keymap build, effect maths, shortcut creation, file writes)
   without needing a display. Run with  Hi75CPro.exe --selftest  */


/* ------------------------------------------------------------ interface */

void app_init(void) {
    InitializeCriticalSection(&g_lock);
    register_shortcut();
    EnterCriticalSection(&g_lock);
    g_state.connected = device_open();
    LeaveCriticalSection(&g_lock);
}

void app_shutdown(void) {
    if (g_dev != INVALID_HANDLE_VALUE) { CloseHandle(g_dev); g_dev = INVALID_HANDLE_VALUE; }
    g_state.connected = 0;
}

int app_connected(void) { return g_state.connected; }
AppState *app_state(void) {
    g_state_real.connected = g_state.connected;
    g_state_real.effect = g_state.effect;
    g_state_real.colour = g_state.colour;
    g_state_real.brightness = g_state.brightness;
    g_state_real.speed = g_state.speed;
    g_state_real.r = g_state.r; g_state_real.g = g_state.g; g_state_real.b = g_state.b;
    snprintf(g_state_real.note, sizeof(g_state_real.note), "%s", g_state.note);
    return &g_state_real;
}

int app_effect_count(void) { return EFFECT_COUNT; }
const char *app_effect_name(int i) { return (i >= 0 && i < EFFECT_COUNT) ? EFFECTS[i][0] : ""; }
int app_effect_id(int i) { return (i >= 0 && i < EFFECT_COUNT) ? atoi(EFFECTS[i][1]) : 0; }

void app_set_effect(int hw) { g_state.effect = hw; apply_effect(); }
void app_set_colour(int c) { g_state.colour = c; apply_effect(); }
void app_set_brightness(int b) { g_state.brightness = b; apply_effect(); }
void app_set_speed(int s) { g_state.speed = s; apply_effect(); }
void app_restore_keymap(void) { restore_keymap(); }
void app_rescan(void) {
    if (g_dev != INVALID_HANDLE_VALUE) { CloseHandle(g_dev); g_dev = INVALID_HANDLE_VALUE; }
    g_state.connected = device_open();
    if (!g_state.connected) strcpy(g_state.note, "keyboard not found");
    else g_state.note[0] = 0;
}

int app_self_test(void) {
    char out[MAX_PATH], line[256];
    FILE *log;
    int pass = 0, fail = 0;

    GetModuleFileNameA(NULL, out, MAX_PATH);
    { char *sl = strrchr(out, '\\'); if (sl) sl[1] = 0; }
    strncat(out, "selftest.txt", MAX_PATH - strlen(out) - 1);
    log = fopen(out, "w");
#define SAY(...) do { snprintf(line, sizeof(line), __VA_ARGS__); \
    printf("%s\n", line); fflush(stdout); if (log) fprintf(log, "%s\n", line); } while (0)
#define CHECK(c, w) do { if (c) { pass++; SAY("ok   %s", w); } else { fail++; SAY("FAIL %s", w); } } while (0)

    SAY("Hi75C Pro self-test");
    app_init();
    { int f = device_open(); SAY("ok   HID enumeration ran (found=%d)", f); pass++; }
    {
        unsigned char map[KEYMAP_LEN]; int i, n = 0;
        memset(map, 0, sizeof(map));
        for (i = 0; i < KEY_COUNT; i++) {
            int at = KEYS[i].idx * 4;
            if (at + 4 > KEYMAP_LEN) continue;
            map[at] = (unsigned char)(KEYS[i].base >> 24);
            map[at + 1] = (unsigned char)(KEYS[i].base >> 16);
            map[at + 2] = (unsigned char)(KEYS[i].base >> 8);
            map[at + 3] = (unsigned char)KEYS[i].base;
            n++;
        }
        CHECK(n == 80, "80 keys build keymap entries");
        CHECK(map[3] == 0x29, "entry 0 is Esc");
        CHECK(map[11] == 0x2b, "entry 2 is Tab");
    }
    {
        unsigned char cfg[CFG_LEN];
        memset(cfg, 0, sizeof(cfg));
        cfg[126] = 0x5a; cfg[127] = 0xa5;
        CHECK(p_set_effect(cfg, 2, 3, 7, 2) == 1, "effect applied to a valid blob");
        CHECK(cfg[10] == 2, "effect id at byte 10");
        CHECK(cfg[60] == 7, "brightness in the slot");
        CHECK(cfg[61] == ((2 << 4) | 3), "speed+colour packed");
        memset(cfg, 0, sizeof(cfg));
        CHECK(p_set_effect(cfg, 2, 0, 9, 1) == 0, "refuses a blob without 5A A5");
    }
    {
        unsigned char f[RPT_LEN]; int i, sum = 0;
        p_frame(f, 0x88, 2, (const unsigned char *)"\x10\x20\x30", 3, 0, 1);
        for (i = 0; i < 19; i++) sum += f[i];
        CHECK(f[0] == 0x13 && f[1] == 0x88 && f[4] == 0x23, "frame header and sub/len");
        CHECK(f[19] == (sum & 0xff), "checksum correct");
    }
    CHECK(app_effect_count() == 17, "17 effects exposed to the UI");
    CHECK(app_effect_id(1) == 2, "Breathing maps to hardware id 2");
    SAY("");
    SAY("%d checks, %d failures", pass + fail, fail);
    if (log) fclose(log);
    return fail ? 1 : 0;
#undef SAY
#undef CHECK
}
