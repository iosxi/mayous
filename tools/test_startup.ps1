# test_startup.ps1 - スタートアップ登録がショートカット方式で動くか検証する
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'uilib.ps1')

$startup = [Environment]::GetFolderPath('Startup')
$lnk     = Join-Path $startup 'Mayous.lnk'
$runKey  = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'

function Show-State([string]$when) {
    $hasLnk = Test-Path $lnk
    $hasReg = $null -ne (Get-ItemProperty -Path $runKey -Name 'Mayous' -ErrorAction SilentlyContinue)
    Write-Host ("  {0,-16} ショートカット={1}  レジストリ Run={2}" -f $when, $hasLnk, $hasReg)
}

Write-Host ("スタートアップフォルダ: {0}" -f $startup)

# 旧方式が残っていた場合の移行も確かめたいので、わざとレジストリに置いてみる
Set-ItemProperty -Path $runKey -Name 'Mayous' -Value '"C:\dummy\mayous.exe"' -Force
Write-Host ''
Show-State '開始前'

$proc = Get-MayousProc
if (-not $proc) { throw 'mayous が起動していません。' }
$tray = Find-ProcWnd ([uint32]$proc.Id) 'MayousHiddenWnd'

Write-Host ''
Write-Host 'トレイメニューの [Windows 起動時に実行] を ON にします...'
[UI]::Post($tray, 0x0111, [IntPtr]1004, [IntPtr]0) | Out-Null   # IDM_STARTUP
Start-Sleep -Seconds 2
Show-State 'ON のあと'

if (Test-Path $lnk) {
    $sh = New-Object -ComObject WScript.Shell
    $s = $sh.CreateShortcut($lnk)
    Write-Host ("    ショートカット先: {0}" -f $s.TargetPath)
    Write-Host ("    作業フォルダ    : {0}" -f $s.WorkingDirectory)
    Write-Host ("    説明            : {0}" -f $s.Description)
}

Write-Host ''
Write-Host 'もう一度押して OFF にします...'
[UI]::Post($tray, 0x0111, [IntPtr]1004, [IntPtr]0) | Out-Null
Start-Sleep -Seconds 2
Show-State 'OFF のあと'

Remove-ItemProperty -Path $runKey -Name 'Mayous' -ErrorAction SilentlyContinue
Write-Host ''
Write-Host ("mayous 生存: {0}" -f ((Get-MayousProc) -ne $null))
