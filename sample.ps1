# sample.ps1 [-Samples 30] [-Warmup 15] [-Env 'K=V;...'] - "poor man's profiler" for the native build.
# Starts the game (D3D11), then N times: attach gdb, record the top 6 frames of every thread, detach.
# Functions that show up most often are where the time goes. No install / no admin (uses our mingw gdb).
param([int]$Samples = 30, [int]$Warmup = 15, [string]$Env = 'RECOMP_PB_BACKEND=d3d11', [string]$Out = 'C:\Users\iron_\OneDrive\Emulation\xr\breakdown\samples.txt')
Set-Location C:\Users\iron_\OneDrive\Emulation\xr\breakdown
Get-Process breakdown_recomp -EA 0 | Stop-Process -Force
Get-ChildItem env: | ? { $_.Name -like 'RECOMP_*' } | % { Remove-Item "env:$($_.Name)" }
$env:RECOMP_VBLANK = '1'
foreach ($kv in ($Env -split ';' | ? { $_ })) { $k, $v = $kv -split '=', 2; Set-Item "env:$k" $v }
$p = Start-Process build-win\breakdown_recomp.exe -PassThru -RedirectStandardError sample_err.txt -RedirectStandardOutput sample_out.txt
Start-Sleep $Warmup
$gdb = 'D:\Emulation\RE\mingw64\bin\gdb.exe'
Remove-Item $Out -EA 0
for ($i = 0; $i -lt $Samples -and -not $p.HasExited; $i++) {
    "=== SAMPLE $i" | Add-Content $Out
    & $gdb -batch -q -p $p.Id -ex 'set pagination off' -ex 'thread apply all bt 6' -ex detach 2>$null | Add-Content $Out
    Start-Sleep -Milliseconds 300
}
Stop-Process -Id $p.Id -Force -EA 0
"samples written: $i -> $Out"
