/* ==================================================================
 * startup.c - Windows 起動時の自動実行
 *
 *  レジストリ(HKCU\...\Run)は使わず、スタートアップフォルダに
 *  ショートカット(.lnk)を置く方式にしている。
 *    ・レジストリを汚さない
 *    ・ユーザーが「スタートアップ」フォルダを開けば目で見て確認・削除できる
 *    ・タスクマネージャーのスタートアップタブからも制御できる
 *
 *  以前のバージョンが書いた Run キーが残っていれば黙って掃除する。
 * ================================================================== */

#include "common.h"
#include <shlobj.h>
#include <objbase.h>

#define OLD_RUNKEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"

static void lnk_path(WCHAR *out, int cch)
{
    WCHAR dir[MAX_PATH];

    out[0] = 0;
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_STARTUP, NULL, 0, dir))) return;
    lstrcpynW(out, dir, cch);
    lstrcatW(out, L"\\" MAYOUS_APPNAME L".lnk");
}

/* 昔のバージョンが残したレジストリ登録を消す */
void startup_cleanup_legacy(void)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, OLD_RUNKEY, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &k)
        != ERROR_SUCCESS)
        return;
    if (RegQueryValueExW(k, MAYOUS_APPNAME, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
        RegDeleteValueW(k, MAYOUS_APPNAME);
    RegCloseKey(k);
}

BOOL startup_enabled(void)
{
    WCHAR path[MAX_PATH];
    lnk_path(path, ARRAYSIZE(path));
    return path[0] && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static BOOL create_shortcut(const WCHAR *lnk)
{
    IShellLinkW  *sl = NULL;
    IPersistFile *pf = NULL;
    WCHAR exe[MAX_PATH], dir[MAX_PATH], *slash;
    HRESULT hrInit, hr;
    BOOL ok = FALSE;

    GetModuleFileNameW(NULL, exe, ARRAYSIZE(exe));
    lstrcpynW(dir, exe, ARRAYSIZE(dir));
    slash = wcsrchr(dir, L'\\');
    if (slash) *slash = 0;

    /* 既に COM が初期化済みでも壊さないよう、戻り値を見て後始末を決める */
    hrInit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IShellLinkW, (void **)&sl);
    if (SUCCEEDED(hr)) {
        sl->lpVtbl->SetPath(sl, exe);
        sl->lpVtbl->SetWorkingDirectory(sl, dir);
        sl->lpVtbl->SetDescription(sl, L"マウスの同時押しをショートカットに変える常駐ツール");
        sl->lpVtbl->SetIconLocation(sl, exe, 0);

        hr = sl->lpVtbl->QueryInterface(sl, &IID_IPersistFile, (void **)&pf);
        if (SUCCEEDED(hr)) {
            ok = SUCCEEDED(pf->lpVtbl->Save(pf, lnk, TRUE));
            pf->lpVtbl->Release(pf);
        }
        sl->lpVtbl->Release(sl);
    }

    if (SUCCEEDED(hrInit)) CoUninitialize();
    return ok;
}

void startup_set(BOOL on)
{
    WCHAR path[MAX_PATH];

    startup_cleanup_legacy();

    lnk_path(path, ARRAYSIZE(path));
    if (!path[0]) return;

    if (on) {
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
            create_shortcut(path);
    } else {
        DeleteFileW(path);
    }
}

/* 設定ウィンドウの説明に出すため、実際の置き場所を返す */
void startup_folder(WCHAR *out, int cch)
{
    out[0] = 0;
    SHGetFolderPathW(NULL, CSIDL_STARTUP, NULL, 0, out);
    (void)cch;
}
