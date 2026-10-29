# U6 instrument on a real client: the block models render (globe, clock, hourglass, wind vane, sepia
# globe), and real right clicks reach the server (empty hand on a globe and a clock, sand into an
# hourglass, the altimeter in the air).
# Screenshots: out/instrument/{row,globe-spin,hourglass-sand,clock-time,altimeter}.png

function Face-Block([string]$at) {
    Invoke-Chat "/tp @s ~ ~ ~ facing $at"
    Start-Sleep -Milliseconds 600
}

Invoke-Chat '/clear @s'
Invoke-Chat '/time set 7500'
Invoke-Chat '/weather clear'
Invoke-Chat '/tp @s ~ ~ ~ 0 15'
Invoke-Chat '/setblock ~-3 ~ ~4 complementaries:globe ["minecraft:cardinal_direction"="north"]'
Invoke-Chat '/setblock ~-1 ~ ~4 complementaries:clock_block ["minecraft:cardinal_direction"="north"]'
Invoke-Chat '/setblock ~1 ~ ~4 complementaries:hourglass ["minecraft:block_face"="up"]'
Invoke-Chat '/setblock ~3 ~ ~4 complementaries:wind_vane'
Invoke-Chat '/setblock ~5 ~ ~4 complementaries:globe_sepia ["minecraft:cardinal_direction"="north"]'
Start-Sleep -Seconds 2
Save-Screenshot 'row' | Out-Null

# Empty hand on a globe: spins (needs the client to send the click for a custom block).
Invoke-Chat '/setblock ~ ~ ~2 complementaries:globe ["minecraft:cardinal_direction"="north"]'
Start-Sleep -Milliseconds 500
Face-Block '~ ~0.5 ~2.5'
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'instrument: globe at .* spun by' 15 -Since $mark | Out-Null
Start-Sleep -Milliseconds 300
Save-Screenshot 'globe-spin' | Out-Null
Start-Sleep -Seconds 4

# Empty hand on a clock: shows the time.
Invoke-Chat '/setblock ~ ~ ~2 complementaries:clock_block ["minecraft:cardinal_direction"="north"]'
Start-Sleep -Milliseconds 500
Face-Block '~ ~0.5 ~2.5'
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'instrument: time shown to' 15 -Since $mark | Out-Null
Start-Sleep -Milliseconds 300
Save-Screenshot 'clock-time' | Out-Null

# Sand into an hourglass.
Invoke-Chat '/setblock ~ ~ ~2 complementaries:hourglass ["minecraft:block_face"="up"]'
Invoke-Chat '/give @s sand 4'
Start-Sleep -Milliseconds 500
Face-Block '~ ~0.5 ~2.5'
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'instrument: hourglass at .* filled with minecraft:sand' 15 -Since $mark | Out-Null
Start-Sleep -Seconds 2
Save-Screenshot 'hourglass-sand' | Out-Null

# The altimeter in the air.
Invoke-Chat '/clear @s'
Invoke-Chat '/give @s complementaries:altimeter'
Invoke-Chat '/tp @s ~ ~ ~ 0 -60'
Start-Sleep -Milliseconds 600
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'instrument: altimeter used by' 15 -Since $mark | Out-Null
Start-Sleep -Milliseconds 300
Save-Screenshot 'altimeter' | Out-Null
Invoke-Chat '/clear @s'

$r = Invoke-SelfTest 'instrument.*'
if ($r.Pass -lt 12) { throw "expected at least 12 instrument selftests to pass, got $($r.Pass)" }
