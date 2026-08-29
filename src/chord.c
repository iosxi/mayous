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
 *  プレフィクスになれるのは 右・サイド1・サイド2 の 3 つ。
 *  サフィックスはそれに中ボタンとホイール上下を加えた 7 つ。
 *  さらに「登録キー」として、キーボードのキーをサフィックスにできる
 *  (右クリックを押しながら F13 など)。こちらはキーボードフックで拾う。
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

/* 登録キーとして飲み込んだキーの控え。
   押下を握り潰したら、その離上も必ず握り潰さなければならない。片方だけ
   世に出すと、相手のアプリにはキーが押しっぱなしのまま残る。
   同時に押されうるトリガーは高々 BTN_COUNT × REGKEY_COUNT なので、
   その数だけ持てば足りる。 */
#define KEYSW_MAX (BTN_COUNT * REGKEY_COUNT)
static WORD      g_keySwVk[KEYSW_MAX];
static ULONGLONG g_keySwT[KEYSW_MAX];

static int keysw_find(WORD vk)
{
    int i;
    if (!vk) return -1;
    for (i = 0; i < KEYSW_MAX; ++i)
        if (g_keySwVk[i] == vk) return i;
    return -1;
}

static void keysw_mark(WORD vk)
{
    int i;
    if (keysw_find(vk) >= 0) return;
    for (i = 0; i < KEYSW_MAX; ++i) {
        if (g_keySwVk[i]) continue;
        g_keySwVk[i] = vk;
        g_keySwT[i]  = GetTickCount64();
        return;
    }
}

static void keysw_drop(WORD vk)
{
    int i = keysw_find(vk);
    if (i >= 0) g_keySwVk[i] = 0;
}

static void keysw_clear(void)
{
    ZeroMemory(g_keySwVk, sizeof(g_keySwVk));
    ZeroMemory(g_keySwT,  sizeof(g_keySwT));
}

/* ==================================================================
 *  物理ボタンの地面 (Raw Input)
 *
 *  低レベルフックで握り潰した押下は GetAsyncKeyState に現れない。自分で
 *  握り潰しているのだから当然だが、そのせいで「まだ押されているのか、
 *  離上を取りこぼしたのか」を区別できない。
 *
 *  他の常駐ツール(X-Mouse Button Control, MouseGestureL.ahk など)が
 *  フックの並びで手前に居ると、離上をそちらに食べられて mayous には
 *  永久に届かないことがある。すると保留のまま居座り、以後の左クリックを
 *  同時押しとして飲み込み続ける。利用者からは「左クリックが効かなくなった。
 *  mayous を落としたら直った」に見える。保険は 60 秒後なので、実質ずっと壊れている。
 *
 *  Raw Input はフックの握り潰しに関係なく届く。実測は tools\test_rawinput.ps1:
 *      3265ms RAW RIGHT DOWN    <- 握り潰した押下。アプリには届いていない
 *      5281ms RAW RIGHT UP
 *      5281ms RAW RIGHT DOWN [extra=4D594F55]  <- mayous が注入したクリック
 *      5312ms ASYNC RIGHT DOWN  <- ASYNC は注入分しか見ていない
 *  自分が注入したものは ulExtraInformation に MAYOUS_TAG が入るので除ける。
 *
 *  マウス移動でも WM_INPUT は飛ぶが、フックはどのみち移動を全部受けている
 *  ので、増える仕事はたかが知れている。登録を出し入れすると「登録前に
 *  離された」を取りこぼすので、起動時に一度だけ登録して持ち続ける。
 * ================================================================== */

static BOOL      g_physDown[BTN_COUNT];    /* 物理的に押されているか   */
static ULONGLONG g_physUpT[BTN_COUNT];     /* 物理的に離された時刻     */
static BOOL      g_rawOn;                  /* Raw Input を登録できたか */

/* 通常の離上とのすれ違いで誤判定しないための猶予。
   本物の離上はフックへ数 ms で届くので、これだけ空けば十分。 */
#define RAW_LOST_MS 250

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

enum { Q_CLICK, Q_DOWN, Q_UP, Q_KEYS, Q_HWHEEL, Q_VWHEEL, Q_ZOOM, Q_DRAG_START,
       Q_HOLD_DOWN, Q_HOLD_UP, Q_MARK_ON, Q_MARK_OFF };

typedef struct {
    BYTE    kind;
    BYTE    btn;
    BYTE    nsteps;
    int     amount;
    POINT   from, to;      /* Q_DRAG_START: 押した位置 と 今の位置 */
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
    if (a && (kind == Q_KEYS || kind == Q_HOLD_DOWN || kind == Q_HOLD_UP)) {
        it->nsteps = (BYTE)a->nsteps;
        CopyMemory(it->steps, a->steps, (size_t)a->nsteps * sizeof(KeyStep));
    }
    g_qTail = next;

    if (g_hwnd) PostMessageW(g_hwnd, WM_MAYOUS_PUMP, 0, 0);
}

static void q_push_drag(int btn, POINT from, POINT to)
{
    int next = (g_qTail + 1) % QCAP;
    QItem *it;

    if (next == g_qHead) return;
    it = &g_q[g_qTail];
    ZeroMemory(it, sizeof(*it));
    it->kind = Q_DRAG_START;
    it->btn  = (BYTE)btn;
    it->from = from;
    it->to   = to;
    g_qTail  = next;

    if (g_hwnd) PostMessageW(g_hwnd, WM_MAYOUS_PUMP, 0, 0);
}

/* 座標だけを載せて積む(オートスクロールの目印を出す位置) */
static void q_push_point(BYTE kind, POINT p)
{
    int next = (g_qTail + 1) % QCAP;
    QItem *it;

    if (next == g_qHead) return;
    it = &g_q[g_qTail];
    ZeroMemory(it, sizeof(*it));
    it->kind = kind;
    it->to   = p;
    g_qTail  = next;

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

/* 1ステップぶんの押す(または離す)をまとめて 1 回の SendInput で送る。
   押すのは並び順、離すのは逆順。 */
static void emit_step(const KeyStep *k, BOOL down)
{
    INPUT in[MAX_ACTION_KEYS];
    int i, n = 0;

    if (down) for (i = 0; i < k->nkeys; ++i)      fill_key(&in[n++], k->keys[i], FALSE);
    else      for (i = k->nkeys - 1; i >= 0; --i) fill_key(&in[n++], k->keys[i], TRUE);
    if (n) SendInput((UINT)n, in, sizeof(INPUT));
}

/* ==================================================================
 *  キー再生
 *
 *  【押した瞬間に離してはいけない】
 *  押下と離上を間髪入れずに送ると、キーボードフックを張っているアプリは
 *  拾えるが、GetAsyncKeyState を一定間隔で見に行く方式のアプリは
 *  取りこぼす。押されている時間がほぼ 0 なので、サンプリングの隙間に
 *  丸ごと収まってしまうためである(実測: zoom-pon のキー登録が反応しない)。
 *  ゲームをはじめ、状態を見に行く作りのアプリは珍しくない。
 *
 *  そこで KeyHoldMs のあいだ押しっぱなしにしてから離す。
 *  待つのにスリープは使えない(メッセージループが止まり、フックの配送も
 *  止まる)ので、タイマーで段階を進める。
 * ================================================================== */

static KeyStep g_play[MAX_ACTION_STEPS];
static int     g_playN, g_playI;
static BOOL    g_playHeld;      /* いま押下を出して離上待ち */

static int hold_ms(void)   { return g_cfg.keyHoldMs > 0 ? g_cfg.keyHoldMs : 1; }
static int gap_ms(void)    { int g = hold_ms() / 2; return g < 10 ? 10 : g; }

static void play_stop(void)
{
    if (g_hwnd) KillTimer(g_hwnd, TIMER_KEYPLAY);
    g_playN = g_playI = 0;
    g_playHeld = FALSE;
}

/* 再生の途中なら、押しっぱなしのキーを今すぐ離して打ち切る */
static void play_flush(void)
{
    if (g_playHeld && g_playI < g_playN) emit_step(&g_play[g_playI], FALSE);
    play_stop();
}

static void emit_keys(const KeyStep *steps, int nsteps)
{
    if (nsteps <= 0) return;
    play_flush();                       /* 前の再生が残っていれば畳む */

    CopyMemory(g_play, steps, (size_t)nsteps * sizeof(KeyStep));
    g_playN = nsteps;
    g_playI = 0;

    emit_step(&g_play[0], TRUE);
    g_playHeld = TRUE;
    if (g_hwnd) SetTimer(g_hwnd, TIMER_KEYPLAY, (UINT)hold_ms(), NULL);
}

/* TIMER_KEYPLAY から呼ばれ、押す->離す->次のステップ と段階を進める */
void chord_key_tick(void)
{
    if (g_playN <= 0) { play_stop(); return; }

    if (g_playHeld) {
        emit_step(&g_play[g_playI], FALSE);
        g_playHeld = FALSE;
        ++g_playI;
        if (g_playI >= g_playN) { play_stop(); return; }
        if (g_hwnd) SetTimer(g_hwnd, TIMER_KEYPLAY, (UINT)gap_ms(), NULL);
    } else {
        emit_step(&g_play[g_playI], TRUE);
        g_playHeld = TRUE;
        if (g_hwnd) SetTimer(g_hwnd, TIMER_KEYPLAY, (UINT)hold_ms(), NULL);
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

/* 仮想デスクトップ全体を 0..65535 に正規化する(複数モニタでも正しく飛ぶ) */
static void fill_move(INPUT *in, POINT p)
{
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (vw < 2) vw = 2;
    if (vh < 2) vh = 2;

    ZeroMemory(in, sizeof(*in));
    in->type           = INPUT_MOUSE;
    in->mi.dwFlags     = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    in->mi.dx          = (LONG)MulDiv(p.x - vx, 65535, vw - 1);
    in->mi.dy          = (LONG)MulDiv(p.y - vy, 65535, vh - 1);
    in->mi.dwExtraInfo = MAYOUS_TAG;
}

/* ドラッグの開始を「押した場所」で成立させる。
 *
 *  保留していた押下をドラッグ判定で世に出すとき、単に今のカーソル位置で
 *  押下を注入すると、既にしきい値ぶん離れているので、ウィンドウの枠のような
 *  細い当たり判定を掴み損ねる(「枠を掴んだつもりが枠の外を掴んでいる」)。
 *  そこで「押した位置へ戻す → 押す → 今の位置へ動かす」を 1 回の SendInput で
 *  まとめて送る。アプリからは、ユーザーが実際にやった通りに見える。
 */
static void emit_drag_start(int btn, POINT from, POINT to)
{
    INPUT in[3];
    int n = 0;

    fill_move(&in[n++], from);

    ZeroMemory(&in[n], sizeof(in[n]));
    in[n].type           = INPUT_MOUSE;
    in[n].mi.dwFlags     = kDownFlag[btn];
    in[n].mi.mouseData   = kXData[btn];
    in[n].mi.dwExtraInfo = MAYOUS_TAG;
    ++n;

    if (from.x != to.x || from.y != to.y) fill_move(&in[n++], to);

    SendInput((UINT)n, in, sizeof(INPUT));
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
        case Q_HOLD_DOWN: emit_step(&it.steps[0], TRUE);  break;
        case Q_HOLD_UP:   emit_step(&it.steps[0], FALSE); break;
        case Q_DRAG_START: emit_drag_start(it.btn, it.from, it.to); break;
        case Q_HWHEEL:
            /* このプロセスは WH_MOUSE_LL を保持しているため、自分で SendInput
               してもホイールだけは OS に捨てられる(詳細は agent.c 冒頭)。 */
            agent_send_wheel(it.amount, TRUE);
            break;
        case Q_VWHEEL:  agent_send_wheel(it.amount, FALSE); break;
        case Q_ZOOM:    agent_send_zoom(it.amount);         break;
        case Q_MARK_ON: overlay_show(it.to);                break;
        case Q_MARK_OFF: overlay_hide();                    break;
        }
    }
}

/* --- フック側から呼ぶのはこちら。キューに積むだけで即座に戻る。 --- */

static void inject_button(int btn, BOOL down) { q_push(down ? Q_DOWN : Q_UP, btn, 0, NULL); }
static void inject_click(int btn)             { q_push(Q_CLICK, btn, 0, NULL); }
static void inject_hwheel(int amount)         { q_push(Q_HWHEEL, 0, amount, NULL); }
static void inject_vwheel(int amount)         { q_push(Q_VWHEEL, 0, amount, NULL); }
static void inject_zoom(int amount)           { q_push(Q_ZOOM, 0, amount, NULL); }
static void inject_keys(const Action *a)      { q_push(Q_KEYS, 0, 0, a); }

/* ================================================================== */
/*  押しっぱなし                                                       */
/*
 *  同時押しに割り当てたキーは、プレフィクスを離すまで押しっぱなしにする。
 *  これが既定の挙動である(かつては hold: を付けた時だけの特別扱いだった)。
 *
 *  一瞬叩くだけだと、キーボードフックを張らず GetAsyncKeyState を一定間隔で
 *  見に行く作りのアプリに取りこぼされる。押しっぱなしなら、相手の周期が
 *  どれだけ伸びていても必ず観測される。
 *
 *  注入した押下はオートリピートしない(タイプマティックはキーボード側が
 *  作るものなので、SendInput の押下を Windows が繰り返すことはない。
 *  tools\test_autorepeat.ps1 で実測: 3 秒押しっぱなしでも 1 文字)。
 *  だから Ctrl+W のような組み合わせを押しっぱなしにしても、タブが
 *  次々に閉じるような事故は起きない。
 *
 *  離すのは「プレフィクスを離した時」。サフィックス(後から押す側)は
 *  叩いてすぐ離すのが普通なので、そちらを基準にすると結局一瞬になってしまう。
 *  ホイールをサフィックスにした場合は、そもそも離上が存在しない。
 *
 *  ただし同時押しが一瞬で終わると押下時間も一瞬になってしまうので、
 *  KeyHoldMs に満たない間は離上を遅らせる。KeyHoldMs は上限ではなく
 *  「最低でもこれだけは押す」という下限として働く。
 * ================================================================== */

typedef struct {
    KeyStep   step;
    BOOL      down;
    ULONGLONG t0;        /* 押した時刻。最低押下時間の判定に使う */
    BOOL      repress;   /* いったん離して、押し直すのを待っている */
    KeyStep   next;      /* 押し直すキー */
} HoldState;

static HoldState g_hold[BTN_COUNT];

static void hold_push(const KeyStep *s, BOOL down)
{
    Action tmp;
    ZeroMemory(&tmp, sizeof(tmp));
    tmp.nsteps   = 1;
    tmp.steps[0] = *s;
    q_push(down ? Q_HOLD_DOWN : Q_HOLD_UP, 0, 0, &tmp);
}

/* 押しているキーと、これから押すキーが同じ組み合わせか。
   違うなら「離して押し直した」ことが誰の目にも明らかなので、間を空けずに
   入れ替えてよい(hold_begin のコメント参照)。 */
static BOOL step_same(const KeyStep *a, const KeyStep *b)
{
    int i;
    if (a->nkeys != b->nkeys) return FALSE;
    for (i = 0; i < a->nkeys; ++i)
        if (a->keys[i] != b->keys[i]) return FALSE;
    return TRUE;
}

static void hold_rel_timer_kill(int pfx)
{
    if (g_hwnd) KillTimer(g_hwnd, TIMER_KEYREL_BASE + pfx);
}

/* 最低押下時間を待たずに今すぐ離す。畳む処理はどれもこちらを使う
   (終了やデスクトップ切替を待たせるわけにはいかない)。 */
static void hold_release_now(int pfx)
{
    hold_rel_timer_kill(pfx);
    g_hold[pfx].repress = FALSE;
    if (!g_hold[pfx].down) return;
    g_hold[pfx].down = FALSE;
    hold_push(&g_hold[pfx].step, FALSE);
}

static void hold_press_now(int pfx, const KeyStep *s)
{
    g_hold[pfx].step = *s;
    g_hold[pfx].down = TRUE;
    g_hold[pfx].t0   = GetTickCount64();
    hold_push(&g_hold[pfx].step, TRUE);
}

/* プレフィクスを押したまま、同じ組み合わせをもう一度成立させた場合。
 *
 *  ここで「離して即座に押し直す」と、離上と押下が同じ chord_pump() で
 *  連続して出るため、その隙間は 1ms 未満になる。キーの状態を一定間隔で
 *  見に行く作りのアプリからは離した瞬間が一切見えず、押しっぱなしのまま
 *  にしか映らない ── つまり 2 回目以降が無かったことになる。
 *  (実測: 左クリック 4 回に対して押し直しは 3 回しか観測されなかった。
 *   1ms 間隔で見ても取りこぼす。tools\test_refire.ps1)
 *
 *  そこで、離してから RepressGapMs だけ空けて押し直す。押している時間に
 *  下限が要るのと同じ理由で、離している時間にも下限が要る。
 *  この時間はそのまま体感の遅れになるので、相手に合わせて 120/80/40/20ms
 *  から選べるようにしてある(設定画面の「同じキーの押し直し」)。
 */
static void hold_begin(int pfx, const Action *a)
{
    if (g_hold[pfx].repress) {          /* 間を空けている最中の再発火 */
        /* 間を空けていたのは、直前に離したキーと同じキーを押し直すため。
           別のキーになったのなら待つ理由が無いので、約束を破って今すぐ押す。 */
        if (!step_same(&g_hold[pfx].step, &a->steps[0])) {
            hold_rel_timer_kill(pfx);
            g_hold[pfx].repress = FALSE;
            hold_press_now(pfx, &a->steps[0]);
            return;
        }
        g_hold[pfx].next = a->steps[0]; /* 押し直す中身だけ差し替える */
        return;
    }
    if (g_hold[pfx].down) {
        BOOL same = step_same(&g_hold[pfx].step, &a->steps[0]);
        hold_release_now(pfx);          /* ここで repress は falseに戻る */
        /* 違うキーへの入れ替えなら、間を空けずにそのまま押す。
           離上と押下は同じ chord_pump() で続けて出るが、キーが別物である以上、
           状態を見に行く作りのアプリでも「前のキーが離れ、別のキーが押された」
           と読める。待たされないぶん、体感の遅れが消える。
           ホイール上に a・下に b、のような割り当てが効いてくるのはここ。 */
        if (!same || !g_hwnd) { hold_press_now(pfx, &a->steps[0]); return; }
        g_hold[pfx].next    = a->steps[0];
        g_hold[pfx].repress = TRUE;
        SetTimer(g_hwnd, TIMER_KEYREL_BASE + pfx, (UINT)g_cfg.repressGapMs, NULL);
        return;
    }
    hold_press_now(pfx, &a->steps[0]);
}

/* プレフィクスが離された。まだ KeyHoldMs に満たなければ、その分だけ待ってから離す。 */
static void hold_end(int pfx)
{
    ULONGLONG el;
    int rest;

    /* 押し直しを待っている途中で離された。約束したぶんは普通に叩いて返す
       (捨てると、利用者のクリックが 1 回まるごと無かったことになる)。 */
    if (g_hold[pfx].repress) {
        Action tmp;
        hold_rel_timer_kill(pfx);
        g_hold[pfx].repress = FALSE;
        ZeroMemory(&tmp, sizeof(tmp));
        tmp.kind     = ACT_KEYS;
        tmp.nsteps   = 1;
        tmp.steps[0] = g_hold[pfx].next;
        inject_keys(&tmp);
        return;
    }

    if (!g_hold[pfx].down) return;
    el = GetTickCount64() - g_hold[pfx].t0;
    rest = g_cfg.keyHoldMs - (int)el;
    if (rest <= 0 || !g_hwnd) { hold_release_now(pfx); return; }
    SetTimer(g_hwnd, TIMER_KEYREL_BASE + pfx, (UINT)rest, NULL);
}

static void hold_end_all(void)
{
    int i;
    for (i = 0; i < BTN_COUNT; ++i) hold_release_now(i);
}

void chord_key_release_tick(int pfx)
{
    if (pfx < 0 || pfx >= BTN_COUNT) return;
    if (g_hold[pfx].repress) {          /* 間を空け終わった -> 押し直す */
        KeyStep s = g_hold[pfx].next;
        hold_rel_timer_kill(pfx);
        g_hold[pfx].repress = FALSE;
        hold_press_now(pfx, &s);
        return;
    }
    hold_release_now(pfx);
}

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

/* 保留をやめ、本物の押下として世に出す(ドラッグ・長押しの救済)。
   now を渡すと「押した場所で押して、今の場所まで動かした」ことにする。
   NULL なら今のカーソル位置でそのまま押す。 */
static void pfx_promote_at(int b, const POINT *now)
{
    if (g_pfx[b].st != PS_PENDING) return;
    hold_timer_kill(b);
    pfx_set(b, PS_PASSTHRU);
    if (now) q_push_drag(b, g_pfx[b].anchor, *now);
    else     inject_button(b, TRUE);
}

static void pfx_promote(int b)
{
    pfx_promote_at(b, NULL);
}

/* ==================================================================
 *  スクロール・モード (オートスクロール)
 *
 *  中ボタンを押して離すと入り、もう一度クリックするか Esc で出る。
 *  入っているあいだはマウスの移動を握り潰し、その移動量をホイールに
 *  変えて注入する。つまりカーソルはその場に凍りつき、机の上で
 *  マウスを動かすとページが動く。X-Mouse Button Control の
 *  「オートスクロール(Change Movement to Scroll)」と同じ操作感。
 *
 *  【移動を握り潰すのはここだけ】
 *  普段は移動を絶対に殺さない(on_move の末尾を参照)。ここだけが例外で、
 *  そのぶん「抜け損ねるとカーソルが凍ったまま戻らない」という、
 *  この機能で唯一の重大な壊れ方を招く。抜け道は多めに用意してある:
 *      ・どのボタンでもいいので押す (そのクリックは食べる)
 *      ・Esc
 *      ・無効化された / 除外アプリが前面に来た (chord_sanity)
 *      ・設定の再読み込み・終了・デスクトップ切替 (chord_flush)
 *
 *  カーソルが止まるので、そのままでは固まったようにしか見えない。
 *  入った場所に目印を出す(overlay.c)。生成はメッセージループ側で行う
 *  必要があるので、注入キュー経由で頼む。
 * ================================================================== */

static BOOL  g_scroll;        /* スクロール・モード中か */
static POINT g_scrollAt;      /* 入った場所(カーソルはここで固定される) */
static int   g_scrollPx;      /* まだホイールに変えていない移動量(縦) */
static int   g_scrollPxH;     /* 同上(横) */

static void scroll_begin(POINT at)
{
    DBG("scroll: 開始 (%d,%d)", (int)at.x, (int)at.y);
    g_scroll   = TRUE;
    g_scrollAt = at;
    g_scrollPx = g_scrollPxH = 0;
    q_push_point(Q_MARK_ON, at);      /* 目印はメッセージループ側で出す */
}

static void scroll_end(void)
{
    if (!g_scroll) return;
    DBG("scroll: 終了");
    g_scroll = FALSE;
    q_push(Q_MARK_OFF, 0, 0, NULL);
}

/* 1 段ぶんの移動量(px)。速さが上がるほど短くなる。 */
static int scroll_px_per_notch(void)
{
    int sp = g_cfg.autoScrollSpeed > 0 ? g_cfg.autoScrollSpeed : AUTOSCROLL_SPEED_DEFAULT;
    int px = AUTOSCROLL_PX_PER_NOTCH * 100 / sp;
    return px > 0 ? px : 1;
}

/* 貯めた移動量をホイールに変換して注入する。端数は次回へ持ち越す。 */
static void scroll_feed(int *acc, int delta, BOOL horizontal)
{
    int px = scroll_px_per_notch();
    int amount;

    *acc += delta;
    amount = *acc * WHEEL_DELTA / px;
    if (!amount) return;
    *acc -= amount * px / WHEEL_DELTA;

    /* 下へ動かしたら下へ送る(ホイールは下が負)。横は右が正。 */
    if (horizontal) inject_hwheel(+amount);
    else            inject_vwheel(-amount);
}

static void fire_action(const Action *a, int wheelAmount, int pfx)
{
    switch (a->kind) {
    case ACT_KEYS:
    case ACT_HOLD_KEYS:     /* hold: 付きの古い設定。今はどちらも同じ意味 */
        /* 既定でプレフィクスを離すまで押しっぱなしにする。
           押しっぱなしにしようがない 2 つの場合だけ、順に叩く方式に落とす:
             ・複数ステップ(Ctrl+C のあと Ctrl+V など)
             ・単独クリック(pfx < 0)。離した時に発火するので押しようがない */
        if (pfx >= 0 && a->nsteps == 1) hold_begin(pfx, a);
        else                            inject_keys(a);
        break;
    case ACT_HWHEEL_LEFT:  inject_hwheel(-wheelAmount);  break;
    case ACT_HWHEEL_RIGHT: inject_hwheel(+wheelAmount);  break;
    /* ズームはホイール以外(サイドボタン・登録キー)にも割り当てられる。
       その場合 wheelAmount が無いので、1 段ぶんを自分で決める。 */
    case ACT_ZOOM_IN:      inject_zoom(+(wheelAmount ? wheelAmount : WHEEL_DELTA)); break;
    case ACT_ZOOM_OUT:     inject_zoom(-(wheelAmount ? wheelAmount : WHEEL_DELTA)); break;
    case ACT_CLICK:        inject_click(a->btn);         break;
    case ACT_AUTOSCROLL: {
        POINT p;
        GetCursorPos(&p);              /* 読むだけ。注入はしないので安全 */
        scroll_begin(p);
        break;
    }
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
    RAWINPUTDEVICE rid;

    g_hwnd = hwnd;

    /* RIDEV_INPUTSINK: 前面でなくても届く。失敗しても致命傷ではない
       (60 秒の保険だけが残る)ので、黙って諦める。 */
    rid.usUsagePage = 0x01;      /* Generic Desktop */
    rid.usUsage     = 0x02;      /* Mouse           */
    rid.dwFlags     = RIDEV_INPUTSINK;
    rid.hwndTarget  = hwnd;
    g_rawOn = RegisterRawInputDevices(&rid, 1, sizeof(rid)) ? TRUE : FALSE;
    DBG("raw input 登録: %d", (int)g_rawOn);
}

/* WM_INPUT から呼ぶ。物理ボタンの上下だけを拾う。 */
void chord_on_raw_input(HRAWINPUT h)
{
    RAWINPUT ri;
    UINT     sz = sizeof(ri);
    USHORT   f;
    int      i;
    /* 添字は BTN_* の並び */
    static const USHORT kDn[BTN_COUNT] = {
        RI_MOUSE_LEFT_BUTTON_DOWN,  RI_MOUSE_RIGHT_BUTTON_DOWN,
        RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_5_DOWN
    };
    static const USHORT kUp[BTN_COUNT] = {
        RI_MOUSE_LEFT_BUTTON_UP,    RI_MOUSE_RIGHT_BUTTON_UP,
        RI_MOUSE_MIDDLE_BUTTON_UP,  RI_MOUSE_BUTTON_4_UP,   RI_MOUSE_BUTTON_5_UP
    };

    if (GetRawInputData(h, RID_INPUT, &ri, &sz, sizeof(RAWINPUTHEADER)) == (UINT)-1)
        return;
    if (ri.header.dwType != RIM_TYPEMOUSE) return;

    /* 自分が注入したものは物理状態ではない */
    if (ri.data.mouse.ulExtraInformation == (ULONG)MAYOUS_TAG) return;

    f = ri.data.mouse.usButtonFlags;
    if (!f) return;                          /* 移動だけ。ここで抜けるのが大半 */

    for (i = 0; i < BTN_COUNT; ++i) {
        if (f & kDn[i]) g_physDown[i] = TRUE;
        if (f & kUp[i]) { g_physDown[i] = FALSE; g_physUpT[i] = GetTickCount64(); }
    }
}

/* 離上を取りこぼしたまま居座っているプレフィクスを畳む。
   物理的に離れているのに保留のままなら、その押下はもう戻ってこない。
   保留していたクリックは捨てずに世に出す(利用者の操作を失わないため)。 */
static void reap_lost(void)
{
    ULONGLONG now;
    int i;

    if (!g_rawOn) return;
    now = GetTickCount64();

    for (i = 0; i < BTN_COUNT; ++i) {
        if (g_pfx[i].st != PS_PENDING && g_pfx[i].st != PS_CONSUMED) continue;
        if (g_physDown[i]) continue;
        if (now - g_physUpT[i] < RAW_LOST_MS) continue;

        DBG("raw: pfx[%d] の離上を取りこぼした -> 畳む", i);
        hold_timer_kill(i);
        hold_end(i);
        if (g_pfx[i].st == PS_PENDING) inject_click(i);
        g_pfx[i].st = PS_IDLE;
    }

    /* サフィックス役で立てた「離上を殺す」印も、物理的に離れているなら用済み。
       残しておくと次の離上を巻き添えにして、そのボタンが押されっぱなしになる。 */
    for (i = 0; i < BTN_COUNT; ++i)
        if (g_swallowUp[i] && !g_physDown[i] && now - g_physUpT[i] >= RAW_LOST_MS)
            g_swallowUp[i] = FALSE;
}

/* 設定変更後に呼ぶ。どのボタンを乗っ取る必要があるかを再計算する。
   割り当てが全部 none のボタンには一切手を触れない = 副作用ゼロ。 */
void chord_recompute(void)
{
    int b, s;

    for (b = 0; b < BTN_COUNT; ++b) {
        g_armed[b] = FALSE;
        /* 単独クリックが「そのまま」以外なら、それだけでも乗っ取りが要る。
           中ボタンは「先に押す側」にはなれないが、ここには入る
           (オートスクロールや中クリックの差し替えはこの経路)。 */
        if (g_cfg.single[b].kind != ACT_PASSTHRU) { g_armed[b] = TRUE; continue; }
        if (!PFX_CAN(b)) continue;
        for (s = 0; s < SUF_COUNT; ++s) {
            /* トリガーが未登録の登録キーは、動作が入っていても発火しようがない。
               そのために押下を預かるのは丸損なので数に入れない。 */
            if (SUF_IS_KEY(s) && !g_cfg.regKeyVk[b][s - SUF_KEY0]) continue;
            if (chord_at(b, s)) { g_armed[b] = TRUE; break; }
        }
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

    play_flush();       /* 再生途中のキーを押しっぱなしにしない */
    hold_end_all();
    keysw_clear();      /* 登録キーの控えも持ち越さない */
    scroll_end();       /* カーソルを凍らせたまま放置しない */

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

    reap_lost();        /* まずは取りこぼした離上を回収する */

    /* 無効化された(除外アプリが前面に来た・フルスクリーン・停止)のに
       スクロール・モードのままだと、カーソルが凍ったまま抜け道が
       クリックだけになる。ここで必ず解く。 */
    if (g_scroll && !g_on) scroll_end();

    for (i = 0; i < BTN_COUNT; ++i) {
        if (g_pfx[i].st == PS_IDLE) continue;

        if (g_pfx[i].st == PS_PASSTHRU) {
            /* 注入した押下は世に出ているので GetAsyncKeyState にも見えるが、
               手前のフックに食べられていると見えない。Raw Input が
               「物理的に離れている」と言うならそちらを信じて閉じる。 */
            BOOL up = g_rawOn ? (!g_physDown[i] && now - g_physUpT[i] >= RAW_LOST_MS)
                              : !(GetAsyncKeyState(kVk[i]) & 0x8000);
            if (now - g_pfx[i].t0 >= 2000 && up) {
                DBG("sanity: 注入済みの押下を閉じる pfx[%d]", i);
                inject_button(i, FALSE);
                g_pfx[i].st = PS_IDLE;
            }
            continue;
        }

        if (now - g_pfx[i].t0 >= 60000) {
            DBG("sanity: 60秒以上保留のまま pfx[%d] を畳む", i);
            hold_timer_kill(i);
            hold_end(i);                  /* 押さえたままのキーを残さない */
            g_pfx[i].st = PS_IDLE;
        }
    }

    for (i = 0; i < BTN_COUNT; ++i)
        if (g_swallowUp[i] && now - g_swallowT[i] >= 60000)
            g_swallowUp[i] = FALSE;

    /* 登録キーの離上を取りこぼしていた場合の保険。ここを掃除しないと、
       次に同じキーを押したときの離上まで巻き添えで殺してしまう。 */
    for (i = 0; i < KEYSW_MAX; ++i)
        if (g_keySwVk[i] && now - g_keySwT[i] >= 60000)
            g_keySwVk[i] = 0;
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
        hold_end(btn);                    /* 押さえたままのキーを残さない */
        g_pfx[btn].st = PS_IDLE;
    }
    g_swallowUp[btn] = FALSE;
}

static BOOL on_button_down(int btn, const MSLLHOOKSTRUCT *m)
{
    int p;

    /* フックと WM_INPUT はどちらが先に処理されるか決まっていない。
       いま押下を見ているのだから物理的に押されているのは確実なので、
       Raw Input を待たずにここで確定させる。そうしないと、raw が
       遅れた場合に reap_lost() が生まれたての保留を殺してしまう。 */
    g_physDown[btn] = TRUE;

    reconcile(btn);

    /* スクロール・モード中のクリックは「やめる」の合図。
       そのクリック自体はアプリへ渡さない(離上もあとで殺す)。 */
    if (g_scroll) {
        scroll_end();
        g_swallowUp[btn] = TRUE;
        g_swallowT[btn]  = GetTickCount64();
        return TRUE;
    }

    if (!g_on) return FALSE;          /* 無効中は新しく乗っ取らない */

    /* (1) 誰かがプレフィクスとして待機中なら、同時押しの成立を試す */
    for (p = 0; p < BTN_COUNT; ++p) {
        const Action *a;
        if (p == btn || !pfx_active(p)) continue;

        a = chord_at(p, btn);         /* サフィックス添字はボタン添字と同じ並び */
        if (a) {
            fire_action(a, 0, p);
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
    g_physDown[btn] = FALSE;              /* 上と同じ理由で、raw を待たずに確定 */
    g_physUpT[btn]  = GetTickCount64();

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
            fire_action(sa, 0, -1);       /* 単独クリックを別機能に置き換え */
        /* ACT_NONE なら何も起こさない = そのボタンを無効化 */
        return TRUE;
    }
    case PS_CONSUMED:                     /* 同時押しに使われた -> 離上も闇に葬る */
        hold_timer_kill(btn);
        hold_end(btn);                    /* hold: で押さえていたキーを離す */
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

    /* スクロール・モード。移動をここで食べるのでカーソルは動かない。
       食べた移動量がそのままホイールになる。
       pt は「動かした先」なので、固定されている今の位置との差が
       そのまま 1 回ぶんの移動量になる。 */
    if (g_scroll) {
        scroll_feed(&g_scrollPx,  m->pt.y - g_scrollAt.y, FALSE);
        scroll_feed(&g_scrollPxH, m->pt.x - g_scrollAt.x, TRUE);
        return TRUE;
    }

    for (b = 0; b < BTN_COUNT; ++b) {
        if (g_pfx[b].st != PS_PENDING) continue;
        if (abs(m->pt.x - g_pfx[b].anchor.x) > g_dragThresh ||
            abs(m->pt.y - g_pfx[b].anchor.y) > g_dragThresh)
            pfx_promote_at(b, &m->pt);    /* ドラッグを始めた -> 押した場所から通す */
    }
    return FALSE;                         /* 移動は絶対に殺さない */
}

static BOOL on_wheel(const MSLLHOOKSTRUCT *m)
{
    int delta = GET_WHEEL_DELTA_WPARAM(m->mouseData);
    int suf, mag, p;

    if (delta == 0) return FALSE;
    if (g_scroll) return FALSE;       /* 本物のホイールはそのまま効かせる */
    suf = (delta > 0) ? SUF_WUP : SUF_WDN;
    mag = abs(delta);                     /* 高分解能ホイールの刻みをそのまま活かす */

    for (p = 0; p < BTN_COUNT; ++p) {
        const Action *a;
        if (!pfx_active(p)) continue;

        a = g_on ? chord_at(p, suf) : NULL;
        if (a) {
            fire_action(a, mag, p);
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

    /* ボタンが動くたびに、取りこぼした離上が無いか見る。
       1 秒ごとの chord_sanity だけに任せると、その間ずっと
       左クリックを飲み込み続けることになる。 */
    reap_lost();

    DBG("evt 0x%04X  pfx=%d%d%d%d%d sw=%d%d%d%d%d",
        msg, (int)g_pfx[0].st, (int)g_pfx[1].st, (int)g_pfx[2].st,
        (int)g_pfx[3].st, (int)g_pfx[4].st,
        (int)g_swallowUp[0], (int)g_swallowUp[1], (int)g_swallowUp[2],
        (int)g_swallowUp[3], (int)g_swallowUp[4]);

    swallow = dispatch(msg, m);
    DBG("   -> %s", swallow ? "SWALLOW" : "pass");
    return swallow;
}

/* ------------------------------------------------------------------ */
/*  登録キー                                                          */
/*
 *  マウスのボタンを押しながら、登録しておいたキーボードのキーを叩く。
 *  ボタン同士の同時押しと決着のつけ方は同じで、違うのは入口だけ:
 *  こちらは低レベルキーボードフック(main.c の LLKeyProc)から呼ばれる。
 *
 *  【割り当ての無いキーではプレフィクスを昇格させない】
 *  ボタンが後から押された場合は、割り当てが無ければ保留を解いて世に出す
 *  (物理状態に忠実にするため)。キーボードでこれをやってはいけない。
 *  右ボタンを押したまま何か文字を打っただけで右クリックが確定してしまい、
 *  メニューが飛び出す。キーは「一致したときだけ」触り、それ以外は
 *  見なかったことにして素通しする。
 *
 *  フックは全キーストロークを通るので、一致しない場合は最短で返る。
 *  そもそもプレフィクスが待機していなければ即座に抜ける。
 * ------------------------------------------------------------------ */

/* 修飾キーは左右をまとめる。設定側(token_to_vk)が左に寄せているため。 */
static WORD key_normalize(DWORD vk)
{
    switch (vk) {
    case VK_RCONTROL: case VK_CONTROL: return VK_LCONTROL;
    case VK_RMENU:    case VK_MENU:    return VK_LMENU;
    case VK_RSHIFT:   case VK_SHIFT:   return VK_LSHIFT;
    default: return (WORD)vk;
    }
}

BOOL chord_on_key(UINT msg, const KBDLLHOOKSTRUCT *k)
{
    WORD vk = key_normalize(k->vkCode);
    int  p, i;

    if (msg == WM_KEYUP || msg == WM_SYSKEYUP) {
        /* 押下を握り潰したキーの離上は、必ずこちらで殺す */
        if (keysw_find(vk) >= 0) { keysw_drop(vk); return TRUE; }
        return FALSE;
    }
    if (msg != WM_KEYDOWN && msg != WM_SYSKEYDOWN) return FALSE;

    /* スクロール・モードの抜け道。カーソルが凍っているので、
       マウスが信用できない状況でも必ず戻れるようにしておく。 */
    if (g_scroll && vk == VK_ESCAPE) { scroll_end(); return TRUE; }

    /* オートリピート。既に発火済みなので握り潰すだけ(連打にしない)。 */
    if (keysw_find(vk) >= 0) return TRUE;

    if (!g_on) return FALSE;            /* 無効中は新しく乗っ取らない */

    for (p = 0; p < BTN_COUNT; ++p) {
        if (!pfx_active(p)) continue;
        for (i = 0; i < REGKEY_COUNT; ++i) {
            const Action *a;
            if (g_cfg.regKeyVk[p][i] != vk) continue;
            a = chord_at(p, SUF_KEY0 + i);
            if (!a) continue;
            DBG("regkey: pfx=%d key=%u -> 発火", p, (unsigned)vk);
            fire_action(a, 0, p);
            pfx_set(p, PS_CONSUMED);    /* プレフィクスの離上も殺す */
            keysw_mark(vk);             /* このキーの離上も殺す     */
            return TRUE;
        }
    }
    return FALSE;
}
