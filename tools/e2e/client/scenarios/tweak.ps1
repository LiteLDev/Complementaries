# U18 tweak on a real client: a real player places sticks, gunpowder and a hanging pot by right-clicking,
# lights the trail, rings a bell through its chain and throws a brick; the models are checked on
# screenshots (out/tweak/*.png). The selftests run on the integrated server too.
# Flat world: grass top at y = -61, the player stands at y = -60.

# Stands at (x, y, z) and looks at the point (px, py, pz). `tp ... facing` aims from the feet, so the
# target is shifted by the eye height to put that point under the crosshair.
function Look([double]$x, [double]$y, [double]$z, [double]$px, [double]$py, [double]$pz) {
    $fy = $py - 1.62
    Invoke-Chat "/tp @s $x $y $z facing $px $fy $pz"
    Start-Sleep -Milliseconds 700
}
function Hold([string]$item, [int]$count = 1) {
    if ($item) { Invoke-Chat "/replaceitem entity @s slot.hotbar 0 $item $count" } else { Invoke-Chat '/replaceitem entity @s slot.hotbar 0 air' }
    Start-Sleep -Milliseconds 400
}
function Use-Item { Click 0.5 0.5 -Relative -Right; Start-Sleep -Milliseconds 600 }

Invoke-Chat '/gamemode survival'
Invoke-Chat '/time set noon'
Invoke-Chat '/gamerule dodaylightcycle false'
Invoke-Chat '/fill -2 -60 -2 20 -55 8 air'
Invoke-Chat '/clear @s'
Press-Key 1

# ---- placeable sticks: one standing, then a second one across it ---------------------------------------
$mark = Get-LogMark
Hold 'stick' 8
Look 0.5 -60 0.5 0.5 -60 2.5
Use-Item
Wait-LogLine 'tweak: .* placed complementaries:stick at 0 -60 2' 15 -Since $mark | Out-Null
Hold 'blaze_rod' 4
Look 1.5 -60 0.5 1.5 -60 2.5
Use-Item
Wait-LogLine 'tweak: .* placed complementaries:blaze_rod at 1 -60 2' 15 -Since $mark | Out-Null

# ---- gunpowder trail, lit with flint and steel ----------------------------------------------------------
$mark = Get-LogMark
Hold 'gunpowder' 8
Look 4.5 -60 0.5 4.5 -60 2.5
Use-Item
Look 4.5 -60 0.5 5.5 -60 2.5
Use-Item
Look 4.5 -60 0.5 6.5 -60 2.5
Use-Item
Wait-LogLine 'tweak: .* placed gunpowder at 6 -60 2' 15 -Since $mark | Out-Null
Look 5.5 -59 -1.5 5.5 -60 2.5
Save-Screenshot 'tweak-gunpowder' | Out-Null
Hold 'flint_and_steel'
Look 4.5 -60 0.5 4.5 -60.0 2.5
Use-Item
Wait-LogLine 'tweak: .* lit gunpowder at 4 -60 2' 15 -Since $mark | Out-Null
Look 5.5 -59 -1.5 5.5 -60 2.5
Save-Screenshot 'tweak-gunpowder-burning' | Out-Null

# ---- hanging flower pot under a block, with a poppy -----------------------------------------------------
$mark = Get-LogMark
Invoke-Chat '/setblock 9 -57 2 stone'
Hold 'flower_pot' 2
Look 9.5 -60 0.5 9.5 -57 2.5
Use-Item
Wait-LogLine 'tweak: .* hung a flower pot at 9 -58 2' 15 -Since $mark | Out-Null
Hold 'poppy' 1
Look 9.5 -60 0.5 9.5 -57.6 2.5
Use-Item
Wait-LogLine 'tweak: .* planted minecraft:poppy in a hanging pot' 15 -Since $mark | Out-Null

# ---- bell rung through the chain hanging below it -------------------------------------------------------
$mark = Get-LogMark
Invoke-Chat '/setblock 12 -57 2 stone'
Invoke-Chat '/setblock 12 -58 2 bell ["attachment"="hanging"]'
Invoke-Chat '/setblock 12 -59 2 iron_chain'
Invoke-Chat '/setblock 12 -59 2 chain'
Hold ''
Look 12.5 -60 0.5 12.5 -58.5 2.5
Use-Item
Wait-LogLine 'tweak: rang the bell at 12 -58 2' 15 -Since $mark | Out-Null

# ---- overview of the placed blocks ----------------------------------------------------------------------
Look 6.5 -58.5 -3.5 6.5 -59.5 2.5
Save-Screenshot 'tweak-overview' | Out-Null
Look 9.5 -59 0 9.5 -58 2.5
Save-Screenshot 'tweak-hanging-pot' | Out-Null
Look 0.5 -59 0.0 1 -60 2.5
Save-Screenshot 'tweak-sticks' | Out-Null

# ---- a thrown brick breaks glass; the clock tells the time ----------------------------------------------
$mark = Get-LogMark
Invoke-Chat '/setblock 16 -59 5 glass'
Hold 'brick' 4
Look 16.5 -60 0.5 16.5 -58.5 5.5
Use-Item
Wait-LogLine 'tweak: .* threw minecraft:brick' 15 -Since $mark | Out-Null
Start-Sleep -Milliseconds 1200
Save-Screenshot 'tweak-brick' | Out-Null
Hold 'clock'
Look 16.5 -60 0.5 16.5 -40 10
Use-Item
Start-Sleep -Milliseconds 300
Save-Screenshot 'tweak-clock' | Out-Null

$r = Invoke-SelfTest 'tweak.*' -TimeoutSeconds 600
if ($r.Pass -lt 12) { throw "expected at least 12 tweak selftests to pass, got $($r.Pass)" }
