# press_marked.ps1 -Log tr2 -Keys 'Z:2500,UP:1500,Z:8000'
# Presses keys in the running game and records the log line count before each press, so the
# log can be cut per press. Writes <Log>_marks.txt: "<line> <key>".
param([string]$Log = 'tr2', [string]$Keys = 'Z:2500')
Set-Location $PSScriptRoot
$marks = @()
foreach ($kw in $Keys.Split(',')) {
    $k, $w = $kw.Split(':')
    $marks += "$((Get-Content "$($Log)_err.txt").Count) $k"
    & .\press.ps1 -Keys $k -Wait ([int]$w) -Shot "pm_$k" | Out-Null
}
$marks += "$((Get-Content "$($Log)_err.txt").Count) END"
$marks | Set-Content "$($Log)_marks.txt"
$marks
