/* dpifont.c - 「設定画面の文字が実際の表示スケールに合っていない」の原因を数字で見る。
 *
 *   SystemParametersInfo(SPI_GETNONCLIENTMETRICS) が返すフォントは、
 *   プロセスが動き出した時点のシステム DPI のもので、以後は変わらない
 *   (マニフェストで PerMonitorV2 を宣言していても、この関数だけは追従しない)。
 *   一方 SystemParametersInfoForDpi は、渡した DPI のフォントを返す。
 *
 *   このツールは両方を呼んで、フォントの高さ(= 設定画面のレイアウト単位)を
 *   並べて出す。表示スケールを変えてからもう一度実行すると、
 *     ・SPI の行     … 変わらない (これが不具合の原因)
 *     ・ForDpi の行  … 変わる     (mayous はこちらを使うようにした)
 *   ことが確かめられる。
 *
 *   使い方: dpifont.exe            自分の窓の DPI で比べる
 *           dpifont.exe -watch     5 秒ごとに出し続ける(スケール変更の観察用)
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef UINT (WINAPI *fnGetDpiForWindow)(HWND);
typedef UINT (WINAPI *fnGetDpiForSystem)(void);
typedef BOOL (WINAPI *fnSPIForDpi)(UINT, UINT, PVOID, UINT, UINT);

static fnGetDpiForWindow p_GetDpiForWindow;
static fnGetDpiForSystem p_GetDpiForSystem;
static fnSPIForDpi       p_SPIForDpi;

/* そのフォントで組んだときのレイアウト単位(文字の高さ)を返す */
static int unit_of(const LOGFONTW *lf)
{
    HFONT f = CreateFontIndirectW(lf);
    HDC   dc = GetDC(NULL);
    HFONT old;
    TEXTMETRICW tm;
    int h = 0;

    if (!f) { ReleaseDC(NULL, dc); return 0; }
    old = (HFONT)SelectObject(dc, f);
    if (GetTextMetricsW(dc, &tm)) h = (int)tm.tmHeight;
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    DeleteObject(f);
    return h;
}

static void report(HWND ref)
{
    NONCLIENTMETRICSW ncm;
    UINT winDpi = p_GetDpiForWindow ? p_GetDpiForWindow(ref) : 0;
    UINT sysDpi = p_GetDpiForSystem ? p_GetDpiForSystem() : 0;
    static const UINT kDpi[4] = { 96, 120, 144, 192 };
    int i;

    printf("窓の DPI = %u (%u%%)   システム DPI = %u\n",
           winDpi, winDpi ? winDpi * 100 / 96 : 0, sysDpi);

    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        printf("  SystemParametersInfo         lfHeight=%4ld  文字の高さ=%2d"
               "   <- 起動時のまま。追従しない\n",
               (long)ncm.lfMessageFont.lfHeight, unit_of(&ncm.lfMessageFont));
    else
        printf("  SystemParametersInfo         取得できず\n");

    if (!p_SPIForDpi) {
        printf("  SystemParametersInfoForDpi   この環境には無い(1607 未満)\n");
        return;
    }
    for (i = 0; i < 4; ++i) {
        ncm.cbSize = sizeof(ncm);
        if (!p_SPIForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, kDpi[i])) continue;
        printf("  ForDpi(%3u = %3u%%)           lfHeight=%4ld  文字の高さ=%2d%s\n",
               kDpi[i], kDpi[i] * 100 / 96,
               (long)ncm.lfMessageFont.lfHeight, unit_of(&ncm.lfMessageFont),
               (kDpi[i] == winDpi) ? "   <- 今の窓はこれを使うべき" : "");
    }
}

int main(int argc, char **argv)
{
    HMODULE u = GetModuleHandleW(L"user32.dll");
    HWND    w;
    int     watch = (argc >= 2 && !strcmp(argv[1], "-watch"));
    BOOL (WINAPI *p_SetCtx)(HANDLE);

    /* mayous と同じ土俵に立つ。DPI 非対応のままだと、どちらの API も
       96dpi に仮想化された値しか返さず、比べる意味が無くなる。 */
    p_SetCtx = (BOOL (WINAPI *)(HANDLE))(void *)
               GetProcAddress(u, "SetProcessDpiAwarenessContext");
    if (p_SetCtx) p_SetCtx((HANDLE)(INT_PTR)-4);   /* PER_MONITOR_AWARE_V2 */

    p_GetDpiForWindow = (fnGetDpiForWindow)(void *)GetProcAddress(u, "GetDpiForWindow");
    p_GetDpiForSystem = (fnGetDpiForSystem)(void *)GetProcAddress(u, "GetDpiForSystem");
    p_SPIForDpi       = (fnSPIForDpi)(void *)GetProcAddress(u, "SystemParametersInfoForDpi");

    /* 比較の相手になる窓。mayous の設定画面と同じ扱いにするため実際に作る。 */
    w = CreateWindowExW(0, L"STATIC", L"dpifont", WS_OVERLAPPED,
                        100, 100, 200, 100, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!w) { printf("窓を作れませんでした\n"); return 1; }

    for (;;) {
        MSG m;
        report(w);
        if (!watch) break;
        printf("---- 5 秒後にもう一度 (Ctrl+C で終了) ----\n");
        {
            DWORD end = GetTickCount() + 5000;
            for (;;) {
                DWORD left = end - GetTickCount();
                if ((int)left <= 0) break;
                if (MsgWaitForMultipleObjects(0, NULL, FALSE, left, QS_ALLINPUT) == WAIT_TIMEOUT)
                    break;
                while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
            }
        }
    }
    DestroyWindow(w);
    return 0;
}
