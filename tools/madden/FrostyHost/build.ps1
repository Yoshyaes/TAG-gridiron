# Builds FrostyHost.exe (net48, x64) with the VS 2022 Build Tools Roslyn compiler.
# References the shipped Frosty 1.0.6.3 DLLs in place. Nothing is copied next to the exe,
# the host resolves Frosty assemblies from FROSTY_DIR at runtime.
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$frosty = if ($env:FROSTY_DIR) { $env:FROSTY_DIR } else { Join-Path $env:USERPROFILE 'Downloads\FrostyEditor\FrostyEditor' }
$csc = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\Roslyn\csc.exe'
$ref = 'C:\Program Files (x86)\Reference Assemblies\Microsoft\Framework\.NETFramework\v4.8'
$out = Join-Path $here 'bin'
New-Item -ItemType Directory -Force $out | Out-Null

$refs = @(
  "$ref\mscorlib.dll", "$ref\System.dll", "$ref\System.Core.dll", "$ref\Microsoft.CSharp.dll",
  "$ref\System.Runtime.Serialization.dll", "$ref\PresentationFramework.dll", "$ref\PresentationCore.dll",
  "$ref\WindowsBase.dll", "$ref\System.Xaml.dll", "$ref\Facades\netstandard.dll", "$ref\Facades\System.Runtime.dll",
  "$frosty\FrostySdk.dll", "$frosty\FrostyCore.dll", "$frosty\FrostyControls.dll", "$frosty\FrostyHash.dll",
  "$frosty\Plugins\MeshSetPlugin.dll", "$frosty\Plugins\TexturePlugin.dll",
  "$frosty\ThirdParty\Newtonsoft.Json.dll", "$frosty\ThirdParty\SharpDX.dll", "$frosty\ThirdParty\SharpDX.Mathematics.dll",
  "$frosty\ThirdParty\SharpDX.Direct3D11.dll", "$frosty\ThirdParty\SharpDX.DXGI.dll"
)
$args = @('/nologo', '/noconfig', '/nostdlib+', '/target:exe', '/platform:x64', '/langversion:latest', '/optimize+', '/unsafe',
  "/out:$out\FrostyHost.exe") + ($refs | ForEach-Object { "/r:$_" }) + @("$here\Program.cs")
& $csc @args
if ($LASTEXITCODE -ne 0) { throw "csc failed with $LASTEXITCODE" }
# App config so the CLR allows mixed mode FrostyHash and large object graphs.
@'
<?xml version="1.0" encoding="utf-8"?>
<configuration>
  <startup useLegacyV2RuntimeActivationPolicy="true"><supportedRuntime version="v4.0" sku=".NETFramework,Version=v4.8"/></startup>
  <runtime><loadFromRemoteSources enabled="true"/><gcAllowVeryLargeObjects enabled="true"/></runtime>
</configuration>
'@ | Set-Content -Encoding utf8 "$out\FrostyHost.exe.config"
Write-Output "built $out\FrostyHost.exe"
