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

/* forward: the request queue is used by several sections above its definition */
static void enqueue(int kind, int a, int b, int c);
#include <urlmon.h>
#include <winhttp.h>
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
#include "expr.h"
#include <commdlg.h>
#include <shlobj.h>
#include <urlmon.h>
#include <winhttp.h>

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

/* the GUI worker, the imported-effect thread and the daemon can all reach for
   the device; every HID transaction goes through this lock so two writers can
   never interleave frames on one handle */
static CRITICAL_SECTION g_hid_lock;
static int g_hid_lock_ready;

static void hid_enter(void) { if (g_hid_lock_ready) EnterCriticalSection(&g_hid_lock); }
static void hid_leave(void) { if (g_hid_lock_ready) LeaveCriticalSection(&g_hid_lock); }

static Link g_link = {NULL, link_send, link_recv};

static Link *dev(void) {
    g_link.ctx = g_dev;
    return &g_link;
}

static int device_open_locked(void);

static int device_open(void) {
    int r;
    hid_enter();
    r = device_open_locked();
    hid_leave();
    return r;
}

static char g_dev_path[512];   /* last hidraw path we opened, for diagnostics */

const char *app_device_path(void) { return g_dev_path; }

static int device_open_locked(void) {
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
                    WideCharToMultiByte(CP_UTF8, 0, detail->DevicePath, -1, g_dev_path,
                                        sizeof(g_dev_path), NULL, NULL);
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
        WideCharToMultiByte(CP_UTF8, 0, fallback, -1, g_dev_path, sizeof(g_dev_path), NULL, NULL);
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
        hid_enter();
        link_send(g_dev, f, RPT_LEN);
        hid_leave();
        strcpy(g_state.note, "lighting off");
        return;
    }
    hid_enter();
    if (!p_read(dev(), 0x44, 0, cfg, CFG_LEN)) {
        hid_leave();
        strcpy(g_state.note, "could not read the keyboard settings");
        return;
    }
    hid_leave();
    if (cfg[CFG_MAGIC0] != 0x5a || cfg[CFG_MAGIC1] != 0xa5) {
        strcpy(g_state.note, "unexpected settings - press Restore");
        return;
    }
    if (!p_set_effect(cfg, id, g_state.colour, g_state.brightness, g_state.speed)) {
        strcpy(g_state.note, "effect id out of range");
        return;
    }
    hid_enter();
    if (!p_write(dev(), 0x04, 0, cfg, CFG_LEN)) {
        hid_leave();
        strcpy(g_state.note, "the keyboard did not accept the settings");
        return;
    }
    hid_leave();
    g_state.note[0] = 0;
}

static unsigned char g_map_cache[KEYMAP_LEN];
static int g_map_valid;
static int g_layer;              /* 0 = base, 1 = FN */
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
    hid_enter();
    if (!p_write(dev(), 0x01, 0, map, KEYMAP_LEN)) {
        hid_leave();
        strcpy(g_state.note, "keymap restore failed");
        return;
    }
    hid_leave();
    strcpy(g_state.note, "factory keymap restored");
    g_map_valid = 0;
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
    if (GetFileAttributesW(link) != INVALID_FILE_ATTRIBUTES) return;   /* already there */

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




/* ------------------------------------------------------------- updates */
/*
 * The app checks the same GitHub release the installer uses. A download is
 * accepted only if it is a real PE of plausible size; the running exe is then
 * renamed aside (Windows allows that) and replaced, and the app relaunches.
 */
#define APP_VERSION_STR "1.3.0"
static char g_latest[64];

const char *app_version(void) { return APP_VERSION_STR; }
const char *app_latest_tag(void) { return g_latest; }

static int version_gt(const char *a, const char *b) {
    /* dotted numeric compare, ignores a leading v */
    while (*a && (*a < '0' || *a > '9')) a++;
    while (*b && (*b < '0' || *b > '9')) b++;
    for (;;) {
        long x = 0, y = 0;
        while (*a >= '0' && *a <= '9') x = x * 10 + (*a++ - '0');
        while (*b >= '0' && *b <= '9') y = y * 10 + (*b++ - '0');
        if (x != y) return x > y;
        if (*a != '.' && *b != '.') break;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
    return 0;
}

static int file_is_exe(const wchar_t *path) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    int ok = 0;
    if (h != INVALID_HANDLE_VALUE) {
        unsigned char mz[2];
        DWORD got = 0;
        LARGE_INTEGER size;
        if (ReadFile(h, mz, 2, &got, NULL) && got == 2 && mz[0] == 'M' && mz[1] == 'Z' &&
            GetFileSizeEx(h, &size) && size.QuadPart > 40960)
            ok = 1;
        CloseHandle(h);
    }
    return ok;
}

static HINTERNET g_http_session, g_http_connect;

static void http_close_all(void) {
    if (g_http_connect) { WinHttpCloseHandle(g_http_connect); g_http_connect = NULL; }
    if (g_http_session) { WinHttpCloseHandle(g_http_session); g_http_session = NULL; }
}

static HINTERNET http_open(const wchar_t *host, const wchar_t *path) {
    HINTERNET s, c, r;
    s = WinHttpOpen(L"Hi75CPro/1.3", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return NULL;
    WinHttpSetTimeouts(s, 6000, 6000, 15000, 15000);
    c = WinHttpConnect(s, host, 443, 0);
    if (!c) { WinHttpCloseHandle(s); return NULL; }
    r = WinHttpOpenRequest(c, L"GET", path, NULL, WINHTTP_NO_REFERER,
                           WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    g_http_session = s;
    g_http_connect = c;
    if (r) {
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(r, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
        WinHttpAddRequestHeaders(r, L"User-Agent: Hi75CPro", (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    }
    return r;
}

int app_update_check(void) {
    HINTERNET r = http_open(L"api.github.com", L"/repos/scwfyahu/hi75c-pro/releases/latest");
    char buf[8192];
    DWORD got = 0, total = 0;
    const char *tag;

    g_latest[0] = 0;
    if (!r) { http_close_all(); return 0; }
    if (!WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(r, NULL)) {
        WinHttpCloseHandle(r);
        http_close_all();
        return 0;
    }
    while (WinHttpReadData(r, buf + total, (DWORD)(sizeof(buf) - total - 1), &got) && got > 0) {
        total += got;
        if (total >= sizeof(buf) - 1) break;
    }
    buf[total] = 0;
    WinHttpCloseHandle(r);
    http_close_all();

    tag = strstr(buf, "\"tag_name\":");
    if (!tag) return 0;
    {
        const char *q = strchr(tag, '"');
        const char *e;
        if (!q) return 0;
        q++;
        if (*q == 'v') q++;
        e = strchr(q, '"');
        if (!e) return 0;
        if (e - q < (int)sizeof(g_latest)) {
            memcpy(g_latest, q, (size_t)(e - q));
            g_latest[e - q] = 0;
        }
    }
    return version_gt(g_latest, APP_VERSION_STR);
}

void app_update_install(void) { enqueue(9, 0, 0, 0); }

int app_update_apply(void) {
    wchar_t tmp[MAX_PATH], self[MAX_PATH], parked[MAX_PATH];
    DWORD n;
    if (GetTempPathW(MAX_PATH, tmp) == 0) return 0;
    lstrcatW(tmp, L"Hi75CPro-new.exe");
    DeleteFileW(tmp);
    if (FAILED(URLDownloadToFileW(NULL,
            L"https://github.com/scwfyahu/hi75c-pro/releases/latest/download/Hi75CPro.exe",
            tmp, 0, NULL))) return 0;
    if (!file_is_exe(tmp)) { DeleteFileW(tmp); return 0; }

    GetModuleFileNameW(NULL, self, MAX_PATH);
    {
        wchar_t staged[MAX_PATH];
        lstrcpynW(staged, self, MAX_PATH);
        lstrcatW(staged, L".new");
        DeleteFileW(staged);
        if (!CopyFileW(tmp, staged, FALSE)) { DeleteFileW(tmp); return 0; }
        /* the copy beside the target is complete and validated, so the window in
           which no executable exists is two renames wide, not a whole copy */
        lstrcpynW(parked, self, MAX_PATH);
        lstrcatW(parked, L".old");
        DeleteFileW(parked);
        if (!MoveFileW(self, parked)) { DeleteFileW(staged); DeleteFileW(tmp); return 0; }
        if (!MoveFileW(staged, self)) { MoveFileW(parked, self); DeleteFileW(tmp); return 0; }
    }
    DeleteFileW(tmp);
    return 1;
}

/* ------------------------------------------------------- effect files */
/*
   name = Wave
   fps  = 30
   r = 128 + 127*sin(t*2 + x*0.4)
   g = 40
   b = 128 + 127*cos(t*2 + y*0.6)
   variables: t seconds, x/y key position in units, i index, n key count
*/
static HANDLE g_fx_thread;
static volatile LONG g_fx_stop = 1;
static char g_fx_name[64] = "";

const char *app_import_name(void) { return g_fx_name; }

void app_stop_effect(void) {
    InterlockedExchange(&g_fx_stop, 1);
    if (g_fx_thread) {
        WaitForSingleObject(g_fx_thread, 1500);
        CloseHandle(g_fx_thread);
        g_fx_thread = NULL;
    }
}

typedef struct {
    Expr *r, *g, *b;
    int fps;
    char name[64];
} Effect;

static DWORD WINAPI fx_run(LPVOID param) {
    Effect *fx = (Effect *)param;
    unsigned char profile[PROFILE];
    double t0 = (double)GetTickCount64();
    int frame = 0;

    while (!g_fx_stop) {
        double now = ((double)GetTickCount64() - t0) / 1000.0;
        int i;
        memset(profile, 0, sizeof(profile));
        for (i = 0; i < KEY_COUNT; i++) {
            int at = KEYS[i].idx;
            if (at < 0 || at >= 126) continue;
            {
                double v;
                v = expr_eval(fx->r, now, KEYS[i].x, KEYS[i].y, i, KEY_COUNT);
                profile[at] = (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v));
                v = expr_eval(fx->g, now, KEYS[i].x, KEYS[i].y, i, KEY_COUNT);
                profile[126 + at] = (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v));
                v = expr_eval(fx->b, now, KEYS[i].x, KEYS[i].y, i, KEY_COUNT);
                profile[252 + at] = (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v));
            }
        }
        if (!device_open()) { strcpy(g_state.note, "keyboard not found"); break; }
        hid_enter();
        if (!p_write(dev(), 0x02, 0, profile, PROFILE)) {
            hid_leave();
            strcpy(g_state.note, "the keyboard stopped accepting frames");
            break;
        }
        hid_leave();
        frame++;
        Sleep((DWORD)(1000 / (fx->fps > 0 ? fx->fps : 30)));
    }
    expr_free(fx->r); expr_free(fx->g); expr_free(fx->b);
    free(fx);
    return 0;
}

static int load_effect(const char *path, Effect **out, char *err, int errlen) {
    FILE *f = fopen(path, "rb");
    char line[512];
    char er[128] = "", eg[128] = "", eb[128] = "";
    Effect *fx;
    if (!f) { snprintf(err, errlen, "cannot open that file"); return 0; }
    fx = (Effect *)calloc(1, sizeof(Effect));
    fx->fps = 30;
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        char *v, *k = line, *trim;
        if (!eq) continue;
        *eq = 0;
        v = eq + 1;
        trim = k + strlen(k);
        while (trim > k && (trim[-1] == ' ' || trim[-1] == '\t')) *--trim = 0;
        while (*v == ' ' || *v == '\t') v++;
        { char *nl; while ((nl = strpbrk(v, "\r\n")) != NULL) *nl = 0; }
        if (!_stricmp(k, "name")) lstrcpynA(fx->name, v, sizeof(fx->name));
        else if (!_stricmp(k, "fps")) {
            fx->fps = atoi(v);
            if (fx->fps < 1) fx->fps = 30;      /* 1000/0 would fault, 1000/fps<1 spins */
            if (fx->fps > 120) fx->fps = 120;
        }
        else if (!_stricmp(k, "r")) lstrcpynA(er, v, sizeof(er));
        else if (!_stricmp(k, "g")) lstrcpynA(eg, v, sizeof(eg));
        else if (!_stricmp(k, "b")) lstrcpynA(eb, v, sizeof(eb));
    }
    fclose(f);
    if (!er[0]) lstrcpyA(er, "0");
    if (!eg[0]) lstrcpyA(eg, "0");
    if (!eb[0]) lstrcpyA(eb, "0");
    fx->r = expr_compile(er, err, errlen);
    fx->g = expr_compile(eg, err, errlen);
    fx->b = expr_compile(eb, err, errlen);
    if (!fx->r || !fx->g || !fx->b) {
        expr_free(fx->r); expr_free(fx->g); expr_free(fx->b); free(fx);
        return 0;
    }
    *out = fx;
    return 1;
}

void app_import_effect(void *owner) {
    OPENFILENAMEA ofn;
    char path[MAX_PATH] = "";
    Effect *fx = NULL;
    char err[128] = "";

    app_stop_effect();
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = (HWND)owner;
    ofn.lpstrFilter = "Effect files (*.txt)\0*.txt\0All files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameA(&ofn)) return;

    if (!load_effect(path, &fx, err, sizeof(err))) {
        snprintf(g_state.note, sizeof(g_state.note), "import failed: %s", err);
        return;
    }
    lstrcpynA(g_fx_name, fx->name[0] ? fx->name : "imported effect", sizeof(g_fx_name));
    InterlockedExchange(&g_fx_stop, 0);
    g_fx_thread = CreateThread(NULL, 0, fx_run, fx, 0, NULL);
    snprintf(g_state.note, sizeof(g_state.note), "running \"%s\" at %d fps", g_fx_name, fx->fps);
}


/* ------------------------------------------------------------ keymap */

static int g_map_valid;

static void enqueue(int kind, int a, int b, int c);

static int keymap_load(void) {
    if (g_map_valid) return 1;
    if (!device_open()) { strcpy(g_state.note, "keyboard not found"); return 0; }
    hid_enter();
    if (!p_read(dev(), 0x41, g_layer, g_map_cache, KEYMAP_LEN)) {
        hid_leave();
        strcpy(g_state.note, "could not read the keymap");
        return 0;
    }
    hid_leave();
    g_map_valid = 1;
    return 1;
}

int app_keymap_cached(int idx) {
    if (idx < 0 || idx * 4 + 3 >= KEYMAP_LEN || !g_map_valid) return -1;
    return g_map_cache[idx * 4 + 3];
}

void app_keymap_request(void) { enqueue(7, 0, 0, 0); }

void app_keymap_apply(int idx, int usage) { enqueue(8, idx, usage, g_layer); }

void app_stop_background_driver(void) {
    /* the daemon and the window cannot both drive the board; the window wins,
       because the user is looking at it */
    HANDLE held = OpenMutexA(SYNCHRONIZE, FALSE, "Hi75CPro_Daemon_Singleton");
    if (!held) return;
    CloseHandle(held);
    {
        HANDLE stop = OpenEventA(EVENT_MODIFY_STATE, FALSE, "Hi75CPro_Daemon_Stop");
        if (stop) {
            SetEvent(stop);
            CloseHandle(stop);
            Sleep(300);        /* let it finish the frame it is on */
        }
    }
}

int app_keymap_get(int idx) {
    if (idx < 0 || idx * 4 + 3 >= KEYMAP_LEN) return -1;
    if (!keymap_load()) return -1;
    return g_map_cache[idx * 4 + 3];
}

int app_keymap_set(int idx, int type, int usage) {
    if (idx < 0 || idx * 4 + 3 >= KEYMAP_LEN) return 0;
    if (!keymap_load()) return 0;
    g_map_cache[idx * 4 + 0] = (unsigned char)(type & 0xff);
    g_map_cache[idx * 4 + 1] = 0;
    g_map_cache[idx * 4 + 2] = 0;
    g_map_cache[idx * 4 + 3] = (unsigned char)(usage & 0xff);
    hid_enter();
    if (!p_write(dev(), 0x01, g_layer, g_map_cache, KEYMAP_LEN)) {
        hid_leave();
        strcpy(g_state.note, "the keyboard rejected the keymap");
        return 0;
    }
    hid_leave();
    snprintf(g_state.note, sizeof(g_state.note), "%s key %d set to 0x%02x",
             g_layer ? "FN" : "base", idx, usage & 0xff);
    return 1;
}

void app_clear_note(void) { g_state.note[0] = 0; }

void app_set_layer(int layer) { g_layer = layer ? 1 : 0; g_map_valid = 0; }
int app_layer(void) { return g_layer; }


/* ------------------------------------------------------- remembered state */
/* lighting and keymap live in the keyboard; only the app's own view state
   needs remembering, so it goes in a tiny ini under %LOCALAPPDATA% */
static void state_path(char *out, int cap) {
    char dir[MAX_PATH] = {0};
    out[0] = 0;

    /* the shell folder call can fail (seen under a minimal Windows session),
       and when it does the app used to save nothing at all; try the
       environment first and fall back to the folder beside the executable */
    if (!GetEnvironmentVariableA("LOCALAPPDATA", dir, MAX_PATH) || !dir[0]) {
        if (FAILED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, dir)) || !dir[0]) {
            DWORD n = GetModuleFileNameA(NULL, dir, MAX_PATH);
            if (!n) return;
            {
                char *slash = strrchr(dir, '\\');
                if (slash) *slash = 0; else return;
            }
            lstrcatA(dir, "\\state");
            CreateDirectoryA(dir, NULL);
            snprintf(out, cap, "%s\\state.ini", dir);
            return;
        }
    }
    lstrcatA(dir, "\\Hi75C Pro");
    CreateDirectoryA(dir, NULL);
    snprintf(out, cap, "%s\\state.ini", dir);
}

void app_save_state(int wx, int wy, int ww, int wh) {
    char path[MAX_PATH];
    FILE *f;
    state_path(path, MAX_PATH);
    if (!path[0]) return;
    f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "effect=%d\ncolour=%d\nbrightness=%d\nspeed=%d\nwindow=%d,%d,%d,%d\n",
            g_state.effect, g_state.colour, g_state.brightness, g_state.speed, wx, wy, ww, wh);
    fclose(f);
}

void app_load_state(int *wx, int *wy, int *ww, int *wh) {
    char path[MAX_PATH], line[256];
    FILE *f;
    state_path(path, MAX_PATH);
    if (!path[0]) return;
    f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        int a, b, c, d;
        if (sscanf(line, "effect=%d", &a) == 1) {
            /* only accept ids that exist; anything else would be written to the
               board as a bogus slot */
            int i, okid = (a == 0);
            for (i = 0; i < EFFECT_COUNT && !okid; i++)
                if (atoi(EFFECTS[i][1]) == a) okid = 1;
            g_state.effect = okid ? a : 0;
        }
        else if (sscanf(line, "colour=%d", &a) == 1) {
            g_state.colour = (a >= 0 && a <= 6) ? a : 4;
        }
        else if (sscanf(line, "brightness=%d", &a) == 1) {
            g_state.brightness = (a >= 0 && a <= 9) ? a : 5;
        }
        else if (sscanf(line, "speed=%d", &a) == 1) {
            g_state.speed = (a >= 0 && a <= 3) ? a : 1;
        }
        else if (sscanf(line, "window=%d,%d,%d,%d", &a, &b, &c, &d) == 4 && wx) {
            *wx = a; *wy = b; *ww = c; *wh = d;
        }
    }
    fclose(f);
}


/* --------------------------------------------------- worker thread */
/*
 * HID traffic must never run on the UI thread: a read can wait 2.5 s and a
 * bulk write paces at 120 ms, which makes the window look dead if a click does
 * it inline. Requests are queued here and executed on one worker thread.
 */
typedef struct { int kind, a, b, c; } Req;   /* kind: 1 effect 2 colour 3 bright 4 speed 5 restore 6 rescan */

static int keymap_load(void);
static void enqueue(int kind, int a, int b, int c);
static Req g_q[64];
static int g_q_head, g_q_tail;
static HANDLE g_wake, g_worker;
static HWND g_notify;
static CRITICAL_SECTION g_q_lock;

static void enqueue(int kind, int a, int b, int c) {
    EnterCriticalSection(&g_q_lock);
    /* a slider drag produces one request per pixel; keep only the newest value
       for the four continuous controls, otherwise the queue fills and the last
       (correct) value can be dropped */
    if (kind >= 1 && kind <= 4 && g_q_head != g_q_tail) {
        int last = (g_q_tail + 63) % 64;
        if (g_q[last].kind == kind) {
            g_q[last].a = a; g_q[last].b = b; g_q[last].c = c;
            LeaveCriticalSection(&g_q_lock);
            if (g_wake) SetEvent(g_wake);
            return;
        }
    }
    if (((g_q_tail + 1) % 64) != g_q_head) {
        g_q[g_q_tail].kind = kind; g_q[g_q_tail].a = a;
        g_q[g_q_tail].b = b; g_q[g_q_tail].c = c;
        g_q_tail = (g_q_tail + 1) % 64;
    }
    LeaveCriticalSection(&g_q_lock);
    if (g_wake) SetEvent(g_wake);
}

static DWORD WINAPI worker_main(LPVOID unused) {
    (void)unused;
    for (;;) {
        Req r;
        int have = 0;
        WaitForSingleObject(g_wake, INFINITE);
        for (;;) {
            EnterCriticalSection(&g_q_lock);
            if (g_q_head != g_q_tail) {
                r = g_q[g_q_head];
                g_q_head = (g_q_head + 1) % 64;
                have = 1;
            } else {
                have = 0;
            }
            LeaveCriticalSection(&g_q_lock);
            if (!have) break;
            switch (r.kind) {
                case 1: g_state.effect = r.a; apply_effect(); break;
                case 2: g_state.colour = r.a; apply_effect(); break;
                case 3: g_state.brightness = r.a; apply_effect(); break;
                case 4: g_state.speed = r.a; apply_effect(); break;
                case 5: g_map_valid = 0; restore_keymap(); break;
                case 7: keymap_load(); break;
                case 9:   /* download + swap off the UI thread */
                    if (app_update_apply())
                        strcpy(g_state.note, "update installed - restart the app");
                    else
                        strcpy(g_state.note, "update failed - download the new build manually");
                    break;   /* refresh the cache off the UI thread */
                case 8:                                    /* remap without blocking */
                    {
                        int want = r.c, save = g_layer;
                        if (want != save) { g_layer = want; g_map_valid = 0; }
                        app_keymap_set(r.a, 0, r.b);
                        if (want != save) { g_layer = save; g_map_valid = 0; }
                    }
                    break;
                case 6:
                    hid_enter();
                    if (g_dev != INVALID_HANDLE_VALUE) { CloseHandle(g_dev); g_dev = INVALID_HANDLE_VALUE; }
                    hid_leave();
                    g_map_valid = 0;
                    g_state.connected = device_open();
                    if (!g_state.connected) strcpy(g_state.note, "keyboard not found");
                    else g_state.note[0] = 0;
                    break;
            }
            if (g_notify) InvalidateRect(g_notify, NULL, FALSE);
        }
    }
    return 0;
}

void app_pump(void *hwnd) {
    g_notify = (HWND)hwnd;
    if (g_wake) return;
    InitializeCriticalSection(&g_q_lock);
    g_wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_worker = CreateThread(NULL, 0, worker_main, NULL, 0, NULL);
}

/* ------------------------------------------------------------ interface */

void app_init(void) {
    InitializeCriticalSection(&g_lock);
    if (!g_hid_lock_ready) { InitializeCriticalSection(&g_hid_lock); g_hid_lock_ready = 1; }
    register_shortcut();
    EnterCriticalSection(&g_lock);
    g_state.connected = device_open();
    LeaveCriticalSection(&g_lock);
}

void app_shutdown(void) {
    hid_enter();
    if (g_dev != INVALID_HANDLE_VALUE) { CloseHandle(g_dev); g_dev = INVALID_HANDLE_VALUE; }
    hid_leave();
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

void app_set_effect(int hw) { enqueue(1, hw, 0, 0); }
void app_set_colour(int c) { enqueue(2, c, 0, 0); }
void app_set_brightness(int b) { enqueue(3, b, 0, 0); }
void app_set_speed(int s) { enqueue(4, s, 0, 0); }
void app_restore_keymap(void) { enqueue(5, 0, 0, 0); }
void app_rescan(void) { enqueue(6, 0, 0, 0); }

/* headless check of the importer: load a file, evaluate the first frame */
int app_effect_test(const char *path) {
    Effect *fx = NULL;
    char err[128] = "";
    int i, printed = 0;
    if (!load_effect(path, &fx, err, sizeof(err))) {
        printf("FAIL import: %s\n", err);
        return 1;
    }
    printf("ok   imported \"%s\" at %d fps\n", fx->name, fx->fps);
    for (i = 0; i < KEY_COUNT && printed < 4; i += 20) {
        double r = expr_eval(fx->r, 0.0, KEYS[i].x, KEYS[i].y, i, KEY_COUNT);
        double g = expr_eval(fx->g, 0.0, KEYS[i].x, KEYS[i].y, i, KEY_COUNT);
        double b = expr_eval(fx->b, 0.0, KEYS[i].x, KEYS[i].y, i, KEY_COUNT);
        printf("ok   key %-6s rgb(%.0f, %.0f, %.0f)\n", KEYS[i].label, r, g, b);
        printed++;
    }
    expr_free(fx->r); expr_free(fx->g); expr_free(fx->b); free(fx);
    printf("3 checks, 0 failures\n");
    return 0;
}

/* Runs an effect file with no user interface at all. Spawned by the app when
   "keep running after the window closes" is set; exits when the file is gone
   or the keyboard disappears. */
static int g_daemon_dry;
static HANDLE g_daemon_stop;

int app_daemon(const char *path) {
    /* a second daemon would interleave frames with the first on the same
       handle; one per machine is the only safe number */
    HANDLE once = CreateMutexA(NULL, TRUE, "Hi75CPro_Daemon_Singleton");
    if (once && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(once);
        return 4;
    }
    g_daemon_dry = GetEnvironmentVariableA("HI75C_DAEMON_DRY", NULL, 0) > 0;
    Effect *fx = NULL;
    char err[128] = "";
    unsigned char profile[PROFILE];
    double t0;

    if (!load_effect(path, &fx, err, sizeof(err))) return 2;
    {
        HANDLE stop = CreateEventA(NULL, TRUE, FALSE, "Hi75CPro_Daemon_Stop");
        g_daemon_stop = stop;
    }
    t0 = (double)GetTickCount64();
    for (;;) {
        double now = ((double)GetTickCount64() - t0) / 1000.0;
        int i;
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) break;
        if (g_daemon_stop && WaitForSingleObject(g_daemon_stop, 0) == WAIT_OBJECT_0) {
            const char *lg = getenv("HI75C_DAEMON_LOG");
            if (lg) { FILE *lf = fopen(lg, "a"); if (lf) { fputs("stopped by window\n", lf); fclose(lf); } }
            break;
        }
        memset(profile, 0, sizeof(profile));
        for (i = 0; i < KEY_COUNT; i++) {
            int at = KEYS[i].idx;
            double v;
            if (at < 0 || at >= 126) continue;
            v = expr_eval(fx->r, now, KEYS[i].x, KEYS[i].y, i, KEY_COUNT);
            profile[at] = (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v));
            v = expr_eval(fx->g, now, KEYS[i].x, KEYS[i].y, i, KEY_COUNT);
            profile[126 + at] = (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v));
            v = expr_eval(fx->b, now, KEYS[i].x, KEYS[i].y, i, KEY_COUNT);
            profile[252 + at] = (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v));
        }
        if (!g_daemon_dry) {
            if (!device_open()) break;
            hid_enter();
        if (!p_write(dev(), 0x02, 0, profile, PROFILE)) { hid_leave(); break; }
        hid_leave();
        }
        Sleep((DWORD)(1000 / (fx->fps > 0 ? fx->fps : 30)));
    }
    if (g_dev != INVALID_HANDLE_VALUE) CloseHandle(g_dev);
    return 0;
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
