# test_appcmd.ps1 - appcmd:back がキーを経由せずに届くことを確かめる
#
#   矢印キーを握っているページで Alt+Left が効かない、という問題への答えが
#   WM_APPCOMMAND である。ここで確かめるのは 2 つ。
#     (1) 同時押しから WM_APPCOMMAND が前面ウィンドウへ届くこと
#     (2) そのときキーが 1 つも流れないこと (= ページ側に食われようがない)
$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$test  = Join-Path $build 'test_appcmd'
$tlog  = Join-Path $build 'appcmd_target.log'
$plog  = Join-Path $build 'appcmd_probe.log'

foreach ($t in 'probe', 'target') {
    & gcc -O2 -std=gnu11 -Wall -mconsole (Join-Path $PSScriptRoot "$t.c") `
          -o (Join-Path $build "$t.exe") -luser32 -lgdi32
    if ($LASTEXITCODE -ne 0) { throw "$t.c のビルドに失敗" }
}

if (Test-Path $test) { Remove-Item $test -Recurse -Force }
New-Item -ItemType Directory -Path $test | Out-Null
Copy-Item (Join-Path $build 'mayous.exe') $test

@"
[General]
Enabled=1
SuspendOnFullscreen=0
[Chords]
RightThenLeft=appcmd:back
RightThenRight=none
RightThenMiddle=appcmd:refresh
Side1ThenLeft=appcmd:forward
[Single]
Side1Alone=none
[Exclude]
Processes=
"@ | Set-Content -Path (Join-Path $test 'mayous.ini') -Encoding ASCII

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class S2 {
    [StructLayout(LayoutKind.Sequential)]
    struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)]
    struct INPUT { public uint type; public MOUSEINPUT mi; }
    [DllImport("user32.dll", SetLastError = true)]
    static extern uint SendInput(uint n, INPUT[] p, int cb);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    static void One(uint flags, uint data) {
        INPUT[] a = new INPUT[1];
        a[0].mi.dwFlags = flags; a[0].mi.mouseData = data;
        if (SendInput(1, a, Marshal.SizeOf(typeof(INPUT))) != 1)
            throw new Exception("SendInput failed " + Marshal.GetLastWin32Error());
    }
    public static void LDown(){One(0x0002,0);}  public static void LUp(){One(0x0004,0);}
    public static void RDown(){One(0x0008,0);}  public static void RUp(){One(0x0010,0);}
    public static void MDown(){One(0x0020,0);}  public static void MUp(){One(0x0040,0);}
    public static void X1Down(){One(0x0080,1);} public static void X1Up(){One(0x0100,1);}
    /* WM_APPCOMMAND の宛先は「前面ウィンドウ」なので、測る前に的を前面へ出す。
       前面を譲る側(この PowerShell)から呼ぶぶんには Windows も許してくれる。 */
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
}
'@
function W([int]$ms) { Start-Sleep -Milliseconds $ms }

$tgt   = Start-Process (Join-Path $build 'target.exe') -ArgumentList "`"$tlog`"", '60' -PassThru
W 1200
$probe = Start-Process (Join-Path $build 'probe.exe') -ArgumentList "`"$plog`"", '60' -PassThru -WindowStyle Hidden
W 800
$may   = Start-Process (Join-Path $test 'mayous.exe') -PassThru
W 1500
[S2]::SetCursorPos(520, 420) | Out-Null
$tgt.Refresh()
[S2]::SetForegroundWindow($tgt.MainWindowHandle) | Out-Null
W 500
if ([S2]::GetForegroundWindow() -ne $tgt.MainWindowHandle) {
    Write-Host '  (注意) target を前面にできなかった。結果は当てにならない。'
}

Write-Host '  T1 右クリック + 左クリック   -> APPCOMMAND cmd=1 (戻る)'
[S2]::RDown(); W 80; [S2]::LDown(); W 60; [S2]::LUp(); W 120; [S2]::RUp(); W 700

Write-Host '  T2 サイド1 + 左クリック      -> APPCOMMAND cmd=2 (進む)'
[S2]::X1Down(); W 80; [S2]::LDown(); W 60; [S2]::LUp(); W 120; [S2]::X1Up(); W 700

Write-Host '  T3 右クリック + 中クリック   -> APPCOMMAND cmd=3 (更新)'
[S2]::RDown(); W 80; [S2]::MDown(); W 60; [S2]::MUp(); W 120; [S2]::RUp(); W 700

Start-Process (Join-Path $test 'mayous.exe') -ArgumentList '--exit' -Wait
W 600
if (-not $may.HasExited)   { $may.Kill() }
if (-not $probe.HasExited) { Stop-Process -Id $probe.Id -Force }
W 300
if (-not $tgt.HasExited)   { $tgt.CloseMainWindow() | Out-Null; W 700 }
if (-not $tgt.HasExited)   { $tgt.Kill() }
W 400

$tl = Get-Content $tlog
$pl = Get-Content $plog

Write-Host ''
Write-Host '===== アプリが受け取ったもの ====='
$tl | ForEach-Object { Write-Host ('  ' + $_) }
Write-Host ''
Write-Host '===== 流れたキー ====='
$keys = $pl | Where-Object { $_ -cmatch 'KEY\s+\S+\s+(DOWN|UP)' }
if ($keys) { $keys | ForEach-Object { Write-Host ('  ' + $_) } }
else       { Write-Host '  (なし)' }

$ok = $true
function Check($cond, $msg) {
    if ($cond) { Write-Host "  OK  $msg" }
    else       { Write-Host "  NG  $msg" -ForegroundColor Red; $script:ok = $false }
}
Write-Host ''
Write-Host '===== 判定 ====='
Check ([bool]($tl -match 'APPCOMMAND cmd=1')) '戻る が WM_APPCOMMAND として届いた'
Check ([bool]($tl -match 'APPCOMMAND cmd=2')) '進む が WM_APPCOMMAND として届いた'
Check ([bool]($tl -match 'APPCOMMAND cmd=3')) '更新 が WM_APPCOMMAND として届いた'
Check (-not $keys)                            'キーは 1 つも流れていない'

if ($ok) { Write-Host ''; Write-Host '全部通った。' }
else     { Write-Host ''; Write-Host '失敗あり。'; exit 1 }
