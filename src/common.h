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
#define MAYOUS_VERSION      L"v1"
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
#define TIMER_HOLD_BASE     10    /* +ボタン添字 */

/* ---------------- ボタン ---------------- */

/* ボタン添字。サフィックス添字と先頭 5 つが一致するように並べてある。
   こうしておくと押下ハンドラで添字を変換せずに済む。 */
enum { BTN_L = 0, BTN_R = 1, BTN_M = 2, BTN_X1 = 3, BTN_X2 = 4, BTN_COUNT = 5 };

/* サフィックス(後から来る入力) */
enum {
    SUF_L = 0, SUF_R = 1, SUF_M = 2, SUF_X1 = 3, SUF_X2 = 4,
    SUF_WUP = 5, SUF_WDN = 6, SUF_COUNT = 7
};

/* プレフィクスはボタン添字をそのまま使う。中ボタンだけは対象外
   (押しっぱなしにする用途が無く、オートスクロールと衝突するため)。 */
#define PFX_CAN(btn)  ((btn) != BTN_M)

/* 同時押し1つ分の識別子。プレフィクスとサフィックスから機械的に決まる。 */
#define CH_ID(pfx, suf)  ((pfx) * SUF_COUNT + (suf))
#define CH_COUNT         (BTN_COUNT * SUF_COUNT)   /* 35 枠。実際に使うのは 24 */

/* ---------------- アクション ---------------- */

#define MAX_ACTION_KEYS   5      /* 1ステップあたりの同時押しキー数 */
#define MAX_ACTION_STEPS  8      /* 記録できるステップ数            */
#define ACTION_SPEC_CCH   160

typedef enum {
    ACT_NONE = 0,
    ACT_KEYS,          /* steps[] を順に再生する            */
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

/* ---------------- 設定 ---------------- */

#define MAX_EXCLUDE 2048

typedef struct {
    BOOL   enabled;
    int    dragThreshold;              /* px, 0 = システム値(SM_CXDRAG)を使う */
    int    holdTimeoutMs[BTN_COUNT];   /* 0 = 無効(離すまで保留し続ける)     */
    BOOL   suspendOnFullscreen;
    Action chord[CH_COUNT];
    Action single[BTN_COUNT];          /* 単独クリックの置き換え(サイドボタン用) */
    /* ";notepad.exe;game.exe;" 形式の小文字化済み除外リスト */
    WCHAR  exclude[MAX_EXCLUDE];
    WCHAR  iniPath[MAX_PATH];
} Config;

extern Config g_cfg;

/* config.c */
void  cfg_resolve_path(void);
void  cfg_load(void);
BOOL  cfg_write_default_if_missing(void);
BOOL  cfg_is_excluded(const WCHAR *exeName);
BOOL  cfg_parse_action(const WCHAR *src, Action *a);   /* 解釈できたら TRUE */
BOOL  cfg_action_valid(const WCHAR *spec);
void  cfg_write_str(const WCHAR *sec, const WCHAR *key, const WCHAR *val);
void  cfg_write_int(const WCHAR *sec, const WCHAR *key, int val);
void  cfg_chord_ini_key(int pfx, int suf, WCHAR *out, int cch);
void  cfg_single_ini_key(int btn, WCHAR *out, int cch);
const WCHAR *cfg_btn_name(int btn);        /* "左クリック" など       */
const WCHAR *cfg_suf_name(int suf);        /* "ホイール上" など       */
const WCHAR *cfg_hold_ini_key(int btn);    /* 長押し判定の ini キー   */

/* chord.c */
void  chord_init(HWND hwnd);
void  chord_recompute(void);
BOOL  chord_on_mouse(UINT msg, const MSLLHOOKSTRUCT *m);
void  chord_on_hold_timeout(int btn);
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
void  settings_apply_callback(void);   /* main.c が実装。保存直後の再読み込み */

/* startup.c - スタートアップフォルダのショートカットで自動起動 */
BOOL  startup_enabled(void);
void  startup_set(BOOL on);
void  startup_cleanup_legacy(void);
void  startup_folder(WCHAR *out, int cch);

/* capture.c - キー入力の記録 */
BOOL  capture_run(HINSTANCE inst, HWND owner, WCHAR *out, int cch);

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
