# A/B a list of cvar sets against the steady state (small pack budget so streaming is done before the window).
#   powershell -File Tools\Perf\ab.ps1 -Sets "base=","nort=r.RayTracing.Geometry.SkeletalMeshes=0" [-Secs 30] [-Warm 40] [-Extra ""]
# Each set is name=cvar=value,cvar=value (passed as -dpcvars). Prints one summary line per set.
param([string[]]$Sets = @("base="), [int]$Secs = 30, [int]$Warm = 40, [string]$Extra = "-TagPackMB=50", [int]$ResX = 1920, [int]$ResY = 1080)
if ($Sets.Count -eq 1 -and $Sets[0].Contains(';')) { $Sets = $Sets[0] -split ';' }
foreach ($s in $Sets) {
  $i = $s.IndexOf('=')
  $name = $s.Substring(0, $i); $cv = $s.Substring($i + 1)
  $x = $Extra
  if ($cv) { $x = "$x -dpcvars=$cv" }
  $line = & (Join-Path $PSScriptRoot "bench.ps1") -Name "ab_$name" -Secs $Secs -Warm $Warm -Extra $x -ResX $ResX -ResY $ResY | Select-Object -Last 1
  Write-Output ("{0,-14} {1}" -f $name, ($line -replace 'TAG bench summary ', ''))
}
