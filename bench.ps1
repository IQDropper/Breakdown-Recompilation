# bench.ps1 -Env 'KEY=VAL;KEY2=VAL2' [-Seconds 35] [-Name tag] - one A/B speed test of the native build.
# Prints: median frame time (ms) from [PB-PERF], game frames/s from the frame counter (0x29BB18), kickwait.
param([string]$Env = '', [int]$Seconds = 35, [string]$Name = 'bench', [switch]$NoFb)   # -NoFb: no framebuffer window (D3D11 shows its own)
Set-Location C:\Users\iron_\OneDrive\Emulation\xr\breakdown
Get-Process breakdown_recomp -EA 0 | Stop-Process -Force
$keep = 'RECOMP_FB_WINDOW','RECOMP_VBLANK','RECOMP_FLIP_LOG','RECOMP_TRACE'
Get-ChildItem env: | ? { $_.Name -like 'RECOMP_*' -and $keep -notcontains $_.Name } | % { Remove-Item "env:$($_.Name)" }
$env:RECOMP_FB_WINDOW = '1'; $env:RECOMP_VBLANK = '1'; $env:RECOMP_FLIP_LOG = '1'; $env:RECOMP_TRACE = 'peek=0x29BB18,pb_report_ms=5000'
if ($NoFb) { Remove-Item env:RECOMP_FB_WINDOW -EA 0 }
foreach ($kv in ($Env -split ';' | ? { $_ })) { $k, $v = $kv -split '=', 2; Set-Item "env:$k" $v }
$p = Start-Process build-win\breakdown_recomp.exe -PassThru -RedirectStandardError "bench_$Name.txt" -RedirectStandardOutput "bench_$Name.out" -WindowStyle Minimized
Start-Sleep $Seconds; Stop-Process -Id $p.Id -Force -EA 0
$L = Get-Content "bench_$Name.txt"
$w = $L | ? { $_ -match '\[PB-PERF\].*wall ([\d.]+).*kickwait ([\d.]+)' } | % { [double]($_ -replace '.*wall ([\d.]+).*', '$1') } | Sort-Object
$k = $L | ? { $_ -match 'kickwait ([\d.]+)' } | % { [double]($_ -replace '.*kickwait ([\d.]+).*', '$1') } | Sort-Object
$f = $L | ? { $_ -match '\[PEEK\] 0x0029BB18 = ([0-9A-F]+)' } | % { [Convert]::ToInt32(($_ -replace '.*= ([0-9A-F]+).*', '$1'), 16) }
$fps = if ($f.Count -ge 3) { ($f[-1] - $f[1]) / (5.0 * ($f.Count - 2)) } else { 0 }
"{0,-14} frames {1,4}  median frame {2,6:n1} ms  kickwait {3,6:n1} ms  game counter {4,5:n1}/s   [{5}]" -f $Name, $w.Count, ($w[[int]($w.Count/2)]), ($k[[int]($k.Count/2)]), $fps, $Env
