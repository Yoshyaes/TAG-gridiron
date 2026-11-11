# Films the game's own startup: the brand sting and the match loading screen are drawn by the engine's movie player,
# outside the game viewport, so the in-game screenshot path cannot see them. This grabs the game window from the
# desktop every 1.5 s (changed pictures only) for up to 7 min of a -TagPlayNow run (then the game runs on; -TagPauseTest also exits).
#   powershell -File Tools/Art/loadscreen_shots.ps1 [-Mode PauseTest|PlayNow] [-Out Saved\LoadShots]
param([string]$Mode = "PauseTest", [string]$Out = "")
$Proj = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if ($Out -eq "") { $Out = Join-Path $Proj "Saved\LoadShots" }
New-Item -ItemType Directory -Force $Out | Out-Null
Get-ChildItem $Out -Filter *.png | Remove-Item -Force
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public struct RECT { public int Left, Top, Right, Bottom; }
public static class Win { [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h); }
"@
$exe = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$args = @(("`"" + (Join-Path $Proj "TAGGridiron.uproject") + "`""), "-Tag$Mode", "-TagLoadSecs=14", "-game", "-windowed", "-ResX=1600", "-ResY=900", "-WinX=40", "-WinY=40", "-NoLiveCoding", "-log=tag_$($Mode.ToLower()).log")
$p = Start-Process -FilePath $exe -ArgumentList $args -PassThru
$t0 = Get-Date
$i = 0
$prev = ""
while (((Get-Date) - $t0).TotalSeconds -lt 420 -and -not $p.HasExited) {
    Start-Sleep -Milliseconds 1500
    $p.Refresh()
    $h = $p.MainWindowHandle
    if ($h -eq 0) { continue }
    $r = New-Object RECT
    [Win]::GetWindowRect($h, [ref]$r) | Out-Null
    $w = $r.Right - $r.Left; $hh = $r.Bottom - $r.Top
    if ($w -lt 200 -or $hh -lt 200) { continue }
    [Win]::SetForegroundWindow($h) | Out-Null
    $bmp = New-Object System.Drawing.Bitmap $w, $hh
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
    # only a changed picture is kept (a 6x4 sample of pixels tells the frames apart)
    $sig = ""
    foreach ($sy in 1..4) { foreach ($sx in 1..6) { $c = $bmp.GetPixel([int]($w * $sx / 7), [int]($hh * $sy / 5)); $sig += ("{0:X2}{1:X2}{2:X2}" -f $c.R, $c.G, $c.B) } }
    if ($sig -eq $prev) { $g.Dispose(); $bmp.Dispose(); continue }
    $prev = $sig
    $name = Join-Path $Out ("load_{0:D2}_{1:F1}s.png" -f $i, ((Get-Date) - $t0).TotalSeconds)
    $bmp.Save($name, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    $i++
}
if ($Mode -eq "PauseTest") { try { Wait-Process -Id $p.Id -Timeout 180 } catch {} }
Write-Output ("{0} desktop shots in {1}" -f $i, $Out)

