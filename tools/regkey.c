/* regkey.c - 登録キー(マウスのボタン + キーボードのキー)の実測。
 *
 *   ini に RightThenKey1Trigger=f13 / RightThenKey1=f14 を入れた mayous を
 *   相手に、次の 3 つを数字で確かめる。
 *
 *     1. 右ボタンを押していないときの F13 は素通しするか
 *     2. 右ボタンを押しながらの F13 は握り潰され、F14 が何 ms 後に出るか
 *     3. その F14 は、右ボタンを離すまで押されたままか
 *
 *   【フックの並びが肝】
 *   低レベルフックは「後から張ったものが先に呼ばれる」。このツールは
 *   自分のフックを張ってから mayous を起動するので、mayous より下流に立つ。
 *   つまり、ここで F13 が見えたら「mayous が握り潰さなかった」ことになる。
 *   逆順(mayous が下流)だと、握り潰したかどうかは観測できない。
 *
 *   【デスクトップが切り替わると何も測れない】
 *   低レベルフックも SendInput も、そのデスクトップの中でしか効かない。
 *   UAC のセキュアデスクトップやロック画面へ移っている最中は、注入した
 *   キーが自分のフックにすら戻ってこない。それを「握り潰された」と
 *   読み違えると嘘の結果になるので、
 *     ・毎回まず「単独の F13 が自分に戻ってくるか」で足場を確かめる
 *     ・EVENT_SYSTEM_DESKTOPSWITCH を自分でも見張る
 *   の 2 つで、測れなかった場合をはっきり分けている(終了コード 2)。
 *
 *   使い方:  regkey.exe <mayous.exe のパス> [起動待ち ms] [試行回数]
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define VK_F13_ 0x7C
#define VK_F14_ 0x7D

static HHOOK g_kb;
static HWINEVENTHOOK g_we;
static LARGE_INTEGER g_freq, g_base;
static double g_f13d, g_f13u, g_f14d, g_f14u;   /* -1 = 見えなかった */
static int    g_switched;

static double now_ms(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)(t.QuadPart - g_base.QuadPart) * 1000.0 / (double)g_freq.QuadPart;
}

static void mark_start(void)
{
    QueryPerformanceCounter(&g_base);
    g_f13d = g_f13u = g_f14d = g_f14u = -1.0;
}

static LRESULT CALLBACK kb(int code, WPARAM w, LPARAM l)
{
    const KBDLLHOOKSTRUCT *k = (const KBDLLHOOKSTRUCT *)l;
    if (code == HC_ACTION) {
        int down = (w == WM_KEYDOWN || w == WM_SYSKEYDOWN);
        if (k->vkCode == VK_F13_) {
            if (down) { if (g_f13d < 0) g_f13d = now_ms(); }
            else      { if (g_f13u < 0) g_f13u = now_ms(); }
        } else if (k->vkCode == VK_F14_) {
            if (down) { if (g_f14d < 0) g_f14d = now_ms(); }
            else      { if (g_f14u < 0) g_f14u = now_ms(); }
        }
    }
    return CallNextHookEx(g_kb, code, w, l);
}

static void CALLBACK we(HWINEVENTHOOK h, DWORD ev, HWND w, LONG o, LONG c, DWORD t, DWORD ti)
{
    (void)h; (void)w; (void)o; (void)c; (void)t; (void)ti;
    if (ev == EVENT_SYSTEM_DESKTOPSWITCH) ++g_switched;
}

/* スリープではなくメッセージを回して待つ。止めるとフックが呼ばれない。 */
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

static void key(WORD vk, int up)
{
    INPUT in;
    ZeroMemory(&in, sizeof(in));
    in.type       = INPUT_KEYBOARD;
    in.ki.wVk     = vk;
    in.ki.wScan   = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    SendInput(1, &in, sizeof(in));
}

static void mouse(DWORD flags)
{
    INPUT in;
    ZeroMemory(&in, sizeof(in));
    in.type       = INPUT_MOUSE;
    in.mi.dwFlags = flags;
    SendInput(1, &in, sizeof(in));
}

static const char *okng(int ok) { return ok ? "OK" : "NG"; }

/* 1 回ぶんの計測。 0 = 全部 OK / 1 = NG / 2 = 測れなかった */
static int attempt(int n)
{
    int fail = 0;
    double t14d;

    printf("--- %d 回目 ---\n", n);

    /* 足場の確認: 単独の F13 は素通しして、自分のフックに戻ってくるはず */
    g_switched = 0;
    mark_start();
    key(VK_F13_, 0);
    pump(150);
    key(VK_F13_, 1);
    pump(350);
    if (g_f13d < 0) {
        printf("   注入した F13 が自分のフックにも戻らない"
               " (デスクトップ切替 %d 回)。計測できません。\n", g_switched);
        return 2;
    }
    printf("1. F13 単独            素通し OK      余計な F14 %s\n",
           g_f14d < 0 ? "無し OK" : "出た NG");
    if (g_f14d >= 0) fail = 1;

    /* 右ボタンを押しながらの F13 */
    SetCursorPos(900, 500);
    pump(200);
    mouse(MOUSEEVENTF_RIGHTDOWN);
    pump(250);

    mark_start();
    key(VK_F13_, 0);
    pump(400);
    key(VK_F13_, 1);
    pump(300);
    t14d = g_f14d;

    if (g_switched) {                 /* 途中で足場が動いた = 結果は信用できない */
        mouse(MOUSEEVENTF_RIGHTUP);
        pump(300);
        printf("   計測中にデスクトップが %d 回切り替わりました。計測できません。\n",
               g_switched);
        return 2;
    }

    printf("2. 右+F13              F13 握り潰し %s   F14 押下 %s\n",
           okng(g_f13d < 0), t14d >= 0 ? "OK" : "出ず NG");
    if (t14d >= 0)
        printf("                       F13 を送ってから F14 が出るまで %.1f ms\n", t14d);
    if (g_f13d >= 0 || t14d < 0) fail = 1;

    printf("3. 右ボタンを離す前     F14 離上 %s\n",
           g_f14u < 0 ? "まだ = 押しっぱなし OK" : "もう出た NG");
    if (g_f14u >= 0) fail = 1;

    {
        double t = now_ms();
        mouse(MOUSEEVENTF_RIGHTUP);
        pump(600);
        if (g_f14u >= 0)
            printf("                       右ボタン離上の %.1f ms 後に F14 離上 OK\n",
                   g_f14u - t);
        else {
            printf("                       右ボタンを離しても F14 が離されない NG\n");
            fail = 1;
        }
    }
    return fail;
}

int main(int argc, char **argv)
{
    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    char cmd[MAX_PATH * 2];
    int wait  = (argc >= 3) ? atoi(argv[2]) : 2500;
    int tries = (argc >= 4) ? atoi(argv[3]) : 4;
    int i, r = 2;

    if (argc < 2) {
        printf("使い方: regkey.exe <mayous.exe> [起動待ち ms] [試行回数]\n");
        return 2;
    }

    QueryPerformanceFrequency(&g_freq);
    g_kb = SetWindowsHookExW(WH_KEYBOARD_LL, kb, GetModuleHandleW(NULL), 0);
    if (!g_kb) { printf("キーボードフックを張れませんでした\n"); return 2; }
    g_we = SetWinEventHook(EVENT_SYSTEM_DESKTOPSWITCH, EVENT_SYSTEM_DESKTOPSWITCH,
                           NULL, we, 0, 0, WINEVENT_OUTOFCONTEXT);

    /* フックを張ったあとで起動する = mayous が上流に立つ */
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    wsprintfA(cmd, "\"%s\"", argv[1]);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        printf("mayous を起動できませんでした\n");
        return 2;
    }
    CloseHandle(pi.hThread);
    pump(wait);                       /* 常駐が立ち上がるまで待つ */

    for (i = 1; i <= tries; ++i) {
        r = attempt(i);
        if (r != 2) break;            /* 測れたら(合否が付いたら)そこで終わり */
        pump(1500);                   /* 足場が戻るのを待って測り直す */
    }

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
    if (g_we) UnhookWinEvent(g_we);
    UnhookWindowsHookEx(g_kb);

    printf("%s\n", r == 0 ? "=> すべて OK" : r == 1 ? "=> NG" : "=> 計測できませんでした");
    return r;
}
