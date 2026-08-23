/* keystate_test.c
   フックで押下を握り潰したとき、GetAsyncKeyState は「押されている」と
   答えるのか? chord_sanity の生死判定はこの前提に乗っているので確認する。 */
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdio.h>

#define TAG ((ULONG_PTR)0x4D594F55u)
static HHOOK g_h;
static int   g_swallowed;

static void mi(DWORD flags)
{
    INPUT in; ZeroMemory(&in, sizeof in);
    in.type = INPUT_MOUSE; in.mi.dwFlags = flags; in.mi.dwExtraInfo = TAG;
    SendInput(1, &in, sizeof in);
}

static LRESULT CALLBACK Proc(int code, WPARAM w, LPARAM l)
{
    const MSLLHOOKSTRUCT *m = (const MSLLHOOKSTRUCT *)l;
    if (code == HC_ACTION && m->dwExtraInfo != TAG) {
        if (w == WM_RBUTTONDOWN || w == WM_RBUTTONUP) {
            printf("  hook: %s を握り潰す\n", w == WM_RBUTTONDOWN ? "R_DOWN" : "R_UP");
            fflush(stdout);
            g_swallowed = 1;
            return 1;
        }
    }
    return CallNextHookEx(NULL, code, w, l);
}

static void report(const char *when)
{
    printf("  %-34s GetAsyncKeyState(VK_RBUTTON)=%s  GetKeyState=%s\n", when,
           (GetAsyncKeyState(VK_RBUTTON) & 0x8000) ? "DOWN" : "up  ",
           (GetKeyState(VK_RBUTTON) & 0x8000) ? "DOWN" : "up  ");
    fflush(stdout);
}

static void pump(int ms)
{
    MSG msg; DWORD end = GetTickCount() + (DWORD)ms;
    while (GetTickCount() < end) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        Sleep(10);
    }
}

int main(void)
{
    g_h = SetWindowsHookExW(WH_MOUSE_LL, Proc, GetModuleHandleW(NULL), 0);
    printf("hook=%p\n", (void *)g_h);
    pump(300);

    report("押す前");
    printf("R_DOWN を注入(フックが握り潰す)\n");
    mi(MOUSEEVENTF_RIGHTDOWN);
    pump(400);
    report("握り潰した直後");
    pump(2500);
    report("2.5 秒後(sanity が動く頃)");

    printf("R_UP を注入(これも握り潰す)\n");
    mi(MOUSEEVENTF_RIGHTUP);
    pump(400);
    report("離上も握り潰した後");

    UnhookWindowsHookEx(g_h);
    return 0;
}
