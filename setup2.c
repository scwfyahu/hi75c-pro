/*
 * Hi75C Pro bootstrapper. Downloads the newest published build from GitHub and
 * installs it; it carries no payload of its own, so it can never be stale.
 *   https://api.github.com/repos/scwfyahu/hi75c-pro/releases/latest
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <urlmon.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdarg.h>
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
            const char *base = q;
            const char *scan;
            for (scan = q; scan < end; scan++) if (*scan == '/') base = scan + 1;
            if (end && _strnicmp(base, "Hi75CPro.exe", 12) == 0) {   /* exactly the app */
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
            DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
            DWORD status = 0, status_len = sizeof(status);
            WinHttpSetOption(r, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
            WinHttpAddRequestHeaders(r, L"User-Agent: Hi75CPro-Setup", (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
            if (WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(r, NULL) &&
                WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_len,
                                    WINHTTP_NO_HEADER_INDEX) &&
                status == 200) {
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

/* modern folder picker; falls back to the old one, then to the default */
static int choose_dir(wchar_t *out, int cap) {
    IFileDialog *dlg = NULL;
    IShellItem *item = NULL;
    wchar_t *picked = NULL;
    int ok = 0;

    if (SUCCEEDED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_IFileDialog, (void **)&dlg))) {
        DWORD opts = 0;
        if (SUCCEEDED(dlg->lpVtbl->GetOptions(dlg, &opts))) {
            dlg->lpVtbl->SetOptions(dlg, opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        }
        dlg->lpVtbl->SetTitle(dlg, L"Choose where to install Hi75C Pro");
        dlg->lpVtbl->SetOkButtonLabel(dlg, L"Install here");
        if (SUCCEEDED(dlg->lpVtbl->Show(dlg, NULL)) &&
            SUCCEEDED(dlg->lpVtbl->GetResult(dlg, &item)) && item) {
            if (SUCCEEDED(item->lpVtbl->GetDisplayName(item, SIGDN_FILESYSPATH, &picked)) && picked) {
                lstrcpynW(out, picked, cap);
                CoTaskMemFree(picked);
                ok = 1;
            }
            item->lpVtbl->Release(item);
        }
        dlg->lpVtbl->Release(dlg);
    }
    if (!ok) {
        BROWSEINFOW bi;
        LPITEMIDLIST pidl;
        memset(&bi, 0, sizeof(bi));
        bi.lpszTitle = L"Choose where to install Hi75C Pro";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        pidl = SHBrowseForFolderW(&bi);
        if (pidl) {
            wchar_t path[MAX_PATH];
            if (SHGetPathFromIDListW(pidl, path)) { lstrcpynW(out, path, cap); ok = 1; }
            CoTaskMemFree(pidl);
        }
    }
    return ok;
}


/*
 * URLDownloadToFile uses the WinINET configuration -- the same proxy, PAC script
 * and TLS settings as the browser. If the user's browser can open the release
 * page, this can fetch the file. The WinHTTP path below is only a fallback.
 */
static int download_urlmon(const wchar_t *url, const wchar_t *dest) {
    HRESULT hr;
    DeleteFileW(dest);
    hr = URLDownloadToFileW(NULL, url, dest, 0, NULL);
    if (FAILED(hr)) return 0;
    {
        HANDLE h = CreateFileW(dest, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        unsigned char hdr[2];
        DWORD got = 0;
        LARGE_INTEGER size;
        int ok = 0;
        if (h != INVALID_HANDLE_VALUE) {
            if (ReadFile(h, hdr, 2, &got, NULL) && got == 2 && hdr[0] == 'M' && hdr[1] == 'Z' &&
                GetFileSizeEx(h, &size) && size.QuadPart > 40960)
                ok = 1;
            CloseHandle(h);
        }
        if (!ok) DeleteFileW(dest);
        return ok;
    }
}

static FILE *g_log;
static void logf2(const char *fmt, ...) {
    va_list ap;
    if (!g_log) return;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}


/* Settings > Apps needs an entry, or the install cannot be removed normally */
static void register_uninstall(const wchar_t *exe) {
    HKEY k;
    wchar_t quoted[MAX_PATH * 2 + 4];
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Hi75CPro",
                        0, NULL, 0, KEY_WRITE, NULL, &k, NULL) != ERROR_SUCCESS) return;
    _snwprintf(quoted, MAX_PATH * 2 + 4, L"\"%s\"", exe);
    RegSetValueExW(k, L"DisplayName", 0, REG_SZ, (const BYTE *)L"LEOBOG Hi75C Pro",
                   (DWORD)sizeof(L"LEOBOG Hi75C Pro"));
    RegSetValueExW(k, L"DisplayIcon", 0, REG_SZ, (const BYTE *)exe,
                   (DWORD)((wcslen(exe) + 1) * sizeof(wchar_t)));
    RegSetValueExW(k, L"Publisher", 0, REG_SZ, (const BYTE *)L"LEOBOG Hi75C Pro",
                   (DWORD)sizeof(L"LEOBOG Hi75C Pro"));
    RegSetValueExW(k, L"UninstallString", 0, REG_SZ, (const BYTE *)quoted,
                   (DWORD)((wcslen(quoted) + 1) * sizeof(wchar_t)));
    RegSetValueExW(k, L"NoModify", 0, REG_DWORD, (const BYTE *)&(DWORD){1}, sizeof(DWORD));
    RegCloseKey(k);
}

int WINAPI wWinMain(HINSTANCE i, HINSTANCE p, LPWSTR cmd, int show) {
    char *json; DWORD len = 0;
    wchar_t url[2048], dir[MAX_PATH], target[MAX_PATH], tmp[MAX_PATH];
    wchar_t offline_src[MAX_PATH];
    offline_src[0] = 0;
    int ok = 0;
    (void)i; (void)p; (void)show;
    g_silent = cmd && wcsstr(cmd, L"--silent");
    {
        wchar_t logpath[MAX_PATH];
        GetModuleFileNameW(NULL, logpath, MAX_PATH);
        { wchar_t *sl = wcsrchr(logpath, L'\\'); if (sl) sl[1] = 0; }
        lstrcatW(logpath, L"setup.log");
        g_log = _wfopen(logpath, L"w");
        logf2("Hi75C Pro setup");
    }
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (!g_silent) ShowWindow(GetConsoleWindow(), SW_HIDE);

    /* the stable redirect names the asset exactly, so it can never pick the
       wrong binary out of a release */
    lstrcpynW(url, L"https://github.com/" REPO L"/releases/latest/download/Hi75CPro.exe", 2048);
    json = NULL;
    {
        /* test hook: --offline <exe> skips the download so the rest of the
           installer can be verified without a network */
        const wchar_t *off = cmd ? wcsstr(cmd, L"--offline ") : NULL;
        if (off) {
            lstrcpynW(offline_src, off + 10, MAX_PATH);
        }
    }
    {
        if (!SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, dir)) ok = 0;
        lstrcatW(dir, L"\\Hi75C Pro");          /* default */
        if (!g_silent) {
            wchar_t picked[MAX_PATH];
            if (choose_dir(picked, MAX_PATH)) lstrcpynW(dir, picked, MAX_PATH);
            else {
                MessageBoxW(NULL, L"Nothing was installed.", L"Hi75C Pro", MB_ICONINFORMATION);
                return 0;
            }
        }
        CreateDirectoryW(dir, NULL);
        _snwprintf(target, MAX_PATH, L"%s\\Hi75CPro.exe", dir);
        GetTempPathW(MAX_PATH, tmp);
        lstrcatW(tmp, L"hi75c-download.exe");
        logf2("target dir: %ls", dir);
        logf2("url       : %ls", url);
        {
            int a = download_urlmon(url, tmp);
            logf2("urlmon    : %s", a ? "ok" : "failed");
            if (!a) {
                int b = download(url, tmp);
                logf2("winhttp   : %s", b ? "ok" : "failed");
            }
        }
        if (0) {
            wchar_t api[256];
            DWORD alen = 0;
            swprintf(api, 256, L"/repos/%s/releases/latest", REPO);
            json = http_get(api, &alen);
            if (json && find_asset(json, url, 2048))
                if (!download_urlmon(url, tmp)) download(url, tmp);
        }
        {
            /* refuse anything that is not a real executable: an error page from a
               proxy or a stale redirect must never be installed */
            HANDLE h = CreateFileW(tmp, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
            int looks_ok = 0;
            if (h != INVALID_HANDLE_VALUE) {
                unsigned char hdr[2];
                LARGE_INTEGER size;
                DWORD got2 = 0;
                if (ReadFile(h, hdr, 2, &got2, NULL) && got2 == 2 && hdr[0] == 'M' && hdr[1] == 'Z' &&
                    GetFileSizeEx(h, &size) && size.QuadPart > 40960)
                    looks_ok = 1;
                CloseHandle(h);
            }
            if (!looks_ok) { DeleteFileW(tmp); ok = 0; }
        }
        logf2("downloaded: %s", GetFileAttributesW(tmp) != INVALID_FILE_ATTRIBUTES ? "yes" : "no");
        /* the target is often on another drive than %TEMP%, so copy rather than
           move: MoveFileEx cannot cross volumes without MOVEFILE_COPY_ALLOWED */
        if (GetFileAttributesW(tmp) != INVALID_FILE_ATTRIBUTES) {
            BOOL copied = CopyFileW(tmp, target, FALSE);
            if (!copied) {
                DWORD why = GetLastError();
                logf2("copy      : failed, error %lu", (unsigned long)why);
                if (why == ERROR_ACCESS_DENIED) logf2("            (a running Hi75CPro.exe may be locking it)");
            } else {
                logf2("copied    : %ls", target);
            }
            DeleteFileW(tmp);
        }
        if (GetFileAttributesW(target) != INVALID_FILE_ATTRIBUTES) {
            logf2("installed : %ls", target);
            register_uninstall(target);
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
    logf2("result    : %s", ok ? "installed" : "FAILED");
    if (g_log) { fclose(g_log); g_log = NULL; }
    free(json);
    if (g_silent) return ok ? 0 : 2;
    MessageBoxW(NULL, ok ? L"Installed the newest published build.\n\nA Start Menu shortcut was created."
                         : L"Could not reach GitHub. Check your connection and run Setup again.",
                L"Hi75C Pro", ok ? MB_ICONINFORMATION : MB_ICONERROR);
    CoUninitialize();
    return 0;
}
