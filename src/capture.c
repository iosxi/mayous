/* ==================================================================
 * capture.c - キー入力の記録
 *
 *  実際にキーを押してもらい、その組み合わせを設定文字列に変換する。
 *
 *  ・低レベルキーボードフックで全キーを握り潰しながら記録する。
 *    こうしないと記録中の Alt+Tab や Win キーで OS 側が反応してしまう。
 *  ・同時に押された一組を 1 ステップとして扱い、全部離れた時点で確定する。
 *    続けて別の組を押せば 2 ステップ目になる(例: ctrl+c, ctrl+v)。
 *  ・キーボードを全部握り潰すので、逃げ道は必ずマウスで確保しておく。
 *    ボタンは常に押せるため、記録が暴走しても [キャンセル] で必ず抜けられる。
 *
 *  フックの中で SendInput をしないという原則はここでも同じだが、
 *  記録中は何も注入しないので問題にならない。
 * ================================================================== */

#include "common.h"
#include <wchar.h>

#define WNDCLASS_CAPTURE L"MayousCaptureWnd"

#define IDC_CAP_OK     1
#define IDC_CAP_CLEAR  2
#define IDC_CAP_CANCEL 3

const WCHAR *cfg_vk_name(WORD vk);          /* config.c */

static HWND   g_wnd;
static HWND   g_hView, g_hHint;
static HHOOK  g_kbHook;
static HFONT  g_font;
static BOOL   g_ok;

/* 記録中のステップ列 */
static KeyStep g_steps[MAX_ACTION_STEPS];
static int     g_nsteps;

/* いま押されているキー(押された順) */
static WORD    g_held[MAX_ACTION_KEYS];
static int     g_nheld;
static BOOL    g_stepDirty;                 /* 今の押下をまだ確定していない */

/* ------------------------------------------------------------------ */

/* 修飾キーは左右をまとめる。記録の表記を安定させるため。 */
static WORD normalize_vk(DWORD vk)
{
    switch (vk) {
    case VK_LCONTROL: case VK_RCONTROL: case VK_CONTROL: return VK_LCONTROL;
    case VK_LMENU:    case VK_RMENU:    case VK_MENU:    return VK_LMENU;
    case VK_LSHIFT:   case VK_RSHIFT:   case VK_SHIFT:   return VK_LSHIFT;
    default: return (WORD)vk;
    }
}

static BOOL is_modifier(WORD vk)
{
    return vk == VK_LCONTROL || vk == VK_LMENU || vk == VK_LSHIFT ||
           vk == VK_LWIN     || vk == VK_RWIN;
}

/* 記録済みの内容を "ctrl+c, ctrl+v" 形式にする */
static void build_spec(WCHAR *out, int cch, BOOL includeHeld)
{
    int s, i;

    out[0] = 0;
    for (s = 0; s < g_nsteps; ++s) {
        if (out[0]) lstrcatW(out, L", ");
        for (i = 0; i < g_steps[s].nkeys; ++i) {
            if (i) lstrcatW(out, L"+");
            lstrcatW(out, cfg_vk_name(g_steps[s].keys[i]));
        }
    }
    if (includeHeld && g_nheld > 0) {
        if (out[0]) lstrcatW(out, L", ");
        for (i = 0; i < g_nheld; ++i) {
            if (i) lstrcatW(out, L"+");
            lstrcatW(out, cfg_vk_name(g_held[i]));
        }
    }
    (void)cch;
}

static void refresh_view(void)
{
    WCHAR spec[ACTION_SPEC_CCH];
    build_spec(spec, ARRAYSIZE(spec), TRUE);
    SetWindowTextW(g_hView, spec[0] ? spec : L"(まだ何も記録されていません)");
}

/* 押されているキーが全部離れた -> 1 ステップとして確定する */
static void commit_step(void)
{
    if (!g_stepDirty || g_nheld != 0) return;
    g_stepDirty = FALSE;
    refresh_view();
}

static void on_key_down(WORD vk)
{
    int i;

    /* 前のステップが確定済みで、かつ上限に達していたら無視 */
    if (!g_stepDirty && g_nsteps >= MAX_ACTION_STEPS) return;

    for (i = 0; i < g_nheld; ++i)
        if (g_held[i] == vk) return;              /* オートリピート */

    if (!g_stepDirty) {                            /* 新しいステップの開始 */
        g_stepDirty = TRUE;
        g_steps[g_nsteps].nkeys = 0;
        g_nheld = 0;
    }
    if (g_nheld < MAX_ACTION_KEYS) g_held[g_nheld++] = vk;

    /* 修飾キーは前に、実キーは後ろに来るよう、押された順のまま貯める。
       修飾キーだけのステップも「win」のように有効な指定になる。 */
    {
        KeyStep *st = &g_steps[g_nsteps];
        if (st->nkeys < MAX_ACTION_KEYS) {
            /* 既に実キーが入っている状態で修飾キーが来たら差し込む */
            if (is_modifier(vk) && st->nkeys > 0 && !is_modifier(st->keys[st->nkeys - 1])) {
                st->keys[st->nkeys] = st->keys[st->nkeys - 1];
                st->keys[st->nkeys - 1] = vk;
                st->nkeys++;
            } else {
                st->keys[st->nkeys++] = vk;
            }
        }
    }
    if (g_nsteps < MAX_ACTION_STEPS && g_steps[g_nsteps].nkeys > 0) {
        /* まだ確定していないので nsteps は進めない */
    }
    refresh_view();
}

static void on_key_up(WORD vk)
{
    int i, j;

    for (i = 0; i < g_nheld; ++i) {
        if (g_held[i] != vk) continue;
        for (j = i; j + 1 < g_nheld; ++j) g_held[j] = g_held[j + 1];
        --g_nheld;
        break;
    }
    if (g_nheld == 0 && g_stepDirty) {
        if (g_steps[g_nsteps].nkeys > 0 && g_nsteps < MAX_ACTION_STEPS) g_nsteps++;
        g_stepDirty = FALSE;
        refresh_view();
    }
}

static LRESULT CALLBACK CaptureKbProc(int code, WPARAM wp, LPARAM lp)
{
    const KBDLLHOOKSTRUCT *k = (const KBDLLHOOKSTRUCT *)lp;

    if (code != HC_ACTION || k->dwExtraInfo == MAYOUS_TAG)
        return CallNextHookEx(NULL, code, wp, lp);

    if (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN)
        on_key_down(normalize_vk(k->vkCode));
    else if (wp == WM_KEYUP || wp == WM_SYSKEYUP)
        on_key_up(normalize_vk(k->vkCode));

    return 1;                       /* 記録中はキーを一切世に出さない */
}

/* ------------------------------------------------------------------ */

static void clear_all(void)
{
    g_nsteps = 0;
    g_nheld = 0;
    g_stepDirty = FALSE;
    ZeroMemory(g_steps, sizeof(g_steps));
    refresh_view();
}

static LRESULT CALLBACK CaptureProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_CAP_OK:     g_ok = TRUE;  DestroyWindow(hwnd); break;
        case IDC_CAP_CLEAR:  clear_all();                       break;
        case IDC_CAP_CANCEL: g_ok = FALSE; DestroyWindow(hwnd); break;
        }
        return 0;

    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);

    case WM_CLOSE:
        g_ok = FALSE;
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (g_kbHook) { UnhookWindowsHookEx(g_kbHook); g_kbHook = NULL; }
        g_wnd = NULL;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */

/* 記録ウィンドウを出して、確定したら out に設定文字列を入れて TRUE を返す。
   呼び出し元(設定ウィンドウ)は閉じるまでブロックされる。 */
BOOL capture_run(HINSTANCE inst, HWND owner, WCHAR *out, int cch)
{
    static BOOL registered;
    WNDCLASSEXW wc;
    MSG msg;
    RECT orc;
    int x, y, w = 460, h = 210;
    NONCLIENTMETRICSW ncm;

    if (g_wnd) return FALSE;

    if (!registered) {
        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = CaptureProc;
        wc.hInstance     = inst;
        wc.hCursor       = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = WNDCLASS_CAPTURE;
        if (!RegisterClassExW(&wc)) return FALSE;
        registered = TRUE;
    }

    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        g_font = CreateFontIndirectW(&ncm.lfMessageFont);

    if (GetWindowRect(owner, &orc)) {
        x = orc.left + ((orc.right - orc.left) - w) / 2;
        y = orc.top  + ((orc.bottom - orc.top) - h) / 2;
    } else {
        x = y = CW_USEDEFAULT;
    }

    g_wnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, WNDCLASS_CAPTURE,
                            L"キー入力を記録",
                            WS_POPUP | WS_CAPTION | WS_SYSMENU,
                            x, y, w, h, owner, NULL, inst, NULL);
    if (!g_wnd) return FALSE;

    {
        HWND c;
        c = CreateWindowExW(0, L"STATIC",
                L"割り当てたいキーを実際に押してください。\r\n"
                L"続けて別のキーを押すと、順番に再生される複数ステップになります。",
                WS_CHILD | WS_VISIBLE, 16, 12, w - 40, 40, g_wnd, NULL, inst, NULL);
        SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
        g_hHint = c;

        g_hView = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
                SS_CENTER | SS_CENTERIMAGE | WS_CHILD | WS_VISIBLE,
                16, 58, w - 40, 34, g_wnd, NULL, inst, NULL);
        SendMessageW(g_hView, WM_SETFONT, (WPARAM)g_font, TRUE);

        c = CreateWindowExW(0, L"STATIC",
                L"記録中はキーボードが一時的に無効になります。ボタンはマウスで押してください。",
                WS_CHILD | WS_VISIBLE, 16, 98, w - 40, 20, g_wnd, NULL, inst, NULL);
        SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);

        c = CreateWindowExW(0, L"BUTTON", L"消去", WS_CHILD | WS_VISIBLE,
                16, 130, 90, 28, g_wnd, (HMENU)IDC_CAP_CLEAR, inst, NULL);
        SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
        c = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE,
                w - 220, 130, 96, 28, g_wnd, (HMENU)IDC_CAP_OK, inst, NULL);
        SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
        c = CreateWindowExW(0, L"BUTTON", L"キャンセル", WS_CHILD | WS_VISIBLE,
                w - 116, 130, 96, 28, g_wnd, (HMENU)IDC_CAP_CANCEL, inst, NULL);
        SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    }

    clear_all();
    g_ok = FALSE;

    EnableWindow(owner, FALSE);            /* 疑似モーダル */
    ShowWindow(g_wnd, SW_SHOW);
    SetForegroundWindow(g_wnd);

    g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, CaptureKbProc, inst, 0);
    if (!g_kbHook) {
        EnableWindow(owner, TRUE);
        DestroyWindow(g_wnd);
        MessageBoxW(owner, L"キーボードフックを設置できませんでした。",
                    MAYOUS_APPNAME, MB_OK | MB_ICONERROR);
        return FALSE;
    }

    /* 自前のモーダルループ。記録ウィンドウが閉じるまでここに留まる。 */
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (!g_wnd) break;
    }

    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    if (g_font) { DeleteObject(g_font); g_font = NULL; }

    if (!g_ok) return FALSE;
    commit_step();
    build_spec(out, cch, FALSE);
    return out[0] != 0;
}
