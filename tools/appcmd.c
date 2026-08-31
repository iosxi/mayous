/* appcmd.c - 「ブラウザーバック」をキー以外の経路で出せるか測る
 *
 *  矢印キーを握っているサイトでは Alt+Left が効かない。キー入力を
 *  経由しない道が本当にあるのか、4 つの出し方を並べて比べる。
 *
 *      appcmd.exe msg    … WM_APPCOMMAND を前面ウィンドウへ直接投げる
 *                          (キー入力の経路をまるごと通らない)
 *      appcmd.exe vk     … VK_BROWSER_BACK を SendInput で押す
 *                          (キーではあるが、矢印キーではない)
 *      appcmd.exe alt    … Alt+Left を SendInput で押す (今までの方法)
 *      appcmd.exe xbtn   … サイドボタン1 を SendInput で押す
 *                          (マウスの「戻る」ボタンと同じ経路)
 *
 *  第2引数に秒数を書くと、その秒数だけ待ってから撃つ (既定 3 秒)。
 *  その間にブラウザへ切り替えて、問題のサイトを前面にしておくこと。
 *
 *      appcmd.exe msg 5
 *
 *  「進む」を測りたいときは末尾に fwd を足す:  appcmd.exe msg 3 fwd
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
#include <stdlib.h>

#define PROBE_TAG 0x41504301u

/* 前面ウィンドウを、トップレベルまで辿って返す。
   WM_APPCOMMAND を受け取るのはトップレベル側なので、子窓に当てても
   意味が無い(DefWindowProc が親へ送り直すが、余計な段を挟むだけ)。 */
static HWND foreground_top(void)
{
    HWND h = GetForegroundWindow();
    HWND root;
    if (!h) return NULL;
    root = GetAncestor(h, GA_ROOT);
    return root ? root : h;
}

static void describe(HWND h)
{
    WCHAR title[128] = L"", cls[128] = L"";
    DWORD pid = 0;

    if (!h) { printf("  前面ウィンドウが取れない\n"); return; }
    GetWindowTextW(h, title, ARRAYSIZE(title));
    GetClassNameW(h, cls, ARRAYSIZE(cls));
    GetWindowThreadProcessId(h, &pid);
    wprintf(L"  hwnd=%p pid=%lu class=%s\n  title=%s\n", (void *)h, pid, cls, title);
}

/* --- (1) WM_APPCOMMAND を直接送る -------------------------------- */
/*  lParam の作り: 上位ワードに「コマンド | 発生源」、下位ワードに修飾キー。
    発生源は FAPPCOMMAND_MOUSE / _KEY / _OEM のどれか。マウスの戻るボタンを
    真似るなら _MOUSE。ここが違っても受け取る側はコマンドしか見ないことが多い。 */
static LRESULT send_appcommand(HWND h, int cmd, DWORD device)
{
    LRESULT res = 0;
    DWORD_PTR out = 0;

    /* SendMessage をそのまま使うと、相手が固まっていたら一緒に固まる。
       応答待ちに上限を付け、固まっている相手は諦める。 */
    if (!SendMessageTimeoutW(h, WM_APPCOMMAND, (WPARAM)h,
                             (LPARAM)MAKELONG(0, (WORD)(cmd | device)),
                             SMTO_ABORTIFHUNG, 200, &out)) {
        printf("  SendMessageTimeout 失敗 err=%lu\n", GetLastError());
        return 0;
    }
    res = (LRESULT)out;
    printf("  戻り値=%lld  (0 以外なら「処理した」の意味。0 でも実際に\n"
           "  戻っていることはある。画面を見て判断すること)\n", (long long)res);
    return res;
}

/* --- (2) キーを押す ---------------------------------------------- */
static void fill_key(INPUT *in, WORD vk, BOOL up, BOOL ext)
{
    ZeroMemory(in, sizeof(*in));
    in->type           = INPUT_KEYBOARD;
    in->ki.wVk         = vk;
    in->ki.wScan       = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    in->ki.dwFlags     = (up ? KEYEVENTF_KEYUP : 0)
                       | (ext ? KEYEVENTF_EXTENDEDKEY : 0);
    in->ki.dwExtraInfo = PROBE_TAG;
}

static void send_vk(WORD vk)
{
    INPUT in[2];
    UINT n;
    fill_key(&in[0], vk, FALSE, TRUE);
    fill_key(&in[1], vk, TRUE,  TRUE);
    n = SendInput(2, in, sizeof(INPUT));
    printf("  SendInput=%u err=%lu\n", n, GetLastError());
}

static void send_alt_arrow(WORD arrow)
{
    INPUT in[4];
    UINT n;
    fill_key(&in[0], VK_LMENU, FALSE, FALSE);
    fill_key(&in[1], arrow,    FALSE, TRUE);
    fill_key(&in[2], arrow,    TRUE,  TRUE);
    fill_key(&in[3], VK_LMENU, TRUE,  FALSE);
    n = SendInput(4, in, sizeof(INPUT));
    printf("  SendInput=%u err=%lu\n", n, GetLastError());
}

/* --- (3) サイドボタンを押す -------------------------------------- */
static void send_xbutton(DWORD which)
{
    INPUT in[2];
    UINT n;
    ZeroMemory(in, sizeof(in));
    in[0].type           = INPUT_MOUSE;
    in[0].mi.dwFlags     = MOUSEEVENTF_XDOWN;
    in[0].mi.mouseData   = which;
    in[0].mi.dwExtraInfo = PROBE_TAG;
    in[1] = in[0];
    in[1].mi.dwFlags     = MOUSEEVENTF_XUP;
    n = SendInput(2, in, sizeof(INPUT));
    printf("  SendInput=%u err=%lu\n", n, GetLastError());
}

int main(int argc, char **argv)
{
    const char *how  = (argc > 1) ? argv[1] : "msg";
    int         wait = (argc > 2) ? atoi(argv[2]) : 3;
    int         fwd  = (argc > 3) && !strcmp(argv[3], "fwd");
    HWND        h;
    int         i;

    if (wait < 0) wait = 0;

    printf("方法=%s  向き=%s  %d 秒後に撃ちます。\n",
           how, fwd ? "進む" : "戻る", wait);
    printf("いま計りたいウィンドウ(ブラウザ)を前面にしてください。\n\n");
    for (i = wait; i > 0; --i) { printf("  %d...\n", i); fflush(stdout); Sleep(1000); }

    h = foreground_top();
    printf("\n前面ウィンドウ:\n");
    describe(h);
    printf("\n撃ちます:\n");

    if (!strcmp(how, "msg")) {
        if (!h) return 1;
        send_appcommand(h, fwd ? APPCOMMAND_BROWSER_FORWARD
                               : APPCOMMAND_BROWSER_BACKWARD, FAPPCOMMAND_MOUSE);
    } else if (!strcmp(how, "vk")) {
        send_vk(fwd ? VK_BROWSER_FORWARD : VK_BROWSER_BACK);
    } else if (!strcmp(how, "alt")) {
        send_alt_arrow(fwd ? VK_RIGHT : VK_LEFT);
    } else if (!strcmp(how, "xbtn")) {
        send_xbutton(fwd ? XBUTTON2 : XBUTTON1);
    } else {
        printf("  方法は msg / vk / alt / xbtn のどれか。\n");
        return 2;
    }

    printf("\n終了。ブラウザが実際に戻ったかどうかを見てください。\n");
    return 0;
}
