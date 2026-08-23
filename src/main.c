/* ==================================================================
 * main.c - 常駐本体
 *   ・非表示ウィンドウ + タスクトレイ常駐
 *   ・低レベルマウスフック(WH_MOUSE_LL)の設置と後始末
 *   ・前面ウィンドウの監視(除外アプリ / フルスクリーン時の自動停止)
 *
 *   低レベルフックはこれを設置したスレッドのメッセージループ上で呼ばれる。
 *   つまりフック処理・タイマー・トレイ操作はすべて同一スレッドで直列化され、
 *   ロックを一切持たずに状態を触れる。この性質に全面的に寄りかかっている。
 * ================================================================== */

#include "common.h"
#include <shellapi.h>
#include <wchar.h>

#define IDI_MAIN   101
#define IDI_OFF    102

#define IDM_SETTINGS  1000
#define IDM_ENABLE    1001
#define IDM_OPENINI   1002
#define IDM_RELOAD    1003
#define IDM_STARTUP   1004
#define IDM_ABOUT     1005
#define IDM_EXIT      1006

#define RUNKEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"

volatile LONG g_active = 0;

#ifdef MAYOUS_DEBUG
#include <stdio.h>
#include <stdarg.h>
void dbg(const char *fmt, ...)
{
    static FILE *f;
    va_list ap;
    if (!f) {
        WCHAR path[MAX_PATH];
        GetTempPathW(MAX_PATH, path);
        lstrcatW(path, L"mayous_debug.log");
        f = _wfopen(path, L"w");
        if (!f) return;
    }
    fprintf(f, "%8lu  ", (unsigned long)GetTickCount());
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fflush(f);
}
#endif

static HINSTANCE      g_inst;
static HWND           g_hwnd;
static HHOOK          g_mouseHook;
static HWINEVENTHOOK  g_winEvent;
static NOTIFYICONDATAW g_nid;
static BOOL           g_suspended;
static WCHAR          g_suspendedBy[MAX_PATH];
static UINT           g_msgTaskbarCreated;
static UINT           g_msgShowInfo;
static HICON          g_iconOn, g_iconOff;

/* ================================================================== */
/*  フック                                                             */
/* ================================================================== */

static LRESULT CALLBACK LLMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    const MSLLHOOKSTRUCT *m = (const MSLLHOOKSTRUCT *)lParam;

    if (nCode != HC_ACTION)
        return CallNextHookEx(NULL, nCode, wParam, lParam);

    /* 自分が注入したイベントには絶対に触らない(再入と無限ループの遮断) */
    if (m->dwExtraInfo == MAYOUS_TAG)
        return CallNextHookEx(NULL, nCode, wParam, lParam);

    /* 無効中でも状態機械には通す。ここで打ち切ると、押している最中に
       無効化された場合に「押下は握り潰したのに離上は素通し」という
       不整合が残る。無効化の意味は chord_set_active() 側に持たせてある。 */
    if (chord_on_mouse((UINT)wParam, m))
        return 1;                      /* ここでイベントは消滅する */

    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

static BOOL hook_install(void)
{
    if (g_mouseHook) return TRUE;
    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, LLMouseProc, g_inst, 0);
    return g_mouseHook != NULL;
}

static void hook_remove(void)
{
    if (g_mouseHook) {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = NULL;
    }
}

/* ================================================================== */
/*  前面ウィンドウの監視                                               */
/* ================================================================== */

static BOOL is_fullscreen(HWND hf)
{
    RECT wr;
    MONITORINFO mi;
    WCHAR cls[64];

    if (!hf || hf == GetShellWindow() || hf == GetDesktopWindow()) return FALSE;
    if (GetClassNameW(hf, cls, ARRAYSIZE(cls))) {
        if (!lstrcmpW(cls, L"WorkerW") || !lstrcmpW(cls, L"Progman") ||
            !lstrcmpW(cls, L"Shell_TrayWnd"))
            return FALSE;
    }
    if (!GetWindowRect(hf, &wr)) return FALSE;

    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(MonitorFromWindow(hf, MONITOR_DEFAULTTONEAREST), &mi)) return FALSE;

    /* 最大化ウィンドウは rcWork に収まるので、ここには引っかからない */
    return wr.left  <= mi.rcMonitor.left  && wr.top    <= mi.rcMonitor.top &&
           wr.right >= mi.rcMonitor.right && wr.bottom >= mi.rcMonitor.bottom;
}

static void foreground_exe(HWND hf, WCHAR *out, size_t cch)
{
    DWORD pid = 0;
    HANDLE h;

    out[0] = 0;
    if (!hf) return;
    GetWindowThreadProcessId(hf, &pid);
    if (!pid) return;

    h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return;
    {
        WCHAR path[MAX_PATH];
        DWORD n = ARRAYSIZE(path);
        if (QueryFullProcessImageNameW(h, 0, path, &n)) {
            WCHAR *b = wcsrchr(path, L'\\');
            lstrcpynW(out, b ? b + 1 : path, (int)cch);
        }
    }
    CloseHandle(h);
}

static void tray_update(void);
static BOOL needs_agent(void);

static void refresh_active(void)
{
    g_active = (g_cfg.enabled && !g_suspended) ? 1 : 0;
    chord_set_active(g_active != 0);
}

static void update_suspend(void)
{
    HWND  hf = GetForegroundWindow();
    WCHAR exe[MAX_PATH];
    BOOL  susp = FALSE;
    WCHAR why[MAX_PATH] = L"";

    foreground_exe(hf, exe, ARRAYSIZE(exe));

    if (exe[0] && cfg_is_excluded(exe)) {
        susp = TRUE;
        lstrcpynW(why, exe, ARRAYSIZE(why));
    } else if (g_cfg.suspendOnFullscreen && is_fullscreen(hf)) {
        susp = TRUE;
        lstrcpynW(why, L"フルスクリーン", ARRAYSIZE(why));
    }

    if (susp != g_suspended || lstrcmpW(why, g_suspendedBy)) {
        DBG("suspend %d -> %d  exe=%ls why=%ls", (int)g_suspended, (int)susp, exe, why);
        g_suspended = susp;
        lstrcpynW(g_suspendedBy, why, ARRAYSIZE(g_suspendedBy));
        refresh_active();
        tray_update();
    }
}

static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                                  LONG idObject, LONG idChild, DWORD thread, DWORD time)
{
    (void)hook; (void)idObject; (void)idChild; (void)thread; (void)time;

    if (event == EVENT_SYSTEM_DESKTOPSWITCH) {
        /* UAC のセキュアデスクトップやロック画面へ移った。その間の離上は
           こちらに届かないので、抱えている状態を持ち越さない。 */
        chord_on_desktop_switch();
        return;
    }
    /* この範囲には CAPTURESTART など高頻度のイベントも混ざる。
       前面の変化だけを拾い、それ以外は即座に捨てる。 */
    if (event == EVENT_SYSTEM_FOREGROUND && hwnd)
        update_suspend();
}

/* ================================================================== */
/*  タスクトレイ                                                       */
/* ================================================================== */

static void tray_tooltip(WCHAR *out, size_t cch)
{
    if (!g_cfg.enabled)
        lstrcpynW(out, MAYOUS_APPNAME L" - 停止中", (int)cch);
    else if (g_suspended) {
        lstrcpynW(out, MAYOUS_APPNAME L" - 一時停止 (", (int)cch);
        lstrcatW(out, g_suspendedBy);
        lstrcatW(out, L")");
    } else
        lstrcpynW(out, MAYOUS_APPNAME L" - 動作中", (int)cch);
}

static void tray_add(void)
{
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = g_hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_MAYOUS_TRAY;
    g_nid.hIcon            = g_iconOn;
    tray_tooltip(g_nid.szTip, ARRAYSIZE(g_nid.szTip));
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void tray_update(void)
{
    g_nid.uFlags = NIF_ICON | NIF_TIP;
    g_nid.hIcon  = (g_cfg.enabled && !g_suspended) ? g_iconOn : g_iconOff;
    tray_tooltip(g_nid.szTip, ARRAYSIZE(g_nid.szTip));
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void tray_balloon(const WCHAR *title, const WCHAR *text)
{
    NOTIFYICONDATAW n = g_nid;
    n.uFlags   = NIF_INFO;
    n.dwInfoFlags = NIIF_INFO;
    lstrcpynW(n.szInfoTitle, title, ARRAYSIZE(n.szInfoTitle));
    lstrcpynW(n.szInfo,      text,  ARRAYSIZE(n.szInfo));
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

static void tray_remove(void)
{
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

static void show_menu(void)
{
    HMENU m = CreatePopupMenu();
    POINT pt;

    AppendMenuW(m, MF_STRING, IDM_SETTINGS, L"設定(&S)...");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (g_cfg.enabled ? MF_CHECKED : 0), IDM_ENABLE, L"有効(&E)");
    AppendMenuW(m, MF_STRING | (startup_enabled() ? MF_CHECKED : 0), IDM_STARTUP,
                L"Windows 起動時に実行(&U)");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_OPENINI, L"設定ファイルを開く(&O)");
    AppendMenuW(m, MF_STRING, IDM_RELOAD,  L"設定を再読み込み(&R)");
    AppendMenuW(m, MF_STRING, IDM_ABOUT,   L"バージョン情報(&A)...");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_EXIT,  L"終了(&X)");

    SetMenuDefaultItem(m, IDM_SETTINGS, FALSE);

    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);        /* これが無いとメニューが閉じなくなる */
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, g_hwnd, NULL);
    PostMessage(g_hwnd, WM_NULL, 0, 0); /* 同上、旧来からの定石 */
    DestroyMenu(m);
}

static void show_about(void)
{
    WCHAR msg[1024], folder[MAX_PATH];

    startup_folder(folder, MAX_PATH);
    wsprintfW(msg,
        L"%s %s\r\n\r\n"
        L"マウスの同時押しをショートカットに変える常駐ツール\r\n\r\n"
        L"設定ファイル:\r\n  %s\r\n\r\n"
        L"スタートアップ:\r\n  %s\r\n\r\n"
        L"割り当ての編集は [設定...] から行えます。",
        MAYOUS_APPNAME, MAYOUS_VERSION, g_cfg.iniPath, folder);
    MessageBoxW(NULL, msg, MAYOUS_APPNAME, MB_OK | MB_ICONINFORMATION);
}

static void reload_config(void)
{
    chord_reset();
    cfg_load();
    chord_recompute();
    g_suspended = FALSE;
    g_suspendedBy[0] = 0;
    refresh_active();
    update_suspend();
    tray_update();
    if (needs_agent()) agent_ensure(); else agent_stop();
}

/* 設定ウィンドウが保存した直後に呼ばれる。再起動なしで反映させる。 */
void settings_apply_callback(void)
{
    reload_config();
}

/* ================================================================== */
/*  ウィンドウプロシージャ                                             */
/* ================================================================== */

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == g_msgTaskbarCreated) {   /* explorer 再起動でアイコンが消えた */
        tray_add();
        tray_update();
        return 0;
    }
    if (msg == g_msgShowInfo) {         /* 二重起動された */
        tray_balloon(MAYOUS_APPNAME, L"すでに常駐しています。");
        return 0;
    }

    switch (msg) {
    case WM_MAYOUS_PUMP:
        /* フックが積んだ注入をここで実行する。
           フックの中で SendInput してはいけない理由は chord.c 冒頭を参照。 */
        chord_pump();
        return 0;

    case WM_MAYOUS_TRAY:
        switch (LOWORD(lp)) {
        case WM_LBUTTONDBLCLK:
            settings_open(g_inst, hwnd);
            break;
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
            show_menu();
            break;
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_SETTINGS:
            settings_open(g_inst, hwnd);
            break;

        case IDM_ENABLE:
            g_cfg.enabled = !g_cfg.enabled;
            WritePrivateProfileStringW(L"General", L"Enabled",
                                       g_cfg.enabled ? L"1" : L"0", g_cfg.iniPath);
            refresh_active();
            tray_update();
            break;

        case IDM_OPENINI:
            ShellExecuteW(NULL, L"open", L"notepad.exe", g_cfg.iniPath, NULL, SW_SHOWNORMAL);
            break;

        case IDM_RELOAD:
            reload_config();
            tray_balloon(MAYOUS_APPNAME, L"設定を再読み込みしました。");
            break;

        case IDM_STARTUP:
            startup_set(!startup_enabled());
            break;

        case IDM_ABOUT:
            show_about();
            break;

        case IDM_EXIT:
            DestroyWindow(hwnd);
            break;
        }
        return 0;

    case WM_TIMER:
        if (wp == TIMER_SANITY) {
            chord_sanity();
            update_suspend();           /* Alt+Enter 等、前面が変わらない全画面化を拾う */
            if (needs_agent()) agent_ensure();
        } else if (wp >= TIMER_HOLD_BASE && wp < TIMER_HOLD_BASE + BTN_COUNT) {
            chord_on_hold_timeout((int)(wp - TIMER_HOLD_BASE));
        }
        chord_pump();
        return 0;

    case WM_SETTINGCHANGE:
    case WM_DISPLAYCHANGE:
        chord_recompute();              /* ドラッグしきい値などを取り直す */
        return 0;

    case WM_QUERYENDSESSION:
        chord_reset();
        return TRUE;

    case WM_ENDSESSION:
        chord_reset();
        hook_remove();
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ================================================================== */

/* ホイール系の割り当てが1つでもあれば、注入用の子プロセスが要る */
static BOOL needs_agent(void)
{
    int pfx;
    for (pfx = 0; pfx < BTN_COUNT; ++pfx) {
        if (g_cfg.chord[CH_ID(pfx, SUF_WUP)].kind != ACT_NONE) return TRUE;
        if (g_cfg.chord[CH_ID(pfx, SUF_WDN)].kind != ACT_NONE) return TRUE;
    }
    return FALSE;
}

static BOOL cmdline_has(const WCHAR *cmd, const WCHAR *opt)
{
    WCHAR a[64], b[64];
    lstrcpynW(a, L"/", ARRAYSIZE(a));  lstrcatW(a, opt);
    lstrcpynW(b, L"--", ARRAYSIZE(b)); lstrcatW(b, opt);
    return wcsstr(cmd, a) != NULL || wcsstr(cmd, b) != NULL;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmdLine, int show)
{
    WNDCLASSEXW wc;
    MSG msg;
    HANDLE mutex;
    BOOL firstRun;

    (void)prev; (void)show;
    g_inst = inst;

    /* ホイール注入専用の子プロセスとして起動された場合。
       この経路ではフックもトレイも設定も一切持たない。 */
    if (cmdline_has(cmdLine, L"wheel-agent")) {
        const WCHAR *p = wcsstr(cmdLine, L"wheel-agent");
        DWORD pid = 0;
        if (p) {
            p += 11;
            while (*p == L' ' || *p == L'=') ++p;
            while (*p >= L'0' && *p <= L'9') pid = pid * 10 + (DWORD)(*p++ - L'0');
        }
        return agent_main(inst, pid);
    }

    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    g_msgShowInfo       = RegisterWindowMessageW(L"MayousShowInfo");

    /* 既存インスタンスへの終了指示(アンインストールや入れ替え用) */
    if (cmdline_has(cmdLine, L"exit")) {
        HWND prevWnd = FindWindowW(MAYOUS_WNDCLASS, NULL);
        if (prevWnd) PostMessageW(prevWnd, WM_CLOSE, 0, 0);
        return 0;
    }

    /* 二重起動の抑止 */
    mutex = CreateMutexW(NULL, TRUE, MAYOUS_MUTEX);
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        PostMessageW(HWND_BROADCAST, g_msgShowInfo, 0, 0);
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    cfg_resolve_path();
    firstRun = cfg_write_default_if_missing();
    cfg_load();
    startup_cleanup_legacy();   /* 旧バージョンが残した Run キーを掃除 */

    /* トレイは小アイコン。サイズを明示しないと 32x32 が縮小されて滲む。 */
    {
        int cx = GetSystemMetrics(SM_CXSMICON);
        int cy = GetSystemMetrics(SM_CYSMICON);
        g_iconOn  = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_MAIN), IMAGE_ICON, cx, cy, LR_SHARED);
        g_iconOff = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_OFF),  IMAGE_ICON, cx, cy, LR_SHARED);
        /* UNICODE の定義有無に左右されないよう、リソース ID を直接書く */
        if (!g_iconOn)  g_iconOn  = LoadIconW(NULL, MAKEINTRESOURCEW(32512));
        if (!g_iconOff) g_iconOff = g_iconOn;
    }

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = inst;
    wc.lpszClassName = MAYOUS_WNDCLASS;
    wc.hIcon         = g_iconOn;
    if (!RegisterClassExW(&wc)) {
        MessageBoxW(NULL, L"ウィンドウクラスの登録に失敗しました。", MAYOUS_APPNAME, MB_ICONERROR);
        return 1;
    }

    /* メッセージ専用ウィンドウにはしない。TaskbarCreated のブロードキャストは
       トップレベルウィンドウにしか届かないため。 */
    g_hwnd = CreateWindowExW(0, MAYOUS_WNDCLASS, MAYOUS_APPNAME, WS_OVERLAPPED,
                             0, 0, 0, 0, NULL, NULL, inst, NULL);
    if (!g_hwnd) {
        MessageBoxW(NULL, L"ウィンドウの生成に失敗しました。", MAYOUS_APPNAME, MB_ICONERROR);
        return 1;
    }

    chord_init(g_hwnd);
    chord_recompute();

    if (!hook_install()) {
        MessageBoxW(NULL,
            L"マウスフックの設置に失敗しました。\r\n"
            L"他の常駐ツールと競合している可能性があります。",
            MAYOUS_APPNAME, MB_ICONERROR);
        DestroyWindow(g_hwnd);
        return 1;
    }

    /* EVENT_SYSTEM_FOREGROUND(0x03) 〜 EVENT_SYSTEM_DESKTOPSWITCH(0x20) を
       ひとつのフックでまとめて受ける。必要な 2 つ以外は WinEventProc で捨てる。 */
    g_winEvent = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_DESKTOPSWITCH,
                                 NULL, WinEventProc, 0, 0,
                                 WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    tray_add();
    refresh_active();
    update_suspend();
    tray_update();

    if (needs_agent()) agent_ensure();
    SetTimer(g_hwnd, TIMER_SANITY, 1000, NULL);

    if (firstRun)
        tray_balloon(MAYOUS_APPNAME L" を開始しました",
                     L"右押し+左クリック = Windows キー / 右押し+ホイール = 左右スクロール / "
                     L"左押し+右クリック = Alt+Tab\r\nトレイアイコンから設定できます。");

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        HWND sw = settings_hwnd();
        if (sw && IsDialogMessageW(sw, &msg)) continue;   /* Tab 移動・Enter・Esc */
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    KillTimer(g_hwnd, TIMER_SANITY);
    if (g_winEvent) UnhookWinEvent(g_winEvent);
    hook_remove();
    chord_reset();
    agent_stop();
    tray_remove();
    if (mutex) { ReleaseMutex(mutex); CloseHandle(mutex); }
    return 0;
}
