/* scrolltest.c - オートスクロールと「中クリックの差し替え」の実測。
 *
 *   ini に MiddleAlone=autoscroll / Side1Alone=click:middle を入れた mayous を
 *   相手に、次を数字で確かめる。
 *
 *     1. 中クリック(押して離す)がアプリに漏れないか
 *     2. そのあとマウスを動かすと、移動が消えてホイールに化けるか
 *        (カーソルが実際に動かないことも座標で確かめる)
 *     3. もう一度クリックすると抜けるか。抜けたあと移動が元に戻るか
 *     4. サイドボタン1 が中クリックとしてアプリに届くか
 *
 *   【フックの並びが肝】regkey.c と同じ。自分のフックを張ってから mayous を
 *   起動するので、こちらは mayous の下流に立つ。ここで見えた入力は
 *   「mayous が通した」もの、見えなかった入力は「握り潰した」ものになる。
 *
 *   クリックが余所へ飛ばないよう、自前の最前面ウィンドウを出して、その上に
 *   カーソルを置いてから注入する。アプリ側に何が届いたかもそこで数える。
 *
 *   【4 の参考値が 0/1 になるのは正常】
 *   こちらが SendInput でサイドボタンを注入している最中に、
 *   mayous がその場で中クリックを注入し返す。こちらのスレッドは
 *   自分の SendInput の中で止まっていてフックの呼び出しに応えられず、
 *   Windows は LowLevelHooksTimeout でこちらを飛ばしてアプリへ配送する
 *   (chord.c 冒頭の「フックの中で SendInput を呼ぶな」と同じ現象)。
 *   注入する側とフックを張る側が同じスレッドだから起きることで、
 *   本物のマウスでは起きない。判定にはアプリ側で数えたほうを使う。
 *
 *   使い方: scrolltest.exe <mayous.exe のパス> [起動待ち ms]
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static HHOOK g_hook;
static HWND  g_wnd;

/* 下流(= mayous が通したもの)で見えた数 */
static int g_dnMove, g_dnWheel, g_dnMDown, g_dnMUp, g_dnXDown;
/* アプリ(自前ウィンドウ)に届いた数 */
static int g_appMDown, g_appMUp, g_appWheel;

static void reset(void)
{
    g_dnMove = g_dnWheel = g_dnMDown = g_dnMUp = g_dnXDown = 0;
    g_appMDown = g_appMUp = g_appWheel = 0;
}

static LRESULT CALLBACK hook(int code, WPARAM w, LPARAM l)
{
    const MSLLHOOKSTRUCT *m = (const MSLLHOOKSTRUCT *)l;
    if (code == HC_ACTION) {
        switch (w) {
        case WM_MOUSEMOVE:    ++g_dnMove;  break;
        case WM_MOUSEWHEEL:   g_dnWheel += GET_WHEEL_DELTA_WPARAM(m->mouseData); break;
        case WM_MBUTTONDOWN:  ++g_dnMDown; break;
        case WM_MBUTTONUP:    ++g_dnMUp;   break;
        case WM_XBUTTONDOWN:  ++g_dnXDown; break;
        }
    }
    return CallNextHookEx(g_hook, code, w, l);
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_MBUTTONDOWN: ++g_appMDown; return 0;
    case WM_MBUTTONUP:   ++g_appMUp;   return 0;
    case WM_MOUSEWHEEL:  g_appWheel += GET_WHEEL_DELTA_WPARAM(w); return 0;
    case WM_ERASEBKGND: {
        RECT r; HBRUSH b = CreateSolidBrush(RGB(30, 60, 100));
        GetClientRect(h, &r); FillRect((HDC)w, &r, b); DeleteObject(b);
        return 1;
    }
    }
    return DefWindowProcW(h, msg, w, l);
}

static void pump(int ms)
{
    DWORD end = GetTickCount() + (DWORD)ms;
    MSG m;
    for (;;) {
        DWORD left = end - GetTickCount();
        if ((int)left <= 0) break;
        if (MsgWaitForMultipleObjects(0, NULL, FALSE, left, QS_ALLINPUT) == WAIT_TIMEOUT) break;
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
}

static void mouse(DWORD flags, DWORD data, int dx, int dy)
{
    INPUT in;
    ZeroMemory(&in, sizeof(in));
    in.type         = INPUT_MOUSE;
    in.mi.dwFlags   = flags;
    in.mi.mouseData = data;
    in.mi.dx        = dx;
    in.mi.dy        = dy;
    SendInput(1, &in, sizeof(in));
}

/* 少しずつ動かす。1 回で飛ばすと本物のマウスと動きが違いすぎる。 */
static void move_by(int dx, int dy, int steps)
{
    int i;
    for (i = 0; i < steps; ++i) {
        mouse(MOUSEEVENTF_MOVE, 0, dx, dy);
        pump(16);
    }
}

static const char *okng(int ok) { return ok ? "OK" : "NG"; }

int main(int argc, char **argv)
{
    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    char cmd[MAX_PATH * 2];
    WNDCLASSEXW wc;
    POINT p0, p1;
    int wait = (argc >= 3) ? atoi(argv[2]) : 2500;
    int fail = 0;

    if (argc < 2) { printf("使い方: scrolltest.exe <mayous.exe> [起動待ち ms]\n"); return 2; }

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.hCursor       = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    wc.lpszClassName = L"MayousScrollTest";
    if (!RegisterClassExW(&wc)) { printf("クラス登録に失敗\n"); return 2; }

    g_wnd = CreateWindowExW(WS_EX_TOPMOST, L"MayousScrollTest", L"scrolltest",
                            WS_POPUP | WS_VISIBLE, 200, 200, 500, 400,
                            NULL, NULL, wc.hInstance, NULL);
    if (!g_wnd) { printf("窓を作れませんでした\n"); return 2; }

    g_hook = SetWindowsHookExW(WH_MOUSE_LL, hook, wc.hInstance, 0);
    if (!g_hook) { printf("マウスフックを張れませんでした\n"); return 2; }

    /* フックを張ったあとで起動する = mayous が上流に立つ */
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    wsprintfA(cmd, "\"%s\"", argv[1]);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        printf("mayous を起動できませんでした\n");
        return 2;
    }
    CloseHandle(pi.hThread);
    pump(wait);

    SetCursorPos(400, 380);            /* 自前ウィンドウの真ん中 */
    pump(300);

    /* --- 足場の確認: 素の移動が下流に見えるか --- */
    reset();
    move_by(4, 0, 5);
    pump(200);
    if (g_dnMove == 0) {
        printf("注入した移動が自分のフックにも戻りません。計測できません。\n");
        fail = 2;
        goto done;
    }
    printf("0. 素の移動            下流で %d 回見えた OK\n", g_dnMove);

    /* --- 1. 中クリックは食べられるか --- */
    reset();
    mouse(MOUSEEVENTF_MIDDLEDOWN, 0, 0, 0);
    pump(120);
    mouse(MOUSEEVENTF_MIDDLEUP, 0, 0, 0);
    pump(400);
    printf("1. 中クリック          下流 押下%d 離上%d / アプリ 押下%d  -> %s\n",
           g_dnMDown, g_dnMUp, g_appMDown,
           okng(g_dnMDown == 0 && g_dnMUp == 0 && g_appMDown == 0));
    if (g_dnMDown || g_dnMUp || g_appMDown) fail = 1;
    printf("   目印のウィンドウ    %s\n",
           FindWindowW(L"MayousScrollMark", NULL) ? "出た OK" : "出ていない NG");
    if (!FindWindowW(L"MayousScrollMark", NULL)) fail = 1;

    /* --- 2. 移動がホイールに化けるか。カーソルは止まるか --- */
    reset();
    GetCursorPos(&p0);
    move_by(0, 6, 20);                 /* 下へ 120px ぶん */
    pump(300);
    GetCursorPos(&p1);
    printf("2. 下へ 120px 動かす   下流の移動 %d 回 (0 が正)  ホイール %d (=%d 段)\n",
           g_dnMove, g_dnWheel, g_dnWheel / WHEEL_DELTA);
    printf("   カーソル            (%d,%d) -> (%d,%d)  %s\n",
           (int)p0.x, (int)p0.y, (int)p1.x, (int)p1.y,
           okng(p0.x == p1.x && p0.y == p1.y));
    if (g_dnMove != 0 || g_dnWheel >= 0 || p0.x != p1.x || p0.y != p1.y) fail = 1;

    /* --- 3. クリックで抜ける --- */
    reset();
    mouse(MOUSEEVENTF_LEFTDOWN, 0, 0, 0);
    pump(120);
    mouse(MOUSEEVENTF_LEFTUP, 0, 0, 0);
    pump(400);
    printf("3. 抜けるクリック      目印 %s\n",
           FindWindowW(L"MayousScrollMark", NULL) ? "残っている NG" : "消えた OK");
    if (FindWindowW(L"MayousScrollMark", NULL)) fail = 1;

    reset();
    GetCursorPos(&p0);
    move_by(4, 0, 5);
    pump(200);
    GetCursorPos(&p1);
    printf("   抜けたあとの移動    下流 %d 回  カーソル (%d,%d) -> (%d,%d)  %s\n",
           g_dnMove, (int)p0.x, (int)p0.y, (int)p1.x, (int)p1.y,
           okng(g_dnMove > 0 && p1.x != p0.x));
    if (g_dnMove == 0 || p1.x == p0.x) fail = 1;

    /* --- 4. サイドボタン1 が中クリックになるか --- */
    SetCursorPos(400, 380);
    pump(200);
    reset();
    mouse(MOUSEEVENTF_XDOWN, XBUTTON1, 0, 0);
    pump(120);
    mouse(MOUSEEVENTF_XUP, XBUTTON1, 0, 0);
    pump(500);
    printf("4. サイド1 を押す      下流 サイド押下%d (0 が正)   参考(下記): 下流で見えた中 %d/%d\n",
           g_dnXDown, g_dnMDown, g_dnMUp);
    printf("   アプリに届いた中クリック  押下%d 離上%d  -> %s\n",
           g_appMDown, g_appMUp, okng(g_appMDown == 1 && g_appMUp == 1));
    if (g_dnXDown || g_appMDown != 1 || g_appMUp != 1) fail = 1;

done:
    wsprintfA(cmd, "\"%s\" --exit", argv[1]);
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    {
        PROCESS_INFORMATION p2;
        if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &p2)) {
            WaitForSingleObject(p2.hProcess, 3000);
            CloseHandle(p2.hThread); CloseHandle(p2.hProcess);
        }
    }
    WaitForSingleObject(pi.hProcess, 3000);
    CloseHandle(pi.hProcess);
    UnhookWindowsHookEx(g_hook);
    DestroyWindow(g_wnd);

    printf("%s\n", fail == 0 ? "=> すべて OK" : fail == 2 ? "=> 計測できませんでした" : "=> NG");
    return fail;
}
