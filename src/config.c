/* config.c - mayous.ini の読み書きとアクション文字列の解析
 *
 *  アクションの表記:
 *      none                       何もしない
 *      passthru                   単独クリック用。手を加えずそのまま通す
 *      hwheel_left / hwheel_right 水平ホイール
 *      win / alttab / alttab_back よく使うものの別名
 *      ctrl+alt+t                 1ステップのキーコンボ
 *      ctrl+c, ctrl+v             複数ステップ(記録したものの再生)
 */

#include "common.h"
#include <shlobj.h>
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
    if (!wcscmp(buf, L"hwheel_left")  || !wcscmp(buf, L"wheelleft"))  { a->kind = ACT_HWHEEL_LEFT;  return TRUE; }
    if (!wcscmp(buf, L"hwheel_right") || !wcscmp(buf, L"wheelright")) { a->kind = ACT_HWHEEL_RIGHT; return TRUE; }
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
/* 名前                                                                */
/* ------------------------------------------------------------------ */

static const WCHAR *kBtnName[BTN_COUNT] = {
    L"左クリック", L"右クリック", L"中クリック", L"サイドボタン1", L"サイドボタン2"
};
static const WCHAR *kSufName[SUF_COUNT] = {
    L"左クリック", L"右クリック", L"中クリック",
    L"サイドボタン1", L"サイドボタン2", L"ホイール上", L"ホイール下"
};
/* ini のキー名に使う識別子。既存の設定ファイルと互換を保つため
   左右は従来どおり Left / Right のまま。 */
static const WCHAR *kBtnIni[BTN_COUNT] = { L"Left", L"Right", L"Middle", L"Side1", L"Side2" };
static const WCHAR *kSufIni[SUF_COUNT] = {
    L"Left", L"Right", L"Middle", L"Side1", L"Side2", L"WheelUp", L"WheelDown"
};

const WCHAR *cfg_btn_name(int btn) { return (btn >= 0 && btn < BTN_COUNT) ? kBtnName[btn] : L"?"; }
const WCHAR *cfg_suf_name(int suf) { return (suf >= 0 && suf < SUF_COUNT) ? kSufName[suf] : L"?"; }

void cfg_chord_ini_key(int pfx, int suf, WCHAR *out, int cch)
{
    lstrcpynW(out, kBtnIni[pfx], cch);
    lstrcatW(out, L"Then");
    lstrcatW(out, kSufIni[suf]);
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

/* 既定値。ここに無い組み合わせはすべて none。 */
static const WCHAR *chord_default(int pfx, int suf)
{
    if (pfx == BTN_R && suf == SUF_L)   return L"win";
    if (pfx == BTN_R && suf == SUF_WUP) return L"hwheel_right";
    if (pfx == BTN_R && suf == SUF_WDN) return L"hwheel_left";
    if (pfx == BTN_L && suf == SUF_R)   return L"alttab";
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

void cfg_resolve_path(void)
{
    WCHAR dir[MAX_PATH], probe[MAX_PATH];
    HANDLE h;

    exe_dir(dir, ARRAYSIZE(dir));
    lstrcpynW(probe, dir, ARRAYSIZE(probe));
    lstrcatW(probe, L"mayous.ini");

    if (GetFileAttributesW(probe) != INVALID_FILE_ATTRIBUTES) {
        lstrcpynW(g_cfg.iniPath, probe, MAX_PATH);
        return;
    }
    /* 実際に作れるか試す(UAC の仮想化に騙されないよう実書き込みで判定) */
    h = CreateFileW(probe, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        DeleteFileW(probe);
        lstrcpynW(g_cfg.iniPath, probe, MAX_PATH);
        return;
    }
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, dir))) {
        lstrcatW(dir, L"\\Mayous");
        CreateDirectoryW(dir, NULL);
        lstrcatW(dir, L"\\mayous.ini");
        lstrcpynW(g_cfg.iniPath, dir, MAX_PATH);
    } else {
        lstrcpynW(g_cfg.iniPath, probe, MAX_PATH);
    }
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
L"LeftHoldTimeoutMs=200\r\n"
L"RightHoldTimeoutMs=0\r\n"
L"Side1HoldTimeoutMs=0\r\n"
L"Side2HoldTimeoutMs=0\r\n"
L"\r\n"
L"; フルスクリーンのアプリが前面のあいだは自動で停止する(ゲーム対策)\r\n"
L"SuspendOnFullscreen=1\r\n"
L"\r\n"
L"[Chords]\r\n"
L"; 書ける値:\r\n"
L";   none                        何もしない(そのボタンを一切乗っ取らない)\r\n"
L";   hwheel_left / hwheel_right  水平ホイール\r\n"
L";   win / alttab / alttab_back  よく使うものの別名\r\n"
L";   ctrl+w, alt+left, f5 ...    任意のキーコンボ\r\n"
L";   ctrl+c, ctrl+v              カンマ区切りで複数ステップ(記録の再生)\r\n"
L";\r\n"
L"; キー名: a-z 0-9 f1-f24 tab enter esc space backspace delete insert home end\r\n"
L";         pageup pagedown left right up down apps printscreen numpad0-9\r\n"
L";         volumeup volumedown volumemute medianext mediaprev mediaplay\r\n"
L";         browserback browserfwd ほか\r\n"
L"; 修飾子: ctrl alt shift win\r\n"
L"\r\n"
L"RightThenLeft=win\r\n"
L"RightThenWheelUp=hwheel_right\r\n"
L"RightThenWheelDown=hwheel_left\r\n"
L"LeftThenRight=alttab\r\n"
L"\r\n"
L"; 書かれていない組み合わせは none です。設定ウィンドウから編集するのが簡単です。\r\n"
L"; 例: Side1ThenLeft / Side2ThenWheelUp / LeftThenSide1 ...\r\n"
L"\r\n"
L"[Single]\r\n"
L"; サイドボタンを単独で押したときの動作。\r\n"
L";   passthru = 本来のボタンとしてそのまま通す(既定)\r\n"
L"Side1Alone=passthru\r\n"
L"Side2Alone=passthru\r\n"
L"\r\n"
L"[Exclude]\r\n"
L"; ここに書いた実行ファイルが前面のあいだは全機能を停止する。カンマ区切り。\r\n"
L"Processes=\r\n";

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

    if (g_cfg.dragThreshold < 0)   g_cfg.dragThreshold = 0;
    if (g_cfg.dragThreshold > 200) g_cfg.dragThreshold = 200;

    for (b = 0; b < BTN_COUNT; ++b) {
        int def = (b == BTN_L) ? 200 : 0;
        g_cfg.holdTimeoutMs[b] = PFX_CAN(b)
            ? GetPrivateProfileIntW(L"General", cfg_hold_ini_key(b), def, g_cfg.iniPath)
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

    for (b = 0; b < BTN_COUNT; ++b) {
        Action *a = &g_cfg.single[b];
        if (b != BTN_X1 && b != BTN_X2) { cfg_parse_action(L"passthru", a); continue; }
        cfg_single_ini_key(b, key, ARRAYSIZE(key));
        GetPrivateProfileStringW(L"Single", key, L"passthru", v, ARRAYSIZE(v), g_cfg.iniPath);
        cfg_parse_action(v, a);
    }

    /* 除外リストを ";a.exe;b.exe;" 形式(小文字)へ正規化しておく */
    GetPrivateProfileStringW(L"Exclude", L"Processes", L"", raw, ARRAYSIZE(raw), g_cfg.iniPath);
    str_lower(raw);
    dst = g_cfg.exclude;
    *dst++ = L';';
    for (src = raw; *src; ++src) {
        if (*src == L' ' || *src == L'\t' || *src == L'"') continue;
        if (*src == L',' || *src == L';') {
            if (dst[-1] != L';') *dst++ = L';';
            continue;
        }
        if ((size_t)(dst - g_cfg.exclude) < MAX_EXCLUDE - 3) *dst++ = *src;
    }
    if (dst[-1] != L';') *dst++ = L';';
    *dst = 0;
    if (!wcscmp(g_cfg.exclude, L";")) g_cfg.exclude[0] = 0;
}

BOOL cfg_is_excluded(const WCHAR *exeName)
{
    WCHAR pat[MAX_PATH + 2];

    if (!g_cfg.exclude[0] || !exeName || !*exeName) return FALSE;
    pat[0] = L';';
    lstrcpynW(pat + 1, exeName, MAX_PATH);
    str_lower(pat + 1);
    lstrcatW(pat, L";");
    return wcsstr(g_cfg.exclude, pat) != NULL;
}
