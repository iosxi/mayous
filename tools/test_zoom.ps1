# test_zoom.ps1 - 「ズーム(Ctrl+ホイール)」の検証。
#
#   RightThenWheelUp=zoom_in / RightThenWheelDown=zoom_out の mayous を相手に、
#     0. 素のホイールはそのままアプリへ届く(Ctrl は付かない)
#     1. 右 + ホイール上 が Ctrl 付きの上ホイールとしてアプリに届くか
#     2. 右 + ホイール下 が Ctrl 付きの下ホイールとしてアプリに届くか
#     3. 終わったあと Ctrl が押しっぱなしで残っていないか
#     4. 注入からアプリに届くまでの遅れ(ms)
#   を build\zoomtest.exe で数字にする。
#
#   アプリが「Ctrl+ホイール」と見なすのは WM_MOUSEWHEEL の MK_CONTROL なので、
#   そのビットが立っているかを自前の最前面ウィンドウで数える。
#   利用者の設定は触らない(一時フォルダへ複製して動かす)。
#
#     .\tools\test_zoom.ps1

$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$exe   = Join-Path $build 'mayous.exe'
$tool  = Join-Path $build 'zoomtest.exe'
$src   = Join-Path $PSScriptRoot 'zoomtest.c'

if (-not (Test-Path $exe)) { throw 'build\mayous.exe がありません。build.bat を先に実行してください。' }
if (@(Get-Process mayous -ErrorAction SilentlyContinue).Count -gt 0) {
    throw 'mayous が既に起動しています。終了してから実行してください。'
}
if ((-not (Test-Path $tool)) -or ((Get-Item $src).LastWriteTime -gt (Get-Item $tool).LastWriteTime)) {
    & gcc -O2 -std=gnu11 -Wall -Wextra -mconsole $src -o $tool -luser32 -lgdi32
    if ($LASTEXITCODE -ne 0) { throw 'zoomtest.exe のビルドに失敗しました。' }
}

$dir = Join-Path $build 'zoomtest'
if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
New-Item -ItemType Directory -Path $dir | Out-Null
Copy-Item $exe $dir
@"
[General]
Enabled=1
SuspendOnFullscreen=0
[Chords]
RightThenLeft=none
RightThenMiddle=none
RightThenWheelUp=zoom_in
RightThenWheelDown=zoom_out
[Single]
MiddleAlone=none
Side1Alone=passthru
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
Write-Host '  OK: ズームは Ctrl 付きのホイールとしてアプリに届いています' -ForegroundColor Green
[Console]::OutputEncoding = $prev
