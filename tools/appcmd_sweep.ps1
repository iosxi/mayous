# appcmd_sweep.ps1 - どの APPCOMMAND を相手が受けるのかを総当たりで測る
#
#   「戻る」以外にも使える appcmd があるのか、を決めるための計測。
#   使い捨てのウィンドウを 1 枚立てて、そこへ順に撃ち込む。
#
#       powershell -ExecutionPolicy Bypass -File tools\appcmd_sweep.ps1 chrome
#       powershell -ExecutionPolicy Bypass -File tools\appcmd_sweep.ps1 firefox
#
#   結果は build\appcmd_sweep_<相手>.csv にも残る。
#
#   【戻り値の読み方】
#   WM_APPCOMMAND は「処理した」なら 0 以外を返す ── ことになっているが、
#   守っているかは相手次第である。実測:
#       Chrome  … 正しく区別する (REFRESH=1 / BASS_BOOST=0)
#       Firefox … ほぼ何にでも 1 を返す。履歴の無いページの「戻る」にも 1。
#                 つまり戻り値は当てにならず、目に見える変化でしか判定できない
#   そこでタイトルの変化も一緒に記録している。
#
#   【撃たないもの】
#   8〜18 (音量・メディア・アプリ起動) と 24〜26 (マイク) は外してある。
#   相手が無視すると DefWindowProc がシェルへ回すので、本当に音量が変わったり
#   メールソフトが起動したりする。31 CLOSE は最後 (窓またはタブが閉じる)。
$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$who   = if ($args.Count -ge 1) { $args[0] } else { 'chrome' }

Add-Type @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class ACS {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)]
  public static extern IntPtr SendMessageTimeout(IntPtr h, uint m, IntPtr w, IntPtr l, uint f, uint t, out IntPtr r);
  public static List<IntPtr> Tops() {
    List<IntPtr> v = new List<IntPtr>();
    EnumWindows(delegate(IntPtr h, IntPtr l) {
      if (!IsWindowVisible(h)) return true;
      StringBuilder t = new StringBuilder(512); GetWindowTextW(h, t, 512);
      if (t.Length > 0) v.Add(h);
      return true;
    }, IntPtr.Zero);
    return v;
  }
  public static string Title(IntPtr h) { StringBuilder s = new StringBuilder(512); GetWindowTextW(h, s, 512); return s.ToString(); }
  public static string Cls(IntPtr h)   { StringBuilder s = new StringBuilder(256); GetClassNameW(h, s, 256); return s.ToString(); }
}
"@

function Send-AppCmd([IntPtr]$h, [int]$cmd) {
  # 上位ワードに「コマンド | 発生源」。発生源はマウス(0x8000)に合わせる。
  $lp = [IntPtr](($cmd -bor 0x8000) -shl 16)
  $r = [IntPtr]::Zero
  $ok = [ACS]::SendMessageTimeout($h, 0x0319, $h, $lp, 0x2, 800, [ref]$r)
  [pscustomobject]@{ ok = [bool]$ok; result = [int64]$r }
}

$names = @{
  1='BROWSER_BACKWARD'; 2='BROWSER_FORWARD'; 3='BROWSER_REFRESH'; 4='BROWSER_STOP'
  5='BROWSER_SEARCH'; 6='BROWSER_FAVORITES'; 7='BROWSER_HOME'
  19='BASS_DOWN'; 20='BASS_BOOST'; 21='BASS_UP'; 22='TREBLE_DOWN'; 23='TREBLE_UP'
  27='HELP'; 28='FIND'; 29='NEW'; 30='OPEN'; 31='CLOSE'; 32='SAVE'; 33='PRINT'
  34='UNDO'; 35='REDO'; 36='COPY'; 37='CUT'; 38='PASTE'
  39='REPLY_TO_MAIL'; 40='FORWARD_MAIL'; 41='SEND_MAIL'; 42='SPELL_CHECK'
  43='DICTATE_TOGGLE'; 44='MIC_ON_OFF_TOGGLE'; 45='CORRECTION_LIST'
  46='MEDIA_PLAY'; 47='MEDIA_PAUSE'; 48='MEDIA_RECORD'; 49='MEDIA_FAST_FORWARD'
  50='MEDIA_REWIND'; 51='MEDIA_CHANNEL_UP'; 52='MEDIA_CHANNEL_DOWN'
  53='DELETE'; 54='DWM_FLIP3D'
}
$order = @(1,2,3,4,5,6,7) + @(19,20,21,22,23) + @(27,28,29,30,32,33,34,35,36,37,38) +
         @(39,40,41,42,43,44,45) + @(46,47,48,49,50,51,52,53,54) + @(31)

# --- 使い捨てウィンドウを立てる -------------------------------------------
# Firefox は専用プロファイルで隔離する。利用者のセッションを引き継ぐと、
# ピン留めタブや復元されたタブが混ざり、何が起きたのか読めなくなる
# (実際にそれで一度、計測が丸ごと無駄になった)。
$before = [ACS]::Tops()
$prof = $null
if ($who -eq 'firefox') {
    $prof = Join-Path $build 'appcmd_ffprof'
    if (Test-Path $prof) { Remove-Item $prof -Recurse -Force }
    New-Item -ItemType Directory -Path $prof | Out-Null
    $proc = Start-Process 'firefox.exe' -PassThru `
            -ArgumentList '-no-remote','-profile',"`"$prof`"",'https://example.com/'
    $cls  = 'MozillaWindowClass'
    $wait = 12
} else {
    $proc = Start-Process 'chrome.exe' -PassThru `
            -ArgumentList '--new-window','https://example.com/'
    $cls  = 'Chrome_WidgetWin_1'
    $wait = 6
}
Start-Sleep -Seconds $wait
$new = [ACS]::Tops() | Where-Object { $before -notcontains $_ -and [ACS]::Cls($_) -eq $cls }
if (-not $new) { Write-Host "$who の使い捨てウィンドウが見つかりません。"; exit 1 }
$h = @($new)[0]
Write-Host ("相手: {0}  hwnd={1}  {2}" -f $who, $h, [ACS]::Title($h))
Write-Host ''

# --- 総当たり ---------------------------------------------------------------
$rows = @()
foreach ($c in $order) {
  if (-not [ACS]::IsWindow($h)) { Write-Host "  ウィンドウが消えた (cmd=$c の手前)"; break }
  $t0 = [ACS]::Title($h)
  $r  = Send-AppCmd $h $c
  Start-Sleep -Milliseconds 600
  $alive = [ACS]::IsWindow($h)
  $t1 = if ($alive) { [ACS]::Title($h) } else { '(閉じた)' }
  $rows += [pscustomobject]@{
    cmd = $c; name = $names[$c]; handled = $r.result
    changed = ($t0 -ne $t1); title = $t1
  }
  Write-Host ("  {0,3} {1,-22} handled={2} changed={3}" -f $c, $names[$c], $r.result, ($t0 -ne $t1))
}

$csv = Join-Path $build ("appcmd_sweep_" + $who + ".csv")
$rows | Export-Csv -Path $csv -NoTypeInformation -Encoding UTF8
Write-Host ''
Write-Host ("csv: " + $csv)

# --- 後片付け ---------------------------------------------------------------
# OPEN(30) と SAVE(32) を受ける相手は、本当に「開く」「名前を付けて保存」の
# ダイアログを出す。開いたままにしない。
Start-Sleep -Seconds 1
foreach ($d in [ACS]::Tops()) {
  if ([ACS]::Cls($d) -eq '#32770') {
    Write-Host ("  ダイアログを閉じる: " + [ACS]::Title($d))
    [ACS]::PostMessage($d, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
  }
}
Start-Sleep -Seconds 2
if ([ACS]::IsWindow($h)) {
  [ACS]::PostMessage($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
  Start-Sleep -Seconds 2
}
if ($who -eq 'firefox') {
  # 親を止めても子が残ることがあるので、プロファイルで見分けて全部止める
  Get-CimInstance Win32_Process -Filter "Name='firefox.exe'" |
    Where-Object { $_.CommandLine -like "*appcmd_ffprof*" } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
  Start-Sleep -Seconds 2
  if (Test-Path $prof) { Remove-Item $prof -Recurse -Force -ErrorAction SilentlyContinue }
}
Write-Host '後片付け完了。'
