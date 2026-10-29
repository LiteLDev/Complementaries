# U1 jar on a real client: the jar / faucet visuals (fluids, cookies, mobs drawn by the helper
# entity, faucet streams), right clicks with buckets and bottles, and catching a mob with a jar.
# Screenshots: out/jar/{demo-row,demo-mobs,demo-faucets,water-jar,caught}.png
$r = Invoke-SelfTest 'jar.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) jar selftest(s) failed" }

Invoke-Chat '/gamemode creative'
Invoke-Chat '/time set noon'

# ---- the showcase: filled jars, mobs and running faucets ----
$mark = Get-LogMark
Invoke-Chat '/supplementaries jardemo'
Wait-LogLine 'jar: demo built' 20 -Since $mark | Out-Null
Start-Sleep -Seconds 3
Invoke-Chat "/tp @s ~ ~1 ~-1 $(Get-Look 0 -3.62 5)"
Start-Sleep -Seconds 2
Save-Screenshot 'demo-row' | Out-Null
Invoke-Chat "/tp @s ~5 ~-0.5 ~2 $(Get-Look -5 -2.32 1)"
Start-Sleep -Seconds 2
Save-Screenshot 'demo-mobs' | Out-Null
Invoke-Chat "/tp @s ~-13 ~0.5 ~ $(Get-Look 13 -3.12 3)"
Start-Sleep -Seconds 3
Save-Screenshot 'demo-faucets' | Out-Null

# ---- right clicks on a placed jar (on clear ground away from the demo and its water) ----
Invoke-Chat '/fill 50 -60 -3 58 -56 6 air'
Invoke-Chat '/tp @s 50.5 -60 0.5'
Start-Sleep -Milliseconds 800
Invoke-Chat '/setblock ~ ~ ~2 complementaries:jar'
Invoke-Chat '/replaceitem entity @s slot.weapon.mainhand 0 water_bucket'
Face-Offset 0 0.4 2
Start-Sleep -Milliseconds 600
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'jar: .* poured water into the jar' 15 -Since $mark | Out-Null
Invoke-Chat '/replaceitem entity @s slot.weapon.mainhand 0 glass_bottle'
Face-Offset 0 0.4 2
Start-Sleep -Milliseconds 600
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'jar: .* filled minecraft:potion from the jar' 15 -Since $mark | Out-Null
Start-Sleep -Seconds 1
Save-Screenshot 'water-jar' | Out-Null

# ---- catching a mob with a jar item (left click) ----
Invoke-Chat '/replaceitem entity @s slot.weapon.mainhand 0 complementaries:jar'
Invoke-Chat '/tp @s ~3 ~ ~'
# the frog sits in a one-block stone cell open towards the player (slowed, and the roof stops its
# jumps) so it cannot hop out of the crosshair
Invoke-Chat '/fill 52 -60 2 54 -59 3 stone'
Invoke-Chat '/setblock 53 -60 2 air'
Invoke-Chat '/summon minecraft:frog 53.5 -60 2.5'
Invoke-Chat '/effect @e[type=frog,r=4] slowness 60 10 true'
Face-Offset 0 0.25 2
Start-Sleep -Milliseconds 500
$mark = Get-LogMark
Click 0.5 0.5 -Relative
Wait-LogLine 'jar: .* caught a minecraft:frog' 15 -Since $mark | Out-Null
Start-Sleep -Seconds 1
Save-Screenshot 'caught' | Out-Null
