# U5 blackboard on a real client: drawings rendered by the display entities (all four facings,
# colours, glow), direct drawing with a dye, the edit screen and the item tooltip.
# Screenshots: out/blackboard/*.png
$r = Invoke-SelfTest 'blackboard.*'
if ($r.Pass -lt 10) { throw "expected at least 10 blackboard selftests to pass, got $($r.Pass)" }

# A pattern that shows the orientation: white top row, red left column, yellow diagonal.
function New-Pattern {
    $px = New-Object 'char[]' 256
    for ($i = 0; $i -lt 256; $i++) { $px[$i] = '0' }
    for ($x = 0; $x -lt 16; $x++) { $px[$x] = '1' }
    for ($y = 0; $y -lt 16; $y++) { $px[$y * 16] = 'e' }
    for ($d = 1; $d -lt 16; $d++) { $px[$d * 16 + $d] = '4' }
    -join $px
}
$pattern = New-Pattern

function Set-Board([int]$x, [int]$y, [int]$z, [string]$facing, [string]$hex) {
    Invoke-Chat "/setblock $x $y $z complementaries:blackboard [`"minecraft:cardinal_direction`"=`"$facing`"]"
    if ($hex) {
        $mark = Get-LogMark
        Invoke-Chat "/supplementaries blackboard set $x $y $z `"$hex`""
        Wait-LogLine "blackboard: pixels set at 0:${x}:${y}:${z}" 20 -Since $mark | Out-Null
    }
}

Invoke-Chat '/gamemode creative'
Invoke-Chat '/tp @s 0.5 -60 -1.5 0 8'
# a 3x2 wall facing the player (north), plus one board facing east and one facing west
Set-Board -1 -59 3 'north' $pattern
Set-Board 0 -59 3 'north' $pattern
Set-Board 1 -59 3 'north' ''
Set-Board -1 -60 3 'north' ''
Set-Board 0 -60 3 'north' ''
Set-Board 1 -60 3 'north' ''
Start-Sleep -Seconds 2
Save-Screenshot 'wall-north' | Out-Null

Set-Board -2 -60 0 'east' $pattern
Set-Board 3 -60 0 'west' $pattern
Invoke-Chat '/tp @s 1.5 -60 0.5 90 20'
Start-Sleep -Seconds 2
Save-Screenshot 'board-east' | Out-Null
Invoke-Chat '/tp @s -0.5 -60 0.5 -90 20'
Start-Sleep -Seconds 2
Save-Screenshot 'board-west' | Out-Null
Set-Board 0 -60 -3 'south' $pattern
Invoke-Chat '/tp @s 0.5 -60 0.5 180 20'
Start-Sleep -Seconds 2
Save-Screenshot 'board-south' | Out-Null

# Direct drawing: white dye on the blank board straight ahead (1 -60 3 is the lower right one).
Invoke-Chat '/tp @s 1.5 -60 1.2 0 29'
Invoke-Chat '/replaceitem entity @s slot.weapon.mainhand 0 white_dye'
Start-Sleep -Milliseconds 800
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'blackboard: drew 1 at 0:1:-60:3' 15 -Since $mark | Out-Null
Start-Sleep -Seconds 1
Save-Screenshot 'drawn-by-hand' | Out-Null

# Glow ink and wax on the top left board.
Invoke-Chat '/tp @s -0.5 -60 1.2 0 3'
Invoke-Chat '/replaceitem entity @s slot.weapon.mainhand 0 glow_ink_sac'
Start-Sleep -Milliseconds 800
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'blackboard: glowing at 0:-1:-59:3' 15 -Since $mark | Out-Null
Invoke-Chat '/time set midnight'
Start-Sleep -Seconds 2
Save-Screenshot 'glowing-night' | Out-Null
Invoke-Chat '/time set noon'

# The edit screen: empty hand on the bottom middle board (connected 3x2 canvas).
Invoke-Chat '/tp @s 0.5 -60 1.2 0 29'
Invoke-Chat '/replaceitem entity @s slot.weapon.mainhand 0 air'
Start-Sleep -Milliseconds 800
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'blackboard: editor opened at 0:0:-60:3' 15 -Since $mark | Out-Null
Start-Sleep -Seconds 2
Save-Screenshot 'editor' | Out-Null
Move-Mouse 0.5 0.5 -Relative
Scroll -10
Start-Sleep -Milliseconds 800
Save-Screenshot 'editor-scrolled' | Out-Null
Close-Screen
Start-Sleep -Seconds 1

# The item keeps the drawing: break the top middle board (drops with destroy) and pick it up.
Invoke-Chat '/clear @s'
Invoke-Chat '/gamemode survival'
Invoke-Chat '/setblock 0 -59 3 air destroy'
Invoke-Chat '/tp @s 0.5 -60 2.5'
Start-Sleep -Seconds 2
Open-Inventory
Save-Screenshot 'inventory' | Out-Null
Close-Screen
Invoke-Chat '/gamemode creative'
