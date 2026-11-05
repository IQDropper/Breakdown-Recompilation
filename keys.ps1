# keys.ps1 - run the native build and "press" keys into its window on a schedule (keyboard drives pad 1).
#   -Plan 'seconds:key,...'   keys: ENTER (START), Z (A), X (B), UP/DOWN/LEFT/RIGHT
# Used to reproduce menu crashes without someone at the controller.
param([string]$Plan = '6:ENTER,8:ENTER,10:ENTER,12:ENTER,15:Z,18:Z,21:Z,24:Z', [int]$Seconds = 45, [string]$Log = 'run_keys', [string]$ExtraEnv = '', [switch]$NoFb)
Add-Type -Name K -Namespace U -MemberDefinition '[DllImport("user32.dll")] public static extern bool PostMessage(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l);'
$vk = @{ ENTER = 0x0D; Z = 0x5A; X = 0x58; UP = 0x26; DOWN = 0x28; LEFT = 0x25; RIGHT = 0x27 }
if (-not $NoFb) { $env:RECOMP_FB_WINDOW = '1' } else { Remove-Item env:RECOMP_FB_WINDOW -EA 0 }; foreach ($kv in ($ExtraEnv -split ';' | ? { $_ })) { $k, $v = $kv -split '=', 2; Set-Item "env:$k" $v }; $env:RECOMP_VBLANK = '1'; $env:RECOMP_KEYBOARD = '1'
Set-Location C:\Users\iron_\OneDrive\Emulation\xr\breakdown
$p = Start-Process build-win\breakdown_recomp.exe -PassThru -RedirectStandardError "$Log`_err.txt" -RedirectStandardOutput "$Log`_out.txt"
$t0 = Get-Date
foreach ($step in $Plan.Split(',')) {
    $at, $key = $step.Split(':'); while (((Get-Date) - $t0).TotalSeconds -lt [double]$at) { Start-Sleep -Milliseconds 100 }
    if ($p.HasExited) { break }
    $p.Refresh(); $h = $p.MainWindowHandle
    [U.K]::PostMessage($h, 0x100, [IntPtr]$vk[$key], [IntPtr]0) | Out-Null; Start-Sleep -Milliseconds 150
    [U.K]::PostMessage($h, 0x101, [IntPtr]$vk[$key], [IntPtr]0) | Out-Null
    "{0,5:n1}s {1}" -f ((Get-Date) - $t0).TotalSeconds, $key
}
while (((Get-Date) - $t0).TotalSeconds -lt $Seconds -and -not $p.HasExited) { Start-Sleep -Milliseconds 250 }
"exited by itself: $($p.HasExited)"; if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
