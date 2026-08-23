/* tabtest.c - タブコントロールだけの最小再現コード(切り分け用)
 *   build: gcc -municode -DUNICODE -D_UNICODE -mwindows tools/tabtest.c
 *          -o build/tabtest.exe -luser32 -lcomctl32
 *   ※ マニフェスト無し(= comctl32 v5)でも試せるようにしてある
 */
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <commctrl.h>
#include <wchar.h>

static HWND g_tab;
static HFONT g_font;

static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_NOTIFY:
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static int has(LPWSTR cmd, const WCHAR *w)
{
    return wcsstr(cmd, w) != NULL;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show)
{
    int noCombo = has(cmd, L"nocombo");
    int noDlg   = has(cmd, L"nodlg");
    int noFont  = has(cmd, L"nofont");
    int noClip  = has(cmd, L"noclip");
    WNDCLASSEXW wc;
    HWND hwnd;
    MSG msg;
    TCITEMW ti;
    INITCOMMONCONTROLSEX icc;
    NONCLIENTMETRICSW ncm;
    int t;
    static const WCHAR *names[4] = { L"左クリック", L"右クリック",
                                     L"サイドボタン1", L"サイドボタン2" };
    (void)prev; (void)cmd; (void)show;

    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_TAB_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    ncm.cbSize = sizeof(ncm);
    if (!noFont && SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        g_font = CreateFontIndirectW(&ncm.lfMessageFont);

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = Proc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"TabTestWnd";
    RegisterClassExW(&wc);

    hwnd = CreateWindowExW(WS_EX_CONTROLPARENT, L"TabTestWnd", L"tab test",
                           WS_OVERLAPPEDWINDOW, 300, 300, 520, 420,
                           NULL, NULL, inst, NULL);

    g_tab = CreateWindowExW(0, WC_TABCONTROLW, L"",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                            (noClip ? 0 : WS_CLIPSIBLINGS),
                            12, 12, 480, 320, hwnd, (HMENU)(INT_PTR)900, inst, NULL);
    if (g_font) SendMessageW(g_tab, WM_SETFONT, (WPARAM)g_font, TRUE);

    ZeroMemory(&ti, sizeof(ti));
    ti.mask   = TCIF_TEXT | TCIF_IMAGE;
    ti.iImage = -1;
    for (t = 0; t < 4; ++t) {
        ti.pszText = (LPWSTR)names[t];
        SendMessageW(g_tab, TCM_INSERTITEMW, (WPARAM)t, (LPARAM)&ti);
    }

    /* 実際の設定画面と同じく、タブの上に兄弟ウィンドウを重ねる */
    for (t = 0; !noCombo && t < 6; ++t) {
        HWND c = CreateWindowExW(0, L"COMBOBOX", L"",
                                 WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | WS_TABSTOP,
                                 200, 50 + t * 26, 220, 280, hwnd,
                                 (HMENU)(INT_PTR)(2000 + t), inst, NULL);
        if (g_font) SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
        SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)L"項目A");
        SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)L"項目B");
    }

    ShowWindow(hwnd, SW_SHOW);
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!noDlg && IsDialogMessageW(hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
