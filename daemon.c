/*
 * hi75c-daemon — the background effect runner, built to be as small as a
 * Windows process can be: no C runtime, no GDI, no COM, no heap growth.
 * Entry point is raw; every buffer is static.
 */
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>

#define RPT 0x13
#define RPT_LEN 20
#define PROFILE 378

static HANDLE g_dev = INVALID_HANDLE_VALUE;
static unsigned char g_profile[PROFILE];
static HANDLE g_stop_event;

/* a 256-entry sine table keeps libm out of the binary */
static signed char g_sin[256];

static void build_sin(void) {
    /* sin(a) ~ (4/pi)*a - (4/pi^2)*a*|a| on [-pi, pi], scaled to -127..127.
       The constants matter: using pi/4 and 1/(2pi) here produced a 227-step
       jump at the zero crossing, which made the board strobe instead of fade. */
    const double PI = 3.14159265358979323846;
    const double K1 = 1.2732395447351627;   /* 4/pi     */
    const double K2 = 0.4052847345693511;   /* 4/pi^2   */
    int i;
    for (i = 0; i < 256; i++) {
        double a = (double)i * 2.0 * PI / 256.0;
        double x = a > PI ? a - 2.0 * PI : a;
        double y = K1 * x - K2 * x * (x < 0 ? -x : x);
        int v = (int)(y * 127.0 + (y < 0 ? -0.5 : 0.5));
        if (v > 127) v = 127;
        if (v < -127) v = -127;
        g_sin[i] = (signed char)v;
    }
}

static int frame_ok(const unsigned char *buf) {
    int i, sum = 0;
    if (buf[0] != RPT) return 0;
    for (i = 0; i < RPT_LEN - 1; i++) sum += buf[i];
    return buf[RPT_LEN - 1] == (unsigned char)(sum & 0xff);
}

static void frame_build(unsigned char *f, int cmd, int sub, const unsigned char *p, int len,
                        int index, int count) {
    int i, sum = 0;
    f[0] = RPT;
    f[1] = (unsigned char)cmd;
    f[2] = (unsigned char)count;
    f[3] = (unsigned char)index;
    f[4] = (unsigned char)(((sub & 0xf) << 4) | (len & 0xf));
    for (i = 0; i < len && i < 14; i++) f[5 + i] = p[i];
    for (i = 5 + len; i < 19; i++) f[i] = 0;
    for (i = 0; i < 19; i++) sum += f[i];
    f[19] = (unsigned char)(sum & 0xff);
}

static int open_device(void) {
    GUID guid;
    HDEVINFO set;
    SP_DEVICE_INTERFACE_DATA ifd;
    DWORD i = 0;

    HidD_GetHidGuid(&guid);
    set = SetupDiGetClassDevsW(&guid, 0, 0, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return 0;
    ifd.cbSize = sizeof(ifd);
    while (SetupDiEnumDeviceInterfaces(set, 0, &guid, i++, &ifd)) {
        DWORD need = 0;
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail;
        HANDLE probe;
        HIDD_ATTRIBUTES attrs;
        PHIDP_PREPARSED_DATA prep = 0;
        HIDP_CAPS caps;
        int ok = 0;

        SetupDiGetDeviceInterfaceDetailW(set, &ifd, 0, 0, &need, 0);
        if (!need) continue;
        detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)HeapAlloc(GetProcessHeap(), 0, need);
        if (!detail) break;
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (SetupDiGetDeviceInterfaceDetailW(set, &ifd, detail, need, 0, 0)) {
            probe = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0,
                                OPEN_EXISTING, 0, 0);
            if (probe != INVALID_HANDLE_VALUE) {
                attrs.Size = sizeof(attrs);
                if (HidD_GetAttributes(probe, &attrs) &&
                    (attrs.VendorID == 0x258A || attrs.VendorID == 0x3554) &&
                    HidD_GetPreparsedData(probe, &prep) &&
                    HidP_GetCaps(prep, &caps) == HIDP_STATUS_SUCCESS &&
                    caps.OutputReportByteLength == RPT_LEN && caps.UsagePage >= 0xFF00) {
                    g_dev = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE, 0,
                                        OPEN_EXISTING, 0, 0);
                    ok = (g_dev != INVALID_HANDLE_VALUE);
                }
                if (prep) HidD_FreePreparsedData(prep);
                CloseHandle(probe);
            }
        }
        HeapFree(GetProcessHeap(), 0, detail);
        if (ok) break;
    }
    SetupDiDestroyDeviceInfoList(set);
    return g_dev != INVALID_HANDLE_VALUE;
}

static int send_frame(const unsigned char *f) {
    DWORD written = 0;
    if (g_dev == INVALID_HANDLE_VALUE) return 0;
    if (HidD_SetOutputReport(g_dev, (PVOID)f, RPT_LEN)) return 1;
    return WriteFile(g_dev, f, RPT_LEN, &written, 0) && written == RPT_LEN;
}

/* write one 378-byte profile as 27 acknowledged packets */
static int send_profile(void) {
    unsigned char f[RPT_LEN];
    int off = 0, index = 0, count = (PROFILE + 13) / 14;
    while (index < count) {
        int chunk = PROFILE - off, attempt, acked = 0;
        if (chunk > 14) chunk = 14;
        frame_build(f, 0x02, 0, g_profile + off, chunk, index, count);
        for (attempt = 0; attempt < 2 && !acked; attempt++) {
            unsigned char buf[64];
            DWORD got = 0;
            OVERLAPPED ov;
            int waited = 0;
            memset(&ov, 0, sizeof(ov));
            if (!send_frame(f)) return 0;
            /* the board echoes each packet; wait briefly for it */
            while (waited < 60) {
                ov.hEvent = CreateEventW(0, TRUE, FALSE, 0);
                if (!ov.hEvent) break;
                if (!ReadFile(g_dev, buf, sizeof(buf), &got, &ov) &&
                    GetLastError() == ERROR_IO_PENDING) {
                    if (WaitForSingleObject(ov.hEvent, 5) == WAIT_OBJECT_0)
                        GetOverlappedResult(g_dev, &ov, &got, FALSE);
                    else got = 0;
                }
                CloseHandle(ov.hEvent);
                waited += 5;
                if (got >= RPT_LEN && frame_ok(buf) && (buf[1] & 0x3f) == 0x02 &&
                    buf[3] == (unsigned char)index) { acked = 1; break; }
            }
        }
        if (!acked) return 0;
        off += chunk;
        index++;
    }
    return 1;
}

static void daemon_main(void) {
    unsigned phase = 0;
    int i;
    build_sin();
    {
        char dry[8];
        if (GetEnvironmentVariableA("HI75C_DAEMON_DRY", dry, sizeof(dry)) > 0) {
            for (;;) {                                  /* measurement only */
                if (g_stop_event && WaitForSingleObject(g_stop_event, 0) == WAIT_OBJECT_0)
                    ExitProcess(0);                     /* stoppable like the real loop */
                send_profile();
                Sleep(33);
            }
        }
    }
    if (!open_device()) ExitProcess(2);
    for (;;) {
        for (i = 0; i < 126; i++) {
            unsigned char v = (unsigned char)(g_sin[(phase + i * 3) & 255] + 128);
            g_profile[i] = v;
            g_profile[126 + i] = (unsigned char)(g_sin[(phase + i * 5 + 85) & 255] + 128);
            g_profile[252 + i] = (unsigned char)(g_sin[(phase + i * 7 + 170) & 255] + 128);
        }
        if (g_stop_event && WaitForSingleObject(g_stop_event, 0) == WAIT_OBJECT_0) break;
        if (!send_profile()) break;
        phase += 4;
        Sleep(33);
    }
    ExitProcess(0);
}

/* normal entry so the mingw startup is satisfied; nothing else uses the CRT */
int WINAPI WinMain(HINSTANCE a, HINSTANCE b, LPSTR c, int d) {
    {
        /* one driver per machine: a second copy would interleave frames */
        HANDLE once = CreateMutexA(NULL, TRUE, "Hi75CPro_Daemon_Singleton");
        if (once && GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(once);
            return 4;
        }
        g_stop_event = CreateEventA(NULL, TRUE, FALSE, "Hi75CPro_Daemon_Stop");
    }
    (void)a; (void)b; (void)c; (void)d;
    daemon_main();
    return 0;
}
