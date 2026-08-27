# test_regkey.ps1 - 登録キー(マウスのボタン + キーボードのキー)の検証。
#
#   その1 実測 (build\regkey.exe)
#     「右クリックを押しながら F13 で F14 を出す」設定を作り、
#       1. 右ボタンを押していないときの F13 は素通しするか
#       2. 右ボタンを押しながらの F13 は握り潰され、F14 が何 ms 後に出るか
#       3. その F14 は右ボタンを離すまで押されたままか
#     を数字にする。F13 / F14 は普通のキーボードに無いので、どのアプリも
#     反応しない = 前面に何が居ても副作用が出ない。
#
#   その2 設定画面
#     登録キーの行(キーのボタン + 動作のコンボ)が出ていて、
#     ini へ正しく書けること。ついでに左クリックのタブが消えていること。
#     (タブは 右 / サイド1 / サイド2 / 中クリック の 4 つ)
#
#   利用者の設定は触らない(一時フォルダへ複製して動かす)。
#
#     .\tools\test_regkey.ps1

$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$exe   = Join-Path $build 'mayous.exe'
$tool  = Join-Path $build 'regkey.exe'
$src   = Join-Path $PSScriptRoot 'regkey.c'

if (-not (Test-Path $exe)) { throw 'build\mayous.exe がありません。build.bat を先に実行してください。' }
if (@(Get-Process mayous -ErrorAction SilentlyContinue).Count -gt 0) {
    throw 'mayous が既に起動しています。単一インスタンスなので、終了してから実行してください。'
}
if ((-not (Test-Path $tool)) -or ((Get-Item $src).LastWriteTime -gt (Get-Item $tool).LastWriteTime)) {
    & gcc -O2 -std=gnu11 -Wall -Wextra -mconsole $src -o $tool -luser32
    if ($LASTEXITCODE -ne 0) { throw 'regkey.exe のビルドに失敗しました。' }
}

$dir = Join-Path $build 'regkeytest'
if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
New-Item -ItemType Directory -Path $dir | Out-Null
Copy-Item $exe $dir
$ini = Join-Path $dir 'mayous.ini'
@"
[General]
Enabled=1
SuspendOnFullscreen=0
KeyHoldMs=120
RepressGapMs=120
RightHoldTimeoutMs=0
[Chords]
RightThenLeft=none
RightThenMiddle=none
RightThenWheelUp=none
RightThenWheelDown=none
RightThenKey1Trigger=f13
RightThenKey1=f14
[Single]
[Exclude]
Processes=
"@ | Set-Content -Path $ini -Encoding ASCII

# --- その1: 実測 ------------------------------------------------------
Write-Host '=== その1: 実測 ===' -ForegroundColor Cyan
$prev = [Console]::OutputEncoding
[Console]::OutputEncoding = [Text.Encoding]::UTF8
try {
    & $tool (Join-Path $dir 'mayous.exe')
    $code = $LASTEXITCODE
} finally {
    [Console]::OutputEncoding = $prev
}
Start-Sleep -Milliseconds 800
Get-Process mayous -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 400

if ($code -eq 2) {
    Write-Host '  計測できませんでした(デスクトップの切り替えが走っています)。' -ForegroundColor Yellow
    Write-Host '  UAC やロック画面が出ていないか確かめて、もう一度実行してください。' -ForegroundColor Yellow
    exit 2
}
if ($code -ne 0) { Write-Host '  NG: 上の行を確認してください' -ForegroundColor Red; exit 1 }

# --- その2: 設定画面 --------------------------------------------------
Write-Host ''
Write-Host '=== その2: 設定画面 ===' -ForegroundColor Cyan

Add-Type @'
using System; using System.Runtime.InteropServices; using System.Text;
public static class RK {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  public delegate bool EnumProc(IntPtr h, IntPtr p);

  [StructLayout(LayoutKind.Sequential)] struct KI {
    public ushort vk, scan; public uint flags, time; public IntPtr extra; public ulong pad; }
  [StructLayout(LayoutKind.Sequential)] struct INPUT { public uint type; public KI ki; }
  [DllImport("user32.dll", SetLastError=true)] static extern uint SendInput(uint n, INPUT[] p, int cb);
  [DllImport("user32.dll")] static extern uint MapVirtualKeyW(uint code, uint type);
  public static void SendInputKey(ushort vk, bool up) {
    INPUT[] a = new INPUT[1];
    a[0].type = 1;
    a[0].ki.vk = vk;
    a[0].ki.scan = (ushort)MapVirtualKeyW(vk, 0);
    a[0].ki.flags = up ? 2u : 0u;
    SendInput(1, a, Marshal.SizeOf(typeof(INPUT)));
  }
}
'@

function Find-Wnd([uint32]$procId, [string]$cls) {
  $script:hit = [IntPtr]::Zero; $script:tp = $procId; $script:tc = $cls
  $cb = [RK+EnumProc]{ param($h,$p)
    $q = 0; [RK]::GetWindowThreadProcessId($h, [ref]$q) | Out-Null
    if ($q -eq $script:tp) {
      $sb = New-Object Text.StringBuilder 256; [RK]::GetClassNameW($h,$sb,256) | Out-Null
      if ($sb.ToString() -eq $script:tc) { $script:hit = $h; return $false } }
    return $true }
  [RK]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
  return $script:hit
}
function Text-Of([IntPtr]$h) {
  if ($h -eq [IntPtr]::Zero) { return '' }
  $sb = New-Object Text.StringBuilder 256
  [RK]::GetWindowTextW($h, $sb, 256) | Out-Null
  return $sb.ToString()
}

# 設定画面のコントロール ID (settings.c と common.h から)
#   SUF_COUNT = 7 + REGKEY_COUNT(1) = 8 / CH_ID(BTN_R=1, SUF_KEY0=7) = 15
$IDC_TRIG   = 2300 + 1        # 右クリックタブの登録キー1 のキー
$IDC_ACTION = 2000 + 15       # 同 動作のコンボ
$IDC_HOLD_L = 1110 + 0        # 左クリックの長押し(消えているはず)
$IDC_OK     = 1130
$WM_COMMAND = 0x0111
$WM_SETTEXT = 0x000C
$TCM_GETITEMCOUNT = 0x1304

function Open-Settings([uint32]$pid2) {
  [RK]::PostMessage((Find-Wnd $pid2 'MayousHiddenWnd'), $WM_COMMAND, [IntPtr]1000, [IntPtr]0) | Out-Null
  Start-Sleep -Seconds 2
  $h = Find-Wnd $pid2 'MayousSettingsWnd'
  if ($h -eq [IntPtr]::Zero) { throw '設定ウィンドウが開きませんでした。' }
  return $h
}

$may = Start-Process (Join-Path $dir 'mayous.exe') -PassThru
Start-Sleep -Milliseconds 1500
if ((Find-Wnd ([uint32]$may.Id) 'MayousHiddenWnd') -eq [IntPtr]::Zero) { throw 'トレイ窓が見つかりません。' }

$ng = 0
$sw = Open-Settings ([uint32]$may.Id)

$tab   = [RK]::GetDlgItem($sw, 900)
$tabs  = [int][RK]::SendMessage($tab, $TCM_GETITEMCOUNT, [IntPtr]0, [IntPtr]0)
$trig  = [RK]::GetDlgItem($sw, $IDC_TRIG)
$act   = [RK]::GetDlgItem($sw, $IDC_ACTION)
$holdL = [RK]::GetDlgItem($sw, $IDC_HOLD_L)

Write-Host ("  タブの数            : {0} (期待 5 = 右/サイド1/サイド2/中クリック/停止する条件)" -f $tabs)
if ($tabs -ne 5) { $ng = 1 }
Write-Host ("  左クリックの長押し欄: {0}" -f $(if ($holdL -eq [IntPtr]::Zero) { '無し (期待どおり)' } else { 'まだ有る (NG)' }))
if ($holdL -ne [IntPtr]::Zero) { $ng = 1 }
Write-Host ("  登録キーのキー      : '{0}' (期待 F13)" -f (Text-Of $trig))
if ((Text-Of $trig) -ne 'F13') { $ng = 1 }
Write-Host ("  登録キーの動作の欄  : {0}" -f $(if ($act -ne [IntPtr]::Zero) { '有り' } else { '無し (NG)' }))
if ($act -eq [IntPtr]::Zero) { $ng = 1 }

# 何も変えずに [OK]。読み込めていれば ini はそのまま残る
# (コンボの中身は別プロセスからは読めないので、書き戻しで確かめる)
[RK]::PostMessage($sw, $WM_COMMAND, [IntPtr]$IDC_OK, [IntPtr]0) | Out-Null
Start-Sleep -Milliseconds 1500
$keep = @(Get-Content $ini | Where-Object { $_ -match '^RightThenKey1' })
$keep | ForEach-Object { Write-Host ('  そのまま OK 後 ini: ' + $_) }
if (-not ($keep -contains 'RightThenKey1=f14'))         { $ng = 1 }
if (-not ($keep -contains 'RightThenKey1Trigger=f13'))  { $ng = 1 }

# 動作を一覧に無い指定へ書き換えて保存できるか
$sw = Open-Settings ([uint32]$may.Id)
$act = [RK]::GetDlgItem($sw, $IDC_ACTION)
[RK]::SendMessageW($act, $WM_SETTEXT, [IntPtr]0, 'ctrl+w') | Out-Null
Start-Sleep -Milliseconds 300
[RK]::PostMessage($sw, $WM_COMMAND, [IntPtr]$IDC_OK, [IntPtr]0) | Out-Null
Start-Sleep -Milliseconds 1500

$after = @(Get-Content $ini | Where-Object { $_ -match '^RightThenKey1' })
$after | ForEach-Object { Write-Host ('  書き換え後 ini    : ' + $_) }
if (-not ($after -contains 'RightThenKey1=ctrl+w'))     { $ng = 1 }
if (-not ($after -contains 'RightThenKey1Trigger=f13')) { $ng = 1 }

# --- その3: [記録] でトリガーを登録し直す ----------------------------
Write-Host ''
Write-Host '=== その3: キーの記録 ===' -ForegroundColor Cyan

$BM_CLICK = 0x00F5
$sw = Open-Settings ([uint32]$may.Id)
$trig = [RK]::GetDlgItem($sw, $IDC_TRIG)
[RK]::PostMessage($trig, $BM_CLICK, [IntPtr]0, [IntPtr]0) | Out-Null
Start-Sleep -Milliseconds 1200

$cap = Find-Wnd ([uint32]$may.Id) 'MayousCaptureWnd'
if ($cap -eq [IntPtr]::Zero) { throw '記録ウィンドウが開きませんでした。' }
Write-Host '  記録ウィンドウが開きました'

# F15 を 1 回押す (記録中はキーボードが握り潰されるので副作用は無い)
[RK]::SendInputKey(0x7E, $false)
Start-Sleep -Milliseconds 150
[RK]::SendInputKey(0x7E, $true)
Start-Sleep -Milliseconds 300
[RK]::PostMessage($cap, $WM_COMMAND, [IntPtr]1, [IntPtr]0) | Out-Null   # IDC_CAP_OK
Start-Sleep -Milliseconds 1200

$got = Text-Of ([RK]::GetDlgItem($sw, $IDC_TRIG))
Write-Host ("  記録後のキー        : '{0}' (期待 F15)" -f $got)
if ($got -ne 'F15') { $ng = 1 }

[RK]::PostMessage($sw, $WM_COMMAND, [IntPtr]$IDC_OK, [IntPtr]0) | Out-Null
Start-Sleep -Milliseconds 1500
$last = @(Get-Content $ini | Where-Object { $_ -match '^RightThenKey1Trigger' })
$last | ForEach-Object { Write-Host ('  ini: ' + $_) }
if (-not ($last -contains 'RightThenKey1Trigger=f15')) { $ng = 1 }

Start-Process (Join-Path $dir 'mayous.exe') -ArgumentList '--exit' -Wait
Start-Sleep -Milliseconds 800
Get-Process mayous -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

Write-Host ''
if ($ng -ne 0) { Write-Host '  NG: 設定画面が期待どおりではありません' -ForegroundColor Red; exit 1 }
Write-Host '  OK: 登録キーは実測・設定画面とも期待どおりです' -ForegroundColor Green
