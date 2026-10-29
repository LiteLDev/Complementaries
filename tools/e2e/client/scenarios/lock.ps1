# U3 lock on a real client: the blocks render (doors, trapdoors, gates, safe, lock block), the item
# icons show, and a right click on a safe opens its storage screen, which stores an item put in it.
# Screenshots: out/lock/{blocks,safe-screen,safe-moved,safe-open,inventory}.png
$S = 'complementaries'
Invoke-Chat '/time set noon'
Invoke-Chat '/fill -2 -60 -6 8 -55 6 air'
Invoke-Chat '/fill -2 -61 -6 8 -61 6 grass_block'
# a showcase row in front of the player (x = 4, z = -4 .. 4); the player looks east (+x)
$blocks = @(
    @(-4, "${S}:lock_block", ''),
    @(-3, "${S}:gold_door", '"complementaries:facing"=1'),
    @(-2, "${S}:netherite_door", '"complementaries:facing"=1,"complementaries:open"=true,"complementaries:hinge"=true'),
    @(-1, "${S}:iron_gate", '"complementaries:facing"=1'),
    @(0, "${S}:safe", '"complementaries:facing"=1'),
    @(1, "${S}:gold_gate", '"complementaries:facing"=3,"complementaries:open"=true'),
    @(2, "${S}:gold_trapdoor", '"complementaries:facing"=1'),
    @(3, "${S}:netherite_trapdoor", '"complementaries:facing"=1,"complementaries:open"=true')
)
foreach ($b in $blocks) {
    $states = if ($b[2]) { " [$($b[2])]" } else { '' }
    Invoke-Chat "/setblock 4 -60 $($b[0]) $($b[1])$states"
    if ($b[1] -like '*_door') {
        $up = if ($b[2]) { "$($b[2]),`"complementaries:upper`"=true" } else { '"complementaries:upper"=true' }
        Invoke-Chat "/setblock 4 -59 $($b[0]) $($b[1]) [$up]"
    }
}
# `tp ... facing` aims from the feet: aim below the safe so the crosshair (eyes) is on it
Invoke-Chat '/tp @s 0.5 -60 0.5 facing 4.5 -61.1 0.5'
Invoke-Chat '/gamerule sendcommandfeedback false'
Start-Sleep -Seconds 2
Save-Screenshot 'blocks' | Out-Null

# right click the safe (centre of the screen): its storage screen (3x9 chest grid titled "Safe")
# opens; the diamonds of hotbar slot 1 are moved into the safe's first slot and stay there
Invoke-Chat '/clear @s'
Invoke-Chat '/give @s diamond 5'
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'lock: .* opened the safe at' 15 -Since $mark | Out-Null
Start-Sleep -Milliseconds 1200
Save-Screenshot 'safe-screen' | Out-Null
Click 0.3648 0.7292 -Relative   # hotbar slot 1: pick up the diamonds
Start-Sleep -Milliseconds 400
Click 0.3648 0.3222 -Relative   # first safe slot: put them down
Start-Sleep -Milliseconds 800
Save-Screenshot 'safe-moved' | Out-Null
Close-Screen
Wait-LogLine 'lock: .* closed the safe at .* \(1 stacks inside\)' 15 -Since $mark | Out-Null
Start-Sleep -Milliseconds 500
Save-Screenshot 'safe-open' | Out-Null

# item icons
Invoke-Chat '/clear @s'
foreach ($id in 'key', 'lock_block', 'safe', 'iron_gate', 'gold_gate', 'gold_door', 'gold_trapdoor', 'netherite_door', 'netherite_trapdoor') {
    Invoke-Chat "/give @s ${S}:$id"
}
Open-Inventory
Save-Screenshot 'inventory' | Out-Null
Close-Screen

$r = Invoke-SelfTest 'lock.*'
if ($r.Pass -lt 10) { throw "expected at least 10 lock selftests to pass, got $($r.Pass)" }
