# uilib.ps1 - 他プロセスの GUI を叩くための共通部品
#
#   【重要】ポインタを引数に取るウィンドウメッセージ(TCM_GETITEMRECT など)を
#   プロセスをまたいで SendMessage してはいけない。渡したポインタは
#   こちら側のアドレス空間のものなので、相手プロセスがそこへ書き込もうとして
#   アクセス違反で落ちる。相手プロセス内に VirtualAllocEx で領域を確保し、
#   そのアドレスを渡してから ReadProcessMemory で読み戻すこと。

Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class UI {
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int w, int t, uint f);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
  [DllImport("user32.dll", EntryPoint="PostMessageW")] public static extern bool Post(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", SetLastError=true)] static extern uint SendInput(uint n, INPUT[] p, int cb);
  [DllImport("user32.dll")] static extern int GetSystemMetrics(int i);

  [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint a, bool inh, uint pid);
  [DllImport("kernel32.dll")] static extern IntPtr VirtualAllocEx(IntPtr h, IntPtr a, IntPtr sz, uint t, uint p);
  [DllImport("kernel32.dll")] static extern bool VirtualFreeEx(IntPtr h, IntPtr a, IntPtr sz, uint t);
  [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr h, IntPtr a, byte[] b, IntPtr n, out IntPtr rd);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);

  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X,Y; }
  [StructLayout(LayoutKind.Sequential)] struct MI { public int dx,dy; public uint data,flags,time; public IntPtr extra; }
  [StructLayout(LayoutKind.Sequential)] struct INPUT { public uint type; public MI mi; }
  public delegate bool EnumProc(IntPtr h, IntPtr p);

  // 相手プロセス内にバッファを確保して RECT を返させる
  public static RECT RemoteRectMessage(IntPtr hwnd, uint msg, int wparam) {
    uint pid; GetWindowThreadProcessId(hwnd, out pid);
    IntPtr hp = OpenProcess(0x0008 | 0x0010 | 0x0020 | 0x0400, false, pid); // VM_OP|VM_READ|VM_WRITE|QUERY_INFO
    RECT r = new RECT();
    if (hp == IntPtr.Zero) return r;
    IntPtr mem = VirtualAllocEx(hp, IntPtr.Zero, (IntPtr)16, 0x1000 | 0x2000, 0x04);
    if (mem != IntPtr.Zero) {
      SendMessage(hwnd, msg, (IntPtr)wparam, mem);
      byte[] buf = new byte[16];
      IntPtr rd;
      if (ReadProcessMemory(hp, mem, buf, (IntPtr)16, out rd)) {
        r.L = BitConverter.ToInt32(buf, 0);  r.T = BitConverter.ToInt32(buf, 4);
        r.R = BitConverter.ToInt32(buf, 8);  r.B = BitConverter.ToInt32(buf, 12);
      }
      VirtualFreeEx(hp, mem, IntPtr.Zero, 0x8000);
    }
    CloseHandle(hp);
    return r;
  }

  static void One(uint f){ INPUT[] a=new INPUT[1]; a[0].mi.flags=f; SendInput(1,a,Marshal.SizeOf(typeof(INPUT))); }
  static void Move(int x,int y){
    INPUT[] a=new INPUT[1]; a[0].mi.flags=0x8001;
    a[0].mi.dx=(int)(x*65535.0/(GetSystemMetrics(0)-1));
    a[0].mi.dy=(int)(y*65535.0/(GetSystemMetrics(1)-1));
    SendInput(1,a,Marshal.SizeOf(typeof(INPUT)));
  }
  // 人間と同じように近づいてから押す(ホットトラッキングも正しく起きる)
  public static void ClickAt(int x,int y){
    Move(x-40,y-30); System.Threading.Thread.Sleep(70);
    Move(x-12,y-8);  System.Threading.Thread.Sleep(70);
    Move(x,y);       System.Threading.Thread.Sleep(180);
    One(0x0002); System.Threading.Thread.Sleep(70); One(0x0004);
  }
}
'@

function Find-ProcWnd([uint32]$procId, [string]$cls) {
  $script:uiHit = [IntPtr]::Zero; $script:uiPid = $procId; $script:uiCls = $cls
  $cb = [UI+EnumProc]{ param($h,$p)
    $q = 0; [UI]::GetWindowThreadProcessId($h,[ref]$q) | Out-Null
    if ($q -eq $script:uiPid) {
      $sb = New-Object Text.StringBuilder 256; [UI]::GetClassNameW($h,$sb,256) | Out-Null
      if ($sb.ToString() -eq $script:uiCls) { $script:uiHit = $h; return $false } }
    return $true }
  [UI]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
  return $script:uiHit
}

function Get-MayousProc {
  Get-Process mayous -ErrorAction SilentlyContinue | Where-Object {
    (Get-CimInstance Win32_Process -Filter "ProcessId=$($_.Id)").CommandLine -notmatch 'wheel-agent'
  } | Select-Object -First 1
}

function Save-WndShot([IntPtr]$hwnd, [string]$path) {
  Add-Type -AssemblyName System.Drawing
  [UI]::SetWindowPos($hwnd, [IntPtr]0, 0,0,0,0, 0x0043) | Out-Null
  Start-Sleep -Milliseconds 400
  $r = New-Object UI+RECT
  if (-not [UI]::GetWindowRect($hwnd, [ref]$r)) { throw 'ウィンドウが消えました' }
  $w = $r.R - $r.L; $h = $r.B - $r.T
  if ($w -le 0 -or $h -le 0) { throw ('矩形が不正: {0}x{1}' -f $w,$h) }
  $bmp = New-Object System.Drawing.Bitmap -ArgumentList $w, $h
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  [UI]::PrintWindow($hwnd, $hdc, 2) | Out-Null
  $g.ReleaseHdc($hdc)
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  Write-Host ("  保存: {0}  ({1}x{2})" -f $path, $w, $h)
}
