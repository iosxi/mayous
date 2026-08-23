# stress.ps1 - 状態機械の総当たり検証
#
#   本物のウィンドウ(target.exe)を相手に実際のマウス操作を注入し、
#   「アプリに何が届いたか」を直接記録する。フックの呼び出し順に依存しない。
#
#   最重要の不変条件: 押下と離上が必ず釣り合うこと。
#   崩れる = ボタンが押されっぱなし = 全クリックが壊れる。
#   各シナリオのあとに素の単クリックを打ち、正しく1回ずつ届くかも確認する。
#
#   使い方: powershell -ExecutionPolicy Bypass -File tools\stress.ps1 [-Debug]

param([switch]$UseDebugBuild)

$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$test  = Join-Path $build 'test'
$log   = Join-Path $build 'target.log'

$exeName = if ($UseDebugBuild) { 'mayous-debug.exe' } else { 'mayous.exe' }
if (-not (Test-Path (Join-Path $build $exeName))) { throw "$exeName がありません。" }
if (-not (Test-Path (Join-Path $build 'target.exe'))) {
    & gcc -O2 -std=gnu11 -Wall -mconsole (Join-Path $PSScriptRoot 'target.c') `
          -o (Join-Path $build 'target.exe') -luser32 -lgdi32
    if ($LASTEXITCODE -ne 0) { throw 'target.exe のビルドに失敗しました。' }
}

if (Test-Path $test) { Remove-Item $test -Recurse -Force }
New-Item -ItemType Directory -Path $test | Out-Null
Copy-Item (Join-Path $build $exeName) (Join-Path $test 'mayous.exe')

# 実害の無いキーに割り当てて検証する
@"
[General]
Enabled=1
DragThreshold=8
LeftHoldTimeoutMs=200
RightHoldTimeoutMs=0
SuspendOnFullscreen=0
[Chords]
RightThenLeft=f14
RightThenMiddle=f15
RightThenWheelUp=hwheel_right
RightThenWheelDown=hwheel_left
LeftThenRight=f13
LeftThenMiddle=none
LeftThenWheelUp=none
LeftThenWheelDown=none
[Exclude]
Processes=
"@ | Set-Content -Path (Join-Path $test 'mayous.ini') -Encoding ASCII

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class M {
    [StructLayout(LayoutKind.Sequential)]
    struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)]
    struct INPUT { public uint type; public MOUSEINPUT mi; }
    [DllImport("user32.dll", SetLastError = true)]
    static extern uint SendInput(uint n, INPUT[] p, int cb);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    static void One(uint flags, int dx, int dy, uint data) {
        INPUT[] a = new INPUT[1];
        a[0].mi.dwFlags = flags; a[0].mi.dx = dx; a[0].mi.dy = dy; a[0].mi.mouseData = data;
        if (SendInput(1, a, Marshal.SizeOf(typeof(INPUT))) != 1)
            throw new Exception("SendInput failed " + Marshal.GetLastWin32Error());
    }
    public static void LDown() { One(0x0002,0,0,0); }
    public static void LUp()   { One(0x0004,0,0,0); }
    public static void RDown() { One(0x0008,0,0,0); }
    public static void RUp()   { One(0x0010,0,0,0); }
    public static void MDown() { One(0x0020,0,0,0); }
    public static void MUp()   { One(0x0040,0,0,0); }
    public static void Wheel(int d) { One(0x0800,0,0,(uint)d); }
    public static void Move(int dx,int dy) { One(0x0001,dx,dy,0); }
}
'@

function W([int]$ms) { Start-Sleep -Milliseconds $ms }

Remove-Item "$env:TEMP\mayous_debug.log" -ErrorAction SilentlyContinue

$tgt = Start-Process (Join-Path $build 'target.exe') -ArgumentList "`"$log`"", '150' -PassThru
W 1200
$may = Start-Process (Join-Path $test 'mayous.exe') -PassThru
W 1500

# target ウィンドウの中央あたりへ。ウィンドウは 200,200 に 700x500 で出る。
[M]::SetCursorPos(520, 420) | Out-Null
W 400

$script:sections = @()
function Section([string]$name, [scriptblock]$body) {
    Write-Host ("  {0}" -f $name)
    $script:sections += $name
    W 1000
    & $body
    W 700
    # カナリア: 素の単クリック。壊れていたら直前のシナリオが原因。
    [M]::LDown(); W 60; [M]::LUp(); W 350
    [M]::RDown(); W 60; [M]::RUp(); W 600
}

Write-Host '検証開始'

Section 'S1  右を3秒保持 -> 左クリック -> 右を離す' {
    [M]::RDown(); W 3000; [M]::LDown(); W 60; [M]::LUp(); W 200; [M]::RUp()
}
Section 'S2  右+左、右を先に離す' {
    [M]::RDown(); W 80; [M]::LDown(); W 60; [M]::RUp(); W 100; [M]::LUp()
}
Section 'S3  左を3秒保持(長押し昇格) -> 離す' {
    [M]::LDown(); W 3000; [M]::LUp()
}
Section 'S4  左+右、左を先に離す' {
    [M]::LDown(); W 80; [M]::RDown(); W 60; [M]::LUp(); W 100; [M]::RUp()
}
Section 'S5  左+右、右を先に離す' {
    [M]::LDown(); W 80; [M]::RDown(); W 60; [M]::RUp(); W 100; [M]::LUp()
}
Section 'S6  右保持中に左を2回クリック' {
    [M]::RDown(); W 80
    [M]::LDown(); W 60; [M]::LUp(); W 150
    [M]::LDown(); W 60; [M]::LUp(); W 150
    [M]::RUp()
}
Section 'S7  左を昇格させてから右クリック' {
    [M]::LDown(); W 400; [M]::RDown(); W 60; [M]::RUp(); W 100; [M]::LUp()
}
Section 'S8  右ドラッグ(昇格)' {
    [M]::RDown(); W 60; [M]::Move(60,0); W 80; [M]::Move(60,0); W 80; [M]::RUp(); [M]::Move(-120,0)
}
Section 'S9  左ドラッグ(昇格)' {
    [M]::LDown(); W 60; [M]::Move(-60,0); W 80; [M]::Move(-60,0); W 80; [M]::LUp(); [M]::Move(120,0)
}
Section 'S10 右保持中にホイール' {
    [M]::RDown(); W 80; [M]::Wheel(120); W 80; [M]::Wheel(-120); W 80; [M]::RUp()
}
Section 'S11 右保持中に中クリック' {
    [M]::RDown(); W 80; [M]::MDown(); W 60; [M]::MUp(); W 100; [M]::RUp()
}
Section 'S12 左を高速連打' {
    for ($i=0; $i -lt 5; $i++) { [M]::LDown(); W 40; [M]::LUp(); W 120 }
}
Section 'S13 左と右を両方保持したまま3秒' {
    [M]::LDown(); W 80; [M]::RDown(); W 3000; [M]::LUp(); W 100; [M]::RUp()
}
Section 'S14 右を10秒保持してから左クリック' {
    [M]::RDown(); W 10000; [M]::LDown(); W 60; [M]::LUp(); W 200; [M]::RUp()
}

W 1200
Start-Process (Join-Path $test 'mayous.exe') -ArgumentList '--exit' -Wait
W 600
if (-not $may.HasExited) { $may.Kill() }
W 300
if (-not $tgt.HasExited) { $tgt.CloseMainWindow() | Out-Null; W 600 }
if (-not $tgt.HasExited) { $tgt.Kill() }
W 400

Write-Host ''
Write-Host '===== アプリが実際に受け取ったもの ====='
$lines = Get-Content $log
$lines | ForEach-Object { Write-Host ('  ' + $_) }

$bad = $lines | Where-Object { $_ -match '不整合' }
$fin = $lines | Where-Object { $_ -match '最終収支' }
Write-Host ''
if ($bad) {
    Write-Host ('  ** 不整合 {0} 件 **' -f $bad.Count) -ForegroundColor Red
} else {
    Write-Host '  押下と離上の釣り合いは全区間で正常' -ForegroundColor Green
}
Write-Host ('  {0}' -f $fin)
