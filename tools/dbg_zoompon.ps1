# dbg_zoompon.ps1 - zoom-pon の窓と、直接キーを送ったときの反応を確かめる
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'uilib.ps1')
Add-Type @'
using System; using System.Runtime.InteropServices; using System.Text;
public static class ZD {
  [StructLayout(LayoutKind.Sequential)] struct KI { public ushort vk,scan; public uint flags,time; public IntPtr extra; public int pad; }
  [StructLayout(LayoutKind.Sequential)] struct IN { public uint type; public KI ki; }
  [DllImport("user32.dll", SetLastError=true)] static extern uint SendInput(uint n, IN[] p, int cb);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  static void Key(ushort vk, bool up){ IN[] a=new IN[1]; a[0].type=1; a[0].ki.vk=vk; a[0].ki.flags= up?2u:0u;
      SendInput(1,a,Marshal.SizeOf(typeof(IN))); }
  public static void Down(ushort vk){Key(vk,false);} public static void Up(ushort vk){Key(vk,true);}
}
'@
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$zdir  = Join-Path $build 'zp'
if (Test-Path $zdir) { Remove-Item $zdir -Recurse -Force }
New-Item -ItemType Directory -Path $zdir | Out-Null
Copy-Item 'c:\projects\zoom-pon\dist\zoom-pon.exe' $zdir
Copy-Item 'c:\projects\zoom-pon\dist\config.json'  $zdir

$zp = Start-Process (Join-Path $zdir 'zoom-pon.exe') -PassThru
Start-Sleep -Seconds 4
Write-Host ("zoom-pon 起動: {0}  PID={1}" -f (-not $zp.HasExited), $zp.Id)

$script:list = @(); $script:tp = [uint32]$zp.Id
$cb = [ZD+EnumProc]{ param($h,$p)
  $q=0; [ZD]::GetWindowThreadProcessId($h,[ref]$q)|Out-Null
  if ($q -eq $script:tp) { $sb=New-Object Text.StringBuilder 128; [ZD]::GetClassNameW($h,$sb,128)|Out-Null
    $script:list += ("{0,-16} vis={1}" -f $sb.ToString(), [ZD]::IsWindowVisible($h)) }
  return $true }
[ZD]::EnumWindows($cb,[IntPtr]::Zero)|Out-Null
Write-Host 'zoom-pon の窓:'
$script:list | ForEach-Object { Write-Host ('  ' + $_) }

function ZState { $h = Find-WndByClass 'ZoomPonHost'
  if ($h -eq [IntPtr]::Zero) { return 'ホスト窓なし' }
  return ([UI]::IsWindowVisible($h)) }

Write-Host ''
Write-Host ("キー送信前: {0}" -f (ZState))
Write-Host '"a" を 250ms 押す(mayous を介さず直接)'
[ZD]::Down(0x41); Start-Sleep -Milliseconds 250; [ZD]::Up(0x41)
Start-Sleep -Milliseconds 800
Write-Host ("送信後    : {0}" -f (ZState))
Start-Sleep -Milliseconds 500
Write-Host 'もう一度 "a" を 250ms 押す'
[ZD]::Down(0x41); Start-Sleep -Milliseconds 250; [ZD]::Up(0x41)
Start-Sleep -Milliseconds 800
Write-Host ("2回目後  : {0}" -f (ZState))

if (-not $zp.HasExited) { $zp.Kill() }
