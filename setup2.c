/*
 * Hi75C Pro bootstrapper. Downloads the newest published build from GitHub and
 * installs it; it carries no payload of its own, so it can never be stale.
 *   https://api.github.com/repos/scwfyahu/hi75c-pro/releases/latest
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>

#define REPO L"scwfyahu/hi75c-pro"

static int g_silent;

static char *http_get(const wchar_t *path, DWORD *len) {
    HINTERNET s, c, r;
    char *buf = NULL;
    DWORD got = 0, total = 0;
    *len = 0;
    s = WinHttpOpen(L"Hi75CPro-Setup/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return NULL;
    WinHttpSetTimeouts(s, 8000, 8000, 20000, 20000);
    {
        DWORD tls = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &tls, sizeof(tls));
    }
    c = WinHttpConnect(s, L"api.github.com", 443, 0);
    if (!c) { WinHttpCloseHandle(s); return NULL; }
    r = WinHttpOpenRequest(c, L"GET", path, NULL, WINHTTP_NO_REFERER,
                           WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (r) {
        WinHttpAddRequestHeaders(r, L"User-Agent: Hi75CPro-Setup", (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
        WinHttpAddRequestHeaders(r, L"Accept: application/vnd.github+json", (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
        if (WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(r, NULL)) {
            buf = (char *)malloc(1);
            do {
                char chunk[8192];
                got = 0;
                if (!WinHttpReadData(r, chunk, sizeof(chunk), &got)) break;
                if (got) {
                    char *n = (char *)realloc(buf, total + got + 1);
                    if (!n) break;
                    buf = n;
                    memcpy(buf + total, chunk, got);
                    total += got;
                }
            } while (got > 0);
            if (buf) { buf[total] = 0; *len = total; }
        }
        WinHttpCloseHandle(r);
    }
    WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return buf;
}

/* first "browser_download_url" ending in .exe */
static int find_asset(const char *json, wchar_t *out, int cap) {
    const char *p = json;
    while ((p = strstr(p, "browser_download_url")) != NULL) {
        const char *q = strchr(p, '"');
        if (!q) break;
        q++;
        while (*q && *q != '"' && *q != ':') q++;
        if (*q == ':') q++;
        if (*q == '"') q++;
        {
            const char *end = strchr(q, '"');
            if (end && end - q > 4 && _strnicmp(end - 4, ".exe", 4) == 0) {
                MultiByteToWideChar(CP_UTF8, 0, q, (int)(end - q), out, cap);
                out[end - q] = 0;
                return 1;
            }
        }
        p += 18;
    }
    return 0;
}

static int download(const wchar_t *url, const wchar_t *dest) {
    HINTERNET s, c, r;
    URL_COMPONENTS uc;
    wchar_t host[256], path[1024];
    HANDLE f;
    int ok = 0;
    DWORD secure = 0;

    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 1024;
    if (!WinHttpCrackUrl(url, 0, 0, &uc)) return 0;

    s = WinHttpOpen(L"Hi75CPro-Setup/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return 0;
    WinHttpSetTimeouts(s, 8000, 8000, 30000, 30000);
    {
        DWORD tls = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &tls, sizeof(tls));
    }
    c = WinHttpConnect(s, host, uc.nPort, 0);
    if (c) {
        r = WinHttpOpenRequest(c, L"GET", path, NULL, WINHTTP_NO_REFERER,
                               WINHTTP_DEFAULT_ACCEPT_TYPES,
                               uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
        if (r) {
            WinHttpAddRequestHeaders(r, L"User-Agent: Hi75CPro-Setup", (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
            if (WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(r, NULL)) {
                f = CreateFileW(dest, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                if (f != INVALID_HANDLE_VALUE) {
                    DWORD got = 0;
                    ok = 1;
                    do {
                        char buf[16384];
                        DWORD put = 0;
                        got = 0;
                        if (!WinHttpReadData(r, buf, sizeof(buf), &got)) { ok = 0; break; }
                        if (got && (!WriteFile(f, buf, got, &put, NULL) || put != got)) { ok = 0; break; }
                    } while (got > 0);
                    CloseHandle(f);
                }
            }
            WinHttpCloseHandle(r);
        }
        WinHttpCloseHandle(c);
    }
    WinHttpCloseHandle(s);
    (void)secure;
    return ok;
}

static void shortcut(const wchar_t *exe) {
    wchar_t link[MAX_PATH], prog[MAX_PATH];
    IShellLinkW *lnk = NULL;
    IPersistFile *file = NULL;
    if (!SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, prog))) return;
    _snwprintf(link, MAX_PATH, L"%s\\Hi75C Pro.lnk", prog);
    if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&lnk))) return;
    lnk->lpVtbl->SetPath(lnk, exe);
    lnk->lpVtbl->SetDescription(lnk, L"LEOBOG Hi75C Pro");
    if (SUCCEEDED(lnk->lpVtbl->QueryInterface(lnk, &IID_IPersistFile, (void **)&file))) {
        file->lpVtbl->Save(file, link, TRUE);
        file->lpVtbl->Release(file);
    }
    lnk->lpVtbl->Release(lnk);
}

int WINAPI wWinMain(HINSTANCE i, HINSTANCE p, LPWSTR cmd, int show) {
    char *json; DWORD len = 0;
    wchar_t url[2048], dir[MAX_PATH], target[MAX_PATH], tmp[MAX_PATH];
    int ok = 0;
    (void)i; (void)p; (void)show;
    g_silent = cmd && wcsstr(cmd, L"--silent");
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (!g_silent) ShowWindow(GetConsoleWindow(), SW_HIDE);

    {
        wchar_t api[256];
        swprintf(api, 256, L"/repos/%s/releases/latest", REPO);
        json = http_get(api, &len);
    }
    if (!json || !find_asset(json, url, 2048)) {
        /* the API can be rate limited or blocked; this URL always points at the
           newest upload of that asset name */
        lstrcpynW(url, L"https://github.com/" REPO L"/releases/latest/download/Hi75CPro.exe", 2048);
    }
    {
        if (!SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, dir)) ok = 0;
        lstrcatW(dir, L"\\Hi75C Pro");
        CreateDirectoryW(dir, NULL);
        _snwprintf(target, MAX_PATH, L"%s\\Hi75CPro.exe", dir);
        GetTempPathW(MAX_PATH, tmp);
        lstrcatW(tmp, L"hi75c-download.exe");
        if (download(url, tmp) && MoveFileExW(tmp, target, MOVEFILE_REPLACE_EXISTING)) {
            shortcut(target);
            ok = 1;
        }
        if (g_silent) {
            wchar_t done[MAX_PATH];
            _snwprintf(done, MAX_PATH, L"%s\\installed.txt", dir);
            HANDLE h = CreateFileW(done, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
            if (h != INVALID_HANDLE_VALUE) { DWORD put; WriteFile(h, url, (DWORD)(wcslen(url) * 2), &put, NULL); CloseHandle(h); }
        } else if (ok) {
            ShellExecuteW(NULL, L"open", target, NULL, NULL, SW_SHOWNORMAL);
        }
    }
    free(json);
    if (g_silent) return ok ? 0 : 2;
    MessageBoxW(NULL, ok ? L"Installed the newest published build.\n\nA Start Menu shortcut was created."
                         : L"Could not reach GitHub. Check your connection and run Setup again.",
                L"Hi75C Pro", ok ? MB_ICONINFORMATION : MB_ICONERROR);
    CoUninitialize();
    return 0;
}
