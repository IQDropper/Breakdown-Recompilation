# to_load_screen.ps1 - start the native build and drive it to Main Menu -> LOAD GAME (keyboard keys).
#   -Log name  -> name_err.txt ; -ExtraEnv 'K=V;K=V' ; -Backend d3d11|cpu ; -Load = also load slot 1
# -Load needs a build traced with config\trace_load.json: it reads the game's menu state from the log
# (keys pressed while a menu animates are ignored, and the Yes/No dialog defaults to NO).
param([string]$Log = 'ls', [string]$ExtraEnv = '', [string]$Backend = 'd3d11', [switch]$Load, [int]$Slot = 1)
Set-Location $PSScriptRoot
Get-Process breakdown_recomp -EA 0 | Stop-Process -Force; Start-Sleep 1
$envs = if ($Backend -eq 'd3d11') { 'RECOMP_PB_BACKEND=d3d11' } else { '' }
if ($ExtraEnv) { $envs = ($envs, $ExtraEnv | ? { $_ }) -join ';' }
$a = @('-ExecutionPolicy','Bypass','-File','keys.ps1','-Plan','6:ENTER,9:ENTER','-Seconds','900','-Log',$Log,'-NoFb')
if ($envs) { $a += @('-ExtraEnv', $envs) }
Start-Process powershell -WindowStyle Hidden -ArgumentList $a
Start-Sleep 48
& .\press.ps1 -Keys ENTER -Wait 5000 -Shot ls_title      # title appears after the loading screen
& .\press.ps1 -Keys ENTER -Wait 15000 -Shot ls_menu      # PRESS START -> main menu (cursor on LOAD GAME)
& .\press.ps1 -Keys Z -Wait 14000 -Shot ls_load          # LOAD GAME list
for ($i = 1; $i -lt $Slot; $i++) { & .\press.ps1 -Keys RIGHT -Wait 1500 -Shot ls_slot }   # move to slot N
if (-not $Load) { return }
$f = "$($Log)_err.txt"
function Seen($pat, $from) { (Get-Content $f | Select -Skip $from | Select-String -Pattern $pat -SimpleMatch | Select -First 1) -ne $null }
for ($t = 0; $t -lt 4; $t++) {                           # A on slot 1 until the Yes/No input state runs
    $m = (Get-Content $f).Count
    & .\press.ps1 -Keys Z -Wait 4000 -Shot ls_dialog
    if (Seen '-> sub_001196C0' $m) { break }
}
for ($t = 0; $t -lt 4; $t++) {                           # UP (focus YES) + A until the loader starts
    $m = (Get-Content $f).Count
    & .\press.ps1 -Keys UP -Wait 2500 -Shot ls_yes
    & .\press.ps1 -Keys Z -Wait 3000 -Shot ls_go
    if (Seen '-> sub_00029550' $m) { "load started (try $t)"; break }
}
Start-Sleep 50
& .\press.ps1 -Keys Z -Wait 500 -Shot ls_room            # Z also advances the wake-up dialogue
