# overnight.ps1 - run the xboxrecomp pipeline stages for Breakdown unattended, one log per stage.
param([string[]]$Stages = @('pins refresh', 'import', 'setup', 'all'))
$env:PATH = "C:\Users\iron_\AppData\Roaming\Python\Python312\Scripts;$env:PATH"   # uv / uvx
Set-Location C:\Users\iron_\OneDrive\Emulation\xr\breakdown
foreach ($s in $Stages) {
  $log = "C:\Users\iron_\OneDrive\Emulation\xr\breakdown\log_" + ($s -replace '\W', '_') + '.txt'
  $t = Get-Date
  if ($s -eq 'import') { cmd /c "breakdown.cmd import `"D:\Emulation\ES-DE\ROMs\xbox\Breakdown (USA).xiso.iso`"" *> $log }
  else { cmd /c "breakdown.cmd $s" *> $log }
  "== $s exit=$LASTEXITCODE in {0:n1} min ==" -f ((Get-Date) - $t).TotalMinutes | Tee-Object -Append C:\Users\iron_\OneDrive\Emulation\xr\breakdown\log_summary.txt
  if ($LASTEXITCODE -ne 0) { break }
}
