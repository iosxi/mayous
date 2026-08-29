/* zoomtest.c - 「ズーム(Ctrl+ホイール)」の実測。
 *
 *   ini に RightThenWheelUp=zoom_in / RightThenWheelDown=zoom_out を入れた
 *   mayous を相手に、次を数字で確かめる。
 *
 *     0. 素のホイールはそのままアプリへ届く(Ctrl は付かない)
 *     1. 右ボタンを押しながらホイール上  -> アプリには Ctrl 付きの上ホイール
 *     2. 右ボタンを押しながらホイール下  -> アプリには Ctrl 付きの下ホイール
 *     3. 終わったあと Ctrl が押しっぱなしで残っていない
 *     4. 注入してからアプリに届くまでの遅れ(ms)
 *
 *   【なぜ MK_CONTROL を見るのか】
 *   アプリが「Ctrl+ホイール」と判断するのは WM_MOUSEWHEEL の wParam に
 *   MK_CONTROL が立っているかどうかである。Ctrl の押下とホイールが別々に
 *   届いても、順番が前後すればこのビットは立たない。実際に立っているかを
 *   アプリ側で数えるのが、この機能の唯一の意味のある検証になる。
 *
 *   【フックの並び】scrolltest.c と同じ。自分のフックを張ってから mayous を
 *   起動するので、こちらは mayous の下流に立つ。
 *
 *   使い方: zoomtest.exe <mayous.exe のパス> [起動待ち ms]
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static HHOOK g_mhook, g_khook;
static HWND  g_wnd;

/* 下流(= mayous が通したもの) */
static int g_dnWheel;                    /* delta の合計(符号付き) */
static int g_dnCtrlDown, g_dnCtrlUp;

/* アプリ(自前ウィンドウ)に届いたもの */
static int      g_appWheel;              /* delta の合計 */
static int      g_appCtrlWheel;          /* MK_CONTROL 付きで届いた回数 */
static int      g_appPlainWheel;         /* Ctrl 無しで届いた回数 */
static LONGLONG g_appAt;                 /* 最後の Ctrl 付きホイールの時刻 */

static LONGLONG now(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

static double ms_between(LONGLONG a, LONGLONG b)
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return (double)(b - a) * 1000.0 / (double)f.QuadPart;
}

static void reset(void)
{
    g_dnWheel = g_dnCtrlDown = g_dnCtrlUp = 0;
    g_appWheel = g_appCtrlWheel = g_appPlainWheel = 0;
    g_appAt = 0;
}

static LRESULT CALLBACK mhook(int code, WPARAM w, LPARAM l)
{
    const MSLLHOOKSTRUCT *m = (const MSLLHOOKSTRUCT *)l;
    if (code == HC_ACTION && w == WM_MOUSEWHEEL)
        g_dnWheel += GET_WHEEL_DELTA_WPARAM(m->mouseData);
    return CallNextHookEx(g_mhook, code, w, l);
}

static LRESULT CALLBACK khook(int code, WPARAM w, LPARAM l)
{
    const KBDLLHOOKSTRUCT *k = (const KBDLLHOOKSTRUCT *)l;
    if (code == HC_ACTION &&
        (k->vkCode == VK_CONTROL || k->vkCode == VK_LCONTROL || k->vkCode == VK_RCONTROL)) {
        if (w == WM_KEYDOWN || w == WM_SYSKEYDOWN) ++g_dnCtrlDown;
        if (w == WM_KEYUP   || w == WM_SYSKEYUP)   ++g_dnCtrlUp;
    }
    return CallNextHookEx(g_khook, code, w, l);
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_MOUSEWHEEL:
        g_appWheel += GET_WHEEL_DELTA_WPARAM(w);
        if (GET_KEYSTATE_WPARAM(w) & MK_CONTROL) { ++g_appCtrlWheel; g_appAt = now(); }
        else                                     { ++g_appPlainWheel; }
        return 0;
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

static void mouse(DWORD flags, DWORD data)
{
    INPUT in;
    ZeroMemory(&in, sizeof(in));
    in.type         = INPUT_MOUSE;
    in.mi.dwFlags   = flags;
    in.mi.mouseData = data;
    SendInput(1, &in, sizeof(in));
}

static const char *okng(int ok) { return ok ? "OK" : "NG"; }

/* 右ボタンを押しながらホイールを 1 段。戻り値は注入した時刻。 */
static LONGLONG zoom_once(int up)
{
    LONGLONG t;
    mouse(MOUSEEVENTF_RIGHTDOWN, 0);
    pump(120);
    t = now();
    mouse(MOUSEEVENTF_WHEEL, (DWORD)(up ? WHEEL_DELTA : -WHEEL_DELTA));
    pump(400);
    mouse(MOUSEEVENTF_RIGHTUP, 0);
    pump(200);
    return t;
}

int main(int argc, char **argv)
{
    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    char cmd[MAX_PATH * 2];
    WNDCLASSEXW wc;
    LONGLONG t0;
    double lagUp = 0, lagDown = 0;
    int wait = (argc >= 3) ? atoi(argv[2]) : 2500;
    int fail = 0;

    if (argc < 2) { printf("使い方: zoomtest.exe <mayous.exe> [起動待ち ms]\n"); return 2; }

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.hCursor       = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    wc.lpszClassName = L"MayousZoomTest";
    if (!RegisterClassExW(&wc)) { printf("クラス登録に失敗\n"); return 2; }

    g_wnd = CreateWindowExW(WS_EX_TOPMOST, L"MayousZoomTest", L"zoomtest",
                            WS_POPUP | WS_VISIBLE, 200, 200, 500, 400,
                            NULL, NULL, wc.hInstance, NULL);
    if (!g_wnd) { printf("窓を作れませんでした\n"); return 2; }
    SetForegroundWindow(g_wnd);          /* WM_MOUSEWHEEL はフォーカス先へ行く */
    SetFocus(g_wnd);

    g_mhook = SetWindowsHookExW(WH_MOUSE_LL, mhook, wc.hInstance, 0);
    g_khook = SetWindowsHookExW(WH_KEYBOARD_LL, khook, wc.hInstance, 0);
    if (!g_mhook || !g_khook) { printf("フックを張れませんでした\n"); return 2; }

    /* フックを張ったあとで起動する = mayous が上流に立つ */
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    wsprintfA(cmd, "\"%s\"", argv[1]);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        printf("mayous を起動できませんでした\n");
        return 2;
    }
    CloseHandle(pi.hThread);
    pump(wait);

    SetCursorPos(400, 380);              /* 自前ウィンドウの真ん中 */
    SetForegroundWindow(g_wnd);
    pump(300);

    /* --- 0. 足場: 素のホイールはそのまま届き、Ctrl は付かない --- */
    reset();
    mouse(MOUSEEVENTF_WHEEL, (DWORD)WHEEL_DELTA);
    pump(400);
    if (g_appPlainWheel == 0 && g_appCtrlWheel == 0) {
        printf("注入したホイールがアプリに届きません。計測できません。\n");
        fail = 2;
        goto done;
    }
    printf("0. 素のホイール上      アプリ Ctrl無し%d Ctrl付き%d  -> %s\n",
           g_appPlainWheel, g_appCtrlWheel,
           okng(g_appPlainWheel == 1 && g_appCtrlWheel == 0));
    if (g_appPlainWheel != 1 || g_appCtrlWheel != 0) fail = 1;

    /* --- 1. 右 + ホイール上 = 拡大 --- */
    reset();
    t0 = zoom_once(1);
    if (g_appAt) lagUp = ms_between(t0, g_appAt);
    printf("1. 右 + ホイール上     アプリ Ctrl付き%d Ctrl無し%d  delta=%+d  -> %s\n",
           g_appCtrlWheel, g_appPlainWheel, g_appWheel,
           okng(g_appCtrlWheel == 1 && g_appPlainWheel == 0 && g_appWheel > 0));
    if (g_appCtrlWheel != 1 || g_appPlainWheel != 0 || g_appWheel <= 0) fail = 1;
    printf("   下流の Ctrl         押下%d 離上%d  -> %s\n",
           g_dnCtrlDown, g_dnCtrlUp, okng(g_dnCtrlDown == g_dnCtrlUp && g_dnCtrlDown >= 1));
    if (g_dnCtrlDown != g_dnCtrlUp || g_dnCtrlDown < 1) fail = 1;

    /* --- 2. 右 + ホイール下 = 縮小 --- */
    reset();
    t0 = zoom_once(0);
    if (g_appAt) lagDown = ms_between(t0, g_appAt);
    printf("2. 右 + ホイール下     アプリ Ctrl付き%d Ctrl無し%d  delta=%+d  -> %s\n",
           g_appCtrlWheel, g_appPlainWheel, g_appWheel,
           okng(g_appCtrlWheel == 1 && g_appPlainWheel == 0 && g_appWheel < 0));
    if (g_appCtrlWheel != 1 || g_appPlainWheel != 0 || g_appWheel >= 0) fail = 1;

    /* --- 3. Ctrl が残っていないか --- */
    pump(300);
    {
        int stuck = (GetAsyncKeyState(VK_CONTROL) & 0x8000) ? 1 : 0;
        printf("3. 後始末              Ctrl の残り %s\n", stuck ? "押されたまま NG" : "なし OK");
        if (stuck) fail = 1;
    }

    /* --- 4. 遅れ --- */
    printf("4. 遅れ(注入 -> アプリ)  上 %.1f ms / 下 %.1f ms\n", lagUp, lagDown);

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
    UnhookWindowsHookEx(g_mhook);
    UnhookWindowsHookEx(g_khook);
    DestroyWindow(g_wnd);

    printf("%s\n", fail == 0 ? "=> すべて OK" : fail == 2 ? "=> 計測できませんでした" : "=> NG");
    return fail;
}
