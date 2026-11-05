# probe_menu.ps1 - start the build (D3D11 or CPU), go to the main menu, probe one frame at the given pixels.
#   -Px '560,179;549,288'  (guest 640x480 coords)   -Backend d3d11|cpu   -Log name
param([string]$Px = '560,179', [string]$Backend = 'd3d11', [string]$Log = 'pm')
Set-Location $PSScriptRoot
Get-Process breakdown_recomp -EA 0 | Stop-Process -Force; Start-Sleep 1
$trig = "$PSScriptRoot\px_trigger.flag"; Remove-Item $trig -EA 0
$env:RECOMP_D3D11_PX = $Px; $env:RECOMP_D3D11_PX_TRIGGER = $trig; $env:RECOMP_D3D11_PX_MAX = '400'
$envs = if ($Backend -eq 'd3d11') { 'RECOMP_PB_BACKEND=d3d11' } else { 'RECOMP_FB_WINDOW=1' }
Start-Process powershell -WindowStyle Hidden -ArgumentList @('-ExecutionPolicy','Bypass','-File','keys.ps1','-Plan','6:ENTER,9:ENTER','-Seconds','900','-Log',$Log,'-NoFb','-ExtraEnv',$envs)
Start-Sleep 48
& .\press.ps1 -Keys ENTER -Wait 5000 -Shot pm_title -Hold 400
& .\press.ps1 -Keys ENTER -Wait 16000 -Shot pm_menu -Hold 400
Set-Content $trig 'go'
Start-Sleep 6
& .\press.ps1 -Keys NONE -Wait 100 -Shot pm_after
Select-String -Path "$($Log)_err.txt" -Pattern 'px: triggered' | Select -Last 1 | % Line
