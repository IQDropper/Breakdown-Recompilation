# warmup_shaders.ps1 - P6.3d: fill the shader cache once, so first visits to areas do not stutter.
# Run WARMUP_SHADERS.cmd (it calls this). The game walks itself through the developers' map viewer, one area after
# another, in batches of 15; each area is loaded and looked around so its shaders are compiled into the disk cache
# (%LOCALAPPDATA%\xboxrecomp\ShaderCache, or RECOMP_DATA_DIR\ShaderCache). About 30 s per map-list entry,
# 104 entries: roughly an hour. The game window opens minimised and needs no focus; you can use the PC meanwhile.
# Your saves are not touched (the run uses its own empty save folder). Stop any time with Ctrl+C: the next run
# continues after the last area that loaded (warmup_progress.txt beside the exe).
# Measured (P6.3d, Annex A): a first visit compiled 24 shaders (66 frames over 50 ms); after warm-up 0 (13).
param([int]$Start = -1, [int]$Batch = 15, [int]$Count = 0)   # -Count N: stop after N entries (tests)
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'build-win\breakdown_recomp.exe'
$list = Join-Path $root 'build-win\map_list.txt'
if (-not (Test-Path $exe)) { Write-Output "No game build at $exe. Run SETUP.cmd and breakdown all first."; exit 1 }
if (-not (Test-Path $list)) { Write-Output "No map list at $list (tools/map_list.py writes it during setup)."; exit 1 }
$total = @(Get-Content $list | Where-Object { $_.Trim() }).Count
$progress = Join-Path $root 'build-win\warmup_progress.txt'
if ($Start -lt 0) { $Start = if (Test-Path $progress) { [int](Get-Content $progress -TotalCount 1) } else { 0 } }
if ($Start -ge $total) { Write-Output "Warm-up already finished ($total entries). Delete $progress to run it again."; exit 0 }
if (Get-Process breakdown_recomp -ErrorAction SilentlyContinue) { Write-Output 'The game is running. Close it first.'; exit 1 }
$saves = Join-Path $env:TEMP 'breakdown_warmup_saves'; New-Item -ItemType Directory -Force $saves | Out-Null
$log = Join-Path $root 'warmup_stderr.txt'
$envs = @{ RECOMP_PB_BACKEND = 'd3d11'; RECOMP_D3D11_OCC = 'sync'; RECOMP_VBLANK = '1'; RECOMP_KEYBOARD = '1'
           RECOMP_DEBUG_MENU = '1'; RECOMP_WARMUP = '1'; RECOMP_SAVE_DIR = $saves; RECOMP_PRESENT_FULLSCREEN = '0' }
$fails = 0
$stopAt = if ($Count -gt 0) { [math]::Min($total, $Start + $Count) } else { $total }
Write-Output "Shader warm-up: map-list entries $Start..$($total - 1) of $total, $Batch per game run."
while ($Start -lt $stopAt) {
  $count = [math]::Min($Batch, $stopAt - $Start)
  foreach ($k in $envs.Keys) { Set-Item "env:$k" $envs[$k] }
  $env:RECOMP_WARMUP_START = "$Start"; $env:RECOMP_WARMUP_COUNT = "$count"
  Write-Output ("  entries {0}..{1} ..." -f $Start, ($Start + $count - 1))
  $p = Start-Process -FilePath $exe -WorkingDirectory $root -WindowStyle Minimized -PassThru -RedirectStandardError $log -RedirectStandardOutput (Join-Path $root 'warmup_stdout.txt')
  $limit = (Get-Date).AddSeconds(60 + 60 * $count)
  while (-not $p.HasExited -and (Get-Date) -lt $limit) { Start-Sleep -Seconds 2 }
  if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; Start-Sleep -Seconds 2 }
  $txt = if (Test-Path $log) { Get-Content $log -Raw } else { '' }
  $loaded = [regex]::Matches($txt, '\[WARMUP\] loaded entry (\d+)') | ForEach-Object { [int]$_.Groups[1].Value }
  $next = $Start
  if ($loaded) { $next = ($loaded | Measure-Object -Maximum).Maximum + 1 }
  if ($txt -match '\[WARMUP\] stuck') { $next = [math]::Max($next, [int]([regex]::Match($txt, 'next entry (\d+)').Groups[1].Value)) }
  if ($next -le $Start) { $fails++; $next = $Start + 1; Write-Output "    entry $Start did not load; skipped" }
  if ($fails -ge 5) { Write-Output 'Five entries failed to load; stopping. Send warmup_stderr.txt.'; exit 2 }
  Set-Content $progress "$next"
  $Start = $next
}
if ($stopAt -lt $total) { Write-Output "Stopped at entry $Start (-Count)."; exit 0 }
Write-Output 'Shader warm-up finished. Play as usual: first visits now load their shaders from the disk cache.'
exit 0
