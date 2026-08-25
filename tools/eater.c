/* eater.c - 「他の常駐ツールが離上を食べてしまう」状況を作る
 *
 *  X-Mouse Button Control や MouseGestureL.ahk のような同種のツールが
 *  フックの並びで手前に居ると、mayous に届く前にイベントが消えることがある。
 *  低レベルフックは後から設置したものほど先に呼ばれるので、mayous を
 *  起動したあとにこれを起動すれば、その状況を正確に再現できる。
 *
 *      eater.exe <ログ> <秒数> <食べるボタン: r|l|x1> [食べる回数]
 *
 *  注入された入力(dwExtraInfo が付いているもの)は食べない。
 *  狙いはあくまで「物理的な離上を消す」こと。
 */

#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

static FILE      *g_log;
static ULONGLONG  g_t0;
static HHOOK      g_hook;
static UINT       g_targetUp;      /* 食べる離上のメッセージ */
static int        g_targetX;       /* XBUTTON1 / XBUTTON2。0 なら問わない */
static int        g_left;          /* あと何回食べるか */

static void logline(const char *s)
{
    fprintf(g_log, "%6llu ms  %s\n", GetTickCount64() - g_t0, s);
    fflush(g_log);
}

static LRESULT CALLBACK proc(int code, WPARAM wp, LPARAM lp)
{
    const MSLLHOOKSTRUCT *m = (const MSLLHOOKSTRUCT *)lp;

    /* dwExtraInfo が 0 のものだけ食べる。mayous が注入したものには
       MAYOUS_TAG が付くので巻き込まない。テストは SendInput で押下を作る
       都合上 LLMHF_INJECTED が立つため、そこは条件にできない。 */
    if (code == HC_ACTION && g_left > 0 && (UINT)wp == g_targetUp &&
        m->dwExtraInfo == 0) {
        if (!g_targetX || (int)HIWORD(m->mouseData) == g_targetX) {
            --g_left;
            logline("ATE one button-up (mayous never sees it)");
            return 1;
        }
    }
    return CallNextHookEx(NULL, code, wp, lp);
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "eater.log";
    int seconds      = (argc > 2) ? atoi(argv[2]) : 15;
    const char *btn  = (argc > 3) ? argv[3] : "r";
    MSG msg;
    ULONGLONG end;

    g_left = (argc > 4) ? atoi(argv[4]) : 1;

    if      (!strcmp(btn, "l"))  { g_targetUp = WM_LBUTTONUP; }
    else if (!strcmp(btn, "x1")) { g_targetUp = WM_XBUTTONUP; g_targetX = XBUTTON1; }
    else if (!strcmp(btn, "x2")) { g_targetUp = WM_XBUTTONUP; g_targetX = XBUTTON2; }
    else                         { g_targetUp = WM_RBUTTONUP; }

    g_log = fopen(path, "w");
    if (!g_log) return 1;
    g_t0 = GetTickCount64();

    g_hook = SetWindowsHookExW(WH_MOUSE_LL, proc, GetModuleHandleW(NULL), 0);
    if (!g_hook) { logline("SetWindowsHookEx failed"); return 2; }
    logline("ready (ahead of mayous in the hook chain)");
    printf("READY\n");
    fflush(stdout);

    end = GetTickCount64() + (ULONGLONG)seconds * 1000;
    while (GetTickCount64() < end) {
        if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        } else {
            MsgWaitForMultipleObjects(0, NULL, FALSE, 20, QS_ALLINPUT);
        }
    }
    UnhookWindowsHookEx(g_hook);
    logline("done");
    fclose(g_log);
    return 0;
}
