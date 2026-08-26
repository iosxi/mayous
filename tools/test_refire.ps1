# test_refire.ps1 - 「右を離さずに左クリックを繰り返す」を調べる
#
#   利用者からの報告:
#     3. 右+左 で F13 が出て zoom-pon が反応する
#     4. 右を離さずにもう一度左クリック -> 反応しない
#     5. 何度か押すと反応することもある。その後 右を離して左クリックすると
#        左クリック単体が効かなくなる(しばらくすると直る)
#
#   2 つに分けて測る。
#     A: F13 が何回「離してから押し直された」ように見えるか(1ms 間隔の観測)
#     B: 右を離したあとの左クリックが本当にアプリへ届くか(target.exe)
$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$test  = Join-Path $build 'refire'
$tlog  = Join-Path $build 'refire_target.log'
$klog  = Join-Path $build 'refire_key.txt'

if (-not (Test-Path (Join-Path $build 'target.exe'))) {
    & gcc -O2 -std=gnu11 -Wall -mconsole (Join-Path $PSScriptRoot 'target.c') `
          -o (Join-Path $build 'target.exe') -luser32 -lgdi32
    if ($LASTEXITCODE -ne 0) { throw 'target.exe のビルドに失敗しました。' }
}

if (Test-Path $test) { Remove-Item $test -Recurse -Force }
New-Item -ItemType Directory -Path $test | Out-Null
Copy-Item (Join-Path $build 'mayous.exe') $test
@"
[General]
Enabled=1
KeyHoldMs=120
RightHoldTimeoutMs=0
SuspendOnFullscreen=0
[Chords]
RightThenLeft=f13
[Exclude]
Processes=
"@ | Set-Content -Path (Join-Path $test 'mayous.ini') -Encoding ASCII

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class MF {
    [StructLayout(LayoutKind.Sequential)]
    struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)]
    struct INPUT { public uint type; public MOUSEINPUT mi; }
    [DllImport("user32.dll", SetLastError = true)] static extern uint SendInput(uint n, INPUT[] p, int cb);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    static void One(uint f){ INPUT[] a = new INPUT[1]; a[0].mi.dwFlags = f; SendInput(1, a, Marshal.SizeOf(typeof(INPUT))); }
    public static void LDown(){ One(0x0002); } public static void LUp(){ One(0x0004); }
    public static void RDown(){ One(0x0008); } public static void RUp(){ One(0x0010); }
}
'@

function W([int]$ms) { Start-Sleep -Milliseconds $ms }

$tgt = Start-Process (Join-Path $build 'target.exe') -ArgumentList "`"$tlog`"", '25' -PassThru
W 1200
$may = Start-Process (Join-Path $test 'mayous.exe') -PassThru
W 1800
if ($may.HasExited) { throw 'mayous が起動直後に終了した(多重起動の可能性)' }

# F13 = VK 0x7C。1ms 間隔で見るので、短い離上でも取りこぼさない。
$kp = Start-Process python -ArgumentList @((Join-Path $PSScriptRoot 'keydur.py'), '14', '7C') `
        -NoNewWindow -PassThru -RedirectStandardOutput $klog
W 2000
[MF]::SetCursorPos(520, 420) | Out-Null
W 400

Write-Host '右を押したまま、左クリックを 4 回'
[MF]::RDown(); W 200
for ($i = 0; $i -lt 4; $i++) { [MF]::LDown(); W 70; [MF]::LUp(); W 700 }
W 400
Write-Host '右を離す'
[MF]::RUp()
W 1200

Write-Host 'そのあと素の左クリックを 3 回。アプリに届くか？'
for ($i = 0; $i -lt 3; $i++) { [MF]::LDown(); W 70; [MF]::LUp(); W 500 }
W 800

$kp.WaitForExit()
Start-Process (Join-Path $test 'mayous.exe') -ArgumentList '--exit' -Wait
W 600
if (-not $may.HasExited) { $may.Kill() }
W 300
if (-not $tgt.HasExited) { $tgt.CloseMainWindow() | Out-Null; W 800 }
if (-not $tgt.HasExited) { $tgt.Kill() }
W 300

Write-Host ''
Write-Host '===== A. F13 の見え方 (1ms 間隔で観測) ====='
Get-Content $klog | ForEach-Object { Write-Host ('  ' + $_) }
$presses = ((Get-Content $klog) -match 'PRESSES=(\d+)')
$n = if ("$presses" -match 'PRESSES=(\d+)') { [int]$Matches[1] } else { -1 }
Write-Host ("  左クリック 4 回に対して、押し直しに見えた回数: {0}" -f $n)
if ($n -ge 4) { Write-Host '  4 回とも別々の押下として観測できる' -ForegroundColor Green }
else          { Write-Host '  ** 押し直しが見えていない。押しっぱなしのままに見える **' -ForegroundColor Red }

Write-Host ''
Write-Host '===== B. アプリが受け取ったマウス ====='
$lines = Get-Content $tlog
$lines | ForEach-Object { Write-Host ('  ' + $_) }
# 同時押しに使われた左クリックは握り潰されるので、ログに残る LEFT DOWN は
# 右を離したあとのものだけになる(右の押下も離上も CONSUMED で出てこない)。
$after = ($lines | Where-Object { $_ -match 'LEFT\s+DOWN' }).Count
Write-Host ''
Write-Host ("  右を離したあとに届いた左クリックの押下: {0} / 3" -f $after)
if ($after -ge 3) { Write-Host '  左クリックは生きている' -ForegroundColor Green }
else              { Write-Host '  ** 左クリックが飲み込まれている **' -ForegroundColor Red }
