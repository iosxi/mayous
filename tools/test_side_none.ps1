# test_side_none.ps1 - 「右クリック + サイドボタン1 = なし」の周辺を潰す
#
#   割り当てが無い組み合わせでは、保留していた右クリックを注入で世に出してから
#   サイドボタンを通す。注入はフックから戻ったあとのキューで出るので、
#   アプリには「サイドボタン押下 -> 右押下」と順序が逆に届く可能性がある。
#   押下と離上が釣り合わなくなれば、以後すべてのクリックが壊れる。
#
#   stress.ps1 と同じく本物のウィンドウ(target.exe)に届いたものを直接見る。
param([switch]$Armed)   # -Armed でサイドボタン1 自体にも割り当てがある場合を試す

$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$test  = Join-Path $build 'sidenone'
$log   = Join-Path $build 'sidenone.log'

if (-not (Test-Path (Join-Path $build 'target.exe'))) {
    & gcc -O2 -std=gnu11 -Wall -mconsole (Join-Path $PSScriptRoot 'target.c') `
          -o (Join-Path $build 'target.exe') -luser32 -lgdi32
    if ($LASTEXITCODE -ne 0) { throw 'target.exe のビルドに失敗しました。' }
}

if (Test-Path $test) { Remove-Item $test -Recurse -Force }
New-Item -ItemType Directory -Path $test | Out-Null
Copy-Item (Join-Path $build 'mayous.exe') $test

# 利用者の設定に合わせる。RightThenSide1 だけ none。
# -Armed なしなら サイドボタン1 側には何も割り当てず(= 乗っ取らない)、
# -Armed ありなら サイドボタン1 を先に押す組み合わせを 1 つ持たせる。
$side1 = if ($Armed) { 'Side1ThenLeft=f16' } else { '' }
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
RightThenSide1=none
RightThenSide2=none
RightThenWheelUp=hwheel_right
RightThenWheelDown=hwheel_left
$side1
[Single]
Side1=passthru
Side2=passthru
[Exclude]
Processes=
"@ | Set-Content -Path (Join-Path $test 'mayous.ini') -Encoding ASCII

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class MX {
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
    public static void X1Down(){ One(0x0080,0,0,1); }
    public static void X1Up()  { One(0x0100,0,0,1); }
    public static void Move(int dx,int dy) { One(0x0001,dx,dy,0); }
}
'@

function W([int]$ms) { Start-Sleep -Milliseconds $ms }

$tgt = Start-Process (Join-Path $build 'target.exe') -ArgumentList "`"$log`"", '150' -PassThru
W 1200
$may = Start-Process (Join-Path $test 'mayous.exe') -PassThru
W 1500
[MX]::SetCursorPos(520, 420) | Out-Null
W 400

function Section([string]$name, [scriptblock]$body) {
    Write-Host ("  {0}" -f $name)
    W 900
    & $body
    W 700
    # カナリア: 素の単クリック。壊れていたら直前のシナリオが原因。
    [MX]::LDown(); W 60; [MX]::LUp(); W 350
    [MX]::RDown(); W 60; [MX]::RUp(); W 600
}

Write-Host ('検証開始 (サイドボタン1 に割り当て: {0})' -f $(if ($Armed) { 'あり' } else { 'なし' }))

Section 'N1  右保持 -> サイド1 クリック -> 右を離す' {
    [MX]::RDown(); W 120; [MX]::X1Down(); W 60; [MX]::X1Up(); W 150; [MX]::RUp()
}
Section 'N2  右保持 -> サイド1 押す -> 右を先に離す -> サイド1 離す' {
    [MX]::RDown(); W 120; [MX]::X1Down(); W 60; [MX]::RUp(); W 150; [MX]::X1Up()
}
Section 'N3  右保持 -> サイド1 クリック -> そのまま左クリック -> 右を離す' {
    [MX]::RDown(); W 120; [MX]::X1Down(); W 60; [MX]::X1Up(); W 200
    [MX]::LDown(); W 60; [MX]::LUp(); W 200; [MX]::RUp()
}
Section 'N4  右保持 -> サイド1 を3回クリック -> 右を離す' {
    [MX]::RDown(); W 120
    for ($i = 0; $i -lt 3; $i++) { [MX]::X1Down(); W 50; [MX]::X1Up(); W 150 }
    [MX]::RUp()
}
Section 'N5  右保持 -> サイド1 を3秒保持 -> 両方離す' {
    [MX]::RDown(); W 120; [MX]::X1Down(); W 3000; [MX]::X1Up(); W 150; [MX]::RUp()
}
Section 'N6  右保持 -> サイド1 押しながらドラッグ -> 両方離す' {
    [MX]::RDown(); W 120; [MX]::X1Down(); W 60
    [MX]::Move(50,0); W 80; [MX]::Move(50,0); W 80
    [MX]::X1Up(); W 150; [MX]::RUp(); [MX]::Move(-100,0)
}
Section 'N7  サイド1 単独クリック(右は押さない)' {
    [MX]::X1Down(); W 60; [MX]::X1Up()
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
Write-Host ''
if ($bad) { Write-Host ('  ** 不整合 {0} 件 **' -f $bad.Count) -ForegroundColor Red }
else      { Write-Host '  押下と離上の釣り合いは全区間で正常' -ForegroundColor Green }
