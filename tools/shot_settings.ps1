# shot_settings.ps1 - 設定ウィンドウを開き、指定タブに切り替えてキャプチャする(目視確認用)
#   使い方: powershell -ExecutionPolicy Bypass -File tools\shot_settings.ps1 [-TabIndex 2]
param([int]$TabIndex = 0, [string]$Out = 'settings.png')

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'uilib.ps1')

$proc = Get-MayousProc
if (-not $proc) { throw 'mayous が起動していません。' }

$sw = Find-ProcWnd ([uint32]$proc.Id) 'MayousSettingsWnd'
if ($sw -eq [IntPtr]::Zero) {
    $tray = Find-ProcWnd ([uint32]$proc.Id) 'MayousHiddenWnd'
    if ($tray -eq [IntPtr]::Zero) { throw 'mayous のウィンドウが見つかりません。' }
    [UI]::Post($tray, 0x0111, [IntPtr]1000, [IntPtr]0) | Out-Null   # WM_COMMAND / 設定を開く
    Start-Sleep -Seconds 2
    $sw = Find-ProcWnd ([uint32]$proc.Id) 'MayousSettingsWnd'
    if ($sw -eq [IntPtr]::Zero) { throw '設定ウィンドウが開きませんでした。' }
}

[UI]::SetWindowPos($sw, [IntPtr]0, 0,0,0,0, 0x0043) | Out-Null
[UI]::SetForegroundWindow($sw) | Out-Null
Start-Sleep -Milliseconds 500

if ($TabIndex -gt 0) {
    $tab = [UI]::GetDlgItem($sw, 900)
    # TCM_GETITEMRECT は相手プロセス内のバッファ経由で取得する(uilib.ps1 の注意書き参照)
    $tr = [UI]::RemoteRectMessage($tab, 0x130A, $TabIndex)
    $pt = New-Object UI+POINT
    $pt.X = [int](($tr.L + $tr.R) / 2); $pt.Y = [int](($tr.T + $tr.B) / 2)
    [UI]::ClientToScreen($tab, [ref]$pt) | Out-Null
    # 1回目のクリックはウィンドウのアクティブ化に消費されることがあるので、
    # 実際に選択が変わるまで押す
    for ($try = 0; $try -lt 3; $try++) {
        [UI]::ClickAt($pt.X, $pt.Y)
        Start-Sleep -Milliseconds 600
        if ([UI]::SendMessage($tab, 0x130B, [IntPtr]0, [IntPtr]0) -eq $TabIndex) { break }
    }
    Write-Host ("  タブ {0} を選択 (現在={1})" -f $TabIndex,
                [UI]::SendMessage($tab, 0x130B, [IntPtr]0, [IntPtr]0))
}

if (-not [UI]::IsWindow($sw)) { throw '設定ウィンドウが消えました。' }
Save-WndShot $sw (Join-Path (Split-Path -Parent $PSScriptRoot) ('build\' + $Out))
Write-Host ("  mayous 生存: {0}" -f ((Get-MayousProc) -ne $null))
