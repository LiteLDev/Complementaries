# U14 flag (client): the cloth entity renders and waves with its patterns, awnings and buntings
# render, an empty hand tilts an awning, the Flag loom form opens on a loom, the held flag renders.
# Screenshots: out/flag/{scene,scene-side,scene-later,awning-tilted,loom,held}.png
Invoke-Chat '/gamemode creative'
Invoke-Chat '/gamerule dodaylightcycle false'
Invoke-Chat '/time set noon'

$mark = Get-LogMark
$r = Invoke-SelfTest 'flag.showcase'
if ($r.Fail -gt 0 -or $r.Pass -lt 1) { throw 'flag.showcase failed' }
$lines = Get-LeviLog -Since $mark
function Get-InfoPos([string]$what) {
    $l = $lines | Where-Object { $_ -match "SELFTEST INFO flag\.showcase: $what (-?\d+) (-?\d+) (-?\d+)" } | Select-Object -First 1
    if (-not $l) { throw "no '$what' position in the log" }
    $null = $l -match "$what (-?\d+) (-?\d+) (-?\d+)"
    return @([int]$Matches[1], [int]$Matches[2], [int]$Matches[3])
}
$view = Get-InfoPos 'view'
$awning = Get-InfoPos 'awning'
$loom = Get-InfoPos 'loom'

# Front view (looking south at the wall), then from the side, then again a bit later (waving).
Invoke-Chat "/tp @s $($view[0] + 0.5) $($view[1]) $($view[2] + 0.5) facing $($view[0] + 0.5) $($view[1] + 1.8) $($view[2] + 7)"
Start-Sleep -Seconds 3
Save-Screenshot 'scene' | Out-Null
Start-Sleep -Milliseconds 700
Save-Screenshot 'scene-later' | Out-Null
Invoke-Chat "/tp @s $($view[0] + 9.5) $($view[1] + 2) $($view[2] + 5.5) facing $($view[0] - 1) $($view[1] + 1) $($view[2] + 5.5)"
Start-Sleep -Seconds 2
Save-Screenshot 'scene-side' | Out-Null

# Empty hand on an awning tilts it. The aim of `tp ... facing` depends on the eye height used
# by the game, so a few aim heights are tried until the click lands on the awning.
Invoke-Chat '/clear @s'
$tilted = $false
foreach ($dy in 0.75, -0.1, -0.85) {
    Invoke-Chat "/tp @s $($awning[0] + 0.5) $($view[1]) $($awning[2] - 2.5) facing $($awning[0] + 0.5) $($awning[1] + $dy) $($awning[2] + 0.5)"
    Start-Sleep -Seconds 1
    $mark = Get-LogMark
    Click 0.5 0.5 -Relative -Right
    try { Wait-LogLine 'flag: toggled awning slant' 4 -Since $mark | Out-Null; $tilted = $true; break } catch { }
}
if (-not $tilted) { throw 'an empty-hand click did not tilt the awning' }
Start-Sleep -Milliseconds 500
Save-Screenshot 'awning-tilted' | Out-Null

# A flag used on a loom opens the Flag loom form.
Invoke-Chat '/give @s complementaries:flag_white 1'
Invoke-Chat '/give @s red_dye 4'
$opened = $false
foreach ($dy in 0.5, -0.4, -1.2) {
    Invoke-Chat "/tp @s $($loom[0] + 0.5) $($loom[1]) $($loom[2] - 2.5) facing $($loom[0] + 0.5) $($loom[1] + $dy) $($loom[2] + 0.5)"
    Start-Sleep -Seconds 1
    $mark = Get-LogMark
    Click 0.5 0.5 -Relative -Right
    try { Wait-LogLine 'flag: opened the flag loom' 4 -Since $mark | Out-Null; $opened = $true; break } catch { }
}
if (-not $opened) { throw 'using a flag on a loom did not open the Flag loom' }
Start-Sleep -Seconds 2
Save-Screenshot 'loom' | Out-Null
Close-Screen

# Held flag (third person, back and front).
Press-Key F5
Start-Sleep -Milliseconds 800
Save-Screenshot 'held-back' | Out-Null
Press-Key F5
Start-Sleep -Milliseconds 800
Save-Screenshot 'held' | Out-Null
Press-Key F5
Start-Sleep -Milliseconds 500
