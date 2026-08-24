/* ==================================================================
 * legacy.c - 昔のバージョンが残した痕跡の掃除
 *
 *  Mayous は自動起動の登録を行わない。スタートアップに入れるかどうかは
 *  使う人が自分で決めることで、こちらから勝手に登録するのは余計な世話。
 *
 *  ただし以前のバージョンは HKCU\...\Run に自分を書き込んでいた。
 *  それが残っていると「消したはずのソフトが起動する」ことになるので、
 *  起動時に一度だけ掃除する。
 *
 *  これが Mayous がレジストリに触れる唯一の箇所で、しかも
 *  「自分が作った値を消す」方向にしか働かない。
 *  値が存在しない普通の環境では、読み取り専用で開いて確認して終わる。
 * ================================================================== */

#include "common.h"

#define OLD_RUNKEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"

void startup_cleanup_legacy(void)
{
    HKEY k;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, OLD_RUNKEY, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
        return;
    {
        LONG found = RegQueryValueExW(k, MAYOUS_APPNAME, NULL, NULL, NULL, NULL);
        RegCloseKey(k);
        if (found != ERROR_SUCCESS) return;      /* 何も無い = ここで終わり */
    }

    /* 実在したときだけ、書き込み権限で開き直して消す */
    if (RegOpenKeyExW(HKEY_CURRENT_USER, OLD_RUNKEY, 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
        RegDeleteValueW(k, MAYOUS_APPNAME);
        RegCloseKey(k);
    }
}
