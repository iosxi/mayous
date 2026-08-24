/* ==================================================================
 * theme.c - システムのライト/ダークに合わせた配色
 *
 *  Win32 の古いコントロールにはダークモードの公式 API が無い。
 *  Windows 自身(エクスプローラなど)は uxtheme.dll の序数エクスポート
 *  （名前が無く、番号でしか呼べない非公開関数）を使って実現している。
 *  ここでも同じ方法を取るが、非公開である以上いつ消えてもおかしくないので、
 *  取得できなければ黙ってライトのまま動く作りにしてある。
 *
 *      序数 104  RefreshImmersiveColorPolicyState()
 *      序数 132  ShouldAppsUseDarkMode() -> BYTE
 *      序数 133  AllowDarkModeForWindow(HWND, BYTE)
 *      序数 135  1809: AllowDarkModeForApp(BYTE)
 *                1903+: SetPreferredAppMode(PreferredAppMode)
 *                どちらも 1 を渡せば「ダークを許可」になるので区別せず 1 を渡す
 *
 *  タイトルバーだけは公式 API (DwmSetWindowAttribute) がある。
 *
 *  コントロールの実際の描画は SetWindowTheme() でテーマ名を差し替えて任せる。
 *      "DarkMode_Explorer" … ボタン・チェックボックス・タブ
 *      "DarkMode_CFD"      … エディット・コンボボックス(枠が正しく暗くなる)
 *  テーマが効かないもの(グループ枠・タブの背景)はこちらで描く。
 * ================================================================== */

#include "common.h"
#include <dwmapi.h>
#include <uxtheme.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#define DWMWA_USE_IMMERSIVE_DARK_MODE_OLD 19   /* 1809〜1903 はこちら */

typedef BYTE (WINAPI *fnShouldAppsUseDarkMode)(void);
typedef BYTE (WINAPI *fnAllowDarkModeForWindow)(HWND, BYTE);
typedef int  (WINAPI *fnSetPreferredAppMode)(int);
typedef void (WINAPI *fnRefreshImmersiveColorPolicyState)(void);

static fnShouldAppsUseDarkMode            p_ShouldAppsUseDarkMode;
static fnAllowDarkModeForWindow           p_AllowDarkModeForWindow;
static fnSetPreferredAppMode              p_SetPreferredAppMode;
static fnRefreshImmersiveColorPolicyState p_RefreshImmersiveColorPolicyState;

static BOOL   g_ready;
static BOOL   g_dark;
static HBRUSH g_brBack, g_brCtrl;

/* ------------------------------------------------------------------ */

/* レジストリを読まずに済ませたいので、まず uxtheme に聞く。
   関数が取れなかった場合だけ、設定値を読みに行く(読むだけで書かない)。 */
static BOOL query_system_dark(void)
{
    HKEY  k;
    DWORD v = 1, cb = sizeof(v);

    if (p_ShouldAppsUseDarkMode)
        return p_ShouldAppsUseDarkMode() != 0;

    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_QUERY_VALUE, &k) == ERROR_SUCCESS) {
        if (RegQueryValueExW(k, L"AppsUseLightTheme", NULL, NULL, (BYTE *)&v, &cb)
            != ERROR_SUCCESS)
            v = 1;
        RegCloseKey(k);
    }
    return v == 0;
}

void theme_init(void)
{
    HMODULE ux;

    if (g_ready) return;
    g_ready = TRUE;

    ux = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (ux) {
        p_RefreshImmersiveColorPolicyState =
            (fnRefreshImmersiveColorPolicyState)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(104));
        p_ShouldAppsUseDarkMode =
            (fnShouldAppsUseDarkMode)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(132));
        p_AllowDarkModeForWindow =
            (fnAllowDarkModeForWindow)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(133));
        p_SetPreferredAppMode =
            (fnSetPreferredAppMode)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    }

    if (p_SetPreferredAppMode) p_SetPreferredAppMode(1);   /* AllowDark */
    if (p_RefreshImmersiveColorPolicyState) p_RefreshImmersiveColorPolicyState();

    theme_refresh();
}

/* システム設定または ini の指定を読み直す。変わっていたら TRUE。 */
BOOL theme_refresh(void)
{
    BOOL dark;

    switch (g_cfg.theme) {
    case THEME_LIGHT: dark = FALSE;              break;
    case THEME_DARK:  dark = TRUE;               break;
    default:          dark = query_system_dark(); break;
    }
    if (g_brBack && dark == g_dark) return FALSE;

    g_dark = dark;
    if (g_brBack) { DeleteObject(g_brBack); g_brBack = NULL; }
    if (g_brCtrl) { DeleteObject(g_brCtrl); g_brCtrl = NULL; }
    g_brBack = CreateSolidBrush(theme_back());
    g_brCtrl = CreateSolidBrush(theme_ctrl_back());
    return TRUE;
}

BOOL theme_is_dark(void) { return g_dark; }

COLORREF theme_back(void)      { return g_dark ? RGB(32, 32, 32)    : GetSysColor(COLOR_BTNFACE); }
COLORREF theme_ctrl_back(void) { return g_dark ? RGB(43, 43, 43)    : GetSysColor(COLOR_WINDOW); }
COLORREF theme_text(void)      { return g_dark ? RGB(228, 228, 228) : GetSysColor(COLOR_BTNTEXT); }
COLORREF theme_dim_text(void)  { return g_dark ? RGB(150, 150, 150) : GetSysColor(COLOR_GRAYTEXT); }
COLORREF theme_line(void)      { return g_dark ? RGB(70, 70, 70)    : GetSysColor(COLOR_3DSHADOW); }
COLORREF theme_hot(void)       { return g_dark ? RGB(60, 60, 60)    : GetSysColor(COLOR_BTNHIGHLIGHT); }

HBRUSH theme_back_brush(void)  { return g_brBack; }
HBRUSH theme_ctrl_brush(void)  { return g_brCtrl; }

/* ------------------------------------------------------------------ */

void theme_apply_window(HWND hwnd)
{
    BOOL on = g_dark;

    if (p_AllowDarkModeForWindow) p_AllowDarkModeForWindow(hwnd, (BYTE)(on ? 1 : 0));

    /* タイトルバー。属性番号が途中で変わったので、新しい方から順に試す。 */
    if (FAILED(DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &on, sizeof(on))))
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_OLD, &on, sizeof(on));

    SetWindowTheme(hwnd, on ? L"DarkMode_Explorer" : NULL, NULL);
}

void theme_apply_control(HWND ctl, const WCHAR *cls)
{
    if (!ctl) return;
    if (p_AllowDarkModeForWindow) p_AllowDarkModeForWindow(ctl, (BYTE)(g_dark ? 1 : 0));

    if (!g_dark) { SetWindowTheme(ctl, NULL, NULL); return; }

    /* エディットとコンボは "CFD"(combobox/edit) 系のテーマでないと枠が白く残る */
    if (!lstrcmpiW(cls, L"EDIT") || !lstrcmpiW(cls, L"COMBOBOX"))
        SetWindowTheme(ctl, L"DarkMode_CFD", NULL);
    else
        SetWindowTheme(ctl, L"DarkMode_Explorer", NULL);
}

/* WM_CTLCOLOR* の共通処理。処理したら TRUE を返し、*br にブラシを入れる。 */
BOOL theme_ctlcolor(UINT msg, HDC dc, HBRUSH *br)
{
    if (!g_dark) return FALSE;

    switch (msg) {
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC:
        SetTextColor(dc, theme_text());
        SetBkColor(dc, theme_back());
        SetBkMode(dc, TRANSPARENT);
        *br = g_brBack;
        return TRUE;

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetTextColor(dc, theme_text());
        SetBkColor(dc, theme_ctrl_back());
        *br = g_brCtrl;
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ */
/*  テーマが効かないものを自前で描く                                   */
/* ------------------------------------------------------------------ */

/* グループ枠。SS_OWNERDRAW の STATIC として作っておく。 */
void theme_draw_group(const DRAWITEMSTRUCT *di, HFONT font)
{
    WCHAR  text[128];
    RECT   r = di->rcItem, tr;
    HPEN   pen, oldPen;
    HFONT  oldFont;
    HBRUSH oldBrush;
    SIZE   sz;
    int    top;

    GetWindowTextW(di->hwndItem, text, ARRAYSIZE(text));

    FillRect(di->hDC, &r, theme_back_brush());

    oldFont = (HFONT)SelectObject(di->hDC, font);
    GetTextExtentPoint32W(di->hDC, text, lstrlenW(text), &sz);
    top = r.top + sz.cy / 2;

    pen      = CreatePen(PS_SOLID, 1, theme_line());
    oldPen   = (HPEN)SelectObject(di->hDC, pen);
    oldBrush = (HBRUSH)SelectObject(di->hDC, GetStockObject(NULL_BRUSH));
    RoundRect(di->hDC, r.left, top, r.right, r.bottom, 6, 6);
    SelectObject(di->hDC, oldBrush);
    SelectObject(di->hDC, oldPen);
    DeleteObject(pen);

    /* 見出しの下だけ枠線を消して文字を置く */
    if (text[0]) {
        tr.left   = r.left + 8;
        tr.top    = r.top;
        tr.right  = tr.left + sz.cx + 8;
        tr.bottom = r.top + sz.cy;
        FillRect(di->hDC, &tr, theme_back_brush());
        SetTextColor(di->hDC, theme_text());
        SetBkMode(di->hDC, TRANSPARENT);
        TextOutW(di->hDC, tr.left + 4, tr.top, text, lstrlenW(text));
    }
    SelectObject(di->hDC, oldFont);
}

/* タブ 1 つ分。TCS_OWNERDRAWFIXED で作った場合に呼ぶ。 */
void theme_draw_tab(const DRAWITEMSTRUCT *di, HFONT font)
{
    WCHAR   text[64];
    TCITEMW ti;
    RECT    r = di->rcItem;
    BOOL    sel = (di->itemState & ODS_SELECTED) != 0;
    HBRUSH  br;
    HFONT   oldFont;

    ZeroMemory(&ti, sizeof(ti));
    ti.mask       = TCIF_TEXT;
    ti.pszText    = text;
    ti.cchTextMax = ARRAYSIZE(text);
    text[0] = 0;
    SendMessageW(di->hwndItem, TCM_GETITEMW, (WPARAM)di->itemID, (LPARAM)&ti);

    br = CreateSolidBrush(sel ? theme_ctrl_back() : theme_back());
    FillRect(di->hDC, &r, br);
    DeleteObject(br);

    if (sel) {
        /* 選択中のタブは上に細いアクセント線を引いて区別する */
        RECT a = r;
        a.bottom = a.top + 2;
        br = CreateSolidBrush(RGB(76, 141, 246));
        FillRect(di->hDC, &a, br);
        DeleteObject(br);
    }

    oldFont = (HFONT)SelectObject(di->hDC, font);
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, sel ? theme_text() : theme_dim_text());
    r.top += 2;
    DrawTextW(di->hDC, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(di->hDC, oldFont);
}
