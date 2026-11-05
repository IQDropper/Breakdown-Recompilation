# migrate_to_onedrive.ps1 - make OneDrive\Emulation\xr\breakdown the working copy. D:\xr\breakdown is left untouched.
$old = 'D:\xr\breakdown'; $new = 'C:\Users\iron_\OneDrive\Emulation\xr\breakdown'
$out = "$old\migrate_log.txt"; "start $(Get-Date)" | Set-Content $out
function Log($m) { $m | Add-Content $out }
# 1. final copy of anything new/newer (venv is recreated, not copied)
robocopy $old $new /E /XO /XJ /R:1 /W:1 /MT:8 /NP /NDL /NFL /XD .venv __pycache__ | Select-String 'Files :' | % { Log "copy: $($_.Line.Trim())" }
# 2. point scripts at the new folder (logs keep their history)
$n = 0
Get-ChildItem $new -Recurse -File -Include *.ps1,*.py,*.cmd,*.bat,*.json,*.toml |
  ? { $_.FullName -notmatch '\\(\.venv|third_party|external|build-win|analysis|fbdump|game_files)\\' -and $_.Name -ne 'migrate_to_onedrive.ps1' } | % {
    $t = [IO.File]::ReadAllText($_.FullName)
    $u = $t.Replace('D:\xr\breakdown', $new).Replace('D:/xr/breakdown', $new.Replace('\', '/')).Replace('D:\\xr\\breakdown', $new.Replace('\', '\\'))
    if ($u -ne $t) { [IO.File]::WriteAllText($_.FullName, $u); $n++ }
}
Log "scripts repointed: $n"
# 3. the build cache remembers the old source folder: start it fresh
Remove-Item "$new\build-win\CMakeCache.txt", "$new\build-win\CMakeFiles" -Recurse -Force -EA 0
# 4. fresh Python environment (same 4 packages as the old one)
$env:PATH = "C:\Users\iron_\AppData\Roaming\Python\Python312\Scripts;$env:PATH"
Set-Location $new
uv venv .venv --python 3.14 --prompt breakdown 2>&1 | % { Log "venv: $_" }
uv pip install --python .venv\Scripts\python.exe capstone==5.0.9 cmake==4.4.4 ninja==1.13.2 pefile==2024.8.26 2>&1 | Select -Last 2 | % { Log "pip: $_" }
# 5. regenerate + build in the new place (with the menu-state trace the auto-load script uses)
cmd /c "breakdown.cmd recomp --trace-functions $new\config\trace_load.json > rb_recomp.txt 2>&1 && breakdown.cmd build > rb_build.txt 2>&1"
Log ("build: " + (Get-Content rb_build.txt -Tail 1))
Log "DONE $(Get-Date)"
