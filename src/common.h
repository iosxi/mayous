/* common.h - Mayous 共通定義 */
#ifndef MAYOUS_COMMON_H
#define MAYOUS_COMMON_H

#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

#define MAYOUS_APPNAME      L"Mayous"
#define MAYOUS_VERSION      L"v15"
#define MAYOUS_WNDCLASS     L"MayousHiddenWnd"
#define MAYOUS_AGENT_CLASS  L"MayousWheelAgentWnd"
#define MAYOUS_MUTEX        L"Local\\MayousSingleInstance_{7A1C4E2B-9D3F-4A55-8C10-2E6B0F9D4A31}"

/* 自前で SendInput したイベントを識別するためのタグ。
   これが付いたイベントはフックで一切加工せず素通しする(再入防止)。 */
#define MAYOUS_TAG          ((ULONG_PTR)0x4D594F55u)   /* 'MYOU' */

/* ウィンドウメッセージ */
#define WM_MAYOUS_TRAY      (WM_APP + 1)
#define WM_MAYOUS_SHOWINFO  (WM_APP + 2)
#define WM_MAYOUS_PUMP      (WM_APP + 3)   /* 注入キューを吐き出せ */
/* エージェント宛: wParam 0=水平 1=垂直 / lParam = 量(符号付き) */
#define WM_MAYOUS_AGENT_WHEEL (WM_APP + 4)

/* タイマーID */
#define TIMER_SANITY        1     /* 状態のスタック検出・前面ウィンドウ再評価 */
#define TIMER_KEYPLAY       2     /* 注入したキーを押しっぱなしにする時間の管理 */
#define TIMER_HOLD_BASE     10    /* +ボタン添字 */
#define TIMER_KEYREL_BASE   20    /* +ボタン添字。押しっぱなしのキーを離す時刻 */

/* ---------------- ボタン ---------------- */

/* ボタン添字。サフィックス添字と先頭 5 つが一致するように並べてある。
   こうしておくと押下ハンドラで添字を変換せずに済む。 */
enum { BTN_L = 0, BTN_R = 1, BTN_M = 2, BTN_X1 = 3, BTN_X2 = 4, BTN_COUNT = 5 };

/* 登録キー: マウスのボタンを押しながら叩くキーボードのキー。
   どのキーをトリガーにするかは利用者が設定画面で登録する(既定は空)。
   増やしたいときはここを 2, 3... にするだけでよい。config.c の
   kKeySufName / kKeySufIni の要素数(4)が上限。 */
#define REGKEY_COUNT     1
#define REGKEY_SPEC_CCH  32

/* サフィックス(後から来る入力)。
   SUF_KEY0 から REGKEY_COUNT 個が登録キーの枠。 */
enum {
    SUF_L = 0, SUF_R = 1, SUF_M = 2, SUF_X1 = 3, SUF_X2 = 4,
    SUF_WUP = 5, SUF_WDN = 6,
    SUF_KEY0 = 7,
    SUF_COUNT = SUF_KEY0 + REGKEY_COUNT
};

/* そのサフィックスは登録キーか */
#define SUF_IS_KEY(suf)  ((suf) >= SUF_KEY0)

/* プレフィクスはボタン添字をそのまま使う。対象外が 2 つある。
     中ボタン  … 押しっぱなしにする用途が無く、オートスクロールと衝突する
     左クリック… 押下を離すまで預かる代償(ウィンドウの切り替えが遅れる、
                  枠を掴み損ねる)が大きいので、動作の安定を優先していったん
                  取りやめた。復活させるときはここと settings.c の kTabPfx。
   後から押す側としてはどちらも今までどおり使える。 */
#define PFX_CAN(btn)  ((btn) != BTN_M && (btn) != BTN_L)

/* 同時押し1つ分の識別子。プレフィクスとサフィックスから機械的に決まる。 */
#define CH_ID(pfx, suf)  ((pfx) * SUF_COUNT + (suf))
#define CH_COUNT         (BTN_COUNT * SUF_COUNT)   /* 実際に使うのは 24 + 登録キー */

/* ---------------- アクション ---------------- */

#define MAX_ACTION_KEYS   5      /* 1ステップあたりの同時押しキー数 */
#define MAX_ACTION_STEPS  8      /* 記録できるステップ数            */
#define ACTION_SPEC_CCH   160

typedef enum {
    ACT_NONE = 0,
    ACT_KEYS,          /* steps[] を順に再生する            */
    ACT_HOLD_KEYS,     /* 同時押しを保っている間ずっと押しっぱなしにする */
    ACT_HWHEEL_LEFT,   /* 水平ホイール左                    */
    ACT_HWHEEL_RIGHT,  /* 水平ホイール右                    */
    ACT_PASSTHRU       /* 単独クリック用: 何も変えずそのまま */
} ActionKind;

/* 1ステップ = 同時に押される一組のキー。押す順・離す逆順で送る。 */
typedef struct {
    WORD keys[MAX_ACTION_KEYS];
    int  nkeys;
} KeyStep;

typedef struct {
    ActionKind kind;
    KeyStep    steps[MAX_ACTION_STEPS];
    int        nsteps;
    WCHAR      spec[ACTION_SPEC_CCH];   /* 元の設定文字列(表示・保存用) */
} Action;

/* ---------------- 外観 ---------------- */

typedef enum { THEME_SYSTEM = 0, THEME_LIGHT, THEME_DARK } ThemeMode;

/* ---------------- 設定 ---------------- */

#define MAX_EXCLUDE 2048

/* 注入したキーを「最低でも」押しておく時間。
   同時押しに割り当てたキーはプレフィクスを離すまで押しっぱなしにするので、
   通常はこの値より長くなる。効いてくるのは、同時押しが一瞬で終わった場合と、
   押しっぱなしにしようがない場合(複数ステップ・単独クリック)。
   キーボードフックで待ち受けるアプリは押下の瞬間に気付くので何 ms でも
   構わないが、GetAsyncKeyState を一定間隔で見に行く作りのアプリは、
   押している時間がその間隔より短いと丸ごと取りこぼす。拡大鏡のように
   毎周期で重い描画をするアプリだと間隔が 100ms 前後まで伸びることがある
   ため、既定はそれを超える値にしてある。 */
#define KEY_HOLD_MS_DEFAULT 120
#define KEY_HOLD_MS_MIN     1
#define KEY_HOLD_MS_MAX     2000

/* 同じキーを押し直すときに空ける時間。
   プレフィクスを押したまま同じ組み合わせをもう一度成立させると、いったん
   離してから押し直すことになる。ここで間を空けないと、キーの状態を一定間隔で
   見に行く作りのアプリからは離した瞬間が見えず、2 回目以降が無かったことに
   なる(押している時間に下限が要るのと同じ理由)。
   一方この時間はそのまま体感の遅れになるので、相手に合わせて選べるように
   してある。押し直す先が別のキーなら間は空けない(chord.c の hold_begin)。
   選べるのは下の 4 段階だけ。ini に他の値が書かれていたら一番近いものへ丸める。 */
#define REPRESS_GAP_STEPS      4
#define REPRESS_GAP_MS_DEFAULT 120
extern const int kRepressGapMs[REPRESS_GAP_STEPS];   /* 120 / 80 / 40 / 20 */

typedef struct {
    BOOL   enabled;
    int    dragThreshold;              /* px, 0 = システム値(SM_CXDRAG)を使う */
    int    holdTimeoutMs[BTN_COUNT];   /* 0 = 無効(離すまで保留し続ける)     */
    BOOL   suspendOnFullscreen;
    int    keyHoldMs;                  /* 注入したキーを押しておく時間(ms) */
    int    repressGapMs;               /* 同じキーを押し直すまで空ける時間(ms) */
    ThemeMode theme;                   /* 設定画面の配色 */
    Action chord[CH_COUNT];
    Action single[BTN_COUNT];          /* 単独クリックの置き換え(サイドボタン用) */
    /* 登録キーのトリガー。プレフィクスごとに別々に持つので、
       「右クリック + F13」と「サイドボタン1 + F13」を同時に使える。 */
    WORD   regKeyVk[BTN_COUNT][REGKEY_COUNT];                    /* 0 = 未登録 */
    WCHAR  regKeySpec[BTN_COUNT][REGKEY_COUNT][REGKEY_SPEC_CCH]; /* 表示・保存用 */
    /* ";notepad.exe;game.exe;" 形式の小文字化済み除外リスト */
    WCHAR  exclude[MAX_EXCLUDE];
    WCHAR  iniPath[MAX_PATH];
} Config;

extern Config g_cfg;

/* config.c */
void  cfg_resolve_path(void);
BOOL  cfg_path_writable(void);
void  cfg_load(void);
BOOL  cfg_write_default_if_missing(void);
BOOL  cfg_is_excluded(const WCHAR *exeName);
BOOL  cfg_parse_action(const WCHAR *src, Action *a);   /* 解釈できたら TRUE */
BOOL  cfg_action_valid(const WCHAR *spec);
void  cfg_write_str(const WCHAR *sec, const WCHAR *key, const WCHAR *val);
void  cfg_write_int(const WCHAR *sec, const WCHAR *key, int val);
void  cfg_chord_ini_key(int pfx, int suf, WCHAR *out, int cch);
void  cfg_single_ini_key(int btn, WCHAR *out, int cch);
void  cfg_regkey_ini_key(int pfx, int idx, WCHAR *out, int cch);
WORD  cfg_spec_to_vk(const WCHAR *spec);   /* "f13" -> VK_F13。駄目なら 0 */
const WCHAR *cfg_vk_name(WORD vk);         /* VK -> "f13" */
const WCHAR *cfg_btn_name(int btn);        /* "左クリック" など       */
const WCHAR *cfg_suf_name(int suf);        /* "ホイール上" など       */
const WCHAR *cfg_hold_ini_key(int btn);    /* 長押し判定の ini キー   */
int   cfg_repress_gap_snap(int ms);        /* 一番近い段階の値へ丸める */

/* chord.c */
void  chord_init(HWND hwnd);
void  chord_recompute(void);
BOOL  chord_on_mouse(UINT msg, const MSLLHOOKSTRUCT *m);
BOOL  chord_on_key(UINT msg, const KBDLLHOOKSTRUCT *k);   /* 登録キー用 */
void  chord_on_hold_timeout(int btn);
void  chord_key_tick(void);        /* TIMER_KEYPLAY から呼ぶ */
void  chord_key_release_tick(int pfx);  /* TIMER_KEYREL_BASE + pfx から呼ぶ */
void  chord_on_raw_input(HRAWINPUT h);  /* WM_INPUT から呼ぶ(物理ボタンの地面) */
void  chord_pump(void);
void  chord_set_active(BOOL on);
void  chord_on_desktop_switch(void);
void  chord_sanity(void);
void  chord_reset(void);

/* agent.c - ホイール注入専用の子プロセス(理由は agent.c 冒頭のコメント参照) */
int   agent_main(HINSTANCE inst, DWORD parentPid);
void  agent_ensure(void);
void  agent_send_wheel(int amount, BOOL horizontal);
void  agent_stop(void);

/* settings.c - 設定ウィンドウ */
void  settings_open(HINSTANCE inst, HWND owner);
HWND  settings_hwnd(void);
HFONT settings_font(void);             /* 記録ウィンドウが同じ字面を使うため */
void  settings_apply_callback(void);   /* main.c が実装。保存直後の再読み込み */

/* theme.c - システムのライト/ダークに合わせた配色 */
void     theme_init(void);
BOOL     theme_refresh(void);          /* 変わっていたら TRUE */
BOOL     theme_is_dark(void);
COLORREF theme_back(void);
COLORREF theme_ctrl_back(void);
COLORREF theme_text(void);
COLORREF theme_dim_text(void);
COLORREF theme_line(void);
COLORREF theme_hot(void);
HBRUSH   theme_back_brush(void);
HBRUSH   theme_ctrl_brush(void);
void     theme_apply_window(HWND hwnd);
void     theme_apply_control(HWND ctl, const WCHAR *cls);
BOOL     theme_ctlcolor(UINT msg, HDC dc, HBRUSH *br);
void     theme_draw_group(const DRAWITEMSTRUCT *di, HFONT font);
void     theme_draw_tab(const DRAWITEMSTRUCT *di, HFONT font);

/* legacy.c - 昔のバージョンが残したレジストリ登録の掃除 */
void  startup_cleanup_legacy(void);

/* capture.c - キー入力の記録 */
BOOL  capture_run(HINSTANCE inst, HWND owner, WCHAR *out, int cch);
/* 登録キーのトリガー用。キーを 1 つだけ記録する。
   何も押さずに OK なら out は空 = 解除。返り値は「OK が押されたか」。 */
BOOL  capture_run_key(HINSTANCE inst, HWND owner, WCHAR *out, int cch);

/* main.c */
extern volatile LONG g_active;   /* フックが加工してよいか(有効 && 非サスペンド) */

/* デバッグ用トレース。リリースビルドでは完全に消える。
   出力先: %TEMP%\mayous_debug.log */
#ifdef MAYOUS_DEBUG
void dbg(const char *fmt, ...);
#define DBG(...) dbg(__VA_ARGS__)
#else
#define DBG(...) ((void)0)
#endif

#endif /* MAYOUS_COMMON_H */
