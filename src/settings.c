/* ==================================================================
 * settings.c - 設定ウィンドウ(通常ウィンドウ)
 *
 *  リソースのダイアログテンプレートは使わず、コントロールを直接生成する。
 *  .rc に日本語を入れると windres のコードページ解釈に振り回されるため、
 *  文字列は C ソース側(UTF-8 -> L"..." は UTF-16 になる)に置いている。
 *
 *  プレフィクスになれるボタンが 4 つ、割り当て枠が 24+2 個あるので、
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
#include <wchar.h>

#define WNDCLASS_SETTINGS L"MayousSettingsWnd"

/* コントロール ID */
#define IDC_TAB          900
#define IDC_CHORD_BASE   2000            /* +CH_ID(pfx,suf) */
#define IDC_REC_BASE     2100            /* +CH_ID(pfx,suf) */
#define IDC_SINGLE_BASE  2200            /* +btn */
#define IDC_SREC_BASE    2210            /* +btn */
#define IDC_ENABLED      1100
#define IDC_STARTUP      1101
#define IDC_FULLSCREEN   1102
#define IDC_HOLD_BASE    1110            /* +btn */
#define IDC_DRAG         1119
#define IDC_EXCLUDE      1120
#define IDC_OK           1130
#define IDC_CANCEL       1131
#define IDC_APPLY        1132
#define IDC_OPENINI      1133

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
static HWND   g_hDrag, g_hExclude;

/* タブの並び。中ボタンはプレフィクスになれないので入らない。 */
static const int kTabPfx[] = { BTN_L, BTN_R, BTN_X1, BTN_X2 };
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
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             U(x), U(y), U(w), U(h), parent,
                             (HMENU)(INT_PTR)id, g_inst, NULL);
    if (c) SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static void fill_combo(HWND combo, BOOL withPassthru)
{
    size_t i;
    if (withPassthru) SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)kPassthru.label);
    for (i = 0; i < ARRAYSIZE(kPresets); ++i)
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)kPresets[i].label);
}

static void make_font(void)
{
    NONCLIENTMETRICSW ncm;
    HDC dc;
    TEXTMETRICW tm;
    HFONT old;

    if (g_font) { DeleteObject(g_font); g_font = NULL; }

    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
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
    int t, suf, pfx;

    for (t = 0; t < TAB_COUNT; ++t) {
        int show = (t == tab) ? SW_SHOW : SW_HIDE;
        pfx = kTabPfx[t];

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

static void load_values(void)
{
    int pfx, suf, b, t;
    WCHAR buf[MAX_EXCLUDE], *src, *dst;

    for (t = 0; t < TAB_COUNT; ++t) {
        pfx = kTabPfx[t];
        if (g_scmb[pfx]) set_combo_text(g_scmb[pfx], g_cfg.single[pfx].spec);
        for (suf = 0; suf < SUF_COUNT; ++suf) {
            int id = CH_ID(pfx, suf);
            if (g_cmb[id]) set_combo_text(g_cmb[id], g_cfg.chord[id].spec);
        }
    }

    CheckDlgButton(g_wnd, IDC_ENABLED,    g_cfg.enabled             ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g_wnd, IDC_FULLSCREEN, g_cfg.suspendOnFullscreen ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g_wnd, IDC_STARTUP,    startup_enabled()         ? BST_CHECKED : BST_UNCHECKED);

    for (b = 0; b < BTN_COUNT; ++b)
        if (g_hHold[b]) SetDlgItemInt(g_wnd, IDC_HOLD_BASE + b, (UINT)g_cfg.holdTimeoutMs[b], FALSE);
    SetDlgItemInt(g_wnd, IDC_DRAG, (UINT)g_cfg.dragThreshold, FALSE);

    /* ";a.exe;b.exe;" -> 1行1件 */
    dst = buf;
    for (src = g_cfg.exclude; *src; ++src) {
        if (*src == L';') {
            if (dst != buf && dst[-1] != L'\n' && src[1]) { *dst++ = L'\r'; *dst++ = L'\n'; }
            continue;
        }
        *dst++ = *src;
    }
    *dst = 0;
    SetWindowTextW(g_hExclude, buf);
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
    int t, pfx, suf, b;
    WCHAR key[64], what[128];
    WCHAR raw[MAX_EXCLUDE], list[MAX_EXCLUDE], *src, *dst;
    BOOL ok;

    for (t = 0; t < TAB_COUNT; ++t) {
        pfx = kTabPfx[t];

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
    }

    cfg_write_int(L"General", L"Enabled",
                  IsDlgButtonChecked(g_wnd, IDC_ENABLED) == BST_CHECKED);
    cfg_write_int(L"General", L"SuspendOnFullscreen",
                  IsDlgButtonChecked(g_wnd, IDC_FULLSCREEN) == BST_CHECKED);

    for (b = 0; b < BTN_COUNT; ++b)
        if (g_hHold[b])
            cfg_write_int(L"General", cfg_hold_ini_key(b),
                          (int)GetDlgItemInt(g_wnd, IDC_HOLD_BASE + b, &ok, FALSE));
    cfg_write_int(L"General", L"DragThreshold", (int)GetDlgItemInt(g_wnd, IDC_DRAG, &ok, FALSE));

    /* 1行1件 / カンマ区切りのどちらでも受け取り、カンマ区切りで保存する */
    GetWindowTextW(g_hExclude, raw, ARRAYSIZE(raw));
    dst = list;
    for (src = raw; *src; ++src) {
        if (*src == L'\r' || *src == L'\n' || *src == L',' || *src == L';') {
            if (dst != list && dst[-1] != L',') *dst++ = L',';
            continue;
        }
        if (*src == L' ' || *src == L'\t' || *src == L'"') continue;
        if ((size_t)(dst - list) < MAX_EXCLUDE - 2) *dst++ = *src;
    }
    while (dst != list && dst[-1] == L',') --dst;
    *dst = 0;
    cfg_write_str(L"Exclude", L"Processes", list);

    startup_set(IsDlgButtonChecked(g_wnd, IDC_STARTUP) == BST_CHECKED);

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

/* タブ内の最大行数: 単独 1 + サフィックス(自分を除く) 6 */
#define TAB_ROWS  7
#define TAB_H     (32 + TAB_ROWS * ROW_H + 12)
/* 動作: 上余白22 + チェック3行(22*3) + 長押し2行(26*2) + 距離1行(26) + 下余白10 */
#define GRP2_H    (22 + 22 * 3 + 26 * 3 + 10)
#define GRP3_H     82

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

static void build(HWND hwnd)
{
    const int m  = 12;
    const int gw = WIN_W - m * 2;
    int gy, y, t, suf, b, i;
    TCITEMW ti;

    /* --- タブ --- */
    g_tab = CreateWindowExW(0, WC_TABCONTROLW, L"",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS,
                            U(m), U(m), U(gw), U(TAB_H), hwnd,
                            (HMENU)(INT_PTR)IDC_TAB, g_inst, NULL);
    SendMessageW(g_tab, WM_SETFONT, (WPARAM)g_font, TRUE);

    /* iImage は必ず -1 にすること。0 のままだとイメージリストを設定していなくても
       画像 0 を取りに行き、選択タブの描き直しでアクセス違反になる。 */
    ZeroMemory(&ti, sizeof(ti));
    ti.mask   = TCIF_TEXT | TCIF_IMAGE;
    ti.iImage = -1;
    for (t = 0; t < TAB_COUNT; ++t) {
        ti.pszText = (LPWSTR)cfg_btn_name(kTabPfx[t]);
        SendMessageW(g_tab, TCM_INSERTITEMW, (WPARAM)t, (LPARAM)&ti);
    }

    /* タブの中身。全タブ分をまとめて作り、表示だけ切り替える。 */
    for (t = 0; t < TAB_COUNT; ++t) {
        int pfx = kTabPfx[t];
        y = m + 34;

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
            if (suf == pfx) continue;                  /* 自分自身とは組めない */
            wsprintfW(lb, L"+ %s", cfg_suf_name(suf));
            add_row(hwnd, lb, y, &g_lbl[id], &g_cmb[id], &g_rec[id],
                    IDC_CHORD_BASE + id, IDC_REC_BASE + id, FALSE);
            y += ROW_H;
        }
    }
    gy = m + TAB_H + 10;

    /* --- 動作 --- */
    mk(hwnd, L"BUTTON", L" 動作 ", BS_GROUPBOX, m, gy, gw, GRP2_H, 0);
    y = gy + 22;
    mk(hwnd, L"BUTTON", L"有効にする", BS_AUTOCHECKBOX | WS_TABSTOP,
       m + 14, y, gw - 28, 20, IDC_ENABLED);
    y += 22;
    mk(hwnd, L"BUTTON", L"Windows 起動時に開始する (スタートアップにショートカットを作成)",
       BS_AUTOCHECKBOX | WS_TABSTOP, m + 14, y, gw - 28, 20, IDC_STARTUP);
    y += 22;
    mk(hwnd, L"BUTTON", L"フルスクリーンのアプリが前面のときは停止する",
       BS_AUTOCHECKBOX | WS_TABSTOP, m + 14, y, gw - 28, 20, IDC_FULLSCREEN);
    y += 26;

    /* 長押し判定は 2 列に並べる */
    {
        static const int order[4] = { BTN_L, BTN_R, BTN_X1, BTN_X2 };
        for (i = 0; i < 4; ++i) {
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

    gy += GRP2_H + 10;

    /* --- 除外アプリ --- */
    mk(hwnd, L"BUTTON", L" このアプリが前面のときは停止する ", BS_GROUPBOX,
       m, gy, gw, GRP3_H, 0);
    mk(hwnd, L"STATIC", L"実行ファイル名を 1 行に 1 つ (例: valorant.exe)",
       SS_LEFT, m + 14, gy + 20, gw - 28, 18, 0);
    g_hExclude = mk(hwnd, L"EDIT", L"",
                    ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER | WS_TABSTOP,
                    m + 14, gy + 40, gw - 28, 34, IDC_EXCLUDE);
    gy += GRP3_H + 8;

    mk(hwnd, L"STATIC",
       L"[記録] で実際にキーを押して割り当てられます。一覧に無い指定は直接入力も可能です。",
       SS_LEFT, m + 2, gy, gw - 4, 18, 0);
    gy += 22;

    /* --- ボタン --- */
    mk(hwnd, L"BUTTON", L"設定ファイルを開く", BS_PUSHBUTTON | WS_TABSTOP,
       m, gy, 140, 26, IDC_OPENINI);
    mk(hwnd, L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP,
       WIN_W - m - 92 * 3 - 16, gy, 92, 26, IDC_OK);
    mk(hwnd, L"BUTTON", L"キャンセル", BS_PUSHBUTTON | WS_TABSTOP,
       WIN_W - m - 92 * 2 - 8, gy, 92, 26, IDC_CANCEL);
    mk(hwnd, L"BUTTON", L"適用", BS_PUSHBUTTON | WS_TABSTOP,
       WIN_W - m - 92, gy, 92, 26, IDC_APPLY);
    gy += 26 + m;

    /* クライアント領域をぴったりに合わせる */
    {
        RECT r = { 0, 0, U(WIN_W), U(gy) };
        AdjustWindowRectEx(&r, (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE), FALSE,
                           (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
        SetWindowPos(hwnd, NULL, 0, 0, r.right - r.left, r.bottom - r.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

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
    make_font();
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
    if (capture_run(g_inst, g_wnd, spec, ARRAYSIZE(spec)))
        set_combo_text(combo, spec);
    SetFocus(combo);
}

static LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_NOTIFY: {
        const NMHDR *nh = (const NMHDR *)lp;
        if (nh->idFrom == IDC_TAB && nh->code == (UINT)TCN_SELCHANGE)
            if (!GetEnvironmentVariableW(L"MAYOUS_NO_SHOWTAB", NULL, 0))
                show_tab(TabCtrl_GetCurSel(g_tab));
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);

        if (id >= IDC_REC_BASE && id < IDC_REC_BASE + CH_COUNT) {
            do_capture(g_cmb[id - IDC_REC_BASE]);
            return 0;
        }
        if (id >= IDC_SREC_BASE && id < IDC_SREC_BASE + BTN_COUNT) {
            do_capture(g_scmb[id - IDC_SREC_BASE]);
            return 0;
        }
        switch (id) {
        case IDC_OK:
            if (save_values()) DestroyWindow(hwnd);
            return 0;
        case IDC_APPLY:
            if (save_values()) load_values();
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

    case WM_CTLCOLORSTATIC:
        /* グループボックス内の説明文が親と同じ背景で描かれるように */
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);

    case WM_DPICHANGED: {
        RECT *r = (RECT *)lp;
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

    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_TAB_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    if (!registered) {
        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = SettingsProc;
        wc.hInstance     = inst;
        wc.hCursor       = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = WNDCLASS_SETTINGS;
        wc.hIcon         = LoadIconW(inst, MAKEINTRESOURCEW(101));
        wc.hIconSm       = wc.hIcon;
        if (!RegisterClassExW(&wc)) return;
        registered = TRUE;
    }

    make_font();

    /* WS_EX_CONTROLPARENT + メインループの IsDialogMessageW で
       Tab 移動と Enter/Esc がダイアログと同じように効くようになる。 */
    g_wnd = CreateWindowExW(WS_EX_CONTROLPARENT, WNDCLASS_SETTINGS,
                            MAYOUS_APPNAME L" の設定",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT, U(WIN_W), U(600),
                            NULL, NULL, inst, NULL);
    if (!g_wnd) return;

    build(g_wnd);
    load_values();
    center_on(g_wnd, owner);
    ShowWindow(g_wnd, SW_SHOW);
    SetForegroundWindow(g_wnd);
}
