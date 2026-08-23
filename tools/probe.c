/* ==================================================================
 * probe.c - 検証用の観測プログラム(配布物には含めない)
 *
 *  低レベルフックは「後から入れたものが先に呼ばれる」。
 *  よって probe を mayous より先に起動しておくと、probe は
 *  「mayous が通したイベントだけ」を見ることになる = アプリ側の視点。
 *
 *  受け取ったマウス/キーボードイベントをログファイルに書き出すだけ。
 *  何も握り潰さない。
 *
 *  使い方:  probe.exe <logfile> <実行秒数>
 * ================================================================== */

#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#define MAYOUS_TAG ((ULONG_PTR)0x4D594F55u)

static FILE      *g_log;
static ULONGLONG  g_t0;
static HHOOK      g_hm, g_hk;

static const char *mouse_name(UINT m)
{
    switch (m) {
    case WM_MOUSEMOVE:   return "MOVE";
    case WM_LBUTTONDOWN: return "L_DOWN";
    case WM_LBUTTONUP:   return "L_UP";
    case WM_RBUTTONDOWN: return "R_DOWN";
    case WM_RBUTTONUP:   return "R_UP";
    case WM_MBUTTONDOWN: return "M_DOWN";
    case WM_MBUTTONUP:   return "M_UP";
    case WM_MOUSEWHEEL:  return "WHEEL_V";
    case WM_MOUSEHWHEEL: return "WHEEL_H";
    default:             return "OTHER";
    }
}

static const char *vk_name(DWORD vk)
{
    static char buf[16];
    switch (vk) {
    case VK_LWIN:   return "LWIN";
    case VK_RWIN:   return "RWIN";
    case VK_LMENU:  return "LALT";
    case VK_TAB:    return "TAB";
    case VK_LSHIFT: return "LSHIFT";
    case VK_LCONTROL: return "LCTRL";
    default:
        if (vk >= VK_F1 && vk <= VK_F24) { sprintf(buf, "F%lu", vk - VK_F1 + 1); return buf; }
        sprintf(buf, "VK_%02lX", vk);
        return buf;
    }
}

static void logline(const char *fmt, ...)
{
    va_list ap;
    fprintf(g_log, "%+7lld ms  ", (long long)(GetTickCount64() - g_t0));
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

static LRESULT CALLBACK MouseProc(int code, WPARAM w, LPARAM l)
{
    const MSLLHOOKSTRUCT *m = (const MSLLHOOKSTRUCT *)l;

    if (code == HC_ACTION && (UINT)w != WM_MOUSEMOVE) {
        logline("MOUSE  %-8s  pt=(%5ld,%5ld)  data=%6d  %s",
                mouse_name((UINT)w), m->pt.x, m->pt.y,
                (short)HIWORD(m->mouseData),
                m->dwExtraInfo == MAYOUS_TAG ? "[mayous injected]"
                  : (m->flags & LLMHF_INJECTED) ? "[injected]" : "[physical]");
    }
    return CallNextHookEx(NULL, code, w, l);
}

static LRESULT CALLBACK KeyProc(int code, WPARAM w, LPARAM l)
{
    const KBDLLHOOKSTRUCT *k = (const KBDLLHOOKSTRUCT *)l;

    if (code == HC_ACTION) {
        logline("KEY    %-8s  %-4s              %s",
                vk_name(k->vkCode),
                (w == WM_KEYDOWN || w == WM_SYSKEYDOWN) ? "DOWN" : "UP",
                k->dwExtraInfo == MAYOUS_TAG ? "[mayous injected]"
                  : (k->flags & LLKHF_INJECTED) ? "[injected]" : "[physical]");
    }
    return CallNextHookEx(NULL, code, w, l);
}

int main(int argc, char **argv)
{
    MSG msg;
    UINT seconds = 30;

    if (argc < 2) { fprintf(stderr, "usage: probe <logfile> [seconds]\n"); return 1; }
    if (argc >= 3) seconds = (UINT)atoi(argv[2]);

    g_log = fopen(argv[1], "w");
    if (!g_log) { fprintf(stderr, "cannot open log\n"); return 1; }
    g_t0 = GetTickCount64();

    g_hm = SetWindowsHookExW(WH_MOUSE_LL,    MouseProc, GetModuleHandleW(NULL), 0);
    g_hk = SetWindowsHookExW(WH_KEYBOARD_LL, KeyProc,   GetModuleHandleW(NULL), 0);
    if (!g_hm || !g_hk) { fprintf(stderr, "hook failed\n"); return 1; }

    logline("probe ready");
    printf("READY\n");
    fflush(stdout);

    {
        UINT_PTR tEnd  = SetTimer(NULL, 0, seconds * 1000, NULL);
        UINT_PTR tBeat = SetTimer(NULL, 0, 1000, NULL);

        while (GetMessageW(&msg, NULL, 0, 0) > 0) {
            if (msg.message == WM_TIMER) {
                if (msg.wParam == tEnd) break;
                if (msg.wParam == tBeat) {
                    /* フックがまだ生きているかを可視化する。低レベルフックは
                       コールバックが遅いと OS に黙って外されることがあるため。 */
                    logline("...alive  (mouse hook %s, key hook %s)",
                            g_hm ? "set" : "LOST", g_hk ? "set" : "LOST");
                }
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        KillTimer(NULL, tEnd);
        KillTimer(NULL, tBeat);
    }

    UnhookWindowsHookEx(g_hm);
    UnhookWindowsHookEx(g_hk);
    logline("probe done");
    fclose(g_log);
    return 0;
}
