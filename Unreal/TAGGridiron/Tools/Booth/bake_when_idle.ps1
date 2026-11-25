# The voice bake, run around the game: makes the lines of a BoothScript --bake-all list in chunks, and only while no
# game or editor is running (they want the GPU). Made lines are skipped, so it can be stopped and started at will.
#
#   powershell -ExecutionPolicy Bypass -File Tools\Booth\bake_when_idle.ps1 [-List Tools\build\bake_all.jsonl] [-Chunk 150]
#
# Stop it: create Saved\BoothVoice\_bake_stop (it ends after the chunk it is on). Log: Saved\BoothVoice\_bake.log
param(
    [string]$List = "Tools\build\bake_all.jsonl",
    [string]$Cache = "Saved\BoothVoice",
    [int]$Chunk = 150
)
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $Root
$Py = Join-Path $Root "Tools\Booth\.venv\Scripts\python.exe"
$Tts = Join-Path $Root "Tools\Booth\booth_tts.py"
$Log = Join-Path $Root "$Cache\_bake.log"
$Stop = Join-Path $Root "$Cache\_bake_stop"
function Write-Bake([string]$Text) {
    $Line = "{0} {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $Text
    Add-Content -Path $Log -Value $Line -Encoding utf8
    Write-Output $Line
}
function Get-Left {
    $Out = & $Py $Tts --cache $Cache --once $List --dry-run
    $M = [regex]::Match(($Out -join "`n"), "(\d+) to make")
    if ($M.Success) { return [int]$M.Groups[1].Value }
    return -1
}
$Waiting = $false
$Fails = 0
Write-Bake "bake start: $List, chunks of $Chunk"
while ($true) {
    if (Test-Path $Stop) { Write-Bake "stop file found: ending"; break }
    if (Get-Process -Name "UnrealEditor*" -ErrorAction SilentlyContinue) {
        if (-not $Waiting) { Write-Bake "a game or editor is running: waiting"; $Waiting = $true }
        Start-Sleep -Seconds 20
        continue
    }
    $Waiting = $false
    $Left = Get-Left
    if ($Left -eq 0) { Write-Bake "BAKE DONE: every line of $List is in the cache"; break }
    if ($Left -lt 0) { Write-Bake "BAKE FAIL: could not read the count of lines left"; exit 1 }
    Write-Bake "$Left lines left: making $([Math]::Min($Chunk, $Left))"
    & $Py $Tts --cache $Cache --once $List --limit $Chunk --force | Out-Null
    $Code = $LASTEXITCODE
    $After = Get-Left
    if ($Code -ne 0 -or $After -ge $Left) {
        $Fails++
        Write-Bake "chunk made no progress (exit $Code, $After left): failure $Fails of 5"
        if ($Fails -ge 5) { Write-Bake "BAKE FAIL: five chunks in a row made nothing"; exit 1 }
        Start-Sleep -Seconds 30
    } else { $Fails = 0 }
}
