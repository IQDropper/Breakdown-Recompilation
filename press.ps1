# press.ps1 KEY[,KEY...] [-Wait ms] [-Shot name] [-Hold ms] - press keys into the RUNNING native game window, then screenshot.
# Keys: ENTER (START) Z (A) X (B) C (X) V (Y) UP DOWN LEFT RIGHT, WASD = left stick, IJKL = right stick (look); NONE = just screenshot.  Screenshot -> fbdump\<name>.jpg (400 px wide).
# -Hold: how long each key stays down. The game samples input once per frame, so a slow frame rate
#        (CPU renderer: ~2 fps in menus) needs a longer hold than the default 150 ms or the press is missed.
param([string]$Keys = 'ENTER', [int]$Wait = 1500, [string]$Shot = 'step', [int]$Hold = 150)
Add-Type -Name K2 -Namespace U -MemberDefinition '[DllImport("user32.dll")] public static extern bool PostMessage(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l);'
$vk = @{ ENTER = 0x0D; Z = 0x5A; X = 0x58; UP = 0x26; DOWN = 0x28; LEFT = 0x25; RIGHT = 0x27; W = 0x57; A = 0x41; S = 0x53; D = 0x44; I = 0x49; J = 0x4A; K = 0x4B; L = 0x4C; C = 0x43; V = 0x56 }
$p = Get-Process breakdown_recomp -EA 0 | Select -First 1
if (-not $p) { 'game not running'; exit 1 }
foreach ($k in $Keys.Split(',')) {
    if (-not $vk.ContainsKey($k)) { continue }
    [U.K2]::PostMessage($p.MainWindowHandle, 0x100, [IntPtr]$vk[$k], [IntPtr]0) | Out-Null; Start-Sleep -Milliseconds $Hold
    [U.K2]::PostMessage($p.MainWindowHandle, 0x101, [IntPtr]$vk[$k], [IntPtr]0) | Out-Null; Start-Sleep -Milliseconds 400
}
Start-Sleep -Milliseconds $Wait
powershell -ExecutionPolicy Bypass -File D:\Emulation\RE\pipeline\phase4\now.ps1 -Proc breakdown_recomp -Out "C:\Users\iron_\OneDrive\Emulation\xr\breakdown\fbdump\$Shot.jpg" -Width 400 | Out-Null
"pressed $Keys -> fbdump\$Shot.jpg"
