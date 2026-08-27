# test_autoscroll.ps1 - オートスクロールと「中クリックの差し替え」の検証。
#
#   MiddleAlone=autoscroll / Side1Alone=click:middle の mayous を相手に、
#     1. 中クリックがアプリへ漏れないか
#     2. 移動がホイールに化け、カーソルが止まるか
#     3. もう一度クリックで抜け、移動が元に戻るか
#     4. サイドボタン1 が中クリックとしてアプリに届くか
#   を build\scrolltest.exe で数字にする。
#
#   クリックが余所へ飛ばないよう、専用の最前面ウィンドウの上で注入する。
#   利用者の設定は触らない(一時フォルダへ複製して動かす)。
#
#     .	ools	est_autoscroll.ps1

$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$exe   = Join-Path $build 'mayous.exe'
$tool  = Join-Path $build 'scrolltest.exe'
$src   = Join-Path $PSScriptRoot 'scrolltest.c'

if (-not (Test-Path $exe)) { throw 'build\mayous.exe がありません。build.bat を先に実行してください。' }
if (@(Get-Process mayous -ErrorAction SilentlyContinue).Count -gt 0) {
    throw 'mayous が既に起動しています。終了してから実行してください。'
}
if ((-not (Test-Path $tool)) -or ((Get-Item $src).LastWriteTime -gt (Get-Item $tool).LastWriteTime)) {
    & gcc -O2 -std=gnu11 -Wall -Wextra -mconsole $src -o $tool -luser32 -lgdi32
    if ($LASTEXITCODE -ne 0) { throw 'scrolltest.exe のビルドに失敗しました。' }
}

$dir = Join-Path $build 'scrolltest'
if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
New-Item -ItemType Directory -Path $dir | Out-Null
Copy-Item $exe $dir
@"
[General]
Enabled=1
SuspendOnFullscreen=0
AutoScrollSpeed=100
[Chords]
RightThenLeft=none
RightThenMiddle=none
RightThenWheelUp=none
RightThenWheelDown=none
[Single]
MiddleAlone=autoscroll
Side1Alone=click:middle
Side2Alone=passthru
[Exclude]
Processes=
"@ | Set-Content -Path (Join-Path $dir 'mayous.ini') -Encoding ASCII

$prev = [Console]::OutputEncoding
[Console]::OutputEncoding = [Text.Encoding]::UTF8
try {
    & $tool (Join-Path $dir 'mayous.exe')
    $code = $LASTEXITCODE
} finally {
    if ($code -eq 0) { }          # 出力の文字化けを避けるため、戻すのは最後
}

Start-Sleep -Milliseconds 800
Get-Process mayous -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

if ($code -eq 2) {
    Write-Host '  計測できませんでした。UAC やロック画面が出ていないか確かめてください。' -ForegroundColor Yellow
    exit 2
}
if ($code -ne 0) { Write-Host '  NG: 上の行を確認してください' -ForegroundColor Red; exit 1 }
Write-Host '  OK: オートスクロールも中クリックの差し替えも期待どおりです' -ForegroundColor Green
[Console]::OutputEncoding = $prev
