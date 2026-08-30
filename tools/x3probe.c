/* x3probe.c - 「ボタン6(X3)」は Windows の入力に存在するのか
 *
 *  知りたいことは 2 つ。
 *
 *   (1) 出せるか … SendInput の MOUSEEVENTF_XDOWN に mouseData=4 (= XBUTTON3
 *       のつもり) を渡したら、何が起きるか。低レベルフックまで戻ってくる
 *       mouseData の上位ワードを見れば、Windows がその値を通したのか、
 *       丸めたのか、そもそも捨てたのかが分かる。
 *
 *   (2) 取れるか … 実機のボタン 6 を押したとき、何かが見えるか。
 *       RAWMOUSE.usButtonFlags にはボタン 5 までのビットしか無いので、
 *       見えるとしたら HID の生レポートか、マウス付属ソフトが送っている
 *       キー入力のどちらか。両方まとめて覗く。
 *
 *      x3probe.exe inject          … (1) を測る
 *      x3probe.exe watch <秒数>    … (2) を測る。その間ボタンを押して回る
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
#include <stdarg.h>
#include <stdlib.h>

#define PROBE_TAG 0x58335052u   /* 自分が注入した分を見分ける印 */

static ULONGLONG g_t0;
static HHOOK     g_mouseHook, g_keyHook;

static void logf_(const char *fmt, ...)
{
    va_list ap;
    printf("%6llu ms  ", GetTickCount64() - g_t0);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    fflush(stdout);
}

static const char *msgname(WPARAM w)
{
    switch (w) {
    case WM_LBUTTONDOWN: return "LBUTTONDOWN";
    case WM_LBUTTONUP:   return "LBUTTONUP  ";
    case WM_RBUTTONDOWN: return "RBUTTONDOWN";
    case WM_RBUTTONUP:   return "RBUTTONUP  ";
    case WM_MBUTTONDOWN: return "MBUTTONDOWN";
    case WM_MBUTTONUP:   return "MBUTTONUP  ";
    case WM_XBUTTONDOWN: return "XBUTTONDOWN";
    case WM_XBUTTONUP:   return "XBUTTONUP  ";
    default:             return NULL;
    }
}

/* ---- 低レベルマウスフック: mouseData がどう戻ってくるか ---------------- */
static LRESULT CALLBACK mouse_ll(int code, WPARAM w, LPARAM l)
{
    if (code == HC_ACTION) {
        const MSLLHOOKSTRUCT *m = (const MSLLHOOKSTRUCT *)l;
        const char *n = msgname(w);
        if (n)
            logf_("HOOK  %s  mouseData=%08X (HIWORD=%u)  flags=%X%s  extra=%08X",
                  n, (unsigned)m->mouseData, (unsigned)HIWORD(m->mouseData),
                  (unsigned)m->flags,
                  (m->flags & LLMHF_INJECTED) ? " INJECTED" : "",
                  (unsigned)m->dwExtraInfo);
    }
    return CallNextHookEx(NULL, code, w, l);
}

/* ---- 低レベルキーボードフック ------------------------------------------
   マウス付属ソフトがボタン 6 をキー入力にすり替えている場合、
   マウス側では何も見えず、ここにだけ現れる。 */
static LRESULT CALLBACK key_ll(int code, WPARAM w, LPARAM l)
{
    if (code == HC_ACTION && (w == WM_KEYDOWN || w == WM_SYSKEYDOWN)) {
        const KBDLLHOOKSTRUCT *k = (const KBDLLHOOKSTRUCT *)l;
        logf_("HOOK  KEYDOWN     vk=%02X sc=%02X flags=%X%s",
              (unsigned)k->vkCode, (unsigned)k->scanCode, (unsigned)k->flags,
              (k->flags & LLKHF_INJECTED) ? " INJECTED" : "");
    }
    return CallNextHookEx(NULL, code, w, l);
}

/* ---- Raw Input --------------------------------------------------------- */
static void dump_raw(HRAWINPUT h)
{
    static BYTE buf[4096];
    UINT sz = sizeof(buf);
    RAWINPUT *ri = (RAWINPUT *)buf;

    if (GetRawInputData(h, RID_INPUT, buf, &sz, sizeof(RAWINPUTHEADER)) == (UINT)-1)
        return;

    if (ri->header.dwType == RIM_TYPEMOUSE) {
        USHORT f = ri->data.mouse.usButtonFlags;
        if (f == 0) return;                       /* 移動だけの分は黙る */
        logf_("RAW   MOUSE  usButtonFlags=%04X  usButtonData=%04X  hDev=%p extra=%08X",
              (unsigned)f, (unsigned)ri->data.mouse.usButtonData,
              (void *)ri->header.hDevice,
              (unsigned)ri->data.mouse.ulExtraInformation);
    } else if (ri->header.dwType == RIM_TYPEHID) {
        DWORD n = ri->data.hid.dwCount, len = ri->data.hid.dwSizeHid, i, j;
        for (i = 0; i < n; i++) {
            char line[512];
            int  p = 0;
            const BYTE *r = ri->data.hid.bRawData + i * len;
            for (j = 0; j < len && p < 480; j++)
                p += wsprintfA(line + p, "%02X ", r[j]);
            line[p] = 0;
            logf_("RAW   HID    hDev=%p len=%lu  %s", (void *)ri->header.hDevice,
                  (unsigned long)len, line);
        }
    }
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_INPUT) { dump_raw((HRAWINPUT)l); return 0; }
    return DefWindowProcW(h, m, w, l);
}

/* マウスに加えて、見つかった HID のトップレベル コレクションを全部登録する。
   拡張ボタンを別コレクションで出してくるマウスがあるため。 */
static void register_raw(HWND hwnd)
{
    RAWINPUTDEVICELIST list[128];
    RAWINPUTDEVICE     rid[129];
    UINT n = 128, i, k = 0;

    rid[k].usUsagePage = 0x01; rid[k].usUsage = 0x02;   /* mouse */
    rid[k].dwFlags = RIDEV_INPUTSINK; rid[k].hwndTarget = hwnd; k++;

    if (GetRawInputDeviceList(list, &n, sizeof(list[0])) != (UINT)-1) {
        for (i = 0; i < n && k < 129; i++) {
            RID_DEVICE_INFO di;
            UINT sz = sizeof(di);
            UINT j;
            BOOL dup = FALSE;
            di.cbSize = sizeof(di);
            if (list[i].dwType != RIM_TYPEHID) continue;
            if (GetRawInputDeviceInfoW(list[i].hDevice, RIDI_DEVICEINFO,
                                       &di, &sz) == (UINT)-1) continue;
            for (j = 0; j < k; j++)
                if (rid[j].usUsagePage == di.hid.usUsagePage &&
                    rid[j].usUsage     == di.hid.usUsage) { dup = TRUE; break; }
            if (dup) continue;
            rid[k].usUsagePage = di.hid.usUsagePage;
            rid[k].usUsage     = di.hid.usUsage;
            rid[k].dwFlags     = RIDEV_INPUTSINK;
            rid[k].hwndTarget  = hwnd;
            k++;
        }
    }

    for (i = 0; i < k; i++) {
        if (!RegisterRawInputDevices(&rid[i], 1, sizeof(rid[0])))
            printf("  (登録できず: page=%04X usage=%04X err=%lu)\n",
                   rid[i].usUsagePage, rid[i].usUsage,
                   (unsigned long)GetLastError());
        else
            printf("  登録: usagePage=%04X usage=%04X\n",
                   rid[i].usUsagePage, rid[i].usUsage);
    }
    fflush(stdout);
}

static void inject_x(DWORD data, BOOL down)
{
    INPUT in;
    ZeroMemory(&in, sizeof(in));
    in.type = INPUT_MOUSE;
    in.mi.mouseData  = data;
    in.mi.dwFlags    = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
    in.mi.dwExtraInfo = PROBE_TAG;
    logf_("SEND  XBUTTON%s mouseData=%lu -> SendInput=%u",
          down ? "DOWN" : "UP  ", (unsigned long)data,
          (unsigned)SendInput(1, &in, sizeof(in)));
}

static void pump(DWORD ms)
{
    ULONGLONG end = GetTickCount64() + ms;
    for (;;) {
        MSG msg;
        LONGLONG left = (LONGLONG)(end - GetTickCount64());
        if (left <= 0) break;
        MsgWaitForMultipleObjects(0, NULL, FALSE, (DWORD)left, QS_ALLINPUT);
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

int main(int argc, char **argv)
{
    WNDCLASSW wc;
    HWND hwnd;
    BOOL doInject = (argc > 1 && lstrcmpiA(argv[1], "inject") == 0);
    DWORD secs = (argc > 2) ? (DWORD)atoi(argv[2]) : 15;

    g_t0 = GetTickCount64();

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc   = wndproc;
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.lpszClassName = L"x3probe";
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, L"x3probe", L"x3probe", 0, 0, 0, 0, 0,
                           HWND_MESSAGE, NULL, wc.hInstance, NULL);

    printf("Raw Input を登録します:\n");
    register_raw(hwnd);

    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL,    mouse_ll, wc.hInstance, 0);
    g_keyHook   = SetWindowsHookExW(WH_KEYBOARD_LL, key_ll,   wc.hInstance, 0);
    printf("フック: mouse=%p keyboard=%p\n\n",
           (void *)g_mouseHook, (void *)g_keyHook);
    fflush(stdout);

    if (doInject) {
        /* mouseData に 1,2,3,4,8 を順に入れて撃つ。
           1=XBUTTON1, 2=XBUTTON2 は定義済み。4 が「X3」のつもり。 */
        static const DWORD kData[] = { 1, 2, 3, 4, 8 };
        UINT i;
        printf("--- (1) 出せるか: mouseData を変えて XDOWN/XUP を撃つ ---\n");
        fflush(stdout);
        pump(500);
        for (i = 0; i < sizeof(kData) / sizeof(kData[0]); i++) {
            inject_x(kData[i], TRUE);
            pump(150);
            inject_x(kData[i], FALSE);
            pump(350);
        }
    } else {
        printf("--- (2) 取れるか: %lu 秒のあいだ、マウスのボタンを順に押してください ---\n",
               (unsigned long)secs);
        printf("    左 / 右 / 中 / サイド手前 / サイド奥 / 6 番目のボタン\n\n");
        fflush(stdout);
        pump(secs * 1000);
    }

    UnhookWindowsHookEx(g_mouseHook);
    UnhookWindowsHookEx(g_keyHook);
    DestroyWindow(hwnd);
    printf("\n終了。\n");
    return 0;
}
