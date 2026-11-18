# Build the editor target (the game runs from it with -game). Prints the errors and the result line.
& cmd /c '"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" TAGGridironEditor Win64 Development -Project="C:\Users\soon2\Documents\Madden TAG\Unreal\TAGGridiron\TAGGridiron.uproject" -WaitMutex' > D:\TAGAudio\work\build.log 2>&1
Select-String -Path D:\TAGAudio\work\build.log -Pattern 'error|Result:' | Select-Object -First 30 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(300, $_.Line.Length)) }
