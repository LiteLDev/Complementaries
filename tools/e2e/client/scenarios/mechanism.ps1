# U7 mechanism on a real client: the selftests on the integrated server, the block models / textures
# (screenshots), and real right-clicks on a crank and a turn table.
# Screenshots: out/mechanism/{blocks,blocks-side,crank,turn-table}.png
$r = Invoke-SelfTest 'mechanism.*'
if ($r.Pass -lt 15) { throw "expected at least 15 mechanism selftests to pass, got $($r.Pass)" }

# a display row at z=203 in front of the player (feet level y=-60 on the flat test world)
Invoke-Chat '/tickingarea add 190 -64 190 215 0 215 cm_mechanism'
Invoke-Chat '/tp @s 201.5 -60 197.5 facing 201.5 -60 203.5'
Start-Sleep -Seconds 2
$setup = @(
    '/fill 194 -60 202 209 -57 206 air',
    # launcher facing up, powered from below: extended with its head
    '/setblock 195 -61 203 redstone_block',
    '/setblock 195 -60 203 complementaries:piston_launcher ["minecraft:facing_direction"="down"]',
    # launcher facing the player (north), retracted
    '/setblock 197 -60 203 complementaries:piston_launcher ["minecraft:facing_direction"="south"]',
    # bellows facing the player, powered from behind
    '/setblock 199 -60 204 redstone_block',
    '/setblock 199 -60 203 complementaries:bellows ["minecraft:facing_direction"="south"]',
    # turn table (top up) with a furnace on it
    '/setblock 201 -60 203 complementaries:turn_table ["minecraft:facing_direction"="down"]',
    '/setblock 201 -59 203 furnace ["minecraft:cardinal_direction"="north"]',
    # a powered cog pair
    '/setblock 203 -61 203 redstone_block',
    '/setblock 203 -60 203 complementaries:cog_block',
    '/setblock 204 -60 203 complementaries:cog_block',
    # a crank on the north face of a stone block, and one on top of a stone
    '/setblock 206 -60 204 stone',
    '/setblock 206 -60 203 complementaries:crank ["minecraft:block_face"="north"]',
    '/setblock 208 -60 203 stone',
    '/setblock 208 -59 203 complementaries:crank ["minecraft:block_face"="up"]'
)
foreach ($c in $setup) { Invoke-Chat $c }
Start-Sleep -Seconds 3
Save-Screenshot 'blocks' | Out-Null
Invoke-Chat '/tp @s 191.5 -58 200.5 facing 201.5 -60 203.5'
Start-Sleep -Seconds 2
Save-Screenshot 'blocks-side' | Out-Null

# use the crank with a real right-click (`tp facing` aims from the feet: the target y is 1.62 lower)
Invoke-Chat '/tp @s 206.5 -60 200.5 facing 206.5 -61.1 203.75'
Start-Sleep -Seconds 1
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'mechanism: crank at 206 -60 203 power 1' 15 -Since $mark | Out-Null
Start-Sleep -Milliseconds 600
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'mechanism: crank at 206 -60 203 power 2' 15 -Since $mark | Out-Null
Start-Sleep -Milliseconds 500
Save-Screenshot 'crank' | Out-Null

# toggle the turn table by using its side with an empty hand
Invoke-Chat '/tp @s 201.5 -60 200.5 facing 201.5 -61.2 203'
Start-Sleep -Seconds 1
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'mechanism: turn table at 201 -60 203 inverted=true' 15 -Since $mark | Out-Null
Start-Sleep -Milliseconds 500
Save-Screenshot 'turn-table' | Out-Null

Invoke-Chat '/fill 194 -61 202 209 -57 206 air'
Invoke-Chat '/fill 194 -61 202 209 -61 206 grass_block'
Invoke-Chat '/tickingarea remove cm_mechanism'
