/* ==================================================================
 * chord.c - 同時押し判定の中核
 *
 *  ボタン押下を「保留」し、その先に何が起きたかで運命を決める。
 *
 *      PS_IDLE ──押下(飲み込む)──> PS_PENDING
 *                                    │
 *          相方/ホイールが来た ──────┼──> PS_CONSUMED   離すまで完全に殺す
 *          しきい値を超えて移動 ─────┼──> PS_PASSTHRU   本物のDOWNを注入して素通し
 *          長押しタイムアウト ───────┘        (ドラッグ・長押しの救済)
 *                                    │
 *          そのまま離された ─────────┴──> 単独クリックとして成立させる
 *                                          (別機能に置き換えることもできる)
 *
 *  プレフィクスになれるのは 左・右・サイド1・サイド2 の 4 つ。
 *  サフィックスはそれに中ボタンとホイール上下を加えた 7 つ。
 *
 *  自分で SendInput したイベントには MAYOUS_TAG を付け、フック側で
 *  無条件に素通しさせることで再入を断ち切っている。
 * ================================================================== */

#include "common.h"
#include <stdlib.h>

/* ---------------- 状態 ---------------- */

typedef enum {
    PS_IDLE = 0,   /* 押されていない                                   */
    PS_PENDING,    /* 押下を飲み込んで保留中。運命は未定               */
    PS_CONSUMED,   /* 同時押しが成立した。対応する離上も殺す           */
    PS_PASSTHRU    /* 本物の押下を注入済み。以後は素通し               */
} PfxState;

typedef struct {
    PfxState  st;
    POINT     anchor;      /* 押下位置(ドラッグ判定の基準)           */
    ULONGLONG t0;          /* 状態に入った時刻(スタック検出用)       */
} PfxSlot;

static PfxSlot   g_pfx[BTN_COUNT];
static BOOL      g_swallowUp[BTN_COUNT];   /* サフィックス役で飲み込んだ押下の離上を殺す */
static ULONGLONG g_swallowT[BTN_COUNT];

static HWND      g_hwnd;
static BOOL      g_on = FALSE;             /* 新しくボタンを乗っ取ってよいか */
static int       g_dragThresh = 8;
static BOOL      g_armed[BTN_COUNT];       /* そもそも乗っ取る必要があるか */

static const int   kVk[BTN_COUNT] = {
    VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2
};
static const DWORD kDownFlag[BTN_COUNT] = {
    MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_MIDDLEDOWN,
    MOUSEEVENTF_XDOWN,    MOUSEEVENTF_XDOWN
};
static const DWORD kUpFlag[BTN_COUNT] = {
    MOUSEEVENTF_LEFTUP,   MOUSEEVENTF_RIGHTUP,   MOUSEEVENTF_MIDDLEUP,
    MOUSEEVENTF_XUP,      MOUSEEVENTF_XUP
};
/* サイドボタンは mouseData でどちらかを指定する */
static const DWORD kXData[BTN_COUNT] = { 0, 0, 0, XBUTTON1, XBUTTON2 };

/* ==================================================================
 *  注入キュー
 *
 *  【絶対にフックの中から SendInput を呼ばないこと】
 *  低レベルフックのコールバックの中で SendInput を呼ぶと、実測で
 *  250〜450ms のあいだ戻ってこない。注入した入力を処理するのは、
 *  今まさにこちらの応答を待っている当のスレッドだからである。
 *  その間に Windows は LowLevelHooksTimeout でフックを見限り、
 *  「握り潰したはずのイベント」をアプリへ配送してしまう。
 *  結果、押下だけが消えて離上が届く/クリックが二重になる、といった
 *  収拾のつかない壊れ方をする(実測ログで確認済み)。
 *
 *  そこでフックの中では「握り潰すか否か」の判断だけを行い、
 *  実際の注入はキューに積んでメッセージループへ回す。
 *  フックから戻った直後に処理されるので、体感上の遅れは無い。
 * ================================================================== */

enum { Q_CLICK, Q_DOWN, Q_UP, Q_KEYS, Q_HWHEEL };

typedef struct {
    BYTE    kind;
    BYTE    btn;
    BYTE    nsteps;
    int     amount;
    KeyStep steps[MAX_ACTION_STEPS];
} QItem;

#define QCAP 32
static QItem g_q[QCAP];
static int   g_qHead, g_qTail;      /* フックもポンプも同一スレッド = ロック不要 */

static void q_push(BYTE kind, int btn, int amount, const Action *a)
{
    int next = (g_qTail + 1) % QCAP;
    QItem *it;

    if (next == g_qHead) return;    /* 溢れたら捨てる(詰まるより落とすほうが安全) */

    it = &g_q[g_qTail];
    ZeroMemory(it, sizeof(*it));
    it->kind   = kind;
    it->btn    = (BYTE)btn;
    it->amount = amount;
    if (a && kind == Q_KEYS) {
        it->nsteps = (BYTE)a->nsteps;
        CopyMemory(it->steps, a->steps, (size_t)a->nsteps * sizeof(KeyStep));
    }
    g_qTail = next;

    if (g_hwnd) PostMessageW(g_hwnd, WM_MAYOUS_PUMP, 0, 0);
}

/* ---------------- 入力の合成 ---------------- */

static BOOL is_extended_vk(WORD vk)
{
    switch (vk) {
    case VK_LWIN: case VK_RWIN: case VK_APPS:
    case VK_RCONTROL: case VK_RMENU:
    case VK_INSERT: case VK_DELETE:
    case VK_HOME:   case VK_END:
    case VK_PRIOR:  case VK_NEXT:
    case VK_LEFT:   case VK_RIGHT: case VK_UP: case VK_DOWN:
    case VK_NUMLOCK: case VK_SNAPSHOT: case VK_DIVIDE:
    case VK_BROWSER_BACK: case VK_BROWSER_FORWARD:
    case VK_VOLUME_UP: case VK_VOLUME_DOWN: case VK_VOLUME_MUTE:
    case VK_MEDIA_NEXT_TRACK: case VK_MEDIA_PREV_TRACK: case VK_MEDIA_PLAY_PAUSE:
        return TRUE;
    default:
        return FALSE;
    }
}

static void fill_key(INPUT *in, WORD vk, BOOL up)
{
    ZeroMemory(in, sizeof(*in));
    in->type       = INPUT_KEYBOARD;
    in->ki.wVk     = vk;
    in->ki.wScan   = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    in->ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0)
                   | (is_extended_vk(vk) ? KEYEVENTF_EXTENDEDKEY : 0);
    in->ki.dwExtraInfo = MAYOUS_TAG;
}

/* --- ここから下の emit_* は、必ずメッセージループ側から呼ばれる --- */

/* 1ステップは押す順・離す逆順を 1 回の SendInput にまとめて送る。
   途中に他の入力が割り込まないので Alt+Tab のような組み合わせが確実に決まる。
   複数ステップの場合はステップごとに分けて送る。 */
static void emit_keys(const KeyStep *steps, int nsteps)
{
    INPUT in[MAX_ACTION_KEYS * 2];
    int s, i, n;

    for (s = 0; s < nsteps; ++s) {
        const KeyStep *k = &steps[s];
        n = 0;
        for (i = 0; i < k->nkeys; ++i)      fill_key(&in[n++], k->keys[i], FALSE);
        for (i = k->nkeys - 1; i >= 0; --i) fill_key(&in[n++], k->keys[i], TRUE);
        SendInput((UINT)n, in, sizeof(INPUT));
    }
}

/* 座標を指定しない(MOUSEEVENTF_MOVE を立てない)ので、現在のカーソル位置に落ちる。
   カーソルを飛ばさずに済むぶん、こちらのほうが体感が自然。 */
static void emit_button(int btn, BOOL down)
{
    INPUT in;
    ZeroMemory(&in, sizeof(in));
    in.type           = INPUT_MOUSE;
    in.mi.dwFlags     = down ? kDownFlag[btn] : kUpFlag[btn];
    in.mi.mouseData   = kXData[btn];
    in.mi.dwExtraInfo = MAYOUS_TAG;
    SendInput(1, &in, sizeof(in));
}

static void emit_click(int btn)
{
    INPUT in[2];
    ZeroMemory(in, sizeof(in));
    in[0].type = in[1].type = INPUT_MOUSE;
    in[0].mi.dwFlags     = kDownFlag[btn];
    in[1].mi.dwFlags     = kUpFlag[btn];
    in[0].mi.mouseData   = in[1].mi.mouseData   = kXData[btn];
    in[0].mi.dwExtraInfo = in[1].mi.dwExtraInfo = MAYOUS_TAG;
    SendInput(2, in, sizeof(INPUT));
}

/* キューを吐き出す。メッセージループからのみ呼ぶこと。 */
void chord_pump(void)
{
    while (g_qHead != g_qTail) {
        QItem it = g_q[g_qHead];
        g_qHead = (g_qHead + 1) % QCAP;

        DBG("pump kind=%d btn=%d amount=%d", (int)it.kind, (int)it.btn, it.amount);
        switch (it.kind) {
        case Q_CLICK: emit_click(it.btn);            break;
        case Q_DOWN:  emit_button(it.btn, TRUE);     break;
        case Q_UP:    emit_button(it.btn, FALSE);    break;
        case Q_KEYS:  emit_keys(it.steps, it.nsteps);break;
        case Q_HWHEEL:
            /* このプロセスは WH_MOUSE_LL を保持しているため、自分で SendInput
               してもホイールだけは OS に捨てられる(詳細は agent.c 冒頭)。 */
            agent_send_wheel(it.amount, TRUE);
            break;
        }
    }
}

/* --- フック側から呼ぶのはこちら。キューに積むだけで即座に戻る。 --- */

static void inject_button(int btn, BOOL down) { q_push(down ? Q_DOWN : Q_UP, btn, 0, NULL); }
static void inject_click(int btn)             { q_push(Q_CLICK, btn, 0, NULL); }
static void inject_hwheel(int amount)         { q_push(Q_HWHEEL, 0, amount, NULL); }
static void inject_keys(const Action *a)      { q_push(Q_KEYS, 0, 0, a); }

/* ---------------- タイマー ---------------- */

static void hold_timer_arm(int b)
{
    if (g_hwnd && g_cfg.holdTimeoutMs[b] > 0)
        SetTimer(g_hwnd, TIMER_HOLD_BASE + b, (UINT)g_cfg.holdTimeoutMs[b], NULL);
}

static void hold_timer_kill(int b)
{
    if (g_hwnd) KillTimer(g_hwnd, TIMER_HOLD_BASE + b);
}

/* ---------------- 状態遷移 ---------------- */

static BOOL pfx_active(int b)
{
    return g_pfx[b].st == PS_PENDING || g_pfx[b].st == PS_CONSUMED;
}

static void pfx_set(int b, PfxState st)
{
    DBG("pfx[%d] %d -> %d", b, (int)g_pfx[b].st, (int)st);
    g_pfx[b].st = st;
    g_pfx[b].t0 = GetTickCount64();
}

/* 保留をやめ、本物の押下として世に出す(ドラッグ・長押しの救済) */
static void pfx_promote(int b)
{
    if (g_pfx[b].st != PS_PENDING) return;
    hold_timer_kill(b);
    pfx_set(b, PS_PASSTHRU);
    inject_button(b, TRUE);
}

static void fire_action(const Action *a, int wheelAmount)
{
    switch (a->kind) {
    case ACT_KEYS:         inject_keys(a);               break;
    case ACT_HWHEEL_LEFT:  inject_hwheel(-wheelAmount);  break;
    case ACT_HWHEEL_RIGHT: inject_hwheel(+wheelAmount);  break;
    default: break;
    }
}

/* プレフィクス pfx × サフィックス suf に割り当てがあれば返す。無ければ NULL。 */
static const Action *chord_at(int pfx, int suf)
{
    const Action *a;
    if (!PFX_CAN(pfx) || pfx == suf) return NULL;
    a = &g_cfg.chord[CH_ID(pfx, suf)];
    return (a->kind != ACT_NONE) ? a : NULL;
}

/* ---------------- 公開関数 ---------------- */

void chord_init(HWND hwnd)
{
    g_hwnd = hwnd;
}

/* 設定変更後に呼ぶ。どのボタンを乗っ取る必要があるかを再計算する。
   割り当てが全部 none のボタンには一切手を触れない = 副作用ゼロ。 */
void chord_recompute(void)
{
    int b, s;

    for (b = 0; b < BTN_COUNT; ++b) {
        g_armed[b] = FALSE;
        if (!PFX_CAN(b)) continue;
        /* 単独クリックが「そのまま」以外なら、それだけでも乗っ取りが要る */
        if (g_cfg.single[b].kind != ACT_PASSTHRU) { g_armed[b] = TRUE; continue; }
        for (s = 0; s < SUF_COUNT; ++s)
            if (chord_at(b, s)) { g_armed[b] = TRUE; break; }
    }

    if (g_cfg.dragThreshold > 0) {
        g_dragThresh = g_cfg.dragThreshold;
    } else {
        int sys = GetSystemMetrics(SM_CXDRAG) * 2;
        g_dragThresh = (sys > 8) ? sys : 8;
    }
}

/* 抱えている状態を安全に畳む。
   deliverPending:
     TRUE  … 握り潰したままの押下を通常クリックとして世に出す(設定変更・終了時)
     FALSE … 握り潰した押下は捨てる(UAC 画面への切替など、操作自体が中断された場合)
   どちらの場合も「押下だけ出して離上が出ない」状態は絶対に作らない。 */
static void chord_flush(BOOL deliverPending)
{
    int i;

    DBG("chord_flush(%d)", (int)deliverPending);

    for (i = 0; i < BTN_COUNT; ++i) {
        if (g_pfx[i].st == PS_PASSTHRU)
            inject_button(i, FALSE);              /* 注入した押下を必ず閉じる */
        else if (g_pfx[i].st == PS_PENDING && deliverPending)
            inject_click(i);                      /* 保留していたクリックを失わない */
        hold_timer_kill(i);
        g_pfx[i].st = PS_IDLE;
        g_pfx[i].t0 = 0;
        g_swallowUp[i] = FALSE;
        g_swallowT[i]  = 0;
    }
}

void chord_reset(void)
{
    chord_flush(TRUE);
}

/* 有効/無効の切り替え。
   ここで状態を捨てないのが肝。捨てると、押している最中に切り替わった場合に
   「押下は握り潰したのに離上は素通し」という不整合が生まれる。
   無効化は「これ以上新しく乗っ取らない」だけを意味し、
   進行中のものは最後まで面倒を見る。 */
void chord_set_active(BOOL on)
{
    DBG("chord_set_active %d -> %d", (int)g_on, (int)on);
    g_on = on;
}

/* デスクトップが切り替わった(UAC のセキュアデスクトップ、ロック画面など)。
   その間の離上イベントは一切こちらに届かないので、状態を持ち越さない。 */
void chord_on_desktop_switch(void)
{
    DBG("desktop switch");
    chord_flush(FALSE);
}

void chord_on_hold_timeout(int b)
{
    hold_timer_kill(b);
    if (b >= 0 && b < BTN_COUNT) pfx_promote(b);
}

/* 取りこぼしの保険。
 *
 *  【重要】押下をフックで握り潰すと、その押下は OS のキー状態に記録されない。
 *  つまり PS_PENDING / PS_CONSUMED の間、GetAsyncKeyState は
 *  ユーザーが実際に押していても「離されている」と答える(実測で確認済み)。
 *  かつて、ここで GetAsyncKeyState を根拠に 2 秒で状態を破棄していたため、
 *  「右ボタンを 2 秒以上押していると同時押しが効かなくなり、
 *   さらに離上だけがアプリに素通しして両ボタンの挙動が壊れる」
 *  という不具合になっていた。
 *
 *  したがって:
 *    PS_PASSTHRU  … 自分で押下を注入済み = OS の状態は信用できる → 使う
 *    PS_PENDING / PS_CONSUMED … OS からは見えない。押しっぱなしは正当な状態なので
 *                    判定しない。極端に長い場合だけ最後の保険として畳む。
 */
void chord_sanity(void)
{
    ULONGLONG now = GetTickCount64();
    int i;

    for (i = 0; i < BTN_COUNT; ++i) {
        if (g_pfx[i].st == PS_IDLE) continue;

        if (g_pfx[i].st == PS_PASSTHRU) {
            if (now - g_pfx[i].t0 >= 2000 && !(GetAsyncKeyState(kVk[i]) & 0x8000)) {
                DBG("sanity: 注入済みの押下を閉じる pfx[%d]", i);
                inject_button(i, FALSE);
                g_pfx[i].st = PS_IDLE;
            }
            continue;
        }

        if (now - g_pfx[i].t0 >= 60000) {
            DBG("sanity: 60秒以上保留のまま pfx[%d] を畳む", i);
            hold_timer_kill(i);
            g_pfx[i].st = PS_IDLE;
        }
    }

    for (i = 0; i < BTN_COUNT; ++i)
        if (g_swallowUp[i] && now - g_swallowT[i] >= 60000)
            g_swallowUp[i] = FALSE;
}

/* ------------------------------------------------------------------ */
/*  イベント処理本体。TRUE を返すとフックがそのイベントを握り潰す。    */
/* ------------------------------------------------------------------ */

/* 押下が来た時点で、そのボタンの前回分は必ず決着しているはず。
   決着していない = 離上を取りこぼしている。ここで辻褄を合わせておかないと、
   「押下だけがアプリに届いて離上は握り潰される」= ボタンが押されっぱなし、
   という最悪の事故になる。構造的に防ぐための最後の砦。 */
static void reconcile(int btn)
{
    if (g_pfx[btn].st != PS_IDLE) {
        DBG("reconcile: pfx[%d] が st=%d のまま押下された", btn, (int)g_pfx[btn].st);
        if (g_pfx[btn].st == PS_PASSTHRU)
            inject_button(btn, FALSE);    /* 注入したままの押下を閉じる */
        hold_timer_kill(btn);
        g_pfx[btn].st = PS_IDLE;
    }
    g_swallowUp[btn] = FALSE;
}

static BOOL on_button_down(int btn, const MSLLHOOKSTRUCT *m)
{
    int p;

    reconcile(btn);
    if (!g_on) return FALSE;          /* 無効中は新しく乗っ取らない */

    /* (1) 誰かがプレフィクスとして待機中なら、同時押しの成立を試す */
    for (p = 0; p < BTN_COUNT; ++p) {
        const Action *a;
        if (p == btn || !pfx_active(p)) continue;

        a = chord_at(p, btn);         /* サフィックス添字はボタン添字と同じ並び */
        if (a) {
            fire_action(a, 0);
            pfx_set(p, PS_CONSUMED);  /* プレフィクスの離上も殺す */
            g_swallowUp[btn] = TRUE;  /* このボタンの離上も殺す   */
            g_swallowT[btn]  = GetTickCount64();
            return TRUE;              /* 押下は世に出さない       */
        }
        /* 割り当てが無い組み合わせ。物理状態に忠実になるよう保留を解いてから通す */
        pfx_promote(p);
    }

    /* (2) 自分がプレフィクスになれるなら、押下を飲み込んで保留する */
    if (g_armed[btn] && g_pfx[btn].st == PS_IDLE) {
        pfx_set(btn, PS_PENDING);
        g_pfx[btn].anchor = m->pt;
        hold_timer_arm(btn);
        return TRUE;
    }
    return FALSE;
}

static BOOL on_button_up(int btn)
{
    if (g_swallowUp[btn]) {               /* サフィックスとして殺した押下の相方 */
        g_swallowUp[btn] = FALSE;
        return TRUE;
    }

    switch (g_pfx[btn].st) {
    case PS_PENDING: {                    /* 何も起きなかった = 単独クリック */
        const Action *sa = &g_cfg.single[btn];
        hold_timer_kill(btn);
        g_pfx[btn].st = PS_IDLE;
        if (sa->kind == ACT_PASSTHRU)
            inject_click(btn);            /* 本来のクリックとして成立させる */
        else if (sa->kind != ACT_NONE)
            fire_action(sa, 0);           /* 単独クリックを別機能に置き換え */
        /* ACT_NONE なら何も起こさない = そのボタンを無効化 */
        return TRUE;
    }
    case PS_CONSUMED:                     /* 同時押しに使われた -> 離上も闇に葬る */
        hold_timer_kill(btn);
        g_pfx[btn].st = PS_IDLE;
        return TRUE;

    case PS_PASSTHRU:                     /* 押下は既に注入済み -> 離上は本物を通す */
        g_pfx[btn].st = PS_IDLE;
        return FALSE;

    default:
        return FALSE;
    }
}

static BOOL on_move(const MSLLHOOKSTRUCT *m)
{
    int b;
    for (b = 0; b < BTN_COUNT; ++b) {
        if (g_pfx[b].st != PS_PENDING) continue;
        if (abs(m->pt.x - g_pfx[b].anchor.x) > g_dragThresh ||
            abs(m->pt.y - g_pfx[b].anchor.y) > g_dragThresh)
            pfx_promote(b);               /* ドラッグを始めた -> 本物として通す */
    }
    return FALSE;                         /* 移動は絶対に殺さない */
}

static BOOL on_wheel(const MSLLHOOKSTRUCT *m)
{
    int delta = GET_WHEEL_DELTA_WPARAM(m->mouseData);
    int suf, mag, p;

    if (delta == 0) return FALSE;
    suf = (delta > 0) ? SUF_WUP : SUF_WDN;
    mag = abs(delta);                     /* 高分解能ホイールの刻みをそのまま活かす */

    for (p = 0; p < BTN_COUNT; ++p) {
        const Action *a;
        if (!pfx_active(p)) continue;

        a = g_on ? chord_at(p, suf) : NULL;
        if (a) {
            fire_action(a, mag);
            pfx_set(p, PS_CONSUMED);
            return TRUE;                  /* 縦スクロールは世に出さない */
        }
        pfx_promote(p);
    }
    return FALSE;
}

/* WM_XBUTTONDOWN/UP のどちらのサイドボタンかを取り出す */
static int xbutton_of(const MSLLHOOKSTRUCT *m)
{
    return (HIWORD(m->mouseData) == XBUTTON2) ? BTN_X2 : BTN_X1;
}

static BOOL dispatch(UINT msg, const MSLLHOOKSTRUCT *m)
{
    switch (msg) {
    case WM_LBUTTONDOWN: return on_button_down(BTN_L, m);
    case WM_RBUTTONDOWN: return on_button_down(BTN_R, m);
    case WM_MBUTTONDOWN: return on_button_down(BTN_M, m);
    case WM_XBUTTONDOWN: return on_button_down(xbutton_of(m), m);

    case WM_LBUTTONUP:   return on_button_up(BTN_L);
    case WM_RBUTTONUP:   return on_button_up(BTN_R);
    case WM_MBUTTONUP:   return on_button_up(BTN_M);
    case WM_XBUTTONUP:   return on_button_up(xbutton_of(m));

    case WM_MOUSEMOVE:   return on_move(m);
    case WM_MOUSEWHEEL:  return on_wheel(m);

    default:
        return FALSE;                    /* WM_MOUSEHWHEEL(本物のチルト)等はそのまま */
    }
}

BOOL chord_on_mouse(UINT msg, const MSLLHOOKSTRUCT *m)
{
    BOOL swallow;

    if (msg == WM_MOUSEMOVE) return dispatch(msg, m);

    DBG("evt 0x%04X  pfx=%d%d%d%d%d sw=%d%d%d%d%d",
        msg, (int)g_pfx[0].st, (int)g_pfx[1].st, (int)g_pfx[2].st,
        (int)g_pfx[3].st, (int)g_pfx[4].st,
        (int)g_swallowUp[0], (int)g_swallowUp[1], (int)g_swallowUp[2],
        (int)g_swallowUp[3], (int)g_swallowUp[4]);

    swallow = dispatch(msg, m);
    DBG("   -> %s", swallow ? "SWALLOW" : "pass");
    return swallow;
}
