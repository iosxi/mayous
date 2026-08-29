/* config.c - mayous.ini の読み書きとアクション文字列の解析
 *
 *  アクションの表記:
 *      none                       何もしない
 *      passthru                   単独クリック用。手を加えずそのまま通す
 *      hwheel_left / hwheel_right 水平ホイール
 *      zoom_in / zoom_out         Ctrl+ホイール(拡大・縮小)
 *      click:middle               別のマウスボタンを 1 回出す(middleclick も可)
 *      autoscroll                 押して離すとスクロール・モードに入る
 *      win / alttab / alttab_back よく使うものの別名
 *      ctrl+alt+t                 1ステップのキーコンボ
 *      ctrl+c, ctrl+v             複数ステップ(記録したものの再生)
 *
 *  [Exclude] の Rule1, Rule2, ... は「前面に来たら止める」条件:
 *      valorant.exe            実行ファイル名(既定。* を書ける)
 *      title:Minecraft*        ウィンドウ名の前方一致
 *      title:*メモ帳*          ウィンドウ名の部分一致
 *
 *  登録キー(RightThenKey1 など)は、トリガーにするキーを別のキーで持つ:
 *      RightThenKey1Trigger=f13   マウスと組み合わせるキーボードのキー
 *      RightThenKey1=ctrl+w       そのときの動作(書き方は上と同じ)
 */

#include "common.h"
#include <wchar.h>
#include <stdlib.h>

Config g_cfg;

/* ------------------------------------------------------------------ */
/* キー名テーブル                                                      */
/* ------------------------------------------------------------------ */

typedef struct { const WCHAR *name; WORD vk; } KeyName;

static const KeyName kKeyNames[] = {
    { L"tab",         VK_TAB },
    { L"enter",       VK_RETURN },   { L"return",     VK_RETURN },
    { L"esc",         VK_ESCAPE },   { L"escape",     VK_ESCAPE },
    { L"space",       VK_SPACE },
    { L"backspace",   VK_BACK },     { L"bs",         VK_BACK },
    { L"delete",      VK_DELETE },   { L"del",        VK_DELETE },
    { L"insert",      VK_INSERT },   { L"ins",        VK_INSERT },
    { L"home",        VK_HOME },     { L"end",        VK_END },
    { L"pageup",      VK_PRIOR },    { L"pgup",       VK_PRIOR },
    { L"pagedown",    VK_NEXT },     { L"pgdn",       VK_NEXT },
    { L"left",        VK_LEFT },     { L"right",      VK_RIGHT },
    { L"up",          VK_UP },       { L"down",       VK_DOWN },
    { L"printscreen", VK_SNAPSHOT }, { L"apps",       VK_APPS },
    { L"capslock",    VK_CAPITAL },  { L"numlock",    VK_NUMLOCK },
    { L"scrolllock",  VK_SCROLL },   { L"pause",      VK_PAUSE },
    { L"numpad0",     VK_NUMPAD0 },  { L"numpad1",    VK_NUMPAD1 },
    { L"numpad2",     VK_NUMPAD2 },  { L"numpad3",    VK_NUMPAD3 },
    { L"numpad4",     VK_NUMPAD4 },  { L"numpad5",    VK_NUMPAD5 },
    { L"numpad6",     VK_NUMPAD6 },  { L"numpad7",    VK_NUMPAD7 },
    { L"numpad8",     VK_NUMPAD8 },  { L"numpad9",    VK_NUMPAD9 },
    { L"multiply",    VK_MULTIPLY }, { L"add",        VK_ADD },
    { L"subtract",    VK_SUBTRACT }, { L"divide",     VK_DIVIDE },
    { L"decimal",     VK_DECIMAL },
    { L"minus",       VK_OEM_MINUS },{ L"equal",      VK_OEM_PLUS },
    { L"comma",       VK_OEM_COMMA },{ L"period",     VK_OEM_PERIOD },
    { L"slash",       VK_OEM_2 },    { L"backslash",  VK_OEM_5 },
    { L"semicolon",   VK_OEM_1 },    { L"quote",      VK_OEM_7 },
    { L"lbracket",    VK_OEM_4 },    { L"rbracket",   VK_OEM_6 },
    { L"grave",       VK_OEM_3 },
    { L"volumeup",    VK_VOLUME_UP },{ L"volumedown", VK_VOLUME_DOWN },
    { L"volumemute",  VK_VOLUME_MUTE },
    { L"medianext",   VK_MEDIA_NEXT_TRACK },
    { L"mediaprev",   VK_MEDIA_PREV_TRACK },
    { L"mediaplay",   VK_MEDIA_PLAY_PAUSE },
    { L"mediastop",   VK_MEDIA_STOP },
    { L"browserback", VK_BROWSER_BACK },
    { L"browserfwd",  VK_BROWSER_FORWARD },
};

/* VK -> 表記。記録した内容を文字列に戻すときに使う。 */
const WCHAR *cfg_vk_name(WORD vk)
{
    static WCHAR buf[16];
    size_t i;

    switch (vk) {
    case VK_LCONTROL: case VK_RCONTROL: case VK_CONTROL: return L"ctrl";
    case VK_LMENU:    case VK_RMENU:    case VK_MENU:    return L"alt";
    case VK_LSHIFT:   case VK_RSHIFT:   case VK_SHIFT:   return L"shift";
    case VK_LWIN:                                        return L"win";
    case VK_RWIN:                                        return L"rwin";
    default: break;
    }
    if (vk >= 'A' && vk <= 'Z') { buf[0] = (WCHAR)(vk - 'A' + L'a'); buf[1] = 0; return buf; }
    if (vk >= '0' && vk <= '9') { buf[0] = (WCHAR)vk;                buf[1] = 0; return buf; }
    if (vk >= VK_F1 && vk <= VK_F24) { wsprintfW(buf, L"f%d", vk - VK_F1 + 1); return buf; }
    for (i = 0; i < ARRAYSIZE(kKeyNames); ++i)
        if (kKeyNames[i].vk == vk) return kKeyNames[i].name;
    wsprintfW(buf, L"vk%02X", vk);
    return buf;
}

static void str_lower(WCHAR *s)
{
    for (; *s; ++s)
        if (*s >= L'A' && *s <= L'Z') *s = (WCHAR)(*s - L'A' + L'a');
}

static void str_trim(WCHAR *s)
{
    WCHAR *p = s, *e;
    while (*p == L' ' || *p == L'\t') ++p;
    if (p != s) memmove(s, p, (wcslen(p) + 1) * sizeof(WCHAR));
    e = s + wcslen(s);
    while (e > s && (e[-1] == L' ' || e[-1] == L'\t' || e[-1] == L'\r' || e[-1] == L'\n'))
        *--e = 0;
}

/* 1トークン -> 仮想キーコード。修飾キーは左側の物理キーに割り当てる。 */
static WORD token_to_vk(const WCHAR *t)
{
    size_t i, n;

    if (!wcscmp(t, L"ctrl") || !wcscmp(t, L"control")) return VK_LCONTROL;
    if (!wcscmp(t, L"alt")  || !wcscmp(t, L"menu"))    return VK_LMENU;
    if (!wcscmp(t, L"shift"))                          return VK_LSHIFT;
    if (!wcscmp(t, L"win")  || !wcscmp(t, L"lwin"))    return VK_LWIN;
    if (!wcscmp(t, L"rwin"))                           return VK_RWIN;

    n = wcslen(t);
    if (n == 1) {
        WCHAR c = t[0];
        if (c >= L'a' && c <= L'z') return (WORD)(c - L'a' + 'A');
        if (c >= L'0' && c <= L'9') return (WORD)c;
    }
    if (t[0] == L'f' && n >= 2 && n <= 3 && t[1] >= L'0' && t[1] <= L'9') {
        int num = _wtoi(t + 1);
        if (num >= 1 && num <= 24) return (WORD)(VK_F1 + num - 1);
    }
    if (!wcsncmp(t, L"vk", 2) && n == 4) {              /* 名前の無いキー */
        int v = (int)wcstol(t + 2, NULL, 16);
        if (v > 0 && v < 256) return (WORD)v;
    }
    for (i = 0; i < ARRAYSIZE(kKeyNames); ++i)
        if (!wcscmp(t, kKeyNames[i].name)) return kKeyNames[i].vk;

    return 0;
}

/* "ctrl+c" のような 1 ステップを解析 */
static BOOL parse_step(WCHAR *buf, KeyStep *st)
{
    WCHAR *tok, *ctx;

    st->nkeys = 0;
    for (tok = wcstok(buf, L"+", &ctx); tok; tok = wcstok(NULL, L"+", &ctx)) {
        WORD vk;
        str_trim(tok);
        if (!*tok) continue;
        vk = token_to_vk(tok);
        if (!vk) return FALSE;
        if (st->nkeys < MAX_ACTION_KEYS) st->keys[st->nkeys++] = vk;
    }
    return st->nkeys > 0;
}

/* アクション文字列を解析する。解釈できたら TRUE。
   "none" / "passthru" も正しい入力として TRUE を返す。 */
BOOL cfg_parse_action(const WCHAR *src, Action *a)
{
    WCHAR buf[ACTION_SPEC_CCH], *p, *stepStart;

    ZeroMemory(a, sizeof(*a));
    a->kind = ACT_NONE;
    lstrcpynW(a->spec, L"none", ARRAYSIZE(a->spec));
    if (!src) return TRUE;

    lstrcpynW(buf, src, ARRAYSIZE(buf));
    str_trim(buf);
    if (!*buf) return TRUE;

    lstrcpynW(a->spec, buf, ARRAYSIZE(a->spec));
    str_lower(buf);

    if (!wcscmp(buf, L"none") || !wcscmp(buf, L"off")) {
        lstrcpynW(a->spec, L"none", ARRAYSIZE(a->spec));
        return TRUE;
    }
    if (!wcscmp(buf, L"passthru") || !wcscmp(buf, L"default")) {
        a->kind = ACT_PASSTHRU;
        lstrcpynW(a->spec, L"passthru", ARRAYSIZE(a->spec));
        return TRUE;
    }
    /* "hold:" は昔の設定ファイル用。押しっぱなしは今や既定の挙動なので、
       付いていてもいなくても同じ意味になる。読み捨てずに受け付けるだけ。 */
    if (!wcsncmp(buf, L"hold:", 5)) {
        WCHAR one[64];
        lstrcpynW(one, buf + 5, ARRAYSIZE(one));
        str_trim(one);
        if (!parse_step(one, &a->steps[0])) { a->kind = ACT_NONE; return FALSE; }
        a->nsteps = 1;
        a->kind   = ACT_HOLD_KEYS;
        return TRUE;
    }
    if (!wcscmp(buf, L"hwheel_left")  || !wcscmp(buf, L"wheelleft"))  { a->kind = ACT_HWHEEL_LEFT;  return TRUE; }
    if (!wcscmp(buf, L"hwheel_right") || !wcscmp(buf, L"wheelright")) { a->kind = ACT_HWHEEL_RIGHT; return TRUE; }
    if (!wcscmp(buf, L"zoom_in")  || !wcscmp(buf, L"zoomin"))  { a->kind = ACT_ZOOM_IN;  return TRUE; }
    if (!wcscmp(buf, L"zoom_out") || !wcscmp(buf, L"zoomout")) { a->kind = ACT_ZOOM_OUT; return TRUE; }
    if (!wcscmp(buf, L"autoscroll")) { a->kind = ACT_AUTOSCROLL; return TRUE; }
    {   /* 別のマウスボタンを出す。"click:middle" と "middleclick" のどちらでも。 */
        static const struct { const WCHAR *name; int btn; } kClick[] = {
            { L"left", BTN_L }, { L"right", BTN_R }, { L"middle", BTN_M },
            { L"side1", BTN_X1 }, { L"side2", BTN_X2 },
        };
        size_t i;
        for (i = 0; i < ARRAYSIZE(kClick); ++i) {
            WCHAR a1[32], a2[32];
            lstrcpynW(a1, L"click:", ARRAYSIZE(a1)); lstrcatW(a1, kClick[i].name);
            lstrcpynW(a2, kClick[i].name, ARRAYSIZE(a2)); lstrcatW(a2, L"click");
            if (!wcscmp(buf, a1) || !wcscmp(buf, a2)) {
                a->kind = ACT_CLICK;
                a->btn  = kClick[i].btn;
                return TRUE;
            }
        }
        if (!wcsncmp(buf, L"click:", 6)) { a->kind = ACT_NONE; return FALSE; }
    }
    if (!wcscmp(buf, L"alttab"))      lstrcpynW(buf, L"alt+tab",       ARRAYSIZE(buf));
    if (!wcscmp(buf, L"alttab_back")) lstrcpynW(buf, L"alt+shift+tab", ARRAYSIZE(buf));

    if (!wcsncmp(buf, L"key:", 4))                      /* "key:" 接頭辞は任意 */
        memmove(buf, buf + 4, (wcslen(buf + 4) + 1) * sizeof(WCHAR));

    /* カンマ区切りで複数ステップ。wcstok は入れ子にできないので手で切る。 */
    stepStart = buf;
    for (p = buf; ; ++p) {
        if (*p == L',' || *p == 0) {
            WCHAR save = *p;
            WCHAR one[64];
            *p = 0;
            lstrcpynW(one, stepStart, ARRAYSIZE(one));
            str_trim(one);
            if (*one) {
                if (a->nsteps >= MAX_ACTION_STEPS) { a->kind = ACT_NONE; a->nsteps = 0; return FALSE; }
                if (!parse_step(one, &a->steps[a->nsteps])) { a->kind = ACT_NONE; a->nsteps = 0; return FALSE; }
                a->nsteps++;
            }
            if (!save) break;
            stepStart = p + 1;
        }
    }
    if (a->nsteps == 0) { a->kind = ACT_NONE; return FALSE; }
    a->kind = ACT_KEYS;
    return TRUE;
}

BOOL cfg_action_valid(const WCHAR *spec)
{
    Action a;
    return cfg_parse_action(spec, &a);
}

/* ------------------------------------------------------------------ */
/* 停止する条件                                                        */
/* ------------------------------------------------------------------ */

static WCHAR wlower(WCHAR c)
{
    return (c >= L'A' && c <= L'Z') ? (WCHAR)(c - L'A' + L'a') : c;
}

/* * だけのワイルドカード照合。大文字小文字は区別しない。
   ばらばらの位置に * がいくつあってもよいので、素直な後戻り法で書く
   (相手は高々ウィンドウ名 1 本、1 秒に 1 回。速さは要らない)。 */
static BOOL wild_match(const WCHAR *pat, const WCHAR *s)
{
    const WCHAR *star = NULL, *mark = NULL;

    while (*s) {
        if (*pat == L'*') {
            star = pat++;
            mark = s;
        } else if (wlower(*pat) == wlower(*s)) {
            ++pat; ++s;
        } else if (star) {
            pat = star + 1;
            s   = ++mark;
        } else {
            return FALSE;
        }
    }
    while (*pat == L'*') ++pat;
    return *pat == 0;
}

/* 1 行を規則にする。空行とコメントは FALSE(読み飛ばす)。 */
static BOOL parse_exclude_line(const WCHAR *line, ExcludeRule *r)
{
    WCHAR buf[EXCLUDE_RULE_CCH];

    lstrcpynW(buf, line, ARRAYSIZE(buf));
    str_trim(buf);
    if (!buf[0] || buf[0] == L';' || buf[0] == L'#') return FALSE;

    ZeroMemory(r, sizeof(*r));
    if (!wcsncmp(buf, L"title:", 6) || !wcsncmp(buf, L"TITLE:", 6)) {
        WCHAR *p = buf + 6;
        str_trim(p);
        if (!*p) return FALSE;
        r->byTitle = TRUE;
        lstrcpynW(r->pat, p, EXCLUDE_RULE_CCH);
    } else {
        lstrcpynW(r->pat, buf, EXCLUDE_RULE_CCH);
    }
    return TRUE;
}

BOOL cfg_is_excluded(const WCHAR *exeName, const WCHAR *title)
{
    int i;

    for (i = 0; i < g_cfg.excludeN; ++i) {
        const ExcludeRule *r = &g_cfg.exclude[i];
        const WCHAR *s = r->byTitle ? title : exeName;
        if (!s || !*s) continue;
        if (wild_match(r->pat, s)) return TRUE;
    }
    return FALSE;
}

/* 設定画面のテキスト欄へ出す(1 行 1 規則) */
void cfg_exclude_text(WCHAR *out, int cch)
{
    int i;

    out[0] = 0;
    for (i = 0; i < g_cfg.excludeN; ++i) {
        if (out[0]) lstrcatW(out, L"\r\n");
        if ((int)wcslen(out) + EXCLUDE_RULE_CCH + 8 >= cch) break;
        if (g_cfg.exclude[i].byTitle) lstrcatW(out, L"title:");
        lstrcatW(out, g_cfg.exclude[i].pat);
    }
}

/* 設定画面のテキスト欄から ini へ。Rule1.. に書き、余った番号は消す。 */
void cfg_write_exclude(const WCHAR *text)
{
    WCHAR line[EXCLUDE_RULE_CCH], key[32];
    const WCHAR *p = text;
    ExcludeRule r;
    int n = 0, i;

    for (;;) {
        int len = 0;
        while (*p == L'\r' || *p == L'\n') ++p;
        if (!*p) break;
        while (*p && *p != L'\r' && *p != L'\n') {
            if (len < EXCLUDE_RULE_CCH - 1) line[len++] = *p;
            ++p;
        }
        line[len] = 0;
        if (!parse_exclude_line(line, &r)) continue;
        if (n >= MAX_EXCLUDE_RULES) break;
        ++n;
        wsprintfW(key, L"Rule%d", n);
        {
            WCHAR val[EXCLUDE_RULE_CCH + 8];
            val[0] = 0;
            if (r.byTitle) lstrcatW(val, L"title:");
            lstrcatW(val, r.pat);
            cfg_write_str(L"Exclude", key, val);
        }
    }
    for (i = n + 1; i <= MAX_EXCLUDE_RULES; ++i) {
        wsprintfW(key, L"Rule%d", i);
        WritePrivateProfileStringW(L"Exclude", key, NULL, g_cfg.iniPath);
    }
    /* 古い形式は役目を終えたので消す(読み込みでは今も受け付ける) */
    WritePrivateProfileStringW(L"Exclude", L"Processes", NULL, g_cfg.iniPath);
}

/* ------------------------------------------------------------------ */
/* 名前                                                                */
/* ------------------------------------------------------------------ */

static const WCHAR *kBtnName[BTN_COUNT] = {
    L"左クリック", L"右クリック", L"中クリック", L"サイドボタン1", L"サイドボタン2"
};
static const WCHAR *kSufName[SUF_KEY0] = {
    L"左クリック", L"右クリック", L"中クリック",
    L"サイドボタン1", L"サイドボタン2", L"ホイール上", L"ホイール下"
};
/* 登録キーの枠の名前。REGKEY_COUNT を増やすときは、ここの要素数が上限。 */
static const WCHAR *kKeySufName[] = { L"登録キー1", L"登録キー2", L"登録キー3", L"登録キー4" };
static const WCHAR *kKeySufIni[]  = { L"Key1",      L"Key2",      L"Key3",      L"Key4" };
/* ini のキー名に使う識別子。既存の設定ファイルと互換を保つため
   左右は従来どおり Left / Right のまま。 */
static const WCHAR *kBtnIni[BTN_COUNT] = { L"Left", L"Right", L"Middle", L"Side1", L"Side2" };
static const WCHAR *kSufIni[SUF_KEY0] = {
    L"Left", L"Right", L"Middle", L"Side1", L"Side2", L"WheelUp", L"WheelDown"
};

const WCHAR *cfg_btn_name(int btn) { return (btn >= 0 && btn < BTN_COUNT) ? kBtnName[btn] : L"?"; }

const WCHAR *cfg_suf_name(int suf)
{
    if (suf < 0 || suf >= SUF_COUNT) return L"?";
    return SUF_IS_KEY(suf) ? kKeySufName[suf - SUF_KEY0] : kSufName[suf];
}

void cfg_chord_ini_key(int pfx, int suf, WCHAR *out, int cch)
{
    lstrcpynW(out, kBtnIni[pfx], cch);
    lstrcatW(out, L"Then");
    lstrcatW(out, SUF_IS_KEY(suf) ? kKeySufIni[suf - SUF_KEY0] : kSufIni[suf]);
}

/* 登録キーのトリガーを書く ini キー: "RightThenKey1Trigger" */
void cfg_regkey_ini_key(int pfx, int idx, WCHAR *out, int cch)
{
    cfg_chord_ini_key(pfx, SUF_KEY0 + idx, out, cch);
    lstrcatW(out, L"Trigger");
}

/* トリガーの指定を仮想キーコードにする。空・解釈できない場合は 0。
   修飾キーは token_to_vk が左側に寄せるので、フック側もそれに合わせること。 */
WORD cfg_spec_to_vk(const WCHAR *spec)
{
    WCHAR buf[REGKEY_SPEC_CCH];

    if (!spec) return 0;
    lstrcpynW(buf, spec, ARRAYSIZE(buf));
    str_trim(buf);
    str_lower(buf);
    if (!*buf || !wcscmp(buf, L"none")) return 0;
    return token_to_vk(buf);
}

/* 押し直しの間隔として選べる値。長いほどポーリング方式のアプリに確実で、
   短いほど体感が速い。設定画面のラジオボタンもこの並びをそのまま使う。 */
const int kRepressGapMs[REPRESS_GAP_STEPS] = { 120, 80, 40, 20 };

/* ini に段階以外の値が書かれていても弾かず、一番近い段階へ丸める。 */
int cfg_repress_gap_snap(int ms)
{
    int i, best = 0, bestd = -1;
    for (i = 0; i < REPRESS_GAP_STEPS; ++i) {
        int d = ms - kRepressGapMs[i];
        if (d < 0) d = -d;
        if (bestd < 0 || d < bestd) { bestd = d; best = i; }
    }
    return kRepressGapMs[best];
}

void cfg_single_ini_key(int btn, WCHAR *out, int cch)
{
    lstrcpynW(out, kBtnIni[btn], cch);
    lstrcatW(out, L"Alone");
}

const WCHAR *cfg_hold_ini_key(int btn)
{
    static const WCHAR *k[BTN_COUNT] = {
        L"LeftHoldTimeoutMs", L"RightHoldTimeoutMs", L"",
        L"Side1HoldTimeoutMs", L"Side2HoldTimeoutMs"
    };
    return (btn >= 0 && btn < BTN_COUNT) ? k[btn] : L"";
}

/* 既定値。ここに無い組み合わせはすべて none。
   左クリックは先に押す側になれない(PFX_CAN)ので、ここにも出てこない。 */
static const WCHAR *chord_default(int pfx, int suf)
{
    if (pfx == BTN_R && suf == SUF_L)   return L"win";
    if (pfx == BTN_R && suf == SUF_M)   return L"alttab";
    /* ホイールを手前に回す(下) = 右へ。紙をめくる向きに合わせている。 */
    if (pfx == BTN_R && suf == SUF_WDN) return L"hwheel_right";
    if (pfx == BTN_R && suf == SUF_WUP) return L"hwheel_left";
    return L"none";
}

/* ------------------------------------------------------------------ */
/* INI の置き場所                                                      */
/*   exe と同じフォルダを優先(ポータブル運用)。書き込めない場所に      */
/*   インストールされている場合だけ %APPDATA%\Mayous へ逃がす。        */
/* ------------------------------------------------------------------ */

static void exe_dir(WCHAR *out, size_t cch)
{
    WCHAR *p;
    GetModuleFileNameW(NULL, out, (DWORD)cch);
    p = wcsrchr(out, L'\\');
    if (p) *(p + 1) = 0;
}

/* 設定は必ず exe と同じフォルダに置く。
   書けない場所(Program Files 等)に置かれた場合でも %APPDATA% へは逃がさない。
   逃がすと「設定がどこにあるか分からない」「フォルダを消しても設定が残る」
   という、ポータブル運用で一番困る状態になるため。
   その場合は起動時に一度だけ知らせて、既定値のまま動く。 */
void cfg_resolve_path(void)
{
    WCHAR dir[MAX_PATH];

    exe_dir(dir, ARRAYSIZE(dir));
    lstrcpynW(g_cfg.iniPath, dir, MAX_PATH);
    lstrcatW(g_cfg.iniPath, L"mayous.ini");
}

/* 設定ファイルを実際に書けるか。書けなければ FALSE。 */
BOOL cfg_path_writable(void)
{
    HANDLE h;

    if (GetFileAttributesW(g_cfg.iniPath) != INVALID_FILE_ATTRIBUTES) {
        h = CreateFileW(g_cfg.iniPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return FALSE;
        CloseHandle(h);
        return TRUE;
    }
    /* 実際に作れるか試す(UAC の仮想化に騙されないよう実書き込みで判定) */
    h = CreateFileW(g_cfg.iniPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    CloseHandle(h);
    DeleteFileW(g_cfg.iniPath);
    return TRUE;
}

static const WCHAR *kDefaultIni =
L"; ============================================================\r\n"
L";  Mayous - マウス同時押しジェスチャ常駐ツール\r\n"
L";  トレイアイコンをダブルクリックすると設定ウィンドウが開きます。\r\n"
L";  このファイルを直接編集した場合は [設定を再読み込み] で反映されます。\r\n"
L"; ============================================================\r\n"
L"\r\n"
L"[General]\r\n"
L"Enabled=1\r\n"
L"\r\n"
L"; 保留中のボタンを本物の押下に昇格させるドラッグ距離(px)。0 = システム設定\r\n"
L"DragThreshold=0\r\n"
L"\r\n"
L"; 長押し救済: この時間押しっぱなしにすると、同時押しを諦めて本物の押下に昇格する(ms)\r\n"
L";   0 = 無効(離すまでずっと保留する)\r\n"
L"RightHoldTimeoutMs=0\r\n"
L"Side1HoldTimeoutMs=0\r\n"
L"Side2HoldTimeoutMs=0\r\n"
L"\r\n"
L"; 注入したキーを「最低でも」押しておく時間(ms)。\r\n"
L"; 同時押しに割り当てたキーは先に押したボタンを離すまで押しっぱなしになるので、\r\n"
L"; 普通はこれより長くなる。効いてくるのは同時押しが一瞬で終わった場合と、\r\n"
L"; 押しっぱなしにできない場合(複数ステップ・単独クリック)。\r\n"
L"; 短すぎると、キーの状態を一定間隔で見に行く方式のアプリ(ゲームや拡大鏡など)が、\r\n"
L"; 間隔の隙間に収まった押下を丸ごと取りこぼす。\r\n"
L"KeyHoldMs=120\r\n"
L"\r\n"
L"; オートスクロールの速さ(%)。大きいほど速い。20〜500。\r\n"
L"AutoScrollSpeed=100\r\n"
L"\r\n"
L"; 同じキーを押し直すときに空ける時間(ms)。120 / 80 / 40 / 20 から選ぶ。\r\n"
L"; ボタンを押したまま同じ組み合わせを繰り返すと、いったん離して押し直す。\r\n"
L"; ここで間を空けないと、キーの状態を一定間隔で見に行く方式のアプリからは\r\n"
L"; 離した瞬間が見えず、2 回目以降が無かったことになる。逆にこの時間は\r\n"
L"; そのまま体感の遅れになるので、相手に合わせて選ぶ。\r\n"
L"; 押し直す先が別のキー(ホイール上下に別々のキーなど)なら間は空けない。\r\n"
L"RepressGapMs=40\r\n"
L"\r\n"
L"; フルスクリーンのアプリが前面のあいだは自動で停止する(ゲーム対策)\r\n"
L"SuspendOnFullscreen=1\r\n"
L"\r\n"
L"; 設定画面の配色: system(Windows の設定に従う) / light / dark\r\n"
L"Theme=system\r\n"
L"\r\n"
L"[Chords]\r\n"
L"; 書ける値:\r\n"
L";   none                        何もしない(そのボタンを一切乗っ取らない)\r\n"
L";   hwheel_left / hwheel_right  水平ホイール\r\n"
L";   zoom_in / zoom_out          Ctrl+ホイール(拡大・縮小)\r\n"
L";   win / alttab / alttab_back  よく使うものの別名\r\n"
L";   ctrl+w, alt+left, f5 ...    任意のキーコンボ\r\n"
L";   ctrl+c, ctrl+v              カンマ区切りで複数ステップ(記録の再生)\r\n"
L";\r\n"
L"; 1 ステップの指定は、先に押したボタンを離すまで押しっぱなしになる。\r\n"
L"; (hold: を付けても同じ意味。昔の設定ファイルのために受け付けているだけ)\r\n"
L"; カンマ区切りの複数ステップだけは、押しっぱなしにできないので順に再生する。\r\n"
L";\r\n"
L"; キー名: a-z 0-9 f1-f24 tab enter esc space backspace delete insert home end\r\n"
L";         pageup pagedown left right up down apps printscreen numpad0-9\r\n"
L";         volumeup volumedown volumemute medianext mediaprev mediaplay\r\n"
L";         browserback browserfwd ほか\r\n"
L"; 修飾子: ctrl alt shift win\r\n"
L"\r\n"
L"RightThenLeft=win\r\n"
L"RightThenMiddle=alttab\r\n"
L"RightThenWheelDown=hwheel_right\r\n"
L"RightThenWheelUp=hwheel_left\r\n"
L"\r\n"
L"; 左クリックは「先に押す側」にはできない。押下を離すまで預かることになり、\r\n"
L"; ウィンドウの切り替えが遅れたり、枠を掴み損ねたりするため。\r\n"
L"; (LeftThen... と書いても読み飛ばされる。後から押す側としては使える)\r\n"
L"\r\n"
L"; 書かれていない組み合わせは none です。設定ウィンドウから編集するのが簡単です。\r\n"
L"; 例: Side1ThenLeft / Side2ThenWheelUp / Side1ThenWheelDown ...\r\n"
L"\r\n"
L"; 登録キー: マウスのボタンを押しながら叩くキーボードのキー。\r\n"
L";   ...Trigger にキーを 1 つ書くと、そのキーが組み合わせの相手になる。\r\n"
L";   Trigger が空のあいだは、キーボードには一切触らない。\r\n"
L"; 例: 右クリックを押しながら F13 でタブを閉じる\r\n"
L";   RightThenKey1Trigger=f13\r\n"
L";   RightThenKey1=ctrl+w\r\n"
L"\r\n"
L"[Single]\r\n"
L"; サイドボタン・中ボタンを単独で押したときの動作。\r\n"
L";   passthru     本来のボタンとしてそのまま通す(既定)\r\n"
L";   click:middle 中クリックを出す(middleclick とも書ける)\r\n"
L";   autoscroll   押して離すとスクロール・モードに入る(中ボタン向け)\r\n"
L";   ctrl+c ...   キーコンボでもよい\r\n"
L"Side1Alone=passthru\r\n"
L"Side2Alone=passthru\r\n"
L"MiddleAlone=passthru\r\n"
L"\r\n"
L"[Exclude]\r\n"
L"; ここに書いた条件のウィンドウが前面のあいだは全機能を停止する。\r\n"
L"; Rule1, Rule2, ... と番号を振って 1 行に 1 つ書く。\r\n"
L";\r\n"
L";   valorant.exe        実行ファイル名(既定)\r\n"
L";   title:Minecraft*    ウィンドウ名。* は「任意の文字列」\r\n"
L";   title:*メモ帳*      前後に * を付ければ部分一致になる\r\n"
L";\r\n"
L"; 大文字小文字は区別しない。実行ファイル名だけでは選り分けられない\r\n"
L"; (java.exe が別物のウィンドウを何枚も出す、など)ときはウィンドウ名を使う。\r\n"
L"; 設定画面の「停止する条件」タブから、今開いているウィンドウを選んで足せる。\r\n"
L";\r\n"
L"; Rule1=title:Minecraft*\r\n";

BOOL cfg_write_default_if_missing(void)
{
    HANDLE h;
    DWORD written;
    const BYTE bom[2] = { 0xFF, 0xFE };

    if (GetFileAttributesW(g_cfg.iniPath) != INVALID_FILE_ATTRIBUTES) return FALSE;

    h = CreateFileW(g_cfg.iniPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;

    /* UTF-16LE + BOM。GetPrivateProfile* 系はこの形式を正しく読む。 */
    WriteFile(h, bom, 2, &written, NULL);
    WriteFile(h, kDefaultIni, (DWORD)(wcslen(kDefaultIni) * sizeof(WCHAR)), &written, NULL);
    CloseHandle(h);
    return TRUE;
}

/* ------------------------------------------------------------------ */

void cfg_write_str(const WCHAR *sec, const WCHAR *key, const WCHAR *val)
{
    WritePrivateProfileStringW(sec, key, val, g_cfg.iniPath);
}

void cfg_write_int(const WCHAR *sec, const WCHAR *key, int val)
{
    WCHAR buf[24];
    wsprintfW(buf, L"%d", val);
    WritePrivateProfileStringW(sec, key, buf, g_cfg.iniPath);
}

void cfg_load(void)
{
    WCHAR raw[MAX_EXCLUDE], *src, *dst;
    WCHAR key[64], v[ACTION_SPEC_CCH];
    int pfx, suf, b;

    g_cfg.enabled             = GetPrivateProfileIntW(L"General", L"Enabled", 1, g_cfg.iniPath) != 0;
    g_cfg.dragThreshold       = GetPrivateProfileIntW(L"General", L"DragThreshold", 0, g_cfg.iniPath);
    g_cfg.suspendOnFullscreen = GetPrivateProfileIntW(L"General", L"SuspendOnFullscreen", 1, g_cfg.iniPath) != 0;
    g_cfg.keyHoldMs           = GetPrivateProfileIntW(L"General", L"KeyHoldMs", KEY_HOLD_MS_DEFAULT, g_cfg.iniPath);
    g_cfg.repressGapMs        = cfg_repress_gap_snap(
        GetPrivateProfileIntW(L"General", L"RepressGapMs", REPRESS_GAP_MS_DEFAULT, g_cfg.iniPath));
    g_cfg.autoScrollSpeed     = GetPrivateProfileIntW(L"General", L"AutoScrollSpeed",
                                                      AUTOSCROLL_SPEED_DEFAULT, g_cfg.iniPath);
    if (g_cfg.autoScrollSpeed < AUTOSCROLL_SPEED_MIN) g_cfg.autoScrollSpeed = AUTOSCROLL_SPEED_MIN;
    if (g_cfg.autoScrollSpeed > AUTOSCROLL_SPEED_MAX) g_cfg.autoScrollSpeed = AUTOSCROLL_SPEED_MAX;

    {   /* 設定画面の配色: system / light / dark */
        WCHAR t[32];
        GetPrivateProfileStringW(L"General", L"Theme", L"system", t, ARRAYSIZE(t), g_cfg.iniPath);
        str_trim(t); str_lower(t);
        g_cfg.theme = !wcscmp(t, L"light") ? THEME_LIGHT
                    : !wcscmp(t, L"dark")  ? THEME_DARK
                    : THEME_SYSTEM;
    }

    if (g_cfg.dragThreshold < 0)   g_cfg.dragThreshold = 0;
    if (g_cfg.dragThreshold > 200) g_cfg.dragThreshold = 200;

    /* 短すぎると GetAsyncKeyState を見に行く作りのアプリに取りこぼされ、
       長すぎると文字入力欄でキーリピートが始まる。 */
    if (g_cfg.keyHoldMs < KEY_HOLD_MS_MIN) g_cfg.keyHoldMs = KEY_HOLD_MS_MIN;
    if (g_cfg.keyHoldMs > KEY_HOLD_MS_MAX) g_cfg.keyHoldMs = KEY_HOLD_MS_MAX;

    for (b = 0; b < BTN_COUNT; ++b) {
        g_cfg.holdTimeoutMs[b] = PFX_CAN(b)
            ? GetPrivateProfileIntW(L"General", cfg_hold_ini_key(b), 0, g_cfg.iniPath)
            : 0;
        if (g_cfg.holdTimeoutMs[b] < 0) g_cfg.holdTimeoutMs[b] = 0;
    }

    for (pfx = 0; pfx < BTN_COUNT; ++pfx) {
        for (suf = 0; suf < SUF_COUNT; ++suf) {
            Action *a = &g_cfg.chord[CH_ID(pfx, suf)];
            if (!PFX_CAN(pfx) || pfx == suf) { cfg_parse_action(L"none", a); continue; }
            cfg_chord_ini_key(pfx, suf, key, ARRAYSIZE(key));
            GetPrivateProfileStringW(L"Chords", key, chord_default(pfx, suf),
                                     v, ARRAYSIZE(v), g_cfg.iniPath);
            cfg_parse_action(v, a);
        }
    }

    /* 登録キーのトリガー。動作のほうは上の chord ループで読み終えている。 */
    for (pfx = 0; pfx < BTN_COUNT; ++pfx) {
        int i;
        for (i = 0; i < REGKEY_COUNT; ++i) {
            g_cfg.regKeySpec[pfx][i][0] = 0;
            g_cfg.regKeyVk[pfx][i] = 0;
            if (!PFX_CAN(pfx)) continue;
            cfg_regkey_ini_key(pfx, i, key, ARRAYSIZE(key));
            GetPrivateProfileStringW(L"Chords", key, L"", v, ARRAYSIZE(v), g_cfg.iniPath);
            str_trim(v);
            g_cfg.regKeyVk[pfx][i] = cfg_spec_to_vk(v);
            if (g_cfg.regKeyVk[pfx][i])
                lstrcpynW(g_cfg.regKeySpec[pfx][i], v, REGKEY_SPEC_CCH);
        }
    }

    /* 単独クリックを差し替えられるのはサイドボタンと中ボタン。
       中ボタンは「先に押す側」にはなれないが、単独の動作は差し替えられる
       (オートスクロールはここに入る)。左右クリックは事故のもとなので対象外。 */
    for (b = 0; b < BTN_COUNT; ++b) {
        Action *a = &g_cfg.single[b];
        if (b != BTN_X1 && b != BTN_X2 && b != BTN_M) { cfg_parse_action(L"passthru", a); continue; }
        cfg_single_ini_key(b, key, ARRAYSIZE(key));
        GetPrivateProfileStringW(L"Single", key, L"passthru", v, ARRAYSIZE(v), g_cfg.iniPath);
        cfg_parse_action(v, a);
    }

    /* 停止する条件。Rule1, Rule2, ... を順に読む。 */
    g_cfg.excludeN = 0;
    for (b = 1; b <= MAX_EXCLUDE_RULES; ++b) {
        WCHAR rk[32];
        wsprintfW(rk, L"Rule%d", b);
        GetPrivateProfileStringW(L"Exclude", rk, L"", v, ARRAYSIZE(v), g_cfg.iniPath);
        if (parse_exclude_line(v, &g_cfg.exclude[g_cfg.excludeN]))
            ++g_cfg.excludeN;
    }

    /* 古い形式 (Processes=a.exe,b.exe) も読む。設定を保存した時点で
       Rule1.. へ移り、こちらは消える。 */
    GetPrivateProfileStringW(L"Exclude", L"Processes", L"", raw, ARRAYSIZE(raw), g_cfg.iniPath);
    dst = raw;
    for (src = raw; ; ++src) {
        WCHAR save;
        if (*src != L',' && *src != L';' && *src != 0) continue;
        save = *src;
        *src = 0;
        if (g_cfg.excludeN < MAX_EXCLUDE_RULES &&
            parse_exclude_line(dst, &g_cfg.exclude[g_cfg.excludeN]))
            ++g_cfg.excludeN;
        if (!save) break;
        dst = src + 1;
    }
}
