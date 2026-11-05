# run_once.ps1 - run the recompiled exe for N seconds, keep stdout/stderr, grab a window capture, then stop it.
param([int]$Seconds = 25)
Set-Location C:\Users\iron_\OneDrive\Emulation\xr\breakdown
$p = Start-Process .\build-win\breakdown_recomp.exe -WorkingDirectory C:\Users\iron_\OneDrive\Emulation\xr\breakdown -PassThru `
       -RedirectStandardOutput run_stdout.txt -RedirectStandardError run_stderr.txt
$t = Get-Date
while (-not $p.HasExited -and ((Get-Date) - $t).TotalSeconds -lt $Seconds) { Start-Sleep 1 }
$alive = -not $p.HasExited
if ($alive) {
  & D:\Emulation\RE\pipeline\phase4\now.ps1 -Out C:\Users\iron_\OneDrive\Emulation\xr\breakdown\run_shot.jpg | Out-Null   # (now.ps1 looks for xemu; see below)
  $p | Stop-Process -Force
}
"exited=$(-not $alive) exitcode=$($p.ExitCode) after {0:n1}s" -f ((Get-Date) - $t).TotalSeconds
