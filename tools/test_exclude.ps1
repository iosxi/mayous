# test_exclude.ps1 - 「停止する条件」をウィンドウ名で判定できているかの実測。
#
#   Rule1=title:MayousExcludeTest* と RightThenLeft=f14 を入れた mayous を相手に、
#     1. 名前が一致するあいだ同時押しが止まっているか
#     2. 名前を変えたら、再起動なしで効くようになるか(1 秒ごとの点検が拾う)
#     3. 名前を戻したら、また止まるか
#   を build\exclude.exe で確かめる。自前のウィンドウの題名を書き換えるだけなので、
#   他のアプリ(Minecraft など)が起動していなくても走る。
#
#     .	ools	est_exclude.ps1

$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$exe   = Join-Path $build 'mayous.exe'
$tool  = Join-Path $build 'exclude.exe'
$src   = Join-Path $PSScriptRoot 'exclude.c'

if (-not (Test-Path $exe)) { throw 'build\mayous.exe がありません。build.bat を先に実行してください。' }
if (@(Get-Process mayous -ErrorAction SilentlyContinue).Count -gt 0) {
    throw 'mayous が既に起動しています。終了してから実行してください。'
}
if ((-not (Test-Path $tool)) -or ((Get-Item $src).LastWriteTime -gt (Get-Item $tool).LastWriteTime)) {
    & gcc -O2 -std=gnu11 -Wall -Wextra -mconsole $src -o $tool -luser32 -lgdi32
    if ($LASTEXITCODE -ne 0) { throw 'exclude.exe のビルドに失敗しました。' }
}

$dir = Join-Path $build 'exclrun'
if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
New-Item -ItemType Directory -Path $dir | Out-Null
Copy-Item $exe $dir
@"
[General]
Enabled=1
SuspendOnFullscreen=0
RightHoldTimeoutMs=0
[Chords]
RightThenLeft=f14
RightThenMiddle=none
RightThenWheelUp=none
RightThenWheelDown=none
[Single]
[Exclude]
Rule1=title:MayousExcludeTest*
"@ | Set-Content -Path (Join-Path $dir 'mayous.ini') -Encoding ASCII

[Console]::OutputEncoding = [Text.Encoding]::UTF8
& $tool (Join-Path $dir 'mayous.exe')
$code = $LASTEXITCODE

Start-Sleep -Milliseconds 800
Get-Process mayous -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

if ($code -ne 0) { Write-Host '  NG: 上の行を確認してください' -ForegroundColor Red; exit 1 }

# --- その2: 設定画面から「ウィンドウ名を追加」 ---------------------
Write-Host ''
Write-Host '=== その2: 設定画面の一覧から追加 ===' -ForegroundColor Cyan

Add-Type @'
using System; using System.Runtime.InteropServices; using System.Text;
public static class EX {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  public delegate bool EnumProc(IntPtr h, IntPtr p);
}
'@
function Find-ByClass([string]$cls, [uint32]$procId) {
  $script:hit = [IntPtr]::Zero; $script:c = $cls; $script:pp = $procId
  $cb = [EX+EnumProc]{ param($h,$p)
    $sb = New-Object Text.StringBuilder 256; [EX]::GetClassNameW($h,$sb,256) | Out-Null
    if ($sb.ToString() -eq $script:c) {
      $q = 0; [EX]::GetWindowThreadProcessId($h, [ref]$q) | Out-Null
      if ($script:pp -eq 0 -or $q -eq $script:pp) { $script:hit = $h; return $false } }
    return $true }
  [EX]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
  return $script:hit
}

$dir2 = Join-Path $build 'exclui'
if (Test-Path $dir2) { Remove-Item $dir2 -Recurse -Force }
New-Item -ItemType Directory -Path $dir2 | Out-Null
Copy-Item $exe $dir2
$ini2 = Join-Path $dir2 'mayous.ini'
@"
[General]
Enabled=1
SuspendOnFullscreen=0
[Chords]
RightThenLeft=win
[Single]
[Exclude]
"@ | Set-Content -Path $ini2 -Encoding ASCII

$holder = Start-Process $tool -ArgumentList '-hold','40' -PassThru
Start-Sleep -Milliseconds 1200
$target = Find-ByClass 'MayousExcludeTest' ([uint32]$holder.Id)
if ($target -eq [IntPtr]::Zero) { throw '対象のウィンドウが見つかりません。' }

$may = Start-Process (Join-Path $dir2 'mayous.exe') -PassThru
Start-Sleep -Milliseconds 1500
$tray = Find-ByClass 'MayousHiddenWnd' ([uint32]$may.Id)
[EX]::PostMessage($tray, 0x0111, [IntPtr]1000, [IntPtr]0) | Out-Null
Start-Sleep -Seconds 2
$sw = Find-ByClass 'MayousSettingsWnd' ([uint32]$may.Id)
if ($sw -eq [IntPtr]::Zero) { throw '設定ウィンドウが開きませんでした。' }

$lb = [EX]::GetDlgItem($sw, 1121)
$cnt = [int][EX]::SendMessage($lb, 0x018B, [IntPtr]0, [IntPtr]0)     # LB_GETCOUNT
Write-Host ("  一覧の件数: {0}" -f $cnt)
$sel = -1
for ($i = 0; $i -lt $cnt; $i++) {
    $d = [EX]::SendMessage($lb, 0x0199, [IntPtr]$i, [IntPtr]0)       # LB_GETITEMDATA
    if ($d -eq $target) { $sel = $i; break }
}
Write-Host ("  対象の行: {0}" -f $sel)
$ng2 = 0
if ($sel -lt 0) { $ng2 = 1 } else {
    [EX]::SendMessage($lb, 0x0186, [IntPtr]$sel, [IntPtr]0) | Out-Null   # LB_SETCURSEL
    Start-Sleep -Milliseconds 300
    [EX]::PostMessage($sw, 0x0111, [IntPtr]1124, [IntPtr]0) | Out-Null   # IDC_EXC_ADDTITLE
    Start-Sleep -Milliseconds 600
    [EX]::PostMessage($sw, 0x0111, [IntPtr]1130, [IntPtr]0) | Out-Null   # OK
    Start-Sleep -Milliseconds 1500
}
$rules = @(Get-Content $ini2 | Where-Object { $_ -match '^Rule' })
$rules | ForEach-Object { Write-Host ('  ini: ' + $_) }
if (-not ($rules -contains 'Rule1=title:MayousExcludeTest - Alpha*')) { $ng2 = 1 }

Start-Process (Join-Path $dir2 'mayous.exe') -ArgumentList '--exit' -Wait
Start-Sleep -Milliseconds 600
Get-Process mayous -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
if (-not $holder.HasExited) { $holder.Kill() }

Write-Host ''
if ($ng2 -ne 0) { Write-Host '  NG: 一覧からの追加が期待どおりではありません' -ForegroundColor Red; exit 1 }
Write-Host '  OK: ウィンドウ名での停止も、一覧からの追加も期待どおりです' -ForegroundColor Green
