# test_stuck_release.ps1 - 押しっぱなしの自動解除 (StuckReleaseSec)
#
#   利用者からの報告: あるアプリがマウスを横取りしていて、左クリックの押下と
#   離上が別々の側に渡ると、左が押されたまま残り、どの画面でも左クリックが
#   効かなくなる。
#
#   eater.exe を mayous のあとに起動して手前に立たせ、離上を 1 回だけ食べさせる。
#   OS の上ではボタンが押されたまま残る(GetAsyncKeyState が押下のまま)。
#   StuckReleaseSec 秒後に mayous が離上を注入して戻せば成功。
#
#   場面:
#     eaten-l  左の離上を食べられる          -> 解除される
#     eaten-r  右の離上を食べられる          -> 解除される
#     hold     左を本当に押し続ける(6 秒)    -> 解除されない
#     off      StuckReleaseSec=0 で eaten-l  -> 解除されない
param([string]$Exe = '',       # 検証用に別の mayous.exe を指す(修正前との比較)
      [int]$Sec = 3)           # StuckReleaseSec

$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$test  = Join-Path $build 'stuck'

foreach ($n in @('target', 'eater')) {
    if (-not (Test-Path (Join-Path $build "$n.exe"))) {
        & gcc -O2 -std=gnu11 -Wall -mconsole (Join-Path $PSScriptRoot "$n.c") `
              -o (Join-Path $build "$n.exe") -luser32 -lgdi32
        if ($LASTEXITCODE -ne 0) { throw "$n.exe のビルドに失敗しました。" }
    }
}

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class SR {
    [StructLayout(LayoutKind.Sequential)]
    struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)]
    struct INPUT { public uint type; public MOUSEINPUT mi; }
    [DllImport("user32.dll", SetLastError = true)] static extern uint SendInput(uint n, INPUT[] p, int cb);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] static extern short GetAsyncKeyState(int vk);
    static void One(uint f, long extra){ INPUT[] a = new INPUT[1]; a[0].mi.dwFlags = f; a[0].mi.dwExtraInfo = new IntPtr(extra); SendInput(1, a, Marshal.SizeOf(typeof(INPUT))); }
    public static void Down(bool right){ One(right ? 0x0008u : 0x0002u, 0); }
    public static void Up(bool right){ One(right ? 0x0010u : 0x0004u, 0); }
    // 後始末用。eater は dwExtraInfo 付きを食べない
    public static void UpTagged(bool right){ One(right ? 0x0010u : 0x0004u, 0x4D594F55); }
    public static bool Held(bool right){ return (GetAsyncKeyState(right ? 2 : 1) & 0x8000) != 0; }
}
'@

function W([int]$ms) { Start-Sleep -Milliseconds $ms }

function Run-Case([string]$name, [bool]$right, [bool]$eat, [int]$holdMs, [int]$iniSec) {
    $tlog = Join-Path $build "stuck_$name`_target.log"
    $elog = Join-Path $build "stuck_$name`_eater.log"

    if (Test-Path $test) { Remove-Item $test -Recurse -Force }
    New-Item -ItemType Directory -Path $test | Out-Null
    $src = if ($Exe) { $Exe } else { Join-Path $build 'mayous.exe' }
    Copy-Item $src (Join-Path $test 'mayous.exe')
    # 同時押しは全部 none。右も乗っ取らず素通しにして、OS の状態だけを見る。
    @"
[General]
Enabled=1
SuspendOnFullscreen=0
StuckReleaseSec=$iniSec
[Chords]
RightThenLeft=none
RightThenMiddle=none
RightThenWheelDown=none
RightThenWheelUp=none
[Exclude]
"@ | Set-Content -Path (Join-Path $test 'mayous.ini') -Encoding ASCII

    $tgt = Start-Process (Join-Path $build 'target.exe') -ArgumentList "`"$tlog`"", '30' -PassThru
    W 1200
    $may = Start-Process (Join-Path $test 'mayous.exe') -PassThru
    W 1800
    if ($may.HasExited) { throw 'mayous が起動直後に終了した(多重起動の可能性)' }
    $eaterProc = $null
    if ($eat) {
        $b = if ($right) { 'r' } else { 'l' }
        $eaterProc = Start-Process (Join-Path $build 'eater.exe') `
            -ArgumentList "`"$elog`"", '20', $b, '1' -PassThru -WindowStyle Hidden
        W 1200
    }
    [SR]::SetCursorPos(520, 420) | Out-Null
    W 400

    $released = -1
    $sw = [Diagnostics.Stopwatch]::StartNew()
    try {
        [SR]::Down($right)
        if ($eat) { W 150; [SR]::Up($right) }    # 離上は食べられる
        # 押されたままの間、250ms ごとに OS の状態を見る
        $limit = if ($eat) { ($Sec + 5) * 1000 } else { $holdMs }
        while ($sw.ElapsedMilliseconds -lt $limit) {
            W 250
            if (-not [SR]::Held($right)) { $released = $sw.ElapsedMilliseconds; break }
        }
        if (-not $eat -and $released -lt 0) { [SR]::Up($right) }   # 本物の離上
    } finally {
        W 300
        if ([SR]::Held($right)) { [SR]::UpTagged($right) }   # 本人の環境に残さない
    }

    Start-Process (Join-Path $test 'mayous.exe') -ArgumentList '--exit' -Wait
    W 600
    if (-not $may.HasExited) { $may.Kill() }
    if ($eaterProc) { $eaterProc.WaitForExit() }
    if (-not $tgt.HasExited) { $tgt.CloseMainWindow() | Out-Null; W 800 }
    if (-not $tgt.HasExited) { $tgt.Kill() }

    $ate = if ($eat) { (Get-Content $elog | Where-Object { $_ -match 'ATE' }).Count } else { 0 }
    $ups = (Get-Content $tlog | Where-Object { $_ -match '(LEFT|RIGHT)\s+UP' }).Count
    [pscustomobject]@{ Case = $name; Ate = $ate; ReleasedAtMs = $released; AppUps = $ups }
}

$results = @(
    Run-Case 'eaten-l' $false $true  0    $Sec
    Run-Case 'eaten-r' $true  $true  0    $Sec
    Run-Case 'hold'    $false $false 6000 $Sec
    Run-Case 'off'     $false $true  0    0
)
$results | Format-Table -AutoSize | Out-String | Write-Host

$ok = $true
foreach ($r in $results) {
    switch ($r.Case) {
        { $_ -like 'eaten-*' } {
            $good = $r.Ate -eq 1 -and $r.ReleasedAtMs -ge ($Sec * 1000) -and
                    $r.ReleasedAtMs -le (($Sec + 2) * 1000) -and $r.AppUps -eq 1
        }
        'hold' { $good = $r.ReleasedAtMs -lt 0 -and $r.AppUps -eq 1 }      # 解除されず、本物の離上だけ
        'off'  { $good = $r.Ate -eq 1 -and $r.ReleasedAtMs -lt 0 }         # 押されたまま
    }
    $c = if ($good) { 'Green' } else { 'Red'; $ok = $false }
    Write-Host ("  {0,-8} {1}" -f $r.Case, $(if ($good) { 'OK' } else { 'NG' })) -ForegroundColor $c
}
if (-not $ok) { exit 1 }
