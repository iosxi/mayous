# test_rawinput.ps1 - mayous が握り潰した押下を Raw Input から見えるか
#
#   見えるなら、「他の常駐ツールが離上を食べてしまって mayous が
#   保留のまま詰まる」を検出する地面として使える。
$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$test  = Join-Path $build 'rawtest'
$log   = Join-Path $build 'rawwatch.log'

if (-not (Test-Path (Join-Path $build 'rawwatch.exe'))) {
    & gcc -O2 -std=gnu11 -Wall -mconsole (Join-Path $PSScriptRoot 'rawwatch.c') `
          -o (Join-Path $build 'rawwatch.exe') -luser32
    if ($LASTEXITCODE -ne 0) { throw 'rawwatch.exe のビルドに失敗しました。' }
}

if (Test-Path $test) { Remove-Item $test -Recurse -Force }
New-Item -ItemType Directory -Path $test | Out-Null
Copy-Item (Join-Path $build 'mayous.exe') $test
@"
[General]
Enabled=1
RightHoldTimeoutMs=0
SuspendOnFullscreen=0
[Chords]
RightThenLeft=f14
[Exclude]
Processes=
"@ | Set-Content -Path (Join-Path $test 'mayous.ini') -Encoding ASCII

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class MR {
    [StructLayout(LayoutKind.Sequential)]
    struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)]
    struct INPUT { public uint type; public MOUSEINPUT mi; }
    [DllImport("user32.dll", SetLastError = true)] static extern uint SendInput(uint n, INPUT[] p, int cb);
    static void One(uint f){ INPUT[] a = new INPUT[1]; a[0].mi.dwFlags = f; SendInput(1, a, Marshal.SizeOf(typeof(INPUT))); }
    public static void RDown(){ One(0x0008); } public static void RUp(){ One(0x0010); }
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
}
'@

function W([int]$ms) { Start-Sleep -Milliseconds $ms }

$tlog = Join-Path $build 'rawtarget.log'
$tgt = Start-Process (Join-Path $build 'target.exe') -ArgumentList "`"$tlog`"", '20' -PassThru
W 1200
$w = Start-Process (Join-Path $build 'rawwatch.exe') -ArgumentList "`"$log`"", '14' -PassThru -WindowStyle Hidden
W 1000
$m = Start-Process (Join-Path $test 'mayous.exe') -PassThru
W 1800
if ($m.HasExited) { throw 'mayous が起動直後に終了した(多重起動の可能性)' }
Write-Host ('mayous PID={0} 生存={1}' -f $m.Id, (-not $m.HasExited))
[MR]::SetCursorPos(520, 420) | Out-Null
W 300

Write-Host '右ボタンを 2 秒保持して離す(mayous はこの押下を握り潰しているはず)'
[MR]::RDown()
W 2000
[MR]::RUp()
W 1500

Start-Process (Join-Path $test 'mayous.exe') -ArgumentList '--exit' -Wait
W 500
if (-not $m.HasExited) { $m.Kill() }
$w.WaitForExit()
if (-not $tgt.HasExited) { $tgt.CloseMainWindow() | Out-Null; W 800 }
if (-not $tgt.HasExited) { $tgt.Kill() }
W 300
Write-Host ''
Write-Host '===== アプリ(target.exe)が受け取ったもの ====='
Get-Content $tlog | ForEach-Object { Write-Host ('  ' + $_) }

Write-Host ''
Write-Host '===== rawwatch が見たもの ====='
Get-Content $log | ForEach-Object { Write-Host ('  ' + $_) }
Write-Host ''
$raw = (Get-Content $log) -match 'RAW    RIGHT DOWN'
$asy = (Get-Content $log) -match 'ASYNC  RIGHT DOWN'
if ($raw -and -not $asy) {
    Write-Host '  握り潰された押下は Raw Input には見えるが GetAsyncKeyState には見えない' -ForegroundColor Green
    Write-Host '  -> 物理状態の地面として Raw Input を使える' -ForegroundColor Green
} elseif ($raw -and $asy) {
    Write-Host '  どちらにも見える = そもそも握り潰されていない。設定を疑うこと' -ForegroundColor Yellow
} else {
    Write-Host '  Raw Input にも見えない -> 地面にはできない' -ForegroundColor Red
}
