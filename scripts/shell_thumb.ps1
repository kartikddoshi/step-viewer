# Asks the Windows Shell (same path as File Explorer) for a thumbnail of a file and saves it as PNG.
param([string]$File, [string]$Out, [int]$Size = 256)
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing @"
using System; using System.Runtime.InteropServices; using System.Drawing;
[ComImport, Guid("bcc18b79-ba16-442f-80c4-8a59c30c463b"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IShellItemImageFactory { [PreserveSig] int GetImage(SIZE size, int flags, out IntPtr phbm); }
[StructLayout(LayoutKind.Sequential)] public struct SIZE { public int cx, cy; }
public static class ShellThumb {
  [DllImport("shell32.dll", CharSet=CharSet.Unicode, PreserveSig=false)]
  static extern void SHCreateItemFromParsingName(string path, IntPtr pbc, [MarshalAs(UnmanagedType.LPStruct)] Guid riid, [MarshalAs(UnmanagedType.Interface)] out IShellItemImageFactory ppv);
  [DllImport("gdi32.dll")] static extern bool DeleteObject(IntPtr h);
  public static int Get(string path, string outPng, int size) {
    IShellItemImageFactory f;
    SHCreateItemFromParsingName(path, IntPtr.Zero, typeof(IShellItemImageFactory).GUID, out f);
    IntPtr hbm; SIZE s; s.cx = size; s.cy = size;
    int hr = f.GetImage(s, 0x08 /*SIIGBF_THUMBNAILONLY*/, out hbm);
    if (hr != 0) return hr;
    Bitmap b = Image.FromHbitmap(hbm); b.Save(outPng); DeleteObject(hbm); return 0;
  }
}
"@
$hr = [ShellThumb]::Get($File, $Out, $Size)
"hr=0x{0:X8}" -f $hr
