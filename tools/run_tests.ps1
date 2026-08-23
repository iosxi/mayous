# run_tests.ps1 - Mayous の実動作テスト
#
#   probe.exe を先に起動 (= 低レベルフック鎖の下流 = アプリ側の視点) してから
#   mayous.exe を起動し、SendInput で実際のマウス操作を注入して
#   「何が握り潰され、何が合成されたか」をログで検証する。
#
#   使い方: powershell -ExecutionPolicy Bypass -File tools\run_tests.ps1

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$test  = Join-Path $build 'test'
$log   = Join-Path $build 'probe.log'

if (-not (Test-Path (Join-Path $build 'mayous.exe'))) { throw 'build\mayous.exe がありません。先に build.bat を実行してください。' }

# 観測用プログラムは必要になったときにその場でビルドする
if (-not (Test-Path (Join-Path $build 'probe.exe'))) {
    Write-Host '[0] probe.exe をビルド...'
    & gcc -O2 -std=gnu11 -Wall -mconsole (Join-Path $PSScriptRoot 'probe.c') `
          -o (Join-Path $build 'probe.exe') -luser32
    if ($LASTEXITCODE -ne 0) { throw 'probe.exe のビルドに失敗しました。' }
}

# --- テスト専用の設定で mayous を用意する ----------------------------------
# 実害のないキー(F13/F14)に割り当てて、デスクトップを荒らさずに検証する。
if (Test-Path $test) { Remove-Item $test -Recurse -Force }
New-Item -ItemType Directory -Path $test | Out-Null
Copy-Item (Join-Path $build 'mayous.exe') $test

@"
[General]
Enabled=1
DragThreshold=8
LeftHoldTimeoutMs=200
RightHoldTimeoutMs=0
SuspendOnFullscreen=0
[Chords]
RightThenLeft=f14
RightThenMiddle=none
RightThenWheelUp=hwheel_right
RightThenWheelDown=hwheel_left
LeftThenRight=f13
LeftThenMiddle=none
LeftThenWheelUp=none
LeftThenWheelDown=none
[Exclude]
Processes=
"@ | Set-Content -Path (Join-Path $test 'mayous.ini') -Encoding ASCII

# --- SendInput ラッパ -------------------------------------------------------
if (-not ('Inj' -as [type])) {
Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class Inj {
    [StructLayout(LayoutKind.Sequential)]
    struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
    // x64 では sizeof(INPUT) == 40。余計なメンバを足すと SendInput が
    // ERROR_INVALID_PARAMETER で黙って何もしなくなる。
    [StructLayout(LayoutKind.Sequential)]
    struct INPUT { public uint type; public MOUSEINPUT mi; }

    [DllImport("user32.dll", SetLastError = true)]
    static extern uint SendInput(uint n, INPUT[] p, int cb);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);

    const uint MOVE = 0x0001, LDOWN = 0x0002, LUP = 0x0004, RDOWN = 0x0008, RUP = 0x0010;
    const uint MDOWN = 0x0020, MUP = 0x0040, WHEEL = 0x0800;

    static void One(uint flags, int dx, int dy, uint data) {
        INPUT[] a = new INPUT[1];
        a[0].type = 0;
        a[0].mi.dwFlags = flags;
        a[0].mi.dx = dx; a[0].mi.dy = dy;
        a[0].mi.mouseData = data;
        if (SendInput(1, a, Marshal.SizeOf(typeof(INPUT))) != 1)
            throw new Exception("SendInput failed: " + Marshal.GetLastWin32Error());
    }
    public static void RDown()  { One(RDOWN, 0, 0, 0); }
    public static void RUp()    { One(RUP,   0, 0, 0); }
    public static void LDown()  { One(LDOWN, 0, 0, 0); }
    public static void LUp()    { One(LUP,   0, 0, 0); }
    public static void MDown()  { One(MDOWN, 0, 0, 0); }
    public static void MUp()    { One(MUP,   0, 0, 0); }
    public static void Wheel(int d) { One(WHEEL, 0, 0, (uint)d); }
    public static void Move(int dx, int dy)  { One(MOVE, dx, dy, 0); }
}
'@
}

function Pause-Ms([int]$ms) { Start-Sleep -Milliseconds $ms }

# --- 起動順が肝: probe が先(下流)、mayous が後(上流) ------------------------
Write-Host '[1] probe を起動...'
$probe = Start-Process -FilePath (Join-Path $build 'probe.exe') `
                       -ArgumentList "`"$log`"", '40' -PassThru -WindowStyle Hidden
Pause-Ms 800

Write-Host '[2] mayous を起動...'
$may = Start-Process -FilePath (Join-Path $test 'mayous.exe') -PassThru
Pause-Ms 1500

Write-Host '[3] 入力を注入...'
# 実クリックが何かに当たると困るので、デスクトップを露出させてその上で操作する
$shell = New-Object -ComObject Shell.Application
$shell.MinimizeAll()
Pause-Ms 700
[Inj]::SetCursorPos(300, 300) | Out-Null
Pause-Ms 300

function Mark([string]$s) {
    # ログ上の区切りとして中クリックではなく、あえて何もせず時間で判別する
    Write-Host ("    - " + $s)
}

# T1: 単独の右クリック -> 離した瞬間に DOWN+UP が合成されるはず
Mark 'T1 単独右クリック'
[Inj]::RDown(); Pause-Ms 60; [Inj]::RUp(); Pause-Ms 400
[System.Windows.Forms.SendKeys]::SendWait('{ESC}')   # 出たかもしれない右クリックメニューを閉じる
Pause-Ms 400

# T2: 右押し + 左クリック -> F14。マウスイベントは1つも漏れないはず
Mark 'T2 右押し+左クリック'
[Inj]::RDown(); Pause-Ms 40; [Inj]::LDown(); Pause-Ms 30; [Inj]::LUp(); Pause-Ms 30; [Inj]::RUp()
Pause-Ms 400

# T3: 左押し + 右クリック -> F13
Mark 'T3 左押し+右クリック'
[Inj]::LDown(); Pause-Ms 40; [Inj]::RDown(); Pause-Ms 30; [Inj]::RUp(); Pause-Ms 30; [Inj]::LUp()
Pause-Ms 400

# T4: 右押し + ホイール -> 水平ホイール。縦ホイールは漏れないはず
Mark 'T4 右押し+ホイール'
[Inj]::RDown(); Pause-Ms 40; [Inj]::Wheel(120); Pause-Ms 60; [Inj]::Wheel(-120); Pause-Ms 60; [Inj]::RUp()
Pause-Ms 400

# T5: 右押し + ドラッグ -> 本物の押下に昇格するはず
Mark 'T5 右ドラッグ昇格'
[Inj]::RDown(); Pause-Ms 40
[Inj]::Move(40, 0); Pause-Ms 80; [Inj]::Move(40, 0); Pause-Ms 80
[Inj]::RUp(); Pause-Ms 400
[System.Windows.Forms.SendKeys]::SendWait('{ESC}')
Pause-Ms 400

# T6: 左の長押し -> LeftHoldTimeoutMs(200ms) で昇格するはず
Mark 'T6 左長押し昇格'
[Inj]::LDown(); Pause-Ms 600; [Inj]::LUp(); Pause-Ms 400

# T7: 中ボタン(未割り当て) -> 一切触られず素通しのはず
Mark 'T7 中クリック素通し'
[Inj]::MDown(); Pause-Ms 40; [Inj]::MUp(); Pause-Ms 400

Write-Host '[4] 後始末...'
$shell.UndoMinimizeALL()
Pause-Ms 300
Start-Process -FilePath (Join-Path $test 'mayous.exe') -ArgumentList '--exit' -Wait
Pause-Ms 500
if (-not $may.HasExited) { $may.Kill() }
if (-not $probe.HasExited) { Stop-Process -Id $probe.Id -Force }
Pause-Ms 500

Write-Host ''
Write-Host '===== probe.log ====='
Get-Content $log
