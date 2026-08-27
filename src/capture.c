/* ==================================================================
 * capture.c - キー入力の記録
 *
 *  実際にキーを押してもらい、その組み合わせを設定文字列に変換する。
 *
 *  ・低レベルキーボードフックで全キーを握り潰しながら記録する。
 *    こうしないと記録中の Alt+Tab や Win キーで OS 側が反応してしまう。
 *  ・同時に押された一組を 1 ステップとして扱い、全部離れた時点で確定する。
 *    続けて別の組を押せば 2 ステップ目になる(例: ctrl+c, ctrl+v)。
 *  ・登録キーのトリガーを決めるときだけは「キーを 1 つ」モードで動く
 *    (capture_run_key)。押すたびに最後の 1 つで上書きする。
 *  ・キーボードを全部握り潰すので、逃げ道は必ずマウスで確保しておく。
 *    ボタンは常に押せるため、記録が暴走しても [キャンセル] で必ず抜けられる。
 *
 *  フックの中で SendInput をしないという原則はここでも同じだが、
 *  記録中は何も注入しないので問題にならない。
 *
 *  配色は設定ウィンドウと同じ theme.c に従う。窓の背景は自分で塗り、
 *  文字色はコントロールごとに WM_CTLCOLOR* で指定する。
 *
 *  【大きさを決め打ちにしない】
 *  かつてはウィンドウも文字の置き場所も px 決め打ちだった。フォントの
 *  大きさや DPI が違う環境では、それだけで文章の右や下が切れる
 *  (「…マウスで押してく」で終わる / 行の下半分が隠れる、と報告があった)。
 *  文字の幅も高さも環境ごとに違うので、決め打ちで足りるはずがない。
 *  そこで、出す文章を実際に測ってから、それが収まる大きさで窓を作る。
 * ================================================================== */

#include "common.h"
#include <wchar.h>

#define WNDCLASS_CAPTURE L"MayousCaptureWnd"
#define WM_CAP_REFRESH   (WM_APP + 11)   /* 表示更新をフック外へ逃がす */

#define IDC_CAP_OK     1
#define IDC_CAP_CLEAR  2
#define IDC_CAP_CANCEL 3

static HWND   g_wnd;
static HWND   g_hView, g_hHint;
static HHOOK  g_kbHook;
static HFONT  g_font;
static int    g_unit = 16;   /* レイアウトの基準 = フォントの高さ */
static BOOL   g_ok;
static BOOL   g_oneKey;      /* トリガー用: キーを 1 つだけ記録する */

/* 記録中のステップ列 */
static KeyStep g_steps[MAX_ACTION_STEPS];
static int     g_nsteps;

/* いま押されているキー(押された順) */
static WORD    g_held[MAX_ACTION_KEYS];
static int     g_nheld;
static BOOL    g_stepDirty;                 /* 今の押下をまだ確定していない */

/* ------------------------------------------------------------------ */

/* 修飾キーは左右をまとめる。記録の表記を安定させるため。 */
static WORD normalize_vk(DWORD vk)
{
    switch (vk) {
    case VK_LCONTROL: case VK_RCONTROL: case VK_CONTROL: return VK_LCONTROL;
    case VK_LMENU:    case VK_RMENU:    case VK_MENU:    return VK_LMENU;
    case VK_LSHIFT:   case VK_RSHIFT:   case VK_SHIFT:   return VK_LSHIFT;
    default: return (WORD)vk;
    }
}

static BOOL is_modifier(WORD vk)
{
    return vk == VK_LCONTROL || vk == VK_LMENU || vk == VK_LSHIFT ||
           vk == VK_LWIN     || vk == VK_RWIN;
}

/* 記録済みの内容を "ctrl+c, ctrl+v" 形式にする */
static void build_spec(WCHAR *out, int cch, BOOL includeHeld)
{
    int s, i;

    out[0] = 0;
    for (s = 0; s < g_nsteps; ++s) {
        if (out[0]) lstrcatW(out, L", ");
        for (i = 0; i < g_steps[s].nkeys; ++i) {
            if (i) lstrcatW(out, L"+");
            lstrcatW(out, cfg_vk_name(g_steps[s].keys[i]));
        }
    }
    if (includeHeld && g_nheld > 0) {
        if (out[0]) lstrcatW(out, L", ");
        for (i = 0; i < g_nheld; ++i) {
            if (i) lstrcatW(out, L"+");
            lstrcatW(out, cfg_vk_name(g_held[i]));
        }
    }
    (void)cch;
}

/* 低レベルフックの中から直接ウィンドウを触ると、コールバックが伸びて
   OS にフックを外されかねない。更新要求だけ投げて、実処理は
   メッセージループ側(WM_CAP_REFRESH)で行う。 */
static void refresh_view(void)
{
    if (g_wnd) PostMessageW(g_wnd, WM_CAP_REFRESH, 0, 0);
}

static void refresh_view_now(void)
{
    WCHAR spec[ACTION_SPEC_CCH];
    if (!g_hView) return;
    build_spec(spec, ARRAYSIZE(spec), TRUE);
    SetWindowTextW(g_hView, spec[0] ? spec
                                    : (g_oneKey ? L"(未設定)"
                                                : L"(まだ何も記録されていません)"));
}

/* 押されているキーが全部離れた -> 1 ステップとして確定する */
static void commit_step(void)
{
    if (!g_stepDirty || g_nheld != 0) return;
    g_stepDirty = FALSE;
    refresh_view();
}

static void on_key_down(WORD vk)
{
    int i;

    /* トリガーの記録。組み合わせにはせず、最後に押したキーで上書きする。 */
    if (g_oneKey) {
        g_nsteps         = 1;
        g_steps[0].nkeys = 1;
        g_steps[0].keys[0] = vk;
        g_nheld     = 0;
        g_stepDirty = FALSE;
        refresh_view();
        return;
    }

    /* 前のステップが確定済みで、かつ上限に達していたら無視 */
    if (!g_stepDirty && g_nsteps >= MAX_ACTION_STEPS) return;

    for (i = 0; i < g_nheld; ++i)
        if (g_held[i] == vk) return;              /* オートリピート */

    if (!g_stepDirty) {                            /* 新しいステップの開始 */
        g_stepDirty = TRUE;
        g_steps[g_nsteps].nkeys = 0;
        g_nheld = 0;
    }
    if (g_nheld < MAX_ACTION_KEYS) g_held[g_nheld++] = vk;

    /* 修飾キーは前に、実キーは後ろに来るよう、押された順のまま貯める。
       修飾キーだけのステップも「win」のように有効な指定になる。 */
    {
        KeyStep *st = &g_steps[g_nsteps];
        if (st->nkeys < MAX_ACTION_KEYS) {
            /* 既に実キーが入っている状態で修飾キーが来たら差し込む */
            if (is_modifier(vk) && st->nkeys > 0 && !is_modifier(st->keys[st->nkeys - 1])) {
                st->keys[st->nkeys] = st->keys[st->nkeys - 1];
                st->keys[st->nkeys - 1] = vk;
                st->nkeys++;
            } else {
                st->keys[st->nkeys++] = vk;
            }
        }
    }
    if (g_nsteps < MAX_ACTION_STEPS && g_steps[g_nsteps].nkeys > 0) {
        /* まだ確定していないので nsteps は進めない */
    }
    refresh_view();
}

static void on_key_up(WORD vk)
{
    int i, j;

    if (g_oneKey) return;               /* 押した時点で確定済み */

    for (i = 0; i < g_nheld; ++i) {
        if (g_held[i] != vk) continue;
        for (j = i; j + 1 < g_nheld; ++j) g_held[j] = g_held[j + 1];
        --g_nheld;
        break;
    }
    if (g_nheld == 0 && g_stepDirty) {
        if (g_steps[g_nsteps].nkeys > 0 && g_nsteps < MAX_ACTION_STEPS) g_nsteps++;
        g_stepDirty = FALSE;
        refresh_view();
    }
}

static LRESULT CALLBACK CaptureKbProc(int code, WPARAM wp, LPARAM lp)
{
    const KBDLLHOOKSTRUCT *k = (const KBDLLHOOKSTRUCT *)lp;

    if (code != HC_ACTION || k->dwExtraInfo == MAYOUS_TAG)
        return CallNextHookEx(NULL, code, wp, lp);

    if (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN)
        on_key_down(normalize_vk(k->vkCode));
    else if (wp == WM_KEYUP || wp == WM_SYSKEYUP)
        on_key_up(normalize_vk(k->vkCode));

    return 1;                       /* 記録中はキーを一切世に出さない */
}

/* ------------------------------------------------------------------ */

static void clear_all(void)
{
    g_nsteps = 0;
    g_nheld = 0;
    g_stepDirty = FALSE;
    ZeroMemory(g_steps, sizeof(g_steps));
    refresh_view();
}

static LRESULT CALLBACK CaptureProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CAP_REFRESH:
        refresh_view_now();
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_CAP_OK:     g_ok = TRUE;  DestroyWindow(hwnd); break;
        case IDC_CAP_CLEAR:  clear_all();                       break;
        case IDC_CAP_CANCEL: g_ok = FALSE; DestroyWindow(hwnd); break;
        }
        return 0;

    case WM_ERASEBKGND: {
        RECT r;
        GetClientRect(hwnd, &r);
        FillRect((HDC)wp, &r, theme_back_brush());
        return 1;
    }

    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC: {
        HBRUSH br = NULL;
        /* 記録した内容の枠だけは、周りより明るくして入力欄らしく見せる
           (ダークでは WS_EX_CLIENTEDGE の彫り込みが使えないため) */
        if (theme_is_dark() && (HWND)lp == g_hView) {
            SetTextColor((HDC)wp, theme_text());
            SetBkColor((HDC)wp, theme_ctrl_back());
            return (LRESULT)theme_ctrl_brush();
        }
        if (theme_ctlcolor(msg, (HDC)wp, &br)) return (LRESULT)br;
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }

    case WM_CLOSE:
        g_ok = FALSE;
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        /* ここで PostQuitMessage を呼んではいけない。入れ子のループを抜けた後も
           WM_QUIT がキューに残り、本体のメッセージループがそれを拾って
           mayous ごと終了してしまう(実際にそうなった)。
           ループは g_wnd が NULL になったことで抜ける。 */
        if (g_kbHook) { UnhookWindowsHookEx(g_kbHook); g_kbHook = NULL; }
        g_wnd = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/*  レイアウト                                                         */
/* ------------------------------------------------------------------ */

/* 96dpi で書いた値を、実際のフォントの高さに合わせて伸ばす */
static int U(int v) { return MulDiv(v, g_unit, 16); }

/* 設定ウィンドウと同じ字面・同じ大きさのフォントを、自分の持ち物として作る。
 *
 *  借りたまま使ってはいけない。こちらはモーダルループの最中も設定ウィンドウ宛の
 *  メッセージを配り続けるので、その間に配色が切り替わると設定ウィンドウが
 *  自分を作り直し、こちらが握っていたフォントは削除される。複製を持てば
 *  向こうが何をしようと影響を受けない。
 */
static void make_font(void)
{
    NONCLIENTMETRICSW ncm;
    LOGFONTW lf;
    HFONT src, old;
    TEXTMETRICW tm;
    HDC dc;

    if (g_font) { DeleteObject(g_font); g_font = NULL; }

    src = settings_font();
    if (src && GetObjectW(src, sizeof(lf), &lf))
        g_font = CreateFontIndirectW(&lf);
    if (!g_font) {
        ncm.cbSize = sizeof(ncm);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
            g_font = CreateFontIndirectW(&ncm.lfMessageFont);
    }
    if (!g_font) g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    dc  = GetDC(NULL);
    old = (HFONT)SelectObject(dc, g_font);
    if (GetTextMetricsW(dc, &tm)) g_unit = (int)tm.tmHeight;
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    if (g_unit < 10) g_unit = 16;
}

/* コントロールを 1 つ作る。フォントと配色をまとめて面倒みる。 */
static HWND mk(HWND parent, HINSTANCE inst, const WCHAR *cls, const WCHAR *text,
               DWORD exStyle, DWORD style, int x, int y, int w, int h, HMENU id)
{
    HWND c = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style,
                             x, y, w, h, parent, id, inst, NULL);
    if (c) {
        SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
        theme_apply_control(c, cls);
    }
    return c;
}

/* s を今のフォントで描いたときの大きさ。
   maxW > 0 なら、その幅で折り返したときの高さを返す(STATIC の既定と同じ折り方)。
   測るフォントと、実際にコントロールへ入れるフォントは必ず同じものにすること。 */
static void measure(const WCHAR *s, int maxW, SIZE *out)
{
    HDC   dc  = GetDC(NULL);
    HFONT old = (HFONT)SelectObject(dc, g_font);
    UINT  fmt = DT_CALCRECT | DT_NOPREFIX;
    RECT  r;

    r.left = r.top = r.bottom = 0;
    r.right = (maxW > 0) ? maxW : 30000;
    if (maxW > 0) fmt |= DT_WORDBREAK;

    DrawTextW(dc, s, -1, &r, fmt);
    out->cx = r.right - r.left;
    out->cy = r.bottom - r.top;

    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
}

/* 記録ウィンドウを出して、確定したら out に設定文字列を入れて TRUE を返す。
   呼び出し元(設定ウィンドウ)は閉じるまでブロックされる。
   oneKey のときはキーを 1 つだけ記録し、何も押さずに OK なら空を返す
   (登録キーの解除)。 */
static BOOL capture_do(HINSTANCE inst, HWND owner, WCHAR *out, int cch, BOOL oneKey)
{
    static const WCHAR *kBtn[3] = { L"消去", L"OK", L"キャンセル" };
    static BOOL registered;
    WNDCLASSEXW wc;
    MONITORINFO mi;
    MSG  msg;
    RECT orc, rc;
    SIZE hz, nz, bz;
    const WCHAR *hint, *note;
    int  x, y, w, h, i;
    int  m, cw, ch, limit, btnW, btnH, viewH;
    int  yHint, yView, yNote, yBtn;

    if (g_wnd) return FALSE;

    if (!registered) {
        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = CaptureProc;
        wc.hInstance     = inst;
        wc.hCursor       = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
        wc.hbrBackground = NULL;          /* 背景は WM_ERASEBKGND で塗る */
        wc.lpszClassName = WNDCLASS_CAPTURE;
        if (!RegisterClassExW(&wc)) return FALSE;
        registered = TRUE;
    }

    theme_init();                       /* 設定画面が先に呼んでいるが、念のため */
    make_font();

    hint = oneKey
         ? L"マウスのボタンと組み合わせるキーを 1 つ押してください。\r\n"
           L"押し直すと上書きされます。何も押さずに [OK] で登録を解除します。"
         : L"割り当てたいキーを実際に押してください。\r\n"
           L"続けて別のキーを押すと、順番に再生される複数ステップになります。";
    note = L"記録中はキーボードが一時的に無効になります。ボタンはマウスで押してください。";

    /* 横幅は文章に合わせて伸ばすが、画面からはみ出しては元も子もない。
       上限に当たった文章は折り返す(STATIC は既定で折り返す)。 */
    m  = U(14);
    mi.cbSize = sizeof(mi);
    limit = GetMonitorInfoW(MonitorFromWindow(owner ? owner : GetDesktopWindow(),
                                              MONITOR_DEFAULTTOPRIMARY), &mi)
          ? (mi.rcWork.right - mi.rcWork.left) * 3 / 4
          : U(600);

    measure(hint, 0, &hz);
    measure(note, 0, &nz);
    cw = (hz.cx > nz.cx) ? hz.cx : nz.cx;
    if (cw > limit - m * 2) cw = limit - m * 2;
    if (cw < U(360))        cw = U(360);   /* 記録した内容を出す枠が狭くなりすぎない */
    measure(hint, cw, &hz);                /* 折り返したぶん高さが伸びる */
    measure(note, cw, &nz);

    btnW = 0;
    for (i = 0; i < 3; ++i) {
        measure(kBtn[i], 0, &bz);
        if (bz.cx > btnW) btnW = bz.cx;
    }
    btnW += U(28);                         /* 文字の左右の余白 */
    if (btnW < U(90)) btnW = U(90);
    btnH  = g_unit + U(12);
    viewH = g_unit + U(14);

    ch    = m;
    yHint = ch;  ch += hz.cy + U(12);
    yView = ch;  ch += viewH + U(12);
    yNote = ch;  ch += nz.cy + U(16);
    yBtn  = ch;  ch += btnH  + m;

    rc.left   = 0;
    rc.top    = 0;
    rc.right  = cw + m * 2;
    rc.bottom = ch;
    AdjustWindowRectEx(&rc, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE,
                       WS_EX_DLGMODALFRAME | WS_EX_TOPMOST);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;

    if (GetWindowRect(owner, &orc)) {
        x = orc.left + ((orc.right - orc.left) - w) / 2;
        y = orc.top  + ((orc.bottom - orc.top) - h) / 2;
    } else {
        x = y = CW_USEDEFAULT;
    }

    g_oneKey = oneKey;
    g_wnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, WNDCLASS_CAPTURE,
                            oneKey ? L"キーを登録" : L"キー入力を記録",
                            WS_POPUP | WS_CAPTION | WS_SYSMENU,
                            x, y, w, h, owner, NULL, inst, NULL);
    if (!g_wnd) return FALSE;
    theme_apply_window(g_wnd);          /* タイトルバーも暗くする */

    {
        HWND c;
        g_hHint = mk(g_wnd, inst, L"STATIC", hint, 0, 0, m, yHint, cw, hz.cy, NULL);

        /* 記録した内容は長くなりうる(複数ステップ)。入りきらないときは
           途中で切らず、末尾を ... にして「続きがある」と分かるようにする。
           枠は、ライトでは彫り込み、ダークでは細い線 + 明るい背景で出す。 */
        g_hView = mk(g_wnd, inst, L"STATIC", L"",
                theme_is_dark() ? 0 : WS_EX_CLIENTEDGE,
                SS_CENTER | SS_CENTERIMAGE | SS_ENDELLIPSIS |
                (theme_is_dark() ? WS_BORDER : 0),
                m, yView, cw, viewH, NULL);

        mk(g_wnd, inst, L"STATIC", note, 0, 0, m, yNote, cw, nz.cy, NULL);

        c = mk(g_wnd, inst, L"BUTTON", kBtn[0], 0, 0,
               m, yBtn, btnW, btnH, (HMENU)IDC_CAP_CLEAR);
        c = mk(g_wnd, inst, L"BUTTON", kBtn[1], 0, 0,
               m + cw - btnW * 2 - U(8), yBtn, btnW, btnH, (HMENU)IDC_CAP_OK);
        c = mk(g_wnd, inst, L"BUTTON", kBtn[2], 0, 0,
               m + cw - btnW, yBtn, btnW, btnH, (HMENU)IDC_CAP_CANCEL);
        (void)c;
    }

    clear_all();
    g_ok = FALSE;

    EnableWindow(owner, FALSE);            /* 疑似モーダル */
    ShowWindow(g_wnd, SW_SHOW);
    SetForegroundWindow(g_wnd);

    g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, CaptureKbProc, inst, 0);
    if (!g_kbHook) {
        EnableWindow(owner, TRUE);
        DestroyWindow(g_wnd);
        MessageBoxW(owner, L"キーボードフックを設置できませんでした。",
                    MAYOUS_APPNAME, MB_OK | MB_ICONERROR);
        return FALSE;
    }

    /* 自前のモーダルループ。記録ウィンドウが閉じるまでここに留まる。
       記録中に本体が終了要求を受けた場合は、WM_QUIT を投げ直して
       外側のループに正しく伝える。 */
    while (g_wnd) {
        BOOL r = GetMessageW(&msg, NULL, 0, 0);
        if (r <= 0) {
            if (r == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    if (g_font) { DeleteObject(g_font); g_font = NULL; }

    if (!g_ok) return FALSE;
    commit_step();
    build_spec(out, cch, FALSE);
    /* トリガーは「空 = 解除」も正しい結果なので、中身の有無で判断しない。 */
    return oneKey ? TRUE : (out[0] != 0);
}

BOOL capture_run(HINSTANCE inst, HWND owner, WCHAR *out, int cch)
{
    return capture_do(inst, owner, out, cch, FALSE);
}

BOOL capture_run_key(HINSTANCE inst, HWND owner, WCHAR *out, int cch)
{
    return capture_do(inst, owner, out, cch, TRUE);
}
