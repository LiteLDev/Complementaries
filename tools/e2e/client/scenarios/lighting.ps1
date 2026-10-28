# U9 lighting on a real client: models, light, particles and the real-player interactions
# (empty hand puts a candle holder out, flint and steel lights it, a lantern hangs on a wall, the
# sconce lever toggles).
# Screenshots: out/lighting/{scene-day,scene-night,interactions}.png
#
# Layout (absolute, flat world, ground top y = -61):
#   stone wall x -4..4, y -60..-57, z 3; things on its north face at z 2, floor things at z 0.

# Stands at (px, py, pz) looking at (tx, ty, tz) from the eyes (explicit yaw / pitch: the pitch of
# "tp ... facing" is not applied to the local player's camera reliably).
function Aim([double]$px, [double]$py, [double]$pz, [double]$tx, [double]$ty, [double]$tz) {
    $dx = $tx - $px; $dy = $ty - ($py + 1.62); $dz = $tz - $pz
    $yaw = [Math]::Round([Math]::Atan2(-$dx, $dz) * 180 / [Math]::PI, 2)
    $pitch = [Math]::Round(-[Math]::Atan2($dy, [Math]::Sqrt($dx * $dx + $dz * $dz)) * 180 / [Math]::PI, 2)
    $inv = [Globalization.CultureInfo]::InvariantCulture
    Invoke-Chat ("/tp @s {0} {1} {2} {3} {4}" -f $px.ToString($inv), $py.ToString($inv), $pz.ToString($inv), $yaw.ToString($inv), $pitch.ToString($inv))
    Start-Sleep -Milliseconds 800
}

function Use-Crosshair {
    Click 0.5 0.5 -Relative -Right
    Start-Sleep -Milliseconds 700
}

Invoke-Chat '/gamemode creative'
Invoke-Chat '/tp @s 0 -60 -4'
Start-Sleep -Seconds 3
Invoke-Chat '/fill -6 -60 -2 6 -55 4 air'
Invoke-Chat '/fill -4 -60 3 4 -57 3 stone'
Invoke-Chat '/setblock -3 -57 3 complementaries:stone_lamp'
Invoke-Chat '/setblock -1 -57 3 complementaries:blackstone_lamp'
Invoke-Chat '/setblock 1 -57 3 complementaries:deepslate_lamp'
Invoke-Chat '/setblock 3 -57 3 complementaries:end_stone_lamp'
# wall sconces and the lever (on the wall's north face)
$wall = @(
    @(-3, 'sconce'), @(-2, 'sconce_soul'), @(-1, 'sconce_ender'), @(0, 'sconce_glow'), @(1, 'sconce_green')
)
foreach ($s in $wall) {
    Invoke-Chat "/setblock $($s[0]) -58 2 complementaries:$($s[1]) [`"minecraft:block_face`"=`"north`"]"
}
Invoke-Chat '/setblock 2 -58 2 complementaries:sconce_lever ["minecraft:block_face"="north"]'
Invoke-Chat '/setblock -4 -58 2 complementaries:wall_lantern ["minecraft:block_face"="north","complementaries:lantern"=1]'
Invoke-Chat '/setblock 3 -58 2 complementaries:wall_lantern ["minecraft:block_face"="north","complementaries:lantern"=2]'
# floor: candle holders with 1..4 candles (lit) and standing sconces
Invoke-Chat '/setblock -3 -60 0 complementaries:candle_holder ["minecraft:block_face"="up","complementaries:candles"=1,"complementaries:lit"=true]'
Invoke-Chat '/setblock -2 -60 0 complementaries:candle_holder_red ["minecraft:block_face"="up","complementaries:candles"=2,"complementaries:lit"=true]'
Invoke-Chat '/setblock -1 -60 0 complementaries:candle_holder_light_blue ["minecraft:block_face"="up","complementaries:candles"=3,"complementaries:lit"=true]'
Invoke-Chat '/setblock 0 -60 0 complementaries:candle_holder_black ["minecraft:block_face"="up","complementaries:candles"=4,"complementaries:lit"=true]'
Invoke-Chat '/setblock 1 -60 0 complementaries:sconce ["minecraft:block_face"="up"]'
Invoke-Chat '/setblock 2 -60 0 complementaries:sconce_soul ["minecraft:block_face"="up","complementaries:lit"=false]'
# a wall candle holder
Invoke-Chat '/setblock -2 -59 2 complementaries:candle_holder_yellow ["minecraft:block_face"="north","complementaries:candles"=3,"complementaries:lit"=true]'

Invoke-Chat '/time set noon'
Aim 0 -59.5 -3.5 0 -58.6 3
Start-Sleep -Seconds 2
Save-Screenshot 'scene-day' | Out-Null
Invoke-Chat '/time set midnight'
Start-Sleep -Seconds 3
Save-Screenshot 'scene-night' | Out-Null

# ---- real-player interactions (survival)
Invoke-Chat '/gamemode survival'
Invoke-Chat '/clear @s'
Start-Sleep -Milliseconds 500
Press-Key '1'

# empty hand puts the 4-candle holder at (0,-60,0) out
$mark = Get-LogMark
Aim 0.5 -60 -2.2 0.5 -59.6 0.5
Use-Crosshair
Wait-LogLine 'lighting: extinguished complementaries:candle_holder_black' 10 -Since $mark | Out-Null

# flint and steel lights it again
Invoke-Chat '/give @s flint_and_steel'
Start-Sleep -Milliseconds 500
$mark = Get-LogMark
Aim 0.5 -60 -2.2 0.5 -59.6 0.5
Use-Crosshair
Wait-LogLine 'lighting: lit complementaries:candle_holder_black' 10 -Since $mark | Out-Null

# flint and steel lights the unlit floor soul sconce
$mark = Get-LogMark
Aim 2.5 -60 -2.2 2.5 -59.5 0.5
Use-Crosshair
Wait-LogLine 'lighting: lit complementaries:sconce_soul' 10 -Since $mark | Out-Null

# empty hand toggles the sconce lever
Invoke-Chat '/clear @s'
Start-Sleep -Milliseconds 500
$mark = Get-LogMark
Aim 2.5 -60 0 2.5 -57.5 2.5
Use-Crosshair
Wait-LogLine 'lighting: sconce lever at 2 -58 2 on' 10 -Since $mark | Out-Null

# a lantern used on the side of the wall hangs on it
Invoke-Chat '/give @s lantern'
Start-Sleep -Milliseconds 500
$mark = Get-LogMark
Aim 4.5 -60 0.5 4.5 -59.5 3
Use-Crosshair
Wait-LogLine 'lighting: wall lantern \(minecraft:lantern\) placed at 4 -60 2' 10 -Since $mark | Out-Null

# walking through the wall lantern swings it (its frames show up in the log as block updates only,
# so just make sure nothing breaks)
Invoke-Chat '/tp @s 2.5 -60 2.5'
Invoke-Chat '/tp @s 5.5 -60 2.5'

Invoke-Chat '/gamemode creative'
Aim 0 -59.5 -3.5 0 -58.6 3
Start-Sleep -Seconds 2
Save-Screenshot 'interactions' | Out-Null

$r = Invoke-SelfTest 'lighting.*'
if ($r.Pass -lt 10) { throw "expected at least 10 lighting selftests to pass, got $($r.Pass)" }
