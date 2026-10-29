# U8 circuit on a real client: the blocks render (relayer, crystal displays, illuminator, speaker,
# minecart with dispenser), the speaker screen opens on use, and the selftests pass on this world's server.
# Screenshots: out/circuit/{blocks,speaker-form,after-form}.png
#
# Layout (the player looks south, +z; the viewer's left is east, +x):
#   z+4: [relayer x3 facing up] [display x1 (tens)] [display x0 (units)] [display x-2 (alone)] [illuminator x-4]
#   z+5: redstone blocks behind the displays and the illuminator (power 15); the relayer is powered from below
#   lamp above the relayer; minecart with dispenser on a rail at x-2, z+2
$cmds = @(
    '/tp @s ~ ~ ~ 0 12',
    '/fill ~-6 ~ ~2 ~6 ~3 ~6 air',
    '/setblock ~-4 ~ ~4 complementaries:redstone_illuminator',
    '/setblock ~-2 ~ ~4 complementaries:crystal_display ["minecraft:cardinal_direction"="north"]',
    '/setblock ~0 ~ ~4 complementaries:crystal_display ["minecraft:cardinal_direction"="north"]',
    '/setblock ~1 ~ ~4 complementaries:crystal_display ["minecraft:cardinal_direction"="north"]',
    '/fill ~-4 ~ ~5 ~1 ~ ~5 redstone_block',
    '/setblock ~3 ~-1 ~4 redstone_block',
    '/setblock ~3 ~ ~4 complementaries:relayer ["minecraft:facing_direction"="up"]',
    '/setblock ~3 ~1 ~4 redstone_lamp',
    '/setblock ~-2 ~ ~2 rail',
    '/summon complementaries:dispenser_minecart ~-2 ~ ~2'
)
foreach ($c in $cmds) { Invoke-Chat $c }
Start-Sleep -Seconds 2
Save-Screenshot 'blocks' | Out-Null

# the speaker screen: place a speaker right under the crosshair and use it
Invoke-Chat '/tp @s ~ ~ ~ 0 0'
Invoke-Chat '/setblock ~ ~1 ~2 complementaries:speaker_block ["minecraft:cardinal_direction"="north"]'
Start-Sleep -Milliseconds 800
Click 0.5 0.5 -Relative -Right
Start-Sleep -Milliseconds 1500
Save-Screenshot 'speaker-form' | Out-Null
Wait-LogLine 'speaker screen opened' 15 | Out-Null
Close-Screen
Save-Screenshot 'after-form' | Out-Null

$r = Invoke-SelfTest 'circuit.*'
if ($r.Pass -lt 10) { throw "expected at least 10 circuit selftests to pass, got $($r.Pass)" }
