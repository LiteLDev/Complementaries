# U15 cannon on a real client: the cannon block + barrel model, loading by hand, the cannon screen
# (form), a redstone shot with the cannonball in flight, a thrown bomb and the red merchant.
# Screenshots: out/cannon/*.png
Invoke-Chat '/gamerule domobspawning false'
Invoke-Chat '/time set noon'
# face east, looking slightly down at the block 3 east of the player
Invoke-Chat '/tp @s ~ ~ ~ -90 22'
Start-Sleep -Milliseconds 800
Invoke-Chat '/setblock ~3 ~ ~ complementaries:cannon ["minecraft:block_face"="up"]'
Invoke-Chat '/setblock ~3 ~ ~2 complementaries:cannonball'
Start-Sleep -Milliseconds 1500
Save-Screenshot 'cannon-placed' | Out-Null

# load by hand: gunpowder, then a cannonball (each goes to the selected, empty first slot)
Invoke-Chat '/clear @s'
Invoke-Chat '/give @s gunpowder 8'
Start-Sleep -Milliseconds 500
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'Cannon loaded at .* with' 15 -Since $mark | Out-Null
Invoke-Chat '/clear @s'
Invoke-Chat '/give @s complementaries:cannonball 2'
Start-Sleep -Milliseconds 500
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'Cannon loaded at .* with' 15 -Since $mark | Out-Null

# empty hand: the cannon screen
Invoke-Chat '/clear @s'
Start-Sleep -Milliseconds 500
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'Cannon screen opened' 15 -Since $mark | Out-Null
Start-Sleep -Milliseconds 1500
Save-Screenshot 'cannon-screen' | Out-Null
Close-Screen
Start-Sleep -Milliseconds 500

# redstone fires it (fuse, then the shot)
$mark = Get-LogMark
Invoke-Chat '/setblock ~3 ~ ~-1 redstone_block'
Wait-LogLine 'Cannon ignited' 15 -Since $mark | Out-Null
Wait-LogLine 'Cannon fired complementaries:cannonball' 15 -Since $mark | Out-Null
Start-Sleep -Milliseconds 150
Save-Screenshot 'cannon-fired' | Out-Null
Start-Sleep -Milliseconds 2000

# a thrown bomb
Invoke-Chat '/tp @s ~ ~ ~ -90 -10'
Invoke-Chat '/give @s complementaries:bomb 4'
Start-Sleep -Milliseconds 500
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Start-Sleep -Milliseconds 250
Save-Screenshot 'bomb-thrown' | Out-Null
Wait-LogLine 'Bomb exploded: complementaries:bomb ' 15 -Since $mark | Out-Null
Start-Sleep -Milliseconds 300
Save-Screenshot 'bomb-exploded' | Out-Null

# the red merchant
Invoke-Chat '/clear @s'
Invoke-Chat '/tp @s ~ ~ ~ -90 10'
Invoke-Chat '/summon complementaries:red_merchant ~4 ~ ~-1'
Start-Sleep -Milliseconds 2000
Save-Screenshot 'red-merchant' | Out-Null
Invoke-Chat '/kill @e[type=complementaries:red_merchant]'

# the selftests on the integrated server
$r = Invoke-SelfTest 'cannon.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) cannon selftest(s) failed" }
