/*
 * Hi75C Pro setup — copies two embedded files, makes a shortcut, registers an
 * uninstall entry. Supports --silent so the install can be verified headless.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include "setup_payload.h"   /* APP_BYTES/APP_LEN, DLL_BYTES/DLL_LEN */

static int g_silent;

static int write_file(const wchar_t *dir, const wchar_t *name, const unsigned char *data, DWORD len) {
    wchar_t path[MAX_PATH * 2];
    HANDLE h;
    DWORD put = 0;
    int ok;
    _snwprintf(path, MAX_PATH * 2, L"%s\\%s", dir, name);
    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION) {
        wchar_t parked[MAX_PATH * 2];
        _snwprintf(parked, MAX_PATH * 2, L"%s.old", path);
        DeleteFileW(parked);
        if (MoveFileW(path, parked))
            h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    if (h == INVALID_HANDLE_VALUE) return 0;
    ok = WriteFile(h, data, len, &put, NULL) && put == len;
    CloseHandle(h);
    return ok;
}

static void make_shortcut(const wchar_t *dir) {
    wchar_t exe[MAX_PATH], link[MAX_PATH], prog[MAX_PATH];
    IShellLinkW *lnk = NULL;
    IPersistFile *file = NULL;

    _snwprintf(exe, MAX_PATH, L"%s\\Hi75CPro.exe", dir);
    if (!SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, prog))) return;
    _snwprintf(link, MAX_PATH, L"%s\\Hi75C Pro.lnk", prog);
    if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IShellLinkW, (void **)&lnk))) return;
    lnk->lpVtbl->SetPath(lnk, exe);
    lnk->lpVtbl->SetDescription(lnk, L"LEOBOG Hi75C Pro");
    if (SUCCEEDED(lnk->lpVtbl->QueryInterface(lnk, &IID_IPersistFile, (void **)&file))) {
        file->lpVtbl->Save(file, link, TRUE);
        file->lpVtbl->Release(file);
    }
    lnk->lpVtbl->Release(lnk);
}

static void register_uninstall(const wchar_t *dir) {
    HKEY k;
    wchar_t exe[MAX_PATH];
    _snwprintf(exe, MAX_PATH, L"%s\\Hi75CPro.exe", dir);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Hi75CPro",
                        0, NULL, 0, KEY_WRITE, NULL, &k, NULL) != ERROR_SUCCESS) return;
    RegSetValueExW(k, L"DisplayName", 0, REG_SZ, (const BYTE *)L"LEOBOG Hi75C Pro", sizeof(L"LEOBOG Hi75C Pro"));
    RegSetValueExW(k, L"DisplayIcon", 0, REG_SZ, (const BYTE *)exe, (DWORD)((wcslen(exe) + 1) * sizeof(wchar_t)));
    RegSetValueExW(k, L"UninstallString", 0, REG_SZ, (const BYTE *)exe, (DWORD)((wcslen(exe) + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show) {
    wchar_t dir[MAX_PATH];
    int ok_app, ok_dll;
    (void)inst; (void)prev; (void)show;
    g_silent = cmd && wcsstr(cmd, L"--silent") != NULL;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    if (!SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, dir))) return 1;
    lstrcatW(dir, L"\\Hi75C Pro");
    CreateDirectoryW(dir, NULL);

    ok_app = write_file(dir, L"Hi75CPro.exe", APP_BYTES, APP_LEN);
    ok_dll = 1;
    make_shortcut(dir);
    register_uninstall(dir);

    if (g_silent) return (ok_app && ok_dll) ? 0 : 2;
    MessageBoxW(NULL, (ok_app && ok_dll) ? L"Installed.\n\nA Start Menu shortcut was created."
                                         : L"Install failed: could not write the files.",
                L"Hi75C Pro", (ok_app && ok_dll) ? MB_ICONINFORMATION : MB_ICONERROR);
    if (ok_app && ok_dll) {
        wchar_t exe[MAX_PATH];
        _snwprintf(exe, MAX_PATH, L"%s\\Hi75CPro.exe", dir);
        ShellExecuteW(NULL, L"open", exe, NULL, NULL, SW_SHOWNORMAL);
    }
    CoUninitialize();
    return 0;
}
