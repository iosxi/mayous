/* ==================================================================
 * settings.c - 設定ウィンドウ(通常ウィンドウ)
 *
 *  リソースのダイアログテンプレートは使わず、コントロールを直接生成する。
 *  .rc に日本語を入れると windres のコードページ解釈に振り回されるため、
 *  文字列は C ソース側(UTF-8 -> L"..." は UTF-16 になる)に置いている。
 *
 *  プレフィクスになれるボタンが 3 つ、割り当て枠がその数だけあるので、
 *  タブでプレフィクスごとに切り替える。コントロールは全タブ分まとめて
 *  作っておき、表示・非表示だけを切り替える。
 *
 *  ダイアログではないので Tab 移動と Enter/Esc は自前で面倒を見る:
 *    ・ウィンドウに WS_EX_CONTROLPARENT、各コントロールに WS_TABSTOP
 *    ・メインのメッセージループで IsDialogMessageW() を通す
 *
 *  レイアウトはフォントの実測高さを基準に組み立てるので、DPI が変わっても
 *  比率が保たれる。WM_DPICHANGED では作り直して並べ直す。
 * ================================================================== */

#include "common.h"
#include <shellapi.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <wchar.h>

#ifndef DWMWA_CLOAKED
#define DWMWA_CLOAKED 14
#endif

#define WNDCLASS_SETTINGS L"MayousSettingsWnd"

/* コントロール ID */
#define IDC_TAB          900
#define IDC_CHORD_BASE   2000            /* +CH_ID(pfx,suf) */
#define IDC_REC_BASE     2100            /* +CH_ID(pfx,suf) */
#define IDC_SINGLE_BASE  2200            /* +btn */
#define IDC_SREC_BASE    2210            /* +btn */
#define IDC_KEYTRIG_BASE 2300            /* +btn*REGKEY_COUNT+枠 登録キーのキー */
#define IDC_SCROLLSPEED  1150            /* オートスクロールの速さ(%) */
#define IDC_FULLSCREEN   1102
#define IDC_HOLD_BASE    1110            /* +btn */
#define IDC_DRAG         1119
#define IDC_GAP_BASE     1140            /* +段階(0..REPRESS_GAP_STEPS-1) */
#define IDC_EXCLUDE      1120   /* 条件のテキスト欄       */
#define IDC_EXC_LIST     1121   /* 今開いているウィンドウ */
#define IDC_EXC_REFRESH  1122
#define IDC_EXC_ADDEXE   1123
#define IDC_EXC_ADDTITLE 1124
#define IDC_OK           1130
#define IDC_CANCEL       1131
#define IDC_APPLY        1132
#define IDC_OPENINI      1133
#define IDC_GROUP         950   /* グループ枠(ダーク時は所有者描画) */

/* ------------------------------------------------------------------ */
/* 一覧から選べる機能                                                  */
/*   コンボボックスは編集可能なので、ここに無いキーコンボも直接打ち込める。 */
/* ------------------------------------------------------------------ */

typedef struct { const WCHAR *label; const WCHAR *spec; } Preset;

static const Preset kPresets[] = {
    { L"なし",                              L"none" },

    { L"── ウィンドウ操作 ──",              NULL },
    { L"Windows キー (スタート)",           L"win" },
    { L"Alt+Tab  次のウィンドウ",           L"alttab" },
    { L"Alt+Shift+Tab  前のウィンドウ",     L"alttab_back" },
    { L"Alt+Esc  ウィンドウを順に切り替え", L"alt+esc" },
    { L"タスクビュー (Win+Tab)",            L"win+tab" },
    { L"デスクトップを表示 (Win+D)",        L"win+d" },
    { L"ウィンドウを閉じる (Alt+F4)",       L"alt+f4" },
    { L"最大化 (Win+Up)",                   L"win+up" },
    { L"最小化 (Win+Down)",                 L"win+down" },
    { L"左に寄せる (Win+Left)",             L"win+left" },
    { L"右に寄せる (Win+Right)",            L"win+right" },
    { L"仮想デスクトップ 左 (Win+Ctrl+Left)",  L"win+ctrl+left" },
    { L"仮想デスクトップ 右 (Win+Ctrl+Right)", L"win+ctrl+right" },

    { L"── スクロール ──",                  NULL },
    { L"水平ホイール 左",                   L"hwheel_left" },
    { L"水平ホイール 右",                   L"hwheel_right" },
    { L"オートスクロール",                  L"autoscroll" },

    { L"── マウスのボタン ──",              NULL },
    { L"中クリック",                        L"click:middle" },

    /* ほかの常駐アプリのトリガーとして使うためのキー。同時押しを保っている
       あいだ押しっぱなしになるので、「押している間だけ効く」タイプにも使える。
       F13〜F15 は普通のキーボードに無いので、誤爆の心配がない。 */
    { L"── ほかのアプリのトリガー ──",      NULL },
    { L"F13",                               L"f13" },
    { L"F14",                               L"f14" },
    { L"F15",                               L"f15" },
    { L"Ctrl",                              L"ctrl" },
    { L"Shift",                             L"shift" },

    { L"── ブラウザ・タブ ──",              NULL },
    { L"戻る (Alt+Left)",                   L"alt+left" },
    { L"進む (Alt+Right)",                  L"alt+right" },
    { L"更新 (F5)",                         L"f5" },
    { L"全画面 (F11)",                      L"f11" },
    { L"新しいタブ (Ctrl+T)",               L"ctrl+t" },
    { L"タブを閉じる (Ctrl+W)",             L"ctrl+w" },
    { L"閉じたタブを復元 (Ctrl+Shift+T)",   L"ctrl+shift+t" },
    { L"次のタブ (Ctrl+Tab)",               L"ctrl+tab" },
    { L"前のタブ (Ctrl+Shift+Tab)",         L"ctrl+shift+tab" },

    { L"── 編集 ──",                        NULL },
    { L"コピー (Ctrl+C)",                   L"ctrl+c" },
    { L"貼り付け (Ctrl+V)",                 L"ctrl+v" },
    { L"切り取り (Ctrl+X)",                 L"ctrl+x" },
    { L"元に戻す (Ctrl+Z)",                 L"ctrl+z" },
    { L"やり直し (Ctrl+Y)",                 L"ctrl+y" },
    { L"すべて選択 (Ctrl+A)",               L"ctrl+a" },
    { L"保存 (Ctrl+S)",                     L"ctrl+s" },
    { L"検索 (Ctrl+F)",                     L"ctrl+f" },
    { L"削除 (Delete)",                     L"delete" },

    { L"── メディア・その他 ──",            NULL },
    { L"音量を上げる",                      L"volumeup" },
    { L"音量を下げる",                      L"volumedown" },
    { L"ミュート",                          L"volumemute" },
    { L"再生 / 一時停止",                   L"mediaplay" },
    { L"次の曲",                            L"medianext" },
    { L"前の曲",                            L"mediaprev" },
    { L"エクスプローラー (Win+E)",          L"win+e" },
    { L"スクリーンショット (Win+Shift+S)",  L"win+shift+s" },
    { L"アプリキー (右クリックメニュー)",   L"apps" },
};

/* 単独クリック用の先頭項目 */
static const Preset kPassthru = { L"そのまま (本来のクリック)", L"passthru" };

/* ------------------------------------------------------------------ */

static HINSTANCE g_inst;
static HWND   g_wnd, g_tab;
static HFONT  g_font;
static int    g_unit = 16;               /* レイアウトの基準 = フォント高さ */

static HWND   g_lbl[CH_COUNT], g_cmb[CH_COUNT], g_rec[CH_COUNT];
static HWND   g_slbl[BTN_COUNT], g_scmb[BTN_COUNT], g_srec[BTN_COUNT];
static HWND   g_hHold[BTN_COUNT];
static HWND   g_hDrag, g_hExclude, g_hApply;

/* 登録キーのトリガー。ボタンの文字が今の登録内容そのものになる。
   編集中の値は ini ではなくこちらに置き、[OK]/[適用] でまとめて書く。 */
static HWND   g_trig[BTN_COUNT][REGKEY_COUNT];

/* 中クリックのタブだけに出す部品(速さの欄と注意書き)。
   タブを切り替えたときに一緒に消せるよう、handle を控えておく。 */
static HWND   g_mid[8];
static int    g_midN;

/* 「停止する条件」タブの部品。中身はボタンと無関係なので別に控える。 */
static HWND   g_exc[10];
static int    g_excN;
static HWND   g_hExcList;
static WCHAR  g_trigSpec[BTN_COUNT][REGKEY_COUNT][REGKEY_SPEC_CCH];

/* 未保存の変更があるか。[適用] の活性はこれに従う。 */
static BOOL   g_dirty;

static void rebuild(HWND hwnd);
static void exc_fill_list(void);

static void set_dirty(BOOL dirty)
{
    g_dirty = dirty;
    if (g_hApply) EnableWindow(g_hApply, dirty);
}

/* タブの並び。中ボタンと左クリックはプレフィクスになれないので入らない。
   左クリックは、押下を離すまで預かる代償(ウィンドウの切り替えが遅れる、
   枠を掴み損ねる)が大きく、動作の安定を優先していったん取りやめた。
   復活させるときは PFX_CAN() と、ここへ BTN_L を戻す。 */
/* 最後の 1 枚はボタンではなく「停止する条件」。ボタン添字の代わりに
   この番兵を入れておき、ボタンで添字を引く処理はすべて弾く。 */
#define TAB_EXCLUDE (-1)
static const int kTabPfx[] = { BTN_R, BTN_X1, BTN_X2, BTN_M, TAB_EXCLUDE };

static const WCHAR *tab_name(int pfx)
{
    return (pfx == TAB_EXCLUDE) ? L"停止する条件" : cfg_btn_name(pfx);
}
#define TAB_COUNT ((int)ARRAYSIZE(kTabPfx))

/* 96dpi で書いた値を実際のフォント高さに合わせて伸ばす */
static int U(int v) { return MulDiv(v, g_unit, 16); }

/* ------------------------------------------------------------------ */

static const WCHAR *spec_to_label(const WCHAR *spec)
{
    size_t i;
    if (!spec || !*spec) spec = L"none";
    if (!lstrcmpiW(spec, kPassthru.spec)) return kPassthru.label;
    for (i = 0; i < ARRAYSIZE(kPresets); ++i)
        if (kPresets[i].spec && !lstrcmpiW(kPresets[i].spec, spec))
            return kPresets[i].label;
    return NULL;                         /* 一覧に無い = 手入力・記録された指定 */
}

static void label_to_spec(const WCHAR *label, WCHAR *out, int cch)
{
    size_t i;
    if (!lstrcmpW(label, kPassthru.label)) { lstrcpynW(out, kPassthru.spec, cch); return; }
    for (i = 0; i < ARRAYSIZE(kPresets); ++i)
        if (!lstrcmpW(kPresets[i].label, label)) {
            /* 見出し行が選ばれてしまった場合は「なし」に倒す */
            lstrcpynW(out, kPresets[i].spec ? kPresets[i].spec : L"none", cch);
            return;
        }
    lstrcpynW(out, label, cch);          /* 手入力・記録された指定はそのまま使う */
}

/* ------------------------------------------------------------------ */
/* コントロール生成                                                    */
/* ------------------------------------------------------------------ */

static HWND mk(HWND parent, const WCHAR *cls, const WCHAR *text, DWORD style,
               int x, int y, int w, int h, int id)
{
    /* WS_CLIPSIBLINGS は必須。
       これが無いと、あるコントロールの DC が「上に載っている兄弟」の領域まで
       含んでしまい、そこへ塗ると兄弟を消してしまう。グループ枠は所有者描画で
       自分の矩形を FillRect するので、枠の中にあるチェックボックスや入力欄が
       まとめて消える(Alt+Tab の一覧が重なって離れた直後に発生した)。
       消えたコントロールは非表示になったのではなく、塗り潰されただけ。 */
    HWND c = CreateWindowExW(0, cls, text,
                             WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style,
                             U(x), U(y), U(w), U(h), parent,
                             (HMENU)(INT_PTR)id, g_inst, NULL);
    if (c) {
        SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
        theme_apply_control(c, cls);
    }
    return c;
}

static void fill_combo(HWND combo, BOOL withPassthru)
{
    size_t i;
    if (withPassthru) SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)kPassthru.label);
    for (i = 0; i < ARRAYSIZE(kPresets); ++i)
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)kPresets[i].label);
}

/* ------------------------------------------------------------------ */
/*  DPI
 *
 *  【SystemParametersInfo は今の DPI を教えてくれない】
 *  SPI_GETNONCLIENTMETRICS が返すフォントは、プロセスが動き出した時点の
 *  システム DPI のものである。マニフェストで PerMonitorV2 を宣言していても
 *  この関数だけは追従しない。そのため、
 *      100% で常駐 -> 表示スケールを 150% に変更 -> 設定画面を開く
 *  という手順を踏むと、150% の画面に 100% の文字で描かれ、異常に小さく見える
 *  (利用者からの報告)。開き直しても直らない。プロセスを起動し直すまで
 *  ずっとそのままになる。
 *
 *  1607 以降には DPI を指定して聞ける SystemParametersInfoForDpi がある。
 *  窓の載っているモニタの DPI を GetDpiForWindow で取り、それを渡す。
 *  どちらも user32 にあるが、古い環境でも起動できるよう GetProcAddress で
 *  取り、取れなければ従来どおりの経路に落ちる。
 * ------------------------------------------------------------------ */

typedef UINT (WINAPI *fnGetDpiForWindow)(HWND);
typedef BOOL (WINAPI *fnSPIForDpi)(UINT, UINT, PVOID, UINT, UINT);

static fnGetDpiForWindow p_GetDpiForWindow;
static fnSPIForDpi       p_SPIForDpi;
static BOOL              g_dpiProbed;

static void dpi_probe(void)
{
    HMODULE u;
    if (g_dpiProbed) return;
    g_dpiProbed = TRUE;
    u = GetModuleHandleW(L"user32.dll");
    if (!u) return;
    p_GetDpiForWindow = (fnGetDpiForWindow)(void *)GetProcAddress(u, "GetDpiForWindow");
    p_SPIForDpi       = (fnSPIForDpi)(void *)GetProcAddress(u, "SystemParametersInfoForDpi");
}

/* ref の載っているモニタの DPI に合わせてフォントを作る。
   ref は設定ウィンドウ自身。まだ無いとき(初回)は呼び出し元の窓を渡す。 */
static void make_font(HWND ref)
{
    NONCLIENTMETRICSW ncm;
    HDC dc;
    TEXTMETRICW tm;
    HFONT old;
    UINT dpi = 0;
    BOOL got = FALSE;

    if (g_font) { DeleteObject(g_font); g_font = NULL; }

    dpi_probe();
    if (ref && p_GetDpiForWindow) dpi = p_GetDpiForWindow(ref);

    ncm.cbSize = sizeof(ncm);
    if (dpi && p_SPIForDpi)
        got = p_SPIForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi);
    if (!got)
        got = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    if (got)
        g_font = CreateFontIndirectW(&ncm.lfMessageFont);
    if (!g_font) g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    /* 実際の文字高さをレイアウト単位にする = DPI に自動追従する */
    dc  = GetDC(NULL);
    old = (HFONT)SelectObject(dc, g_font);
    if (GetTextMetricsW(dc, &tm)) g_unit = (int)tm.tmHeight;
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    if (g_unit < 10) g_unit = 16;
}

/* ------------------------------------------------------------------ */
/* タブ切り替え                                                        */
/* ------------------------------------------------------------------ */

static void show_tab(int tab)
{
    int t, suf, pfx, i;

    for (t = 0; t < TAB_COUNT; ++t) {
        int show = (t == tab) ? SW_SHOW : SW_HIDE;
        pfx = kTabPfx[t];

        if (pfx == TAB_EXCLUDE) {
            for (i = 0; i < g_excN; ++i)
                if (g_exc[i]) ShowWindow(g_exc[i], show);
            if (show == SW_SHOW) exc_fill_list();   /* 開くたびに取り直す */
            continue;
        }

        if (g_scmb[pfx]) {
            ShowWindow(g_slbl[pfx], show);
            ShowWindow(g_scmb[pfx], show);
            ShowWindow(g_srec[pfx], show);
        }
        for (suf = 0; suf < SUF_COUNT; ++suf) {
            int id = CH_ID(pfx, suf);
            if (!g_cmb[id]) continue;
            ShowWindow(g_lbl[id], show);
            ShowWindow(g_cmb[id], show);
            ShowWindow(g_rec[id], show);
        }
        for (i = 0; i < REGKEY_COUNT; ++i)
            if (g_trig[pfx][i]) ShowWindow(g_trig[pfx][i], show);
        if (pfx == BTN_M)
            for (i = 0; i < g_midN; ++i)
                if (g_mid[i]) ShowWindow(g_mid[i], show);
    }
}

/* ------------------------------------------------------------------ */
/* 値の出し入れ                                                        */
/* ------------------------------------------------------------------ */

static void set_combo_text(HWND c, const WCHAR *spec)
{
    const WCHAR *lbl = spec_to_label(spec);
    SetWindowTextW(c, lbl ? lbl : spec);
}

/* 登録キーのボタンに、今のトリガーを出す。未登録なら「未設定」。 */
static void set_trig_text(int pfx, int idx)
{
    WCHAR lb[REGKEY_SPEC_CCH];
    int i;

    if (!g_trig[pfx][idx]) return;
    if (!g_trigSpec[pfx][idx][0]) {
        lstrcpynW(lb, L"未設定", ARRAYSIZE(lb));
    } else {
        lstrcpynW(lb, g_trigSpec[pfx][idx], ARRAYSIZE(lb));
        for (i = 0; lb[i]; ++i)          /* f13 -> F13。ボタンの中で目立たせる */
            if (lb[i] >= L'a' && lb[i] <= L'z') lb[i] = (WCHAR)(lb[i] - L'a' + L'A');
    }
    SetWindowTextW(g_trig[pfx][idx], lb);
}

static void load_values(void)
{
    int pfx, suf, b, t, i;
    WCHAR buf[MAX_EXCLUDE];

    for (t = 0; t < TAB_COUNT; ++t) {
        pfx = kTabPfx[t];
        if (pfx == TAB_EXCLUDE) continue;
        if (g_scmb[pfx]) set_combo_text(g_scmb[pfx], g_cfg.single[pfx].spec);
        for (suf = 0; suf < SUF_COUNT; ++suf) {
            int id = CH_ID(pfx, suf);
            if (g_cmb[id]) set_combo_text(g_cmb[id], g_cfg.chord[id].spec);
        }
        for (i = 0; i < REGKEY_COUNT; ++i) {
            lstrcpynW(g_trigSpec[pfx][i], g_cfg.regKeySpec[pfx][i], REGKEY_SPEC_CCH);
            set_trig_text(pfx, i);
        }
    }

    CheckDlgButton(g_wnd, IDC_FULLSCREEN, g_cfg.suspendOnFullscreen ? BST_CHECKED : BST_UNCHECKED);

    for (b = 0; b < BTN_COUNT; ++b)
        if (g_hHold[b]) SetDlgItemInt(g_wnd, IDC_HOLD_BASE + b, (UINT)g_cfg.holdTimeoutMs[b], FALSE);
    SetDlgItemInt(g_wnd, IDC_DRAG, (UINT)g_cfg.dragThreshold, FALSE);
    SetDlgItemInt(g_wnd, IDC_SCROLLSPEED, (UINT)g_cfg.autoScrollSpeed, FALSE);

    for (b = 0; b < REPRESS_GAP_STEPS; ++b)
        CheckDlgButton(g_wnd, IDC_GAP_BASE + b,
                       (kRepressGapMs[b] == g_cfg.repressGapMs) ? BST_CHECKED : BST_UNCHECKED);

    cfg_exclude_text(buf, ARRAYSIZE(buf));
    SetWindowTextW(g_hExclude, buf);

    exc_fill_list();          /* タブを開く前でも中身がある状態にしておく */

    /* ここまでの SetWindowText で EDIT が EN_CHANGE を投げてくるので、
       読み込みの最後に必ず「変更なし」へ戻す。 */
    set_dirty(FALSE);
}

/* 1 つのコンボの内容を検証して ini へ書く */
static BOOL save_one(HWND combo, const WCHAR *sec, const WCHAR *key, const WCHAR *what)
{
    WCHAR text[ACTION_SPEC_CCH], spec[ACTION_SPEC_CCH];

    GetWindowTextW(combo, text, ARRAYSIZE(text));
    label_to_spec(text, spec, ARRAYSIZE(spec));
    if (!cfg_action_valid(spec)) {
        WCHAR msg[512];
        wsprintfW(msg, L"「%s」の指定を解釈できません:\r\n\r\n    %s\r\n\r\n"
                       L"一覧から選ぶか、[記録] ボタンで実際にキーを押して指定してください。",
                  what, spec);
        MessageBoxW(g_wnd, msg, MAYOUS_APPNAME, MB_OK | MB_ICONWARNING);
        SetFocus(combo);
        return FALSE;
    }
    cfg_write_str(sec, key, spec);
    return TRUE;
}

/* 保存。問題があれば FALSE を返し、原因のコントロールへフォーカスを移す。 */
static BOOL save_values(void)
{
    int t, pfx, suf, b, i;
    WCHAR key[64], what[128];
    WCHAR raw[MAX_EXCLUDE];
    BOOL ok;

    for (t = 0; t < TAB_COUNT; ++t) {
        pfx = kTabPfx[t];
        if (pfx == TAB_EXCLUDE) continue;

        if (g_scmb[pfx]) {
            cfg_single_ini_key(pfx, key, ARRAYSIZE(key));
            wsprintfW(what, L"%s の単独クリック", cfg_btn_name(pfx));
            if (!save_one(g_scmb[pfx], L"Single", key, what)) {
                TabCtrl_SetCurSel(g_tab, t);
                show_tab(t);
                return FALSE;
            }
        }
        for (suf = 0; suf < SUF_COUNT; ++suf) {
            int id = CH_ID(pfx, suf);
            if (!g_cmb[id]) continue;
            cfg_chord_ini_key(pfx, suf, key, ARRAYSIZE(key));
            wsprintfW(what, L"%s 押し + %s", cfg_btn_name(pfx), cfg_suf_name(suf));
            if (!save_one(g_cmb[id], L"Chords", key, what)) {
                TabCtrl_SetCurSel(g_tab, t);
                show_tab(t);
                return FALSE;
            }
        }

        /* 登録キーのトリガー。動作だけ選んでキーを忘れると、いつまでも
           発火しないのに理由が見えない。ここで止めて気付いてもらう。 */
        for (i = 0; i < REGKEY_COUNT; ++i) {
            int id = CH_ID(pfx, SUF_KEY0 + i);
            WCHAR text[ACTION_SPEC_CCH], spec[ACTION_SPEC_CCH];
            if (!g_cmb[id]) continue;
            GetWindowTextW(g_cmb[id], text, ARRAYSIZE(text));
            label_to_spec(text, spec, ARRAYSIZE(spec));
            if (lstrcmpiW(spec, L"none") && !g_trigSpec[pfx][i][0]) {
                WCHAR msg[512];
                wsprintfW(msg, L"%s に動作が割り当てられていますが、"
                               L"組み合わせるキーが登録されていません。\r\n\r\n"
                               L"[未設定] のボタンを押してキーを登録するか、"
                               L"動作を「なし」にしてください。",
                          cfg_suf_name(SUF_KEY0 + i));
                TabCtrl_SetCurSel(g_tab, t);
                show_tab(t);
                MessageBoxW(g_wnd, msg, MAYOUS_APPNAME, MB_OK | MB_ICONWARNING);
                if (g_trig[pfx][i]) SetFocus(g_trig[pfx][i]);
                return FALSE;
            }
            cfg_regkey_ini_key(pfx, i, key, ARRAYSIZE(key));
            cfg_write_str(L"Chords", key, g_trigSpec[pfx][i]);
        }
    }

    /* 「有効」はトレイのメニューだけで切り替える。ここで書き戻すと、
       トレイで止めたまま設定を開いて [OK] しただけで動き出してしまう。 */
    cfg_write_int(L"General", L"SuspendOnFullscreen",
                  IsDlgButtonChecked(g_wnd, IDC_FULLSCREEN) == BST_CHECKED);

    for (b = 0; b < BTN_COUNT; ++b)
        if (g_hHold[b])
            cfg_write_int(L"General", cfg_hold_ini_key(b),
                          (int)GetDlgItemInt(g_wnd, IDC_HOLD_BASE + b, &ok, FALSE));
    cfg_write_int(L"General", L"DragThreshold", (int)GetDlgItemInt(g_wnd, IDC_DRAG, &ok, FALSE));
    {
        int sp = (int)GetDlgItemInt(g_wnd, IDC_SCROLLSPEED, &ok, FALSE);
        if (sp < AUTOSCROLL_SPEED_MIN) sp = AUTOSCROLL_SPEED_MIN;
        if (sp > AUTOSCROLL_SPEED_MAX) sp = AUTOSCROLL_SPEED_MAX;
        cfg_write_int(L"General", L"AutoScrollSpeed", sp);
    }

    for (b = 0; b < REPRESS_GAP_STEPS; ++b)
        if (IsDlgButtonChecked(g_wnd, IDC_GAP_BASE + b) == BST_CHECKED) {
            cfg_write_int(L"General", L"RepressGapMs", kRepressGapMs[b]);
            break;
        }

    GetWindowTextW(g_hExclude, raw, ARRAYSIZE(raw));
    cfg_write_exclude(raw);

    settings_apply_callback();      /* main.c 側で読み直して即座に反映させる */
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* レイアウト                                                          */
/* ------------------------------------------------------------------ */

#define WIN_W    524
#define ROW_H     26
#define LBL_W    148
#define CMB_W    228
#define REC_W     62
/* 登録キーの行だけラベルを詰めて、キーのボタンを挟む。
   ラベル + ボタンの右端が LBL_W と揃うので、コンボの左端は他の行と同じ。 */
#define KEYLBL_W  78
#define TRIG_W    (LBL_W - KEYLBL_W - 6)

/* タブ内の最大行数: 単独 1 + サフィックス(自分を除く) 6 + 登録キー */
#define TAB_ROWS  (7 + REGKEY_COUNT)

/* 「停止する条件」タブは行で数えられないので、縦の内訳をそのまま足す。
   ここを変えたらタブの中身の並びも同じだけ変えること(両方この定数で組む)。 */
#define EXC_CHK   26    /* フルスクリーンのチェック */
#define EXC_LBL   20    /* 「止める条件」の見出し   */
#define EXC_EDIT  54    /* 条件のテキスト欄         */
#define EXC_HDR   26    /* 「今開いている…」+ [更新] */
#define EXC_LIST  92    /* ウィンドウの一覧         */
#define EXC_BTN   28    /* 追加ボタン               */
#define EXC_NOTE  16    /* 注意書き                 */

#define TAB_H_ROW (32 + TAB_ROWS * ROW_H + 10)
#define TAB_H_EXC (34 + EXC_CHK + EXC_LBL + EXC_EDIT + EXC_HDR + \
                   EXC_LIST + EXC_BTN + EXC_NOTE + 8)
#define TAB_H     (TAB_H_ROW > TAB_H_EXC ? TAB_H_ROW : TAB_H_EXC)
/* 動作: 上余白22 + 長押し2行(26*2) + 距離1行(26)
        + 押し直し1行(26) + その説明1行(22) + 下余白10 */
#define GRP2_H    (22 + 26 * 4 + 22 + 10)

static void add_row(HWND hwnd, const WCHAR *label, int y,
                    HWND *lbl, HWND *cmb, HWND *rec,
                    int cmbId, int recId, BOOL withPassthru)
{
    const int x = 12 + 14;

    *lbl = mk(hwnd, L"STATIC", label, SS_LEFT, x, y + 4, LBL_W, 20, 0);
    /* CBS_DROPDOWN = 編集可能。一覧に無いキーコンボも直接打ち込める。
       高さはドロップダウン展開時の高さを含めて指定する。 */
    *cmb = mk(hwnd, L"COMBOBOX", L"",
              CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP,
              x + LBL_W + 6, y, CMB_W, 280, cmbId);
    fill_combo(*cmb, withPassthru);
    *rec = mk(hwnd, L"BUTTON", L"記録", BS_PUSHBUTTON | WS_TABSTOP,
              x + LBL_W + 6 + CMB_W + 6, y, REC_W, 22, recId);
}

/* 登録キーの行。ラベル + [キー] + 動作のコンボ + [記録]。
   [キー] のボタンには今のトリガーが出ていて、押すと記録し直せる。 */
static void add_key_row(HWND hwnd, int y, int pfx, int idx)
{
    const int x  = 12 + 14;
    const int id = CH_ID(pfx, SUF_KEY0 + idx);
    WCHAR lb[64];

    wsprintfW(lb, L"＋ %s", cfg_suf_name(SUF_KEY0 + idx));
    g_lbl[id] = mk(hwnd, L"STATIC", lb, SS_LEFT, x, y + 4, KEYLBL_W, 20, 0);
    g_trig[pfx][idx] = mk(hwnd, L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP,
                          x + KEYLBL_W + 6, y, TRIG_W, 22,
                          IDC_KEYTRIG_BASE + pfx * REGKEY_COUNT + idx);
    g_cmb[id] = mk(hwnd, L"COMBOBOX", L"",
                   CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP,
                   x + LBL_W + 6, y, CMB_W, 280, IDC_CHORD_BASE + id);
    fill_combo(g_cmb[id], FALSE);
    g_rec[id] = mk(hwnd, L"BUTTON", L"記録", BS_PUSHBUTTON | WS_TABSTOP,
                   x + LBL_W + 6 + CMB_W + 6, y, REC_W, 22, IDC_REC_BASE + id);
}

/* グループ枠。ダークのときはテーマが効かないので SS_OWNERDRAW にして自分で描く。 */
/* 中身を囲む枠。作った順に Z 順の下へ送る必要があるので、handle を控えておく
   (理由は sink_containers() を参照)。 */
static HWND g_group[4];
static int  g_groupN;

static void mk_group(HWND hwnd, const WCHAR *title, int x, int y, int w, int h)
{
    HWND g = theme_is_dark()
           ? mk(hwnd, L"STATIC", title, SS_OWNERDRAW, x, y, w, h, IDC_GROUP)
           : mk(hwnd, L"BUTTON", title, BS_GROUPBOX,  x, y, w, h, IDC_GROUP);
    if (g && g_groupN < (int)ARRAYSIZE(g_group)) g_group[g_groupN++] = g;
}

/* タブとグループ枠を、中に載っているコントロールより下へ沈める。
 *
 *  子ウィンドウは「先に作ったものほど Z 順で上」になる。タブ枠もグループ枠も
 *  中身より先に作るので、放っておくと中身の上に乗ってしまう。
 *  WS_CLIPSIBLINGS を付けた状態でそれをやると、上に乗った枠に切り取られて
 *  中身が一切描かれない。逆に WS_CLIPSIBLINGS を外すと、今度は枠が
 *  自分の矩形を塗るときに中身を塗り潰してしまい、Alt+Tab の一覧が重なって
 *  離れた直後などに中身が消える。
 *  正しいのは「枠を下、中身を上」に並べたうえで WS_CLIPSIBLINGS を付けること。 */
static void sink_containers(void)
{
    int i;
    for (i = 0; i < g_groupN; ++i)
        if (g_group[i])
            SetWindowPos(g_group[i], HWND_BOTTOM, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (g_tab)                          /* タブ枠が最下段 */
        SetWindowPos(g_tab, HWND_BOTTOM, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

/* チェックボックス。
 *
 *  テーマが効いたチェックボックスは、文字を**テーマが**描くため
 *  WM_CTLCOLORSTATIC で指定した文字色を無視する。ダークにしたつもりでも
 *  黒文字のまま出る環境がある(23H2 で確認。24H2 ではたまたま白く出ていた)。
 *  そこで、チェックボックスには箱だけを持たせ、文字は隣の STATIC に出す。
 *  STATIC なら文字色は確実にこちらの指定が効く。
 *  文字をクリックしても切り替わるよう、SS_NOTIFY で拾って箱へ渡す。
 */
#define CHECK_LABEL_OFFSET 500      /* ラベルの ID = 本体の ID + これ */

static HWND mk_check(HWND hwnd, const WCHAR *text, int x, int y, int w, int id)
{
    HWND box = mk(hwnd, L"BUTTON", L"", BS_AUTOCHECKBOX | WS_TABSTOP,
                  x, y + 1, 18, 18, id);
    mk(hwnd, L"STATIC", text, SS_LEFT | SS_CENTERIMAGE | SS_NOTIFY,
       x + 22, y, w - 22, 20, id + CHECK_LABEL_OFFSET);
    return box;
}

/* ラジオボタン。理由も作りも mk_check() と同じ(文字は隣の STATIC に出す)。
   BS_AUTORADIOBUTTON の自動排他はグループの並びに左右され、間に STATIC が
   挟まると当てにならない。BN_CLICKED を拾って CheckRadioButton() で自分で
   排他させるほうが確実なので、素の BS_RADIOBUTTON にしてある。 */
static void mk_radio(HWND hwnd, const WCHAR *text, int x, int y, int w, int id)
{
    mk(hwnd, L"BUTTON", L"", BS_RADIOBUTTON | WS_TABSTOP, x, y + 1, 18, 18, id);
    mk(hwnd, L"STATIC", text, SS_LEFT | SS_CENTERIMAGE | SS_NOTIFY,
       x + 20, y, w - 20, 20, id + CHECK_LABEL_OFFSET);
}

/* タブは「中身の面」の背景を自分で塗ってくれないので、ダークでは
   サブクラス化して WM_ERASEBKGND を差し替える。 */
static LRESULT CALLBACK TabSubclass(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                    UINT_PTR id, DWORD_PTR ref)
{
    (void)id; (void)ref;
    if (msg == WM_ERASEBKGND) {
        RECT r;
        GetClientRect(h, &r);
        FillRect((HDC)wp, &r, theme_ctrl_brush());
        return 1;
    }
    if (msg == WM_NCDESTROY)
        RemoveWindowSubclass(h, TabSubclass, id);
    return DefSubclassProc(h, msg, wp, lp);
}

static void build(HWND hwnd)
{
    const int m  = 12;
    const int gw = WIN_W - m * 2;
    int gy, y, t, suf, b, i;
    TCITEMW ti;

    g_groupN = 0;               /* 作り直すたびに枠の控えも取り直す */

    /* --- タブ --- */
    g_tab = CreateWindowExW(0, WC_TABCONTROLW, L"",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS |
                            (theme_is_dark() ? TCS_OWNERDRAWFIXED : 0),
                            U(m), U(m), U(gw), U(TAB_H), hwnd,
                            (HMENU)(INT_PTR)IDC_TAB, g_inst, NULL);
    SendMessageW(g_tab, WM_SETFONT, (WPARAM)g_font, TRUE);
    theme_apply_control(g_tab, WC_TABCONTROLW);
    if (theme_is_dark())
        SetWindowSubclass(g_tab, TabSubclass, 1, 0);

    /* iImage は必ず -1 にすること。0 のままだとイメージリストを設定していなくても
       画像 0 を取りに行き、選択タブの描き直しでアクセス違反になる。 */
    ZeroMemory(&ti, sizeof(ti));
    ti.mask   = TCIF_TEXT | TCIF_IMAGE;
    ti.iImage = -1;
    for (t = 0; t < TAB_COUNT; ++t) {
        ti.pszText = (LPWSTR)tab_name(kTabPfx[t]);
        SendMessageW(g_tab, TCM_INSERTITEMW, (WPARAM)t, (LPARAM)&ti);
    }

    /* タブの中身。全タブ分をまとめて作り、表示だけ切り替える。 */
    for (t = 0; t < TAB_COUNT; ++t) {
        int pfx = kTabPfx[t];
        y = m + 34;

        /* 「停止する条件」タブ。ここだけボタンと無関係な作りになる。 */
        if (pfx == TAB_EXCLUDE) {
            const int x = 12 + 14;
            const int w = WIN_W - 24 - 28;
            g_excN = 0;
            g_exc[g_excN++] = mk_check(hwnd, L"フルスクリーンのアプリが前面のときは停止する",
                                       x, y, w, IDC_FULLSCREEN);
            g_exc[g_excN++] = GetDlgItem(hwnd, IDC_FULLSCREEN + CHECK_LABEL_OFFSET);
            y += EXC_CHK;

            g_exc[g_excN++] = mk(hwnd, L"STATIC",
                L"止める条件 (1 行に 1 つ)   例: valorant.exe   title:Minecraft*",
                SS_LEFT, x, y, w, 18, 0);
            y += EXC_LBL;
            g_hExclude = mk(hwnd, L"EDIT", L"",
                            ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER | WS_TABSTOP,
                            x, y, w, EXC_EDIT - 4, IDC_EXCLUDE);
            g_exc[g_excN++] = g_hExclude;
            y += EXC_EDIT;

            g_exc[g_excN++] = mk(hwnd, L"STATIC", L"今開いているウィンドウ",
                                 SS_LEFT, x, y + 4, 200, 18, 0);
            g_exc[g_excN++] = mk(hwnd, L"BUTTON", L"更新", BS_PUSHBUTTON | WS_TABSTOP,
                                 x + w - 70, y, 70, 22, IDC_EXC_REFRESH);
            y += EXC_HDR;
            g_hExcList = mk(hwnd, L"LISTBOX", L"",
                            LBS_NOTIFY | WS_VSCROLL | WS_BORDER | WS_TABSTOP,
                            x, y, w, EXC_LIST - 4, IDC_EXC_LIST);
            g_exc[g_excN++] = g_hExcList;
            y += EXC_LIST;

            g_exc[g_excN++] = mk(hwnd, L"BUTTON", L"実行ファイル名を追加",
                                 BS_PUSHBUTTON | WS_TABSTOP, x, y, 170, 24, IDC_EXC_ADDEXE);
            g_exc[g_excN++] = mk(hwnd, L"BUTTON", L"ウィンドウ名を追加",
                                 BS_PUSHBUTTON | WS_TABSTOP, x + 176, y, 170, 24, IDC_EXC_ADDTITLE);
            y += EXC_BTN;
            g_exc[g_excN++] = mk(hwnd, L"STATIC",
                L"※ * は「任意の文字列」です。title:Minecraft* のように短くすると版が変わっても効きます。",
                SS_LEFT, x, y, w, 16, 0);
            continue;
        }

        /* 中ボタンは「先に押す側」になれないので、同時押しの行は無い。
           単独で押したときの動作と、オートスクロールの速さだけを置く。 */
        if (pfx == BTN_M) {
            add_row(hwnd, L"単独クリック", y,
                    &g_slbl[pfx], &g_scmb[pfx], &g_srec[pfx],
                    IDC_SINGLE_BASE + pfx, IDC_SREC_BASE + pfx, TRUE);
            y += ROW_H;
            g_midN = 0;
            g_mid[g_midN++] = mk(hwnd, L"STATIC", L"オートスクロールの速さ", SS_LEFT,
                                 12 + 14, y + 4, LBL_W, 20, 0);
            g_mid[g_midN++] = mk(hwnd, L"EDIT", L"",
                                 ES_NUMBER | ES_RIGHT | WS_BORDER | WS_TABSTOP,
                                 12 + 14 + LBL_W + 6, y, 56, 22, IDC_SCROLLSPEED);
            g_mid[g_midN++] = mk(hwnd, L"STATIC", L"%   (大きいほど速い。20〜500)", SS_LEFT,
                                 12 + 14 + LBL_W + 6 + 60, y + 4, 240, 20, 0);
            y += ROW_H + 6;
            g_mid[g_midN++] = mk(hwnd, L"STATIC",
                L"※ 中ボタンは「先に押す側」にはできません。押しっぱなしにする用途が無く、",
                SS_LEFT, 12 + 14, y, WIN_W - 24 - 28, 16, 0);
            g_mid[g_midN++] = mk(hwnd, L"STATIC",
                L"　 オートスクロールと衝突するためです。後から押す側としては使えます。",
                SS_LEFT, 12 + 14, y + 16, WIN_W - 24 - 28, 16, 0);
            g_mid[g_midN++] = mk(hwnd, L"STATIC",
                L"※ オートスクロール中はカーソルが止まります。もう一度クリックするか",
                SS_LEFT, 12 + 14, y + 38, WIN_W - 24 - 28, 16, 0);
            g_mid[g_midN++] = mk(hwnd, L"STATIC",
                L"　 Esc で抜けます。",
                SS_LEFT, 12 + 14, y + 54, WIN_W - 24 - 28, 16, 0);
            continue;
        }

        /* サイドボタンだけ「単独クリック」を差し替えられるようにする。
           左右クリックまで差し替えると事故のもとなので対象外。 */
        if (pfx == BTN_X1 || pfx == BTN_X2) {
            add_row(hwnd, L"単独クリック", y,
                    &g_slbl[pfx], &g_scmb[pfx], &g_srec[pfx],
                    IDC_SINGLE_BASE + pfx, IDC_SREC_BASE + pfx, TRUE);
            y += ROW_H;
        }
        for (suf = 0; suf < SUF_COUNT; ++suf) {
            int id = CH_ID(pfx, suf);
            WCHAR lb[64];
            if (SUF_IS_KEY(suf)) continue;             /* 登録キーは下でまとめて */
            if (suf == pfx) continue;                  /* 自分自身とは組めない */
            wsprintfW(lb, L"+ %s", cfg_suf_name(suf));
            add_row(hwnd, lb, y, &g_lbl[id], &g_cmb[id], &g_rec[id],
                    IDC_CHORD_BASE + id, IDC_REC_BASE + id, FALSE);
            y += ROW_H;
        }

        /* 登録キーは行の作りが違うので最後にまとめて置く */
        for (i = 0; i < REGKEY_COUNT; ++i) {
            add_key_row(hwnd, y, pfx, i);
            y += ROW_H;
        }
    }
    gy = m + TAB_H + 10;

    /* --- 動作 --- */
    mk_group(hwnd, L" 動作 ", m, gy, gw, GRP2_H);
    y = gy + 22;

    /* 長押し判定は 2 列に並べる */
    {
        static const int order[3] = { BTN_R, BTN_X1, BTN_X2 };
        for (i = 0; i < 3; ++i) {
            int col = i % 2, row = i / 2;
            int x  = m + 14 + col * (gw - 28) / 2;
            int yy = y + row * ROW_H;
            WCHAR lb[64];
            b = order[i];
            wsprintfW(lb, L"%s 長押し", cfg_btn_name(b));
            mk(hwnd, L"STATIC", lb, SS_LEFT, x, yy + 4, 116, 20, 0);
            g_hHold[b] = mk(hwnd, L"EDIT", L"",
                            ES_NUMBER | ES_RIGHT | WS_BORDER | WS_TABSTOP,
                            x + 118, yy, 48, 22, IDC_HOLD_BASE + b);
            mk(hwnd, L"STATIC", L"ms", SS_LEFT, x + 170, yy + 4, 28, 20, 0);
        }
        y += ROW_H * 2;
    }
    mk(hwnd, L"STATIC", L"ドラッグと判定する距離", SS_LEFT, m + 14, y + 4, 148, 20, 0);
    g_hDrag = mk(hwnd, L"EDIT", L"", ES_NUMBER | ES_RIGHT | WS_BORDER | WS_TABSTOP,
                 m + 14 + 152, y, 48, 22, IDC_DRAG);
    mk(hwnd, L"STATIC", L"px    (長押し・距離とも 0 で 無効 / 自動)", SS_LEFT,
       m + 14 + 204, y + 4, 270, 20, 0);
    y += ROW_H;

    /* 同じキーを押し直すまでの間隔。長いほど確実、短いほど速い。
       別のキーへ切り替わる場合は間を空けないので、ここは効かない。 */
    mk(hwnd, L"STATIC", L"同じキーの押し直し", SS_LEFT, m + 14, y + 4, 148, 20, 0);
    for (i = 0; i < REPRESS_GAP_STEPS; ++i) {
        WCHAR lb[32];
        wsprintfW(lb, L"%d ms", kRepressGapMs[i]);
        mk_radio(hwnd, lb, m + 14 + 152 + i * 76, y, 72, IDC_GAP_BASE + i);
    }
    y += 22;
    mk(hwnd, L"STATIC",
       L"長いほど確実に届き、短いほど速く出ます。別のキーへ変わる場合は間を空けません。",
       SS_LEFT, m + 14, y, gw - 28, 18, 0);

    gy += GRP2_H + 10;

    mk(hwnd, L"STATIC",
       L"[記録] で実際にキーを押して割り当てられます。一覧に無い指定は直接入力も可能です。",
       SS_LEFT, m + 2, gy, gw - 4, 18, 0);
    gy += 20;
    mk(hwnd, L"STATIC",
       L"「登録キー」はマウスと組み合わせるキーボードのキーです。左のボタンから登録します。",
       SS_LEFT, m + 2, gy, gw - 4, 18, 0);
    gy += 22;

    /* --- ボタン --- */
    mk(hwnd, L"BUTTON", L"設定ファイルを開く", BS_PUSHBUTTON | WS_TABSTOP,
       m, gy, 140, 26, IDC_OPENINI);
    mk(hwnd, L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP,
       WIN_W - m - 92 * 3 - 16, gy, 92, 26, IDC_OK);
    mk(hwnd, L"BUTTON", L"キャンセル", BS_PUSHBUTTON | WS_TABSTOP,
       WIN_W - m - 92 * 2 - 8, gy, 92, 26, IDC_CANCEL);
    g_hApply = mk(hwnd, L"BUTTON", L"適用", BS_PUSHBUTTON | WS_TABSTOP,
                  WIN_W - m - 92, gy, 92, 26, IDC_APPLY);
    EnableWindow(g_hApply, FALSE);      /* 変更があるまで押せない */
    gy += 26 + m;

    /* クライアント領域をぴったりに合わせる */
    {
        RECT r = { 0, 0, U(WIN_W), U(gy) };
        AdjustWindowRectEx(&r, (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE), FALSE,
                           (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
        SetWindowPos(hwnd, NULL, 0, 0, r.right - r.left, r.bottom - r.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    sink_containers();
    show_tab(0);
    TabCtrl_SetCurSel(g_tab, 0);
}

static void rebuild(HWND hwnd)
{
    HWND c, next;
    c = GetWindow(hwnd, GW_CHILD);
    while (c) { next = GetWindow(c, GW_HWNDNEXT); DestroyWindow(c); c = next; }
    ZeroMemory(g_cmb,  sizeof(g_cmb));
    ZeroMemory(g_scmb, sizeof(g_scmb));
    ZeroMemory(g_hHold, sizeof(g_hHold));
    ZeroMemory(g_trig, sizeof(g_trig));
    ZeroMemory(g_mid, sizeof(g_mid));
    g_midN = 0;
    ZeroMemory(g_exc, sizeof(g_exc));
    g_excN = 0;
    g_hExcList = NULL;
    g_hApply = NULL;
    ZeroMemory(g_group, sizeof(g_group));
    g_groupN = 0;
    make_font(hwnd);
    build(hwnd);
    load_values();
}

static void center_on(HWND hwnd, HWND owner)
{
    RECT r;
    MONITORINFO mi;
    HMONITOR hm;
    int x, y;

    GetWindowRect(hwnd, &r);
    hm = MonitorFromWindow(owner ? owner : hwnd, MONITOR_DEFAULTTOPRIMARY);
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hm, &mi)) return;

    x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - (r.right - r.left)) / 2;
    y = mi.rcWork.top  + ((mi.rcWork.bottom - mi.rcWork.top) - (r.bottom - r.top)) / 2;
    if (y < mi.rcWork.top) y = mi.rcWork.top;
    SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/* ------------------------------------------------------------------ */

/* [記録] が押された。キー入力を記録して、対応するコンボへ書き戻す。 */
static void do_capture(HWND combo)
{
    WCHAR spec[ACTION_SPEC_CCH];
    if (!combo) return;
    if (capture_run(g_inst, g_wnd, spec, ARRAYSIZE(spec))) {
        set_combo_text(combo, spec);
        set_dirty(TRUE);      /* 記録による書き換えは通知が飛ばないので自分で */
    }
    SetFocus(combo);
}

/* ------------------------------------------------------------------ */
/*  停止する条件                                                       */
/*
 *  「実行ファイル名だけでは選り分けられない」というのが出発点。
 *  java.exe は Minecraft も起動ランチャも同じ名前で動くので、
 *  ウィンドウ名まで見ないと片方だけ止められない。
 *  利用者に名前を手で打たせるのは酷なので、今開いている窓を並べて
 *  そこから選べるようにする。
 * ------------------------------------------------------------------ */

/* 一覧に載せる価値のある窓か。人の目に見えているものだけに絞る。 */
static BOOL exc_listable(HWND h)
{
    DWORD pid = 0, cloak = 0;

    if (!IsWindowVisible(h)) return FALSE;
    if (GetAncestor(h, GA_ROOT) != h) return FALSE;            /* 子・持ち主付きは除く */
    if (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return FALSE;
    if (!GetWindowTextLengthW(h)) return FALSE;

    /* UWP は「見えていることになっているが実体は無い」窓を大量に残す。
       DWM に聞けば分かるので、それを落とす。 */
    if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloak, sizeof(cloak))) && cloak)
        return FALSE;

    GetWindowThreadProcessId(h, &pid);
    return pid != GetCurrentProcessId();                       /* 自分は出さない */
}

static void exc_exe_of(HWND h, WCHAR *out, int cch)
{
    DWORD  pid = 0;
    HANDLE ph;

    out[0] = 0;
    GetWindowThreadProcessId(h, &pid);
    if (!pid) return;
    ph = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!ph) return;
    {
        WCHAR path[MAX_PATH];
        DWORD n = ARRAYSIZE(path);
        if (QueryFullProcessImageNameW(ph, 0, path, &n)) {
            WCHAR *b = wcsrchr(path, L'\\');
            lstrcpynW(out, b ? b + 1 : path, cch);
        }
    }
    CloseHandle(ph);
}

static BOOL CALLBACK exc_enum(HWND h, LPARAM p)
{
    WCHAR t[192], exe[64], line[280];
    int idx;

    (void)p;
    if (!exc_listable(h)) return TRUE;

    GetWindowTextW(h, t, ARRAYSIZE(t));
    exc_exe_of(h, exe, ARRAYSIZE(exe));
    wsprintfW(line, L"%s    [%s]", t, exe[0] ? exe : L"?");

    idx = (int)SendMessageW(g_hExcList, LB_ADDSTRING, 0, (LPARAM)line);
    if (idx >= 0) SendMessageW(g_hExcList, LB_SETITEMDATA, (WPARAM)idx, (LPARAM)h);
    return TRUE;
}

static void exc_fill_list(void)
{
    if (!g_hExcList) return;
    SendMessageW(g_hExcList, LB_RESETCONTENT, 0, 0);
    EnumWindows(exc_enum, 0);
}

/* 一覧で選んでいる窓から条件を 1 行足す。 */
static void exc_add(BOOL byTitle)
{
    int   idx = g_hExcList ? (int)SendMessageW(g_hExcList, LB_GETCURSEL, 0, 0) : LB_ERR;
    HWND  target;
    WCHAR line[EXCLUDE_RULE_CCH], buf[MAX_EXCLUDE];
    int   len;

    if (idx == LB_ERR) {
        MessageBoxW(g_wnd, L"下の一覧から、対象のウィンドウを選んでください。",
                    MAYOUS_APPNAME, MB_OK | MB_ICONINFORMATION);
        return;
    }
    target = (HWND)SendMessageW(g_hExcList, LB_GETITEMDATA, (WPARAM)idx, 0);
    if (!target || !IsWindow(target)) {
        MessageBoxW(g_wnd, L"そのウィンドウはもう閉じられています。[更新] を押してください。",
                    MAYOUS_APPNAME, MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (byTitle) {
        WCHAR t[EXCLUDE_RULE_CCH];
        t[0] = 0;
        GetWindowTextW(target, t, ARRAYSIZE(t) - 8);
        if (!t[0]) return;
        /* 末尾に * を付けて前方一致にしておく。題名は版やファイル名で
           後ろが変わることが多いので、完全一致だとすぐ効かなくなる。 */
        lstrcpynW(line, L"title:", ARRAYSIZE(line));
        lstrcatW(line, t);
        lstrcatW(line, L"*");
    } else {
        exc_exe_of(target, line, ARRAYSIZE(line));
        if (!line[0]) return;
    }

    GetWindowTextW(g_hExclude, buf, ARRAYSIZE(buf));
    len = (int)wcslen(buf);
    if (len && buf[len - 1] != L'\n') lstrcatW(buf, L"\r\n");
    if ((int)wcslen(buf) + (int)wcslen(line) + 4 >= MAX_EXCLUDE) return;
    lstrcatW(buf, line);
    lstrcatW(buf, L"\r\n");
    SetWindowTextW(g_hExclude, buf);
    set_dirty(TRUE);
}

/* 登録キーの [キー] ボタンが押された。トリガーを記録し直す。 */
static void do_capture_key(int pfx, int idx)
{
    WCHAR spec[ACTION_SPEC_CCH];

    if (pfx < 0 || pfx >= BTN_COUNT || idx < 0 || idx >= REGKEY_COUNT) return;
    if (!capture_run_key(g_inst, g_wnd, spec, ARRAYSIZE(spec))) return;

    if (spec[0] && !cfg_spec_to_vk(spec)) {
        WCHAR msg[256];
        wsprintfW(msg, L"「%s」は組み合わせるキーにできません。\r\n"
                       L"キーを 1 つだけ押してください。", spec);
        MessageBoxW(g_wnd, msg, MAYOUS_APPNAME, MB_OK | MB_ICONWARNING);
        return;
    }
    lstrcpynW(g_trigSpec[pfx][idx], spec, REGKEY_SPEC_CCH);
    set_trig_text(pfx, idx);
    set_dirty(TRUE);
    if (g_trig[pfx][idx]) SetFocus(g_trig[pfx][idx]);
}

static LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_NOTIFY: {
        const NMHDR *nh = (const NMHDR *)lp;
        if (nh->idFrom == IDC_TAB && nh->code == (UINT)TCN_SELCHANGE)
            show_tab(TabCtrl_GetCurSel(g_tab));
        return 0;
    }

    case WM_COMMAND: {
        int id   = LOWORD(wp);
        int note = HIWORD(wp);

        if (id >= IDC_REC_BASE && id < IDC_REC_BASE + CH_COUNT) {
            do_capture(g_cmb[id - IDC_REC_BASE]);
            return 0;
        }
        if (id >= IDC_SREC_BASE && id < IDC_SREC_BASE + BTN_COUNT) {
            do_capture(g_scmb[id - IDC_SREC_BASE]);
            return 0;
        }
        if (id >= IDC_KEYTRIG_BASE &&
            id <  IDC_KEYTRIG_BASE + BTN_COUNT * REGKEY_COUNT) {
            int n = id - IDC_KEYTRIG_BASE;
            do_capture_key(n / REGKEY_COUNT, n % REGKEY_COUNT);
            return 0;
        }
        if (note == BN_CLICKED) {
            if (id == IDC_EXC_REFRESH)  { exc_fill_list(); return 0; }
            if (id == IDC_EXC_ADDEXE)   { exc_add(FALSE);  return 0; }
            if (id == IDC_EXC_ADDTITLE) { exc_add(TRUE);   return 0; }
        }

        /* チェックボックス・ラジオボタンの文字(隣の STATIC)が押された
           -> 本体へ渡す */
        if (note == STN_CLICKED &&
            (id == IDC_FULLSCREEN + CHECK_LABEL_OFFSET ||
             (id >= IDC_GAP_BASE + CHECK_LABEL_OFFSET &&
              id <  IDC_GAP_BASE + CHECK_LABEL_OFFSET + REPRESS_GAP_STEPS))) {
            HWND box = GetDlgItem(hwnd, id - CHECK_LABEL_OFFSET);
            if (box) { SendMessageW(box, BM_CLICK, 0, 0); SetFocus(box); }
            return 0;
        }

        /* 押し直しの間隔。素の BS_RADIOBUTTON なので排他は自分でやる */
        if (note == BN_CLICKED &&
            id >= IDC_GAP_BASE && id < IDC_GAP_BASE + REPRESS_GAP_STEPS) {
            CheckRadioButton(hwnd, IDC_GAP_BASE,
                             IDC_GAP_BASE + REPRESS_GAP_STEPS - 1, id);
            set_dirty(TRUE);
            return 0;
        }

        /* 何か触られたら [適用] を押せるようにする。
           一覧からの選択・直接入力・チェック・数値のどれでも拾う。
           プログラムから SetWindowText した場合、EDIT だけは EN_CHANGE が
           飛んでくるので、load_values() の最後で必ず落としている。 */
        if (note == CBN_SELCHANGE || note == CBN_EDITCHANGE || note == EN_CHANGE ||
            (note == BN_CLICKED && id == IDC_FULLSCREEN))
            set_dirty(TRUE);

        switch (id) {
        case IDC_OK:
            if (save_values()) DestroyWindow(hwnd);
            return 0;
        case IDC_APPLY:
            if (save_values()) load_values();   /* 末尾で set_dirty(FALSE) */
            return 0;
        case IDC_CANCEL:
            DestroyWindow(hwnd);
            return 0;
        case IDC_OPENINI:
            ShellExecuteW(hwnd, L"open", L"notepad.exe", g_cfg.iniPath, NULL, SW_SHOWNORMAL);
            return 0;
        }
        return 0;
    }

    case WM_ERASEBKGND: {
        RECT r;
        GetClientRect(hwnd, &r);
        FillRect((HDC)wp, &r, theme_back_brush());
        return 1;
    }

    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT *di = (const DRAWITEMSTRUCT *)lp;
        if (di->CtlType == ODT_TAB)         { theme_draw_tab(di, g_font);   return TRUE; }
        if (di->CtlID   == IDC_GROUP)       { theme_draw_group(di, g_font); return TRUE; }
        return 0;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HBRUSH br = NULL;
        if (theme_ctlcolor(msg, (HDC)wp, &br)) return (LRESULT)br;
        /* ライトのときは、グループ枠内の説明文が親と同じ背景で描かれるように */
        if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLORBTN) {
            SetBkMode((HDC)wp, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        break;
    }

    case WM_SETTINGCHANGE:
        /* Windows のライト/ダーク切り替えは "ImmersiveColorSet" で通知される */
        if (lp && !lstrcmpiW((const WCHAR *)lp, L"ImmersiveColorSet") && theme_refresh()) {
            theme_apply_window(hwnd);
            rebuild(hwnd);
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;

    case WM_DPICHANGED: {
        RECT *r = (RECT *)lp;
        /* 中身を作る前(窓をオーナーのモニタへ移した瞬間)にも飛んでくる。
           そこで作り直すとコントロールが二重にできるので、まだなら何もしない。 */
        if (!g_tab) return 0;
        SetWindowPos(hwnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        rebuild(hwnd);
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        g_wnd = NULL;
        g_tab = NULL;
        ZeroMemory(g_cmb,  sizeof(g_cmb));
        ZeroMemory(g_scmb, sizeof(g_scmb));
        ZeroMemory(g_hHold, sizeof(g_hHold));
        ZeroMemory(g_trig, sizeof(g_trig));
        if (g_font) { DeleteObject(g_font); g_font = NULL; }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */

HWND settings_hwnd(void)
{
    return g_wnd;
}

/* 記録ウィンドウが、設定ウィンドウと同じ字面・同じ大きさで組み立てるために使う。
   借り物なので、向こうでは複製してから持つこと(capture.c の make_font)。 */
HFONT settings_font(void)
{
    return g_font;
}

void settings_open(HINSTANCE inst, HWND owner)
{
    WNDCLASSEXW wc;
    INITCOMMONCONTROLSEX icc;
    static BOOL registered;

    if (g_wnd) {                          /* 既に開いていれば前面に出すだけ */
        ShowWindow(g_wnd, SW_RESTORE);
        SetForegroundWindow(g_wnd);
        return;
    }
    g_inst = inst;
    theme_init();
    dpi_probe();

    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_TAB_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    if (!registered) {
        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = SettingsProc;
        wc.hInstance     = inst;
        wc.hCursor       = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
        wc.hbrBackground = NULL;          /* 背景は WM_ERASEBKGND で塗る */
        wc.lpszClassName = WNDCLASS_SETTINGS;
        wc.hIcon         = LoadIconW(inst, MAKEINTRESOURCEW(101));
        wc.hIconSm       = wc.hIcon;
        if (!RegisterClassExW(&wc)) return;
        registered = TRUE;
    }

    /* まず箱だけ作る。中身の寸法は、この窓がどのモニタに載るか
       (= どの DPI か)が決まってからでないと出せない。 */
    g_wnd = CreateWindowExW(WS_EX_CONTROLPARENT, WNDCLASS_SETTINGS,
                            MAYOUS_APPNAME L" の設定",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
                            NULL, NULL, inst, NULL);
    if (!g_wnd) return;

    /* 最後は center_on() でオーナーと同じモニタへ置くので、測る前に移しておく。
       別の DPI のモニタへ移ると WM_DPICHANGED が飛ぶが、中身がまだ無いので
       そこでは何もしない(上の g_tab の判定)。 */
    {
        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(MonitorFromWindow(owner ? owner : g_wnd,
                                              MONITOR_DEFAULTTOPRIMARY), &mi))
            SetWindowPos(g_wnd, NULL, mi.rcWork.left, mi.rcWork.top, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    theme_apply_window(g_wnd);
    make_font(g_wnd);          /* この窓の DPI に合わせる */
    build(g_wnd);              /* 最後に中身に合わせて窓の大きさを決め直す */
    load_values();
    center_on(g_wnd, owner);
    ShowWindow(g_wnd, SW_SHOW);
    SetForegroundWindow(g_wnd);
}
