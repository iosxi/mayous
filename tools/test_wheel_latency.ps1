# test_wheel_latency.ps1 - 同時押しに割り当てたキーが、ホイールを回してから
#   何 ms 後に出てくるかを測る。
#
#   ホイールを送った時刻と、キーボードフックがその押下を見た時刻の差を取る
#   (build\wheellat.exe)。キーが実際に打ち込まれるので、無害な受け皿として
#   メモ帳を前面に置く。利用者の設定は触らない(一時フォルダへ複製して動かす)。
#
#     .\tools\test_wheel_latency.ps1
#     .\tools\test_wheel_latency.ps1 -Baseline build\latold   # 昔の exe と比べる
param([int]$N = 8, [int]$Gap = 500, [int]$KeyHoldMs = 120,
      [int]$RepressGapMs = 120, [string]$Baseline = '')

$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$exe   = Join-Path $build 'mayous.exe'
$lat   = Join-Path $build 'wheellat.exe'

if (-not (Test-Path $exe)) { throw 'build\mayous.exe がありません。build.bat を先に実行してください。' }
if (-not (Test-Path $lat)) {
    & gcc -O2 -std=gnu11 -Wall -mconsole (Join-Path $PSScriptRoot 'wheellat.c') -o $lat -luser32
    if ($LASTEXITCODE -ne 0) { throw 'wheellat.exe のビルドに失敗しました。' }
}
if (@(Get-Process mayous -ErrorAction SilentlyContinue).Count -gt 0) {
    throw 'mayous が既に起動しています。単一インスタンスなので、終了してから実行してください。'
}

Add-Type @'
using System; using System.Runtime.InteropServices;
public static class FG {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
}
'@

# ホイール上に a、下に b。利用者のテストケースそのまま。
function New-TestDir([string]$dir, [string]$src) {
    if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
    New-Item -ItemType Directory -Path $dir | Out-Null
    Copy-Item $src (Join-Path $dir 'mayous.exe')
    @"
[General]
Enabled=1
SuspendOnFullscreen=0
KeyHoldMs=$KeyHoldMs
RepressGapMs=$RepressGapMs
RightHoldTimeoutMs=0
[Chords]
RightThenWheelUp=a
RightThenWheelDown=b
[Single]
[Exclude]
Processes=
"@ | Set-Content -Path (Join-Path $dir 'mayous.ini') -Encoding ASCII
}

function Measure-Case([string]$dir, [string]$label, [string[]]$extra) {
    $np = Start-Process notepad -PassThru
    Start-Sleep -Milliseconds 1200
    [FG]::SetForegroundWindow($np.MainWindowHandle) | Out-Null
    $may = Start-Process (Join-Path $dir 'mayous.exe') -PassThru
    Start-Sleep -Milliseconds 1500
    [FG]::SetForegroundWindow($np.MainWindowHandle) | Out-Null
    Start-Sleep -Milliseconds 300

    Write-Host ("--- {0} ---" -f $label)
    & $lat @extra

    Start-Process (Join-Path $dir 'mayous.exe') -ArgumentList '--exit' -Wait
    Start-Sleep -Milliseconds 600
    if (-not $may.HasExited) { $may.Kill() }
    if (-not $np.HasExited)  { $np.Kill() }
    Start-Sleep -Milliseconds 400
}

$now = Join-Path $build 'wlat'
New-TestDir $now $exe

if ($Baseline -ne '') {
    $bexe = if (Test-Path (Join-Path $Baseline 'mayous.exe')) { Join-Path $Baseline 'mayous.exe' } else { $Baseline }
    $old  = Join-Path $build 'wlatold'
    New-TestDir $old $bexe
    Measure-Case $old '比較対象: 上下交互(a/b)' @("$N", "$Gap")
    Measure-Case $old '比較対象: 上だけ連続(a)' @("$N", "$Gap", '-same')
}

# 上下交互 = 押しているキーとは別のキー。間を空けずに入れ替わるので速いはず。
Measure-Case $now '今のビルド: 上下交互(a/b)' @("$N", "$Gap")
# 上だけ連続 = 同じキーの押し直し。RepressGapMs だけ間を空けるので、そのぶん遅れる。
Measure-Case $now ("今のビルド: 上だけ連続(a)  RepressGapMs=$RepressGapMs") @("$N", "$Gap", '-same')
