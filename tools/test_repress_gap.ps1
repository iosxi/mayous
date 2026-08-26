# test_repress_gap.ps1 - 設定画面の「同じキーの押し直し」ラジオボタンが
#   ini へ正しく書けるかを、実際にクリックして確かめる。
#   利用者の設定は触らない(一時フォルダへ複製して動かす)。
param([int]$Pick = 2)          # 0=120 1=80 2=40 3=20

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'uilib.ps1')

$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$dir   = Join-Path $build 'gaptest'
$want  = @(120, 80, 40, 20)[$Pick]

$IDC_GAP_BASE = 1140
$IDC_OK       = 1130
$BM_CLICK     = 0x00F5
$BM_GETCHECK  = 0x00F0
$WM_COMMAND   = 0x0111

if (@(Get-Process mayous -ErrorAction SilentlyContinue).Count -gt 0) {
    throw 'mayous が既に起動しています。単一インスタンスなので、終了してから実行してください。'
}
if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
New-Item -ItemType Directory -Path $dir | Out-Null
Copy-Item (Join-Path $build 'mayous.exe') $dir
$ini = Join-Path $dir 'mayous.ini'
@"
[General]
Enabled=1
SuspendOnFullscreen=0
KeyHoldMs=120
RepressGapMs=120
[Chords]
RightThenWheelUp=a
[Single]
[Exclude]
Processes=
"@ | Set-Content -Path $ini -Encoding ASCII

$may = Start-Process (Join-Path $dir 'mayous.exe') -PassThru
Start-Sleep -Milliseconds 1500

$tray = Find-ProcWnd ([uint32]$may.Id) 'MayousHiddenWnd'
if ($tray -eq [IntPtr]::Zero) { throw 'トレイ窓が見つかりません。' }
[UI]::Post($tray, $WM_COMMAND, [IntPtr]1000, [IntPtr]0) | Out-Null   # 設定を開く
Start-Sleep -Seconds 2
$sw = Find-ProcWnd ([uint32]$may.Id) 'MayousSettingsWnd'
if ($sw -eq [IntPtr]::Zero) { throw '設定ウィンドウが開きませんでした。' }

function Checked([int]$i) {
    $h = [UI]::GetDlgItem($sw, $IDC_GAP_BASE + $i)
    if ($h -eq [IntPtr]::Zero) { return '-' }
    if ([int][UI]::SendMessage($h, $BM_GETCHECK, [IntPtr]0, [IntPtr]0) -eq 1) { return 'o' } else { return '.' }
}

function GapRow { (0..3 | ForEach-Object { Checked $_ }) -join ' ' }
Write-Host ('  開いた直後 (ini は 120): ' + (GapRow))

$hit = [UI]::GetDlgItem($sw, $IDC_GAP_BASE + $Pick)
if ($hit -eq [IntPtr]::Zero) { throw ("ラジオ {0} が見つかりません。" -f $want) }
[UI]::SendMessage($hit, $BM_CLICK, [IntPtr]0, [IntPtr]0) | Out-Null
Start-Sleep -Milliseconds 400
Write-Host (("  {0} ms を押したあと:      " -f $want) + (GapRow))

[UI]::Post($sw, $WM_COMMAND, [IntPtr]$IDC_OK, [IntPtr]0) | Out-Null
Start-Sleep -Seconds 1

$line = (Get-Content $ini | Where-Object { $_ -match '^RepressGapMs=' })
Write-Host ("  ini: {0}" -f $line)

Start-Process (Join-Path $dir 'mayous.exe') -ArgumentList '--exit' -Wait
Start-Sleep -Milliseconds 600
if (-not $may.HasExited) { $may.Kill() }

if ($line -eq ("RepressGapMs=" + $want)) { Write-Host ("  OK: {0} ms が書けています" -f $want) -ForegroundColor Green }
else { Write-Host ("  NG: {0} ms を選んだのに {1}" -f $want, $line) -ForegroundColor Red; exit 1 }
