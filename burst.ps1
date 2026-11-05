# burst.ps1 -Name b -Count 8 -GapMs 250 : rapid screenshots of the running game -> fbdump\<Name>_N.jpg
param([string]$Name = 'b', [int]$Count = 8, [int]$GapMs = 250)
for ($i = 0; $i -lt $Count; $i++) {
    powershell -ExecutionPolicy Bypass -File D:\Emulation\RE\pipeline\phase4\now.ps1 -Proc breakdown_recomp -Out "C:\Users\iron_\OneDrive\Emulation\xr\breakdown\fbdump\$($Name)_$i.jpg" -Width 400 | Out-Null
    Start-Sleep -Milliseconds $GapMs
}
"$Count shots -> fbdump\$($Name)_0..$($Count-1).jpg"
