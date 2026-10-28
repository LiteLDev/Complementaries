# U11 rope on a real client: block models / textures, item icons, real right-clicks on ropes
# (extend, pull up), the pulley screen and climbing.
# Screenshots: out/rope/{blocks,items,pulley-form,rope-arrow,climb}.png
Invoke-Chat '/gamerule sendcommandfeedback false'
Invoke-Chat '/gamemode creative'
Invoke-Chat '/time set noon'
Invoke-Chat '/tp @s ~ ~ ~ 270 0'   # face east (+x)
Start-Sleep -Seconds 1
Invoke-Chat '/fill ~1 ~ ~-5 ~9 ~7 ~5 air'
# a rope hanging from planks (x+4, z-2)
Invoke-Chat '/setblock ~4 ~5 ~-2 oak_planks'
Invoke-Chat '/setblock ~4 ~4 ~-2 complementaries:rope'
Invoke-Chat '/setblock ~4 ~3 ~-2 complementaries:rope'
Invoke-Chat '/setblock ~4 ~2 ~-2 complementaries:rope'
# a horizontal rope tied to a stone wall, with a rope hanging from it (x+5, z+1..+3)
Invoke-Chat '/setblock ~5 ~3 ~1 stone'
Invoke-Chat '/setblock ~5 ~3 ~2 complementaries:rope'
Invoke-Chat '/setblock ~5 ~3 ~3 complementaries:rope'
Invoke-Chat '/setblock ~5 ~2 ~3 complementaries:rope'
# a pulley (x+4, y+3, z0)
Invoke-Chat '/setblock ~4 ~3 ~0 complementaries:pulley_block'
# flax: farmland and crops of several ages, wild flax and a flax block
Invoke-Chat '/fill ~3 ~-1 ~-1 ~3 ~-1 ~1 farmland'
Invoke-Chat '/setblock ~3 ~ ~-1 complementaries:flax_crop ["complementaries:age"=2]'
Invoke-Chat '/setblock ~3 ~ ~0 complementaries:flax_crop ["complementaries:age"=5]'
Invoke-Chat '/setblock ~3 ~1 ~0 complementaries:flax_crop ["complementaries:age"=5,"complementaries:upper"=true]'
Invoke-Chat '/setblock ~3 ~ ~1 complementaries:flax_crop ["complementaries:age"=7]'
Invoke-Chat '/setblock ~3 ~1 ~1 complementaries:flax_crop ["complementaries:age"=7,"complementaries:upper"=true]'
Invoke-Chat '/setblock ~2 ~ ~3 complementaries:wild_flax'
Invoke-Chat '/setblock ~2 ~ ~-3 complementaries:flax_block'
Start-Sleep -Seconds 4
Save-Screenshot 'blocks' | Out-Null

# Item icons in the hotbar.
Invoke-Chat '/clear @s'
foreach ($i in 'rope 16', 'rope_arrow 1', 'pulley_block 1', 'flax 8', 'flax_seeds 8', 'flax_block 1', 'wild_flax 1') {
    Invoke-Chat "/give @s complementaries:$i"
}
Start-Sleep -Seconds 2
Save-Screenshot 'items' | Out-Null

# Using a rope on a rope extends it downwards (slot 1 holds ropes).
$mark = Get-LogMark
Press-Key 1
Invoke-Chat "/tp @s ~ ~ ~ facing ~4 ~2.4 ~-2"
Start-Sleep -Milliseconds 600
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'rope: .* extended a rope down' 15 -Since $mark | Out-Null

# Pulling a rope up with an empty hand (slot 9 is empty).
$mark = Get-LogMark
Press-Key 9
Start-Sleep -Milliseconds 300
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'rope: .* pulled up a rope' 15 -Since $mark | Out-Null
Start-Sleep -Seconds 1
$pulled = @(Get-LeviLog -Since $mark | Where-Object { $_ -match 'rope: .* pulled up a rope' }).Count
if ($pulled -ne 1) { throw "one click pulled up $pulled ropes (expected 1)" }

# The pulley screen.
$mark = Get-LogMark
Invoke-Chat "/tp @s ~ ~ ~ facing ~4 ~1.9 ~0"   # facing is computed from the feet: aim 1.6 lower
Start-Sleep -Milliseconds 600
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'rope: opened the pulley screen' 15 -Since $mark | Out-Null
Start-Sleep -Seconds 1
Save-Screenshot 'pulley-form' | Out-Null
Close-Screen

# A rope arrow entity (model and texture).
Invoke-Chat '/tp @s ~ ~ ~ 270 20'
Invoke-Chat '/summon complementaries:rope_arrow ~2 ~1.5 ~0.5'
Start-Sleep -Seconds 2
Save-Screenshot 'rope-arrow' | Out-Null

# Climbing: inside the rope column a survival player slides down like on a ladder (the HUD
# position shows the height); the descent speed itself is checked by the rope.climbable selftest.
Invoke-Chat '/gamemode survival'
Invoke-Chat '/tp @s ~4 ~4 ~-2 facing ~5 ~4 ~-2'
Start-Sleep -Milliseconds 300
Save-Screenshot 'climb' | Out-Null
Start-Sleep -Seconds 2
Invoke-Chat '/gamemode creative'

$r = Invoke-SelfTest 'rope.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) rope selftest(s) failed on the client" }
