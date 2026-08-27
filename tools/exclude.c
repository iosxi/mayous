/* exclude.c - 「停止する条件」をウィンドウ名で判定できているかの実測。
 *
 *   java.exe のように 1 つの exe が別物のウィンドウを何枚も出す場合、
 *   実行ファイル名だけでは選り分けられない。そこでウィンドウ名でも
 *   止められるようにしたが、次の 3 つを確かめないと信用できない。
 *
 *     1. 名前が一致するあいだ、同時押しが止まっているか
 *     2. 名前を変えたら、再起動なしで効くようになるか
 *        (前面が変わらないので、1 秒ごとの点検が拾うしかない)
 *     3. 名前を戻したら、また止まるか
 *
 *   自前のウィンドウの題名を書き換えて試すので、他のアプリに依存しない。
 *   ini には Rule1=title:MayousExcludeTest* と RightThenLeft=f14 を入れておく。
 *
 *   【フックの並び】regkey.c と同じ。自分のフックを張ってから mayous を
 *   起動するので、こちらは下流。ここで f14 が見えたら「発火した」。
 *
 *   使い方: exclude.exe <mayous.exe のパス> [起動待ち ms]
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define VK_F14_ 0x7D
#define TITLE_HIT   L"MayousExcludeTest - Alpha"
#define TITLE_MISS  L"Kankeinai Window"

static HHOOK g_kb;
static HWND  g_wnd;
static int   g_f14;

static LRESULT CALLBACK kb(int code, WPARAM w, LPARAM l)
{
    const KBDLLHOOKSTRUCT *k = (const KBDLLHOOKSTRUCT *)l;
    if (code == HC_ACTION && (w == WM_KEYDOWN || w == WM_SYSKEYDOWN) &&
        k->vkCode == VK_F14_)
        ++g_f14;
    return CallNextHookEx(g_kb, code, w, l);
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    if (msg == WM_ERASEBKGND) {
        RECT r; HBRUSH b = CreateSolidBrush(RGB(40, 70, 40));
        GetClientRect(h, &r); FillRect((HDC)w, &r, b); DeleteObject(b);
        return 1;
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

static void mouse(DWORD flags)
{
    INPUT in;
    ZeroMemory(&in, sizeof(in));
    in.type       = INPUT_MOUSE;
    in.mi.dwFlags = flags;
    SendInput(1, &in, sizeof(in));
}

/* 右クリックを押しながら左クリック = RightThenLeft の同時押し */
static int chord_fires(void)
{
    g_f14 = 0;
    mouse(MOUSEEVENTF_RIGHTDOWN);
    pump(200);
    mouse(MOUSEEVENTF_LEFTDOWN);
    pump(150);
    mouse(MOUSEEVENTF_LEFTUP);
    pump(150);
    mouse(MOUSEEVENTF_RIGHTUP);
    pump(400);
    return g_f14;
}

static void set_title(const WCHAR *t)
{
    SetWindowTextW(g_wnd, t);
    SetForegroundWindow(g_wnd);
    /* 前面が変わらないので、mayous 側は 1 秒ごとの点検でしか気づけない */
    pump(1800);
}

int main(int argc, char **argv)
{
    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    WNDCLASSEXW wc;
    char cmd[MAX_PATH * 2];
    int wait = (argc >= 3) ? atoi(argv[2]) : 2500;
    int fail = 0, n, hold = 0;

    if (argc < 2) { printf("使い方: exclude.exe <mayous.exe> [起動待ち ms]\n"); return 2; }

    /* -hold: 窓を出して待つだけ。設定画面の一覧に載せる相手が欲しいとき用。 */
    if (!lstrcmpA(argv[1], "-hold")) hold = (argc >= 3) ? atoi(argv[2]) : 30;

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.hCursor       = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    wc.lpszClassName = L"MayousExcludeTest";
    if (!RegisterClassExW(&wc)) { printf("クラス登録に失敗\n"); return 2; }

    g_wnd = CreateWindowExW(WS_EX_TOPMOST, L"MayousExcludeTest", TITLE_HIT,
                            WS_OVERLAPPEDWINDOW | WS_VISIBLE, 200, 200, 520, 380,
                            NULL, NULL, wc.hInstance, NULL);
    if (!g_wnd) { printf("窓を作れませんでした\n"); return 2; }

    if (hold) {                        /* 窓だけ出して待つ */
        SetForegroundWindow(g_wnd);
        pump(hold * 1000);
        DestroyWindow(g_wnd);
        return 0;
    }

    g_kb = SetWindowsHookExW(WH_KEYBOARD_LL, kb, wc.hInstance, 0);
    if (!g_kb) { printf("キーボードフックを張れませんでした\n"); return 2; }

    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    wsprintfA(cmd, "\"%s\"", argv[1]);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        printf("mayous を起動できませんでした\n");
        return 2;
    }
    CloseHandle(pi.hThread);
    pump(wait);

    SetForegroundWindow(g_wnd);
    SetCursorPos(400, 350);            /* 自前ウィンドウの中 */
    pump(600);

    /* --- 1. 条件に当たっている = 止まっているはず --- */
    n = chord_fires();
    printf("1. 題名 \"%ls\"\n", TITLE_HIT);
    printf("   条件 title:MayousExcludeTest*   同時押しの発火 %d 回 (0 が正)  %s\n",
           n, n == 0 ? "OK" : "NG");
    if (n != 0) fail = 1;

    /* --- 2. 題名を変えたら効くようになるはず --- */
    set_title(TITLE_MISS);
    n = chord_fires();
    printf("2. 題名を \"%ls\" へ変更\n", TITLE_MISS);
    printf("   前面は変わらないまま            同時押しの発火 %d 回 (1 が正)  %s\n",
           n, n == 1 ? "OK" : "NG");
    if (n != 1) {
        fail = 1;
        if (n == 0) printf("   (0 回のときは、そもそも注入が届いていない可能性がある)\n");
    }

    /* --- 3. 題名を戻したらまた止まるはず --- */
    set_title(TITLE_HIT);
    n = chord_fires();
    printf("3. 題名を戻す                     同時押しの発火 %d 回 (0 が正)  %s\n",
           n, n == 0 ? "OK" : "NG");
    if (n != 0) fail = 1;

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
    UnhookWindowsHookEx(g_kb);
    DestroyWindow(g_wnd);

    printf("%s\n", fail ? "=> NG" : "=> すべて OK");
    return fail;
}
