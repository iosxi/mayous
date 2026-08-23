/* hwheel_test.c - HWHEEL 注入がフック所有スレッド/プロセスで捨てられる件の回避策探し */
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdio.h>

#define TAG ((ULONG_PTR)0x4D594F55u)
static HHOOK g_h;
static int   g_step;

static void mi_send(DWORD flags, DWORD data, const char *what)
{
    INPUT in; UINT r;
    ZeroMemory(&in, sizeof in);
    in.type = INPUT_MOUSE; in.mi.dwFlags = flags; in.mi.mouseData = data;
    in.mi.dwExtraInfo = TAG;
    r = SendInput(1, &in, sizeof in);
    printf("  %-26s sent=%u err=%lu\n", what, r, GetLastError());
    fflush(stdout);
}

static DWORD WINAPI worker(LPVOID p)
{
    (void)p;
    mi_send(MOUSEEVENTF_HWHEEL, 120, "hwheel +120 (別スレッド)");
    return 0;
}

static LRESULT CALLBACK Proc(int code, WPARAM w, LPARAM l)
{
    return CallNextHookEx(NULL, code, w, l);
}

int main(void)
{
    MSG msg;
    g_h = SetWindowsHookExW(WH_MOUSE_LL, Proc, GetModuleHandleW(NULL), 0);
    printf("hook=%p\n", (void *)g_h);
    SetTimer(NULL, 0, 700, NULL);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.message != WM_TIMER) { DispatchMessageW(&msg); continue; }
        switch (g_step++) {
        case 0:
            printf("A: メインスレッド(フック所有)から\n");
            mi_send(MOUSEEVENTF_HWHEEL, 120, "hwheel +120 (main)");
            break;
        case 1: {
            HANDLE t;
            printf("B: 別スレッドから\n");
            t = CreateThread(NULL, 0, worker, NULL, 0, NULL);
            if (t) { WaitForSingleObject(t, 2000); CloseHandle(t); }
            break;
        }
        case 2:
            printf("C: 縦ホイールはどうか(main)\n");
            mi_send(MOUSEEVENTF_WHEEL, 120, "vwheel +120 (main)");
            break;
        case 3:
            printf("D: フックを外してから(main)\n");
            UnhookWindowsHookEx(g_h); g_h = NULL;
            mi_send(MOUSEEVENTF_HWHEEL, 120, "hwheel +120 (main)");
            break;
        default: goto done;
        }
    }
done:
    if (g_h) UnhookWindowsHookEx(g_h);
    Sleep(300);
    return 0;
}
