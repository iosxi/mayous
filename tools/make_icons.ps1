# make_icons.ps1 - res\mayous.ico / res\mayous_off.ico を生成する
# 外部素材に依存しないよう、アイコンはビルド前にこのスクリプトで作る。
# 使い方:  powershell -ExecutionPolicy Bypass -File tools\make_icons.ps1

Add-Type -AssemblyName System.Drawing

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$res  = Join-Path $root 'res'
if (-not (Test-Path $res)) { New-Item -ItemType Directory -Path $res | Out-Null }

function New-RoundedPath([single]$x, [single]$y, [single]$w, [single]$h, [single]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $p.AddArc($x,          $y,          $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y,          $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d,   0, 90)
    $p.AddArc($x,          $y + $h - $d, $d, $d,  90, 90)
    $p.CloseFigure()
    return $p
}

# マウス本体 + 右ボタンを白く塗った図案を 1 枚描く
function New-MouseBitmap([int]$size, [string]$bodyHex, [string]$lineHex, [int]$litAlpha) {
    $fmt = [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
    $bmp = New-Object System.Drawing.Bitmap -ArgumentList $size, $size, $fmt
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality

    $s = [single]$size
    $bx = $s * 0.22; $by = $s * 0.06
    $bw = $s * 0.56; $bh = $s * 0.88
    $r  = $bw * 0.46
    $band = $by + $bh * 0.40          # 上下ボタンの境界
    $cx   = $bx + $bw / 2

    $body = New-RoundedPath $bx $by $bw $bh $r
    $bodyColor = [System.Drawing.ColorTranslator]::FromHtml($bodyHex)
    $brush = New-Object System.Drawing.SolidBrush -ArgumentList $bodyColor
    $g.FillPath($brush, $body)

    # 右ボタンだけ明るく塗る = 「右ボタンが修飾キー」であることの記号
    $g.SetClip($body)
    $litColor = [System.Drawing.Color]::FromArgb($litAlpha, 255, 255, 255)
    $lit = New-Object System.Drawing.SolidBrush -ArgumentList $litColor
    $g.FillRectangle($lit, $cx, $by - 1, $bx + $bw - $cx + 1, $band - $by + 1)

    # 区切り線
    $lineColor = [System.Drawing.ColorTranslator]::FromHtml($lineHex)
    $penWidth  = [single]($s * 0.055)
    $pen = New-Object System.Drawing.Pen -ArgumentList $lineColor, $penWidth
    $g.DrawLine($pen, $bx, $band, $bx + $bw, $band)
    $g.DrawLine($pen, $cx, $by,   $cx,       $band)
    $g.ResetClip()

    $pen.Dispose(); $lit.Dispose(); $brush.Dispose(); $body.Dispose(); $g.Dispose()
    return $bmp
}

# PNG フレームを詰めた ICO を書き出す (Vista 以降が対応する PNG 内包形式)
function Save-Ico([System.Drawing.Bitmap[]]$bitmaps, [string]$path) {
    $pngs = @()
    foreach ($b in $bitmaps) {
        $m = New-Object System.IO.MemoryStream
        $b.Save($m, [System.Drawing.Imaging.ImageFormat]::Png)
        $pngs += , $m.ToArray()
        $m.Dispose()
    }

    $fs = [System.IO.File]::Create($path)
    $bw = New-Object System.IO.BinaryWriter -ArgumentList $fs
    $bw.Write([UInt16]0)                  # reserved
    $bw.Write([UInt16]1)                  # type = icon
    $bw.Write([UInt16]$bitmaps.Count)

    $offset = 6 + 16 * $bitmaps.Count
    for ($i = 0; $i -lt $bitmaps.Count; $i++) {
        $w = $bitmaps[$i].Width
        $h = $bitmaps[$i].Height
        $bw.Write([Byte]$(if ($w -ge 256) { 0 } else { $w }))
        $bw.Write([Byte]$(if ($h -ge 256) { 0 } else { $h }))
        $bw.Write([Byte]0)                # パレット色数
        $bw.Write([Byte]0)                # reserved
        $bw.Write([UInt16]1)              # planes
        $bw.Write([UInt16]32)             # bpp
        $bw.Write([UInt32]$pngs[$i].Length)
        $bw.Write([UInt32]$offset)
        $offset += $pngs[$i].Length
    }
    foreach ($p in $pngs) { $bw.Write($p) }
    $bw.Flush(); $bw.Close(); $fs.Close()
}

$sizes = @(16, 20, 24, 32, 48, 64, 128, 256)

$on  = @(); foreach ($s in $sizes) { $on  += (New-MouseBitmap $s '#3B82F6' '#1D4FBF' 235) }
$off = @(); foreach ($s in $sizes) { $off += (New-MouseBitmap $s '#8A9099' '#5B6169' 190) }

Save-Ico $on  (Join-Path $res 'mayous.ico')
Save-Ico $off (Join-Path $res 'mayous_off.ico')
foreach ($b in $on)  { $b.Dispose() }
foreach ($b in $off) { $b.Dispose() }

Write-Output ("generated: {0}\mayous.ico, {0}\mayous_off.ico" -f $res)
