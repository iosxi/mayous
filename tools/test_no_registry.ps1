# test_no_registry.ps1 - mayous がレジストリを汚さないことを実測で確かめる
#
#   起動 -> 設定を開く -> 自動起動 ON -> OFF -> 終了 という一通りの操作を行い、
#   その前後で HKCU のスナップショットを比較する。
#
#   使い方: powershell -ExecutionPolicy Bypass -File tools\test_no_registry.ps1

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'uilib.ps1')

$build = Join-Path (Split-Path -Parent $PSScriptRoot) 'build'
$dist  = Join-Path $build 'dist'

function Get-Snapshot {
    $snap = @{}
    foreach ($k in @(
        'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run',
        'HKCU:\Software\Microsoft\Windows\CurrentVersion\RunOnce',
        'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run')) {
        $p = Get-ItemProperty -Path $k -ErrorAction SilentlyContinue
        if ($p) {
            foreach ($v in $p.PSObject.Properties) {
                if ($v.Name -like 'PS*') { continue }
                $snap["$k::$($v.Name)"] = ($v.Value -join ',')
            }
        }
    }
    # Mayous 名義のキーが増えていないか
    foreach ($root in 'HKCU:\Software','HKCU:\Software\Classes') {
        Get-ChildItem $root -ErrorAction SilentlyContinue |
            Where-Object { $_.PSChildName -like '*ayous*' } |
            ForEach-Object { $snap["KEY::$($_.Name)"] = 'exists' }
    }
    return $snap
}

function Compare-Snapshot($before, $after) {
    $diff = @()
    foreach ($k in $after.Keys) {
        if (-not $before.ContainsKey($k))      { $diff += "  + 追加: $k = $($after[$k])" }
        elseif ($before[$k] -ne $after[$k])    { $diff += "  ~ 変更: $k : '$($before[$k])' -> '$($after[$k])'" }
    }
    foreach ($k in $before.Keys) {
        if (-not $after.ContainsKey($k))       { $diff += "  - 削除: $k = $($before[$k])" }
    }
    return $diff
}

if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory $dist | Out-Null
Copy-Item (Join-Path $build 'mayous.exe') $dist

Write-Host '前のスナップショットを取得...'
$before = Get-Snapshot
Write-Host ("  監視対象 {0} 項目" -f $before.Count)

Write-Host ''
Write-Host 'mayous を起動 -> 設定を開く -> 自動起動 ON -> OFF -> 終了'
Start-Process (Join-Path $dist 'mayous.exe') | Out-Null
Start-Sleep -Seconds 3

$proc = Get-MayousProc
if (-not $proc) { throw 'mayous が起動しませんでした。' }
$tray = Find-ProcWnd ([uint32]$proc.Id) 'MayousHiddenWnd'

[UI]::Post($tray, 0x0111, [IntPtr]1000, [IntPtr]0) | Out-Null   # 設定を開く
Start-Sleep -Seconds 2
$sw = Find-ProcWnd ([uint32]$proc.Id) 'MayousSettingsWnd'
Write-Host ("  設定ウィンドウ: {0}" -f $(if ($sw -ne [IntPtr]::Zero) { '開いた' } else { '開かず' }))
if ($sw -ne [IntPtr]::Zero) { [UI]::Post($sw, 0x0111, [IntPtr]1131, [IntPtr]0) | Out-Null }  # キャンセル
Start-Sleep -Milliseconds 800

$lnk = Join-Path ([Environment]::GetFolderPath('Startup')) 'Mayous.lnk'
[UI]::Post($tray, 0x0111, [IntPtr]1004, [IntPtr]0) | Out-Null   # 自動起動 ON
Start-Sleep -Seconds 2
Write-Host ("  ON  -> ショートカット存在: {0}" -f (Test-Path $lnk))
[UI]::Post($tray, 0x0111, [IntPtr]1004, [IntPtr]0) | Out-Null   # 自動起動 OFF
Start-Sleep -Seconds 2
Write-Host ("  OFF -> ショートカット存在: {0}" -f (Test-Path $lnk))

Start-Process (Join-Path $dist 'mayous.exe') -ArgumentList '--exit' -Wait
Start-Sleep -Seconds 2
Get-Process mayous -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500

Write-Host ''
Write-Host '後のスナップショットを取得...'
$after = Get-Snapshot
$diff = Compare-Snapshot $before $after

Write-Host ''
if ($diff.Count -eq 0) {
    Write-Host '  レジストリの変化: なし' -ForegroundColor Green
} else {
    Write-Host '  レジストリに変化あり:' -ForegroundColor Yellow
    $diff | ForEach-Object { Write-Host $_ }
}

Write-Host ''
Write-Host '=== 参考: Mayous という名前がレジストリに残っていないか ==='
$hits = & reg query HKCU\Software /f Mayous /s 2>$null | Where-Object { $_ -match 'Mayous' }
if ($hits) { $hits | ForEach-Object { Write-Host ('    ' + $_) } } else { Write-Host '    なし' }
