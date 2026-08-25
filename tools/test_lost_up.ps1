# test_lost_up.ps1 - 他の常駐ツールに離上を食べられても復帰できるか
#
#   利用者からの報告: X-Mouse / MouseGestureL.ahk と併用中、サイドボタンを
#   押したあと左クリックが効かなくなり、mayous を落としたら直った。
#
#   低レベルフックは後から設置したものほど先に呼ばれる。mayous のあとに
#   eater.exe を起動して、物理的な離上を 1 回だけ食べさせる。
#   mayous には離上が永久に届かないので、保留したまま居座ることになる。
#   そのあとの左クリックがアプリに届けば復帰できている。
param([string]$Eat = 'r',      # r=右クリックの離上 / x1=サイドボタン1の離上
      [string]$Exe = '')       # 検証用に別の mayous.exe を指す(修正前との比較)

$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$test  = Join-Path $build 'lostup'
$tlog  = Join-Path $build 'lostup_target.log'
$elog  = Join-Path $build 'lostup_eater.log'

foreach ($n in @('target', 'eater')) {
    if (-not (Test-Path (Join-Path $build "$n.exe"))) {
        & gcc -O2 -std=gnu11 -Wall -mconsole (Join-Path $PSScriptRoot "$n.c") `
              -o (Join-Path $build "$n.exe") -luser32 -lgdi32
        if ($LASTEXITCODE -ne 0) { throw "$n.exe のビルドに失敗しました。" }
    }
}

if (Test-Path $test) { Remove-Item $test -Recurse -Force }
New-Item -ItemType Directory -Path $test | Out-Null
$src = if ($Exe) { $Exe } else { Join-Path $build 'mayous.exe' }
Copy-Item $src (Join-Path $test 'mayous.exe')
@"
[General]
Enabled=1
RightHoldTimeoutMs=0
Side1HoldTimeoutMs=0
SuspendOnFullscreen=0
[Chords]
RightThenLeft=f14
RightThenSide1=none
Side1ThenLeft=f16
[Single]
Side1=passthru
[Exclude]
Processes=
"@ | Set-Content -Path (Join-Path $test 'mayous.ini') -Encoding ASCII

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ML {
    [StructLayout(LayoutKind.Sequential)]
    struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)]
    struct INPUT { public uint type; public MOUSEINPUT mi; }
    [DllImport("user32.dll", SetLastError = true)] static extern uint SendInput(uint n, INPUT[] p, int cb);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    static void One(uint f, uint data){ INPUT[] a = new INPUT[1]; a[0].mi.dwFlags = f; a[0].mi.mouseData = data; SendInput(1, a, Marshal.SizeOf(typeof(INPUT))); }
    public static void LDown(){ One(0x0002,0); } public static void LUp(){ One(0x0004,0); }
    public static void RDown(){ One(0x0008,0); } public static void RUp(){ One(0x0010,0); }
    public static void X1Down(){ One(0x0080,1); } public static void X1Up(){ One(0x0100,1); }
}
'@

function W([int]$ms) { Start-Sleep -Milliseconds $ms }

$tgt = Start-Process (Join-Path $build 'target.exe') -ArgumentList "`"$tlog`"", '25' -PassThru
W 1200
$may = Start-Process (Join-Path $test 'mayous.exe') -PassThru
W 1800
if ($may.HasExited) { throw 'mayous が起動直後に終了した(多重起動の可能性)' }
# mayous のあとに張る = チェーンで手前に立つ
$eaterProc = Start-Process (Join-Path $build 'eater.exe') -ArgumentList "`"$elog`"", '20', $Eat, '1' -PassThru -WindowStyle Hidden
W 1200
[ML]::SetCursorPos(520, 420) | Out-Null
W 400

if ($Eat -eq 'x1') {
    Write-Host 'サイドボタン1 を押して離す(離上は食べられる)'
    [ML]::X1Down(); W 150; [ML]::X1Up()
} else {
    Write-Host '右クリックを押して離す(離上は食べられる)'
    [ML]::RDown(); W 150; [ML]::RUp()
}
W 1500

Write-Host 'そのあと左クリックを 3 回。届くか？'
for ($i = 0; $i -lt 3; $i++) { [ML]::LDown(); W 70; [ML]::LUp(); W 500 }
W 800

Start-Process (Join-Path $test 'mayous.exe') -ArgumentList '--exit' -Wait
W 600
if (-not $may.HasExited) { $may.Kill() }
$eaterProc.WaitForExit()
W 300
if (-not $tgt.HasExited) { $tgt.CloseMainWindow() | Out-Null; W 800 }
if (-not $tgt.HasExited) { $tgt.Kill() }
W 300

Write-Host ''
Write-Host '===== 食べた側 ====='
Get-Content $elog | ForEach-Object { Write-Host ('  ' + $_) }
Write-Host ''
Write-Host '===== アプリが受け取ったもの ====='
$lines = Get-Content $tlog
$lines | ForEach-Object { Write-Host ('  ' + $_) }

$leftDown = ($lines | Where-Object { $_ -match 'LEFT\s+DOWN' }).Count
$bad      = $lines | Where-Object { $_ -match '不整合' }
$final    = $lines | Where-Object { $_ -match '最終収支|final balance' }
Write-Host ''
Write-Host ("  左クリックの押下が届いた回数: {0} / 3" -f $leftDown)
if ($leftDown -ge 3 -and -not $bad) {
    Write-Host '  離上を食べられても復帰できている' -ForegroundColor Green
} else {
    Write-Host '  ** 復帰できていない。左クリックが飲み込まれたままになる **' -ForegroundColor Red
}
