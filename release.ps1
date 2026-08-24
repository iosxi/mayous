# Mayous リリース準備スクリプト
#
#   .\release.ps1 -Version 1
#
# バージョン番号が入る箇所を一箇所でまとめて更新し、ビルドして配布用 zip を作る。
#
#   1. res\mayous.rc の FILEVERSION / PRODUCTVERSION / FileVersion / ProductVersion
#   2. src\common.h の MAYOUS_VERSION
#   3. build.bat でビルド
#   4. dist\mayous-vN.zip (mayous.exe + 利用者向け README.txt)
#   5. dist\mayous-vN.zip.sha256
#
# commit と tag は行わない(内容を確認してから人が実行する)。最後に手順を表示する。

param([Parameter(Mandatory = $true)][int]$Version)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

$tag     = "v$Version"
$zipName = "mayous-$tag.zip"
Write-Host "=== Mayous $tag のリリース準備 ===" -ForegroundColor Cyan

# --- 1. バージョン情報 (.rc) ---
# .rc は BOM 無し ASCII のまま保つこと(windres がコードページで振り回されるため)
$rcPath = Join-Path $PSScriptRoot 'res\mayous.rc'
$rc = [IO.File]::ReadAllText($rcPath)
$rc = [regex]::Replace($rc, 'FILEVERSION\s+\d+,\d+,\d+,\d+',    "FILEVERSION     $Version,0,0,0")
$rc = [regex]::Replace($rc, 'PRODUCTVERSION\s+\d+,\d+,\d+,\d+', "PRODUCTVERSION  $Version,0,0,0")
$rc = [regex]::Replace($rc, '"FileVersion",\s+"[^"]*"',    """FileVersion"",      ""$Version.0.0.0""")
$rc = [regex]::Replace($rc, '"ProductVersion",\s+"[^"]*"', """ProductVersion"",   ""$tag""")
[IO.File]::WriteAllText($rcPath, $rc, [Text.UTF8Encoding]::new($false))
Write-Host "1. res\mayous.rc -> $Version.0.0.0 / $tag" -ForegroundColor DarkGray

# --- 2. バージョン情報 (common.h) ---
$hPath = Join-Path $PSScriptRoot 'src\common.h'
$h = [IO.File]::ReadAllText($hPath)
$h = [regex]::Replace($h, '(#define\s+MAYOUS_VERSION\s+L")[^"]*(")', "`${1}$tag`${2}")
[IO.File]::WriteAllText($hPath, $h, [Text.UTF8Encoding]::new($false))
Write-Host "2. src\common.h  -> MAYOUS_VERSION = $tag" -ForegroundColor DarkGray

# --- 3. ビルド ---
Write-Host "3. ビルド中..." -ForegroundColor DarkGray
Get-Process mayous -ErrorAction SilentlyContinue | ForEach-Object {
    Write-Host "   常駐中の mayous を停止します" -ForegroundColor DarkGray
    try { Start-Process $_.Path -ArgumentList '--exit' -Wait } catch {}
}
Start-Sleep -Milliseconds 800
Get-Process mayous -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

# cmd の作業フォルダが PowerShell と一致しないことがあるので絶対パスで呼ぶ
& cmd /c ('"' + (Join-Path $PSScriptRoot 'build.bat') + '"') | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'ビルドに失敗しました。' }
$exe = Join-Path $PSScriptRoot 'build\mayous.exe'
if (-not (Test-Path $exe)) { throw 'build\mayous.exe が作られませんでした。' }
Write-Host ("   build\mayous.exe ({0:N0} bytes)" -f (Get-Item $exe).Length) -ForegroundColor DarkGray

# --- 4. 配布用 zip ---
# 中身は exe と README.txt だけ。開発用のスクリプトやソースは入れない。
$distDir = Join-Path $PSScriptRoot 'dist'
New-Item -ItemType Directory -Force -Path $distDir | Out-Null
Get-ChildItem $distDir -File | ForEach-Object { [IO.File]::Delete($_.FullName) }

$stage = Join-Path $env:TEMP "mayous-release-$tag"
if (Test-Path $stage) { [IO.Directory]::Delete($stage, $true) }
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item $exe $stage

# 利用者向け README。メモ帳で開かれる前提で CRLF + BOM 付き UTF-8。
$readme = @"
Mayous $tag
===========

マウスの同時押しをショートカットに変える、Windows のタスクトレイ常駐ツールです。


最初から入っている割り当て
--------------------------

  右クリックを押しながら左クリック     Windows キー
  左クリックを押しながら右クリック     Alt+Tab
  右クリックを押しながらホイール下     右へスクロール(上に回すと左へ)

すべて設定画面から変更できます。


使い方
------

1. mayous.exe をダブルクリックして起動します(インストール不要)。
   管理者権限は要求しません。UAC の確認は出ません。

2. タスクトレイにアイコンが出て常駐します。

3. トレイアイコンをダブルクリックすると設定画面が開きます。
   シングルクリック(左右どちらでも)でメニューが出ます。
   常駐中にもう一度 mayous.exe を起動しても設定画面が開きます。

4. 終了するときは、トレイアイコンをクリックして「終了」を選びます。

設定は exe と同じフォルダの mayous.ini に保存されます(必ずここで、別の場所へは
逃がしません)。専用のフォルダを 1 つ作ってそこに置いておくと、あとで片付けるのが
楽です。

設定画面は Windows のライト/ダーク設定に自動で合わせます。


割り当てられる組み合わせ
------------------------

先に押すボタンは 左・右・サイドボタン1・サイドボタン2 の 4 つ。
そこへ後から押すものとして 左・右・中・サイドボタン1・サイドボタン2・
ホイール上・ホイール下 の 7 つを組み合わせられます(計 24 通り)。

サイドボタンは単独クリックの動作も差し替えられます。

中ボタンは「後から押す側」専用です。


割り当ての決め方
----------------

設定画面のタブで「先に押すボタン」を選び、各行に機能を割り当てます。
指定方法は 2 通りあります。

  1. 一覧から選ぶ
     ウィンドウ操作・スクロール・ブラウザ/タブ・編集・メディアなど
     約 40 種類が用意してあります。

  2. キー入力を記録する
     [記録] ボタンを押して、割り当てたいキーを実際に押すだけです。
     続けて別のキーを押すと 2 ステップ目になり、順番に再生されます
     (例: Ctrl+C のあと Ctrl+V)。最大 8 ステップまで。

     記録中はキーボードが一時的に効かなくなります(Alt+Tab や Windows キーで
     Windows 側が反応しないようにするため)。マウスは常に効くので、
     [キャンセル] や [OK] はいつでも押せます。

[OK] または [適用] を押した時点ですぐ反映されます。再起動は要りません。


知っておいてほしいこと
----------------------

・通常の右クリックは「離した瞬間」に発火します。押した瞬間には、それが
  修飾キー役なのか通常のクリックなのか判別できないためです。素早く
  クリックする分には体感できませんが、原理上そうなっています。

・Alt+Tab は「左を押してから 200ms 以内に右」を押す必要があります。
  左ボタンは押しっぱなしにする用途が多いためです。設定画面の
  「左クリック 長押し」で変更できます。

・ゲームには使わないでください。アンチチートが低レベルフックを不正ツールと
  みなす可能性があります。既定で「フルスクリーンのアプリが前面のときは
  停止する」が ON になっていますが、ボーダーレスウィンドウのゲームなどは
  設定画面の除外リストに実行ファイル名を書いてください。

・管理者権限で動いているウィンドウ(タスクマネージャーなど)の上では効きません。
  Windows の保護機構によるものです。

・ほかのマウス常駐ソフトと併用しないでください。X-Mouse Button Control の
  ような同種のツールや、Logitech G HUB・Razer Synapse などのボタン割り当てと
  同時に使うと挙動が不安定になります。特にサイドボタンはこれらに奪われて
  いることが多いので、mayous で使う場合は先方の割り当てを外してください。

・マウスの左右ボタン入れ替え設定には未対応です。


自動起動
--------

Mayous 側では何もしません。必要であれば、スタートアップフォルダに
mayous.exe のショートカットを自分で置いてください。

    Win+R → shell:startup


同梱ファイル
------------

  mayous.exe     本体
  README.txt     このファイル


アンインストール
----------------

レジストリは一切使っていません。フォルダごと削除すれば終わりです。
スタートアップにショートカットを置いた場合は、それも削除してください。

なお Windows は、Mayous に限らずどんな exe に対しても「起動した」
「トレイアイコンを出した」といった記録をレジストリに残します。これは
Mayous が書いたものではなく、こちらから止めることはできません。


ソースコード
------------

https://github.com/iosxi/mayous
"@
$readme = $readme -replace "`r`n", "`n" -replace "`n", "`r`n"
[IO.File]::WriteAllText((Join-Path $stage 'README.txt'), $readme, [Text.UTF8Encoding]::new($true))

$zipPath = Join-Path $distDir $zipName
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zipPath -CompressionLevel Optimal
[IO.Directory]::Delete($stage, $true)

# --- 5. SHA-256 ---
$hash = (Get-FileHash $zipPath -Algorithm SHA256).Hash.ToLower()
"$hash  $zipName" | Set-Content -Path "$zipPath.sha256" -Encoding ascii
$kb = [math]::Round((Get-Item $zipPath).Length / 1024)
Write-Host "4. dist\$zipName ($kb KB)" -ForegroundColor DarkGray
Write-Host "5. sha256 = $hash" -ForegroundColor DarkGray

Write-Host ''
Write-Host '中身:' -ForegroundColor DarkGray
Add-Type -AssemblyName System.IO.Compression.FileSystem
$z = [IO.Compression.ZipFile]::OpenRead($zipPath)
$z.Entries | ForEach-Object { Write-Host ("   {0,-14} {1,10:N0} bytes" -f $_.Name, $_.Length) }
$z.Dispose()

Write-Host ''
Write-Host "準備できました。内容を確認してから次を実行してください:" -ForegroundColor Green
Write-Host "  git add -A" -ForegroundColor Yellow
Write-Host "  git commit -m '...'" -ForegroundColor Yellow
Write-Host "  git tag -a $tag -m 'Mayous $tag'" -ForegroundColor Yellow
Write-Host "  git push; git push origin $tag" -ForegroundColor Yellow
