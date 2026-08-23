# uninstall.ps1 - Mayous を完全に取り除く
#
#   Mayous 自身はレジストリに一切書き込まないが、Windows の側が
#   「exe を起動した」「トレイアイコンを出した」という記録を勝手に残す。
#   これはどんなアプリでも起きることで、Mayous が書いたものではない。
#   気になる場合のために、その痕跡もまとめて掃除できるようにしてある。
#
#   使い方:
#       powershell -ExecutionPolicy Bypass -File tools\uninstall.ps1
#       powershell -ExecutionPolicy Bypass -File tools\uninstall.ps1 -WhatIf   (確認だけ)

param([switch]$WhatIf)

$ErrorActionPreference = 'Continue'
$did = @()

function Do-It([string]$what, [scriptblock]$act) {
    if ($WhatIf) { Write-Host ("  [確認のみ] {0}" -f $what); return }
    try { & $act; $script:did += $what; Write-Host ("  済: {0}" -f $what) }
    catch { Write-Host ("  失敗: {0} ({1})" -f $what, $_.Exception.Message) }
}

Write-Host '=== 1. 常駐を止める ==='
# 強制終了(Kill)すると Windows の互換性アシスタントが「異常終了」として
# レジストリに記録を残してしまう。まず --exit で正常に終わらせること。
$procs = @(Get-Process mayous -ErrorAction SilentlyContinue)
if ($procs.Count -gt 0) {
    $exe = $null
    foreach ($p in $procs) { try { if ($p.Path) { $exe = $p.Path } } catch {} }
    if ($exe -and -not $WhatIf) {
        Write-Host ('  --exit で終了を要求: {0}' -f $exe)
        Start-Process $exe -ArgumentList '--exit' -Wait
        Start-Sleep -Seconds 2
    }
    $left = @(Get-Process mayous -ErrorAction SilentlyContinue)
    if ($left.Count -gt 0) {
        foreach ($p in $left) {
            Do-It ("応答しないので強制終了 PID {0}" -f $p.Id) { $p.Kill() }
        }
        Start-Sleep -Seconds 2
    } else {
        Write-Host '  正常に終了しました'
        $did += 'mayous 終了'
    }
} else { Write-Host '  動いていません' }

Write-Host ''
Write-Host '=== 2. スタートアップのショートカット ==='
$lnk = Join-Path ([Environment]::GetFolderPath('Startup')) 'Mayous.lnk'
if (Test-Path $lnk) { Do-It ("削除 {0}" -f $lnk) { Remove-Item $lnk -Force } }
else { Write-Host '  ありません' }

Write-Host ''
Write-Host '=== 3. 設定ファイル ==='
$appdataIni = Join-Path $env:APPDATA 'Mayous'
if (Test-Path $appdataIni) { Do-It ("削除 {0}" -f $appdataIni) { Remove-Item $appdataIni -Recurse -Force } }
else { Write-Host ('  %APPDATA%\Mayous はありません (exe と同じ場所の mayous.ini は手で消してください)') }

Write-Host ''
Write-Host '=== 4. Windows が勝手に残した痕跡 (Mayous が書いたものではない) ==='

# MuiCache: exe を起動すると Windows がバージョン情報を勝手にキャッシュする
$mui = 'HKCU:\Software\Classes\Local Settings\Software\Microsoft\Windows\Shell\MuiCache'
$p = Get-ItemProperty -Path $mui -ErrorAction SilentlyContinue
if ($p) {
    foreach ($v in $p.PSObject.Properties) {
        if ($v.Name -like 'PS*') { continue }
        if ($v.Name -match 'mayous|tabtest|\\target\.exe') {
            Do-It ("MuiCache から削除: {0}" -f $v.Name) { Remove-ItemProperty -Path $mui -Name $v.Name -Force }
        }
    }
}

# トレイアイコンを出したアプリに Windows が振る AppUserModelId
Get-ChildItem 'HKCU:\Software\Classes\AppUserModelId' -ErrorAction SilentlyContinue |
  Where-Object { $_.PSChildName -like 'NotifyIconGeneratedAumid_*' } |
  ForEach-Object {
    $dn = (Get-ItemProperty -Path $_.PSPath -Name DisplayName -ErrorAction SilentlyContinue).DisplayName
    if ($dn -like '*Mayous*') {
        $path = $_.PSPath
        Do-It ("AppUserModelId 削除: {0}" -f $_.PSChildName) { Remove-Item -Path $path -Recurse -Force }
    }
  }

# 落ちたアプリを Windows の互換性アシスタントが記録したもの
$compat = 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Compatibility Assistant\Store'
$p = Get-ItemProperty -Path $compat -ErrorAction SilentlyContinue
if ($p) {
    foreach ($v in $p.PSObject.Properties) {
        if ($v.Name -like 'PS*') { continue }
        if ($v.Name -match 'mayous|tabtest') {
            Do-It ("互換性アシスタントの記録を削除: {0}" -f $v.Name) { Remove-ItemProperty -Path $compat -Name $v.Name -Force }
        }
    }
}

# アプリ切替の使用統計
$fu = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\FeatureUsage\AppSwitched'
$p = Get-ItemProperty -Path $fu -ErrorAction SilentlyContinue
if ($p) {
    foreach ($v in $p.PSObject.Properties) {
        if ($v.Name -like 'PS*') { continue }
        if ($v.Name -match 'mayous|tabtest') {
            Do-It ("AppSwitched から削除: {0}" -f $v.Name) { Remove-ItemProperty -Path $fu -Name $v.Name -Force }
        }
    }
}

Write-Host ''
Write-Host '=== 5. 確認 ==='
$rest = & reg query HKCU\Software /f Mayous /s 2>$null | Where-Object { $_ -match 'ayous' }
if ($rest) {
    Write-Host '  まだ残っているもの:'
    $rest | ForEach-Object { Write-Host ('    ' + $_.Trim()) }
    Write-Host '  (TortoiseGit の履歴など、Mayous と無関係のものが含まれる場合があります)'
} else {
    Write-Host '  レジストリに Mayous の痕跡はありません'
}
Write-Host ''
Write-Host ("実行した件数: {0}" -f $did.Count)
if ($WhatIf) { Write-Host '(-WhatIf 指定のため実際には何も変更していません)' }
