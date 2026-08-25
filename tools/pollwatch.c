/* ==================================================================
 * pollwatch.c - 「フックで見る」と「状態をポーリングして見る」の差を測る
 *
 *  多くのアプリ(zoom-pon もそう)はキーボードフックを張らず、
 *  GetAsyncKeyState を一定間隔で見に行く方式でキーを検出する。
 *  注入したキーの押下時間が短すぎると、この方式では拾えない。
 *  その差をそのまま観測するための道具。
 *
 *  使い方: pollwatch.exe <logfile> <実行秒数> <監視するVK(16進)> [ポーリング間隔ms]
 *      例: pollwatch.exe poll.log 20 41 8      ('A' キーを 8ms 間隔で監視)
 * ================================================================== */

#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#define MAYOUS_TAG ((ULONG_PTR)0x4D594F55u)

static FILE *g_log;
static HHOOK g_hook;
static int   g_watchVk;
static int   g_hookDown, g_pollDown;   /* それぞれが見た押下の回数 */

static void logline(const char *fmt, ...)
{
    va_list ap;
    fprintf(g_log, "%8lu  ", (unsigned long)GetTickCount());
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

static LRESULT CALLBACK KeyProc(int code, WPARAM wp, LPARAM lp)
{
    const KBDLLHOOKSTRUCT *k = (const KBDLLHOOKSTRUCT *)lp;

    if (code == HC_ACTION && (int)k->vkCode == g_watchVk) {
        BOOL down = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN);
        if (down) ++g_hookDown;
        logline("HOOK  vk=%02X %-4s  %s", (unsigned)k->vkCode, down ? "DOWN" : "UP",
                k->dwExtraInfo == MAYOUS_TAG ? "[mayous injected]"
                  : (k->flags & LLKHF_INJECTED) ? "[injected]" : "[physical]");
    }
    return CallNextHookEx(NULL, code, wp, lp);
}

int main(int argc, char **argv)
{
    MSG   msg;
    UINT  seconds  = 20;
    UINT  interval = 8;
    DWORD end;
    BOOL  prev = FALSE;

    if (argc < 4) {
        fprintf(stderr, "usage: pollwatch <logfile> <seconds> <vkHex> [intervalMs]\n");
        return 1;
    }
    seconds    = (UINT)atoi(argv[2]);
    g_watchVk  = (int)strtol(argv[3], NULL, 16);
    if (argc >= 5) interval = (UINT)atoi(argv[4]);

    g_log = fopen(argv[1], "w");
    if (!g_log) return 1;

    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyProc, GetModuleHandleW(NULL), 0);
    logline("watch vk=%02X  poll=%ums  hook=%p", (unsigned)g_watchVk, interval, (void *)g_hook);
    printf("READY\n");
    fflush(stdout);

    end = GetTickCount() + seconds * 1000;
    while (GetTickCount() < end) {
        BOOL now = (GetAsyncKeyState(g_watchVk) & 0x8000) != 0;
        if (now != prev) {
            if (now) ++g_pollDown;
            logline("POLL  vk=%02X %-4s", (unsigned)g_watchVk, now ? "DOWN" : "UP");
            prev = now;
        }
        /* フックはこのスレッドのメッセージループ上で呼ばれるので、必ず回す */
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        Sleep(interval);
    }

    logline("done  フックが見た押下 = %d 回 / ポーリングが見た押下 = %d 回",
            g_hookDown, g_pollDown);
    if (g_hook) UnhookWindowsHookEx(g_hook);
    fclose(g_log);
    return 0;
}
