/* ==================================================================
 * overlay.c - オートスクロール中に出す目印
 *
 *  スクロール・モードのあいだ、マウスの移動はフックで握り潰される。
 *  つまりカーソルがその場に凍りつく。何も出さないと「固まった」としか
 *  見えないので、入った場所に小さな丸を置いて、今その状態であることと
 *  どこが原点かを示す。
 *
 *  ・クリックを一切受けない (WS_EX_TRANSPARENT)
 *  ・前面を奪わない         (WS_EX_NOACTIVATE)
 *  ・タスクバーに出さない   (WS_EX_TOOLWINDOW)
 *  ・丸く切り抜く           (SetWindowRgn)
 *
 *  【フックの中から呼んではいけない】
 *  ウィンドウの生成も破棄もメッセージループ側で行う。フックの中で
 *  やると、そのコールバックが伸びて OS にフックを見限られる。
 *  呼び出し元(chord.c)は注入キュー経由でここへ来る。
 * ================================================================== */

#include "common.h"

#define WNDCLASS_OVERLAY L"MayousScrollMark"

static HWND g_wnd;
static int  g_size;

/* 上下左右の三角。中心から dir 方向へ、r だけ離した位置に描く。 */
static void arrow(HDC dc, int cx, int cy, int r, int w, int dx, int dy)
{
    POINT p[3];

    p[0].x = cx + dx * r;            p[0].y = cy + dy * r;
    p[1].x = cx + dx * (r - w) - dy * w / 2;
    p[1].y = cy + dy * (r - w) - dx * w / 2;
    p[2].x = cx + dx * (r - w) + dy * w / 2;
    p[2].y = cy + dy * (r - w) + dx * w / 2;
    Polygon(dc, p, 3);
}

static void paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC    dc = BeginPaint(hwnd, &ps);
    RECT   rc;
    HBRUSH br;
    HPEN   pen, oldPen;
    HGDIOBJ oldBr;
    int    d, c, r, w;

    GetClientRect(hwnd, &rc);
    d = rc.right;
    c = d / 2;

    /* 下地。白に近い灰色にしておくと、明るい画面でも暗い画面でも見える。 */
    br = CreateSolidBrush(RGB(246, 246, 246));
    FillRect(dc, &rc, br);
    DeleteObject(br);

    pen    = CreatePen(PS_SOLID, 1, RGB(90, 90, 90));
    oldPen = SelectObject(dc, pen);
    br     = CreateSolidBrush(RGB(60, 60, 60));
    oldBr  = SelectObject(dc, br);

    r = c - d / 10;
    w = d / 5;
    arrow(dc, c, c, r,  w,  0, -1);
    arrow(dc, c, c, r,  w,  0,  1);
    arrow(dc, c, c, r,  w, -1,  0);
    arrow(dc, c, c, r,  w,  1,  0);

    /* 中心の点 */
    Ellipse(dc, c - d / 14, c - d / 14, c + d / 14, c + d / 14);

    SelectObject(dc, oldBr);
    SelectObject(dc, oldPen);
    DeleteObject(br);
    DeleteObject(pen);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT)   { paint(hwnd); return 0; }
    if (msg == WM_DESTROY) { g_wnd = NULL; return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void overlay_show(POINT center)
{
    static BOOL registered;
    HINSTANCE inst = GetModuleHandleW(NULL);
    int d;

    overlay_hide();

    if (!registered) {
        WNDCLASSEXW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = OverlayProc;
        wc.hInstance     = inst;
        wc.lpszClassName = WNDCLASS_OVERLAY;
        if (!RegisterClassExW(&wc)) return;
        registered = TRUE;
    }

    /* カーソルと同じ大きさを基準にする。DPI に自動で追従する。 */
    d = GetSystemMetrics(SM_CXCURSOR);
    if (d < 24) d = 24;
    g_size = d;

    g_wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
                            WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                            WNDCLASS_OVERLAY, L"", WS_POPUP,
                            center.x - d / 2, center.y - d / 2, d, d,
                            NULL, NULL, inst, NULL);
    if (!g_wnd) return;

    SetWindowRgn(g_wnd, CreateEllipticRgn(0, 0, d + 1, d + 1), TRUE);
    SetLayeredWindowAttributes(g_wnd, 0, 210, LWA_ALPHA);
    ShowWindow(g_wnd, SW_SHOWNOACTIVATE);
    UpdateWindow(g_wnd);
}

void overlay_hide(void)
{
    if (g_wnd) {
        DestroyWindow(g_wnd);
        g_wnd = NULL;
    }
}
