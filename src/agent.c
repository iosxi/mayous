/* ==================================================================
 * agent.c - ホイール注入専用の子プロセス
 *
 *  【なぜ別プロセスが要るのか】
 *  Windows は「WH_MOUSE_LL を保持しているプロセス」からのホイール注入を
 *  黙って捨てる。SendInput は 1 を返し GetLastError も 0 なのに、
 *  イベントは自分のフックにすら現れない。実測で確認済み:
 *
 *      フック保持 + メインスレッドから HWHEEL … 消滅
 *      フック保持 + 別スレッドから HWHEEL     … 消滅 (プロセス単位の制約)
 *      フック保持 + 縦ホイール                … 消滅 (水平に限らない)
 *      フックを外した直後に HWHEEL            … 通る
 *
 *  おそらくホイールの無限ループを防ぐための仕様。回避策は3つあった:
 *    (1) 注入の瞬間だけフックを外す
 *        → その隙間に生のホイールが素通ししてしまう。連続スクロール中は
 *          毎ノッチで隙間が空くので、縦スクロールが漏れて実用にならない。
 *    (2) WM_MOUSEHWHEEL をカーソル下のウィンドウへ直接 PostMessage
 *        → 入力ストリームを通らないため、アプリによって効いたり効かなかったり。
 *    (3) フックを持たない別プロセスから注入させる  ← これを採用
 *        → 本物のチルトホイールと完全に同じ経路を通るので、
 *          水平スクロールに対応したアプリなら等しく反応する。
 *
 *  配布物を1ファイルに保つため、エージェントは mayous.exe 自身を
 *  --wheel-agent 付きで起動したもの。フックは一切張らない。
 * ================================================================== */

#include "common.h"

static HWND   g_agentWnd;      /* 親側が持つ、エージェントの窓 */
static HANDLE g_agentProc;

/* ================================================================== */
/*  エージェント側                                                     */
/* ================================================================== */

static void agent_emit(int amount, BOOL horizontal)
{
    INPUT in;
    ZeroMemory(&in, sizeof(in));
    in.type           = INPUT_MOUSE;
    in.mi.dwFlags     = horizontal ? MOUSEEVENTF_HWHEEL : MOUSEEVENTF_WHEEL;
    in.mi.mouseData   = (DWORD)amount;
    in.mi.dwExtraInfo = MAYOUS_TAG;      /* 親のフックに素通しさせるための印 */
    SendInput(1, &in, sizeof(in));
}

/* Ctrl を押しながらの縦ホイール(アプリ側の拡大・縮小)。
   親側で Ctrl を押してからホイールだけを頼む作りにすると、こちらへ届くのが
   PostMessage 経由で遅れるぶん、Ctrl を離した後にホイールが出てしまうことが
   ある。3 つを 1 回の SendInput にまとめて、入力の並びごと固定してしまう。 */
static void agent_emit_zoom(int amount)
{
    INPUT in[3];
    ZeroMemory(in, sizeof(in));

    in[0].type           = INPUT_KEYBOARD;
    in[0].ki.wVk         = VK_CONTROL;
    in[0].ki.wScan       = (WORD)MapVirtualKeyW(VK_CONTROL, MAPVK_VK_TO_VSC);
    in[0].ki.dwExtraInfo = MAYOUS_TAG;

    in[1].type           = INPUT_MOUSE;
    in[1].mi.dwFlags     = MOUSEEVENTF_WHEEL;
    in[1].mi.mouseData   = (DWORD)amount;
    in[1].mi.dwExtraInfo = MAYOUS_TAG;

    in[2]                = in[0];
    in[2].ki.dwFlags     = KEYEVENTF_KEYUP;

    SendInput(3, in, sizeof(INPUT));
}

static LRESULT CALLBACK AgentProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_MAYOUS_AGENT_WHEEL:
        if (wp == 2) agent_emit_zoom((int)(LONG_PTR)lp);
        else         agent_emit((int)(LONG_PTR)lp, wp == 0);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int agent_main(HINSTANCE inst, DWORD parentPid)
{
    WNDCLASSEXW wc;
    HWND   hwnd;
    HANDLE parent;
    MSG    msg;

    parent = parentPid ? OpenProcess(SYNCHRONIZE, FALSE, parentPid) : NULL;

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = AgentProc;
    wc.hInstance     = inst;
    wc.lpszClassName = MAYOUS_AGENT_CLASS;
    if (!RegisterClassExW(&wc)) return 1;

    hwnd = CreateWindowExW(0, MAYOUS_AGENT_CLASS, L"", WS_OVERLAPPED,
                           0, 0, 0, 0, NULL, NULL, inst, NULL);
    if (!hwnd) return 1;

    /* 親が消えたら道連れで終わる。孤児として residual に残さない。 */
    for (;;) {
        DWORD r = MsgWaitForMultipleObjects(parent ? 1 : 0, parent ? &parent : NULL,
                                            FALSE, INFINITE, QS_ALLINPUT);
        if (parent && r == WAIT_OBJECT_0) break;          /* 親が終了した */

        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
done:
    if (parent) CloseHandle(parent);
    DestroyWindow(hwnd);
    return 0;
}

/* ================================================================== */
/*  親側                                                               */
/* ================================================================== */

static BOOL agent_alive(void)
{
    if (g_agentWnd && IsWindow(g_agentWnd)) return TRUE;
    g_agentWnd = FindWindowW(MAYOUS_AGENT_CLASS, NULL);
    return g_agentWnd != NULL;
}

/* 起動していなければ起こす。ウィンドウが出来るのは待たない
   (待つと呼び出し元のメッセージループが止まるため)。次の tick で拾う。 */
void agent_ensure(void)
{
    WCHAR exe[MAX_PATH], cmd[MAX_PATH + 64];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;

    if (agent_alive()) return;

    if (g_agentProc) {                       /* 前回の残骸を片付ける */
        if (WaitForSingleObject(g_agentProc, 0) != WAIT_OBJECT_0) return;  /* 起動途中 */
        CloseHandle(g_agentProc);
        g_agentProc = NULL;
    }

    GetModuleFileNameW(NULL, exe, ARRAYSIZE(exe));
    wsprintfW(cmd, L"\"%s\" --wheel-agent %lu", exe, GetCurrentProcessId());

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    if (CreateProcessW(exe, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                       NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        g_agentProc = pi.hProcess;
    }
}

void agent_send_wheel(int amount, BOOL horizontal)
{
    if (!agent_alive()) {
        agent_ensure();      /* 取りこぼしは1ノッチだけ。次からは効く。 */
        return;
    }
    PostMessageW(g_agentWnd, WM_MAYOUS_AGENT_WHEEL,
                 horizontal ? 0 : 1, (LPARAM)amount);
}

void agent_send_zoom(int amount)
{
    if (!agent_alive()) {
        agent_ensure();      /* 取りこぼしは1ノッチだけ。次からは効く。 */
        return;
    }
    PostMessageW(g_agentWnd, WM_MAYOUS_AGENT_WHEEL, 2, (LPARAM)amount);
}

void agent_stop(void)
{
    if (g_agentWnd && IsWindow(g_agentWnd))
        PostMessageW(g_agentWnd, WM_CLOSE, 0, 0);
    if (g_agentProc) {
        WaitForSingleObject(g_agentProc, 1000);
        CloseHandle(g_agentProc);
        g_agentProc = NULL;
    }
    g_agentWnd = NULL;
}
