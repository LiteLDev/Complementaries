# U13 tableware on a real client: every block rendered from its states, and real right clicks on a
# goblet, a flower box and a pancake through the game's own input.
# Screenshots: out/tableware/{display,display-close,goblet-filled,flower-planted,pancake}.png
# Works in every mode: local, remote-mod and remote-vanilla (no screens; visuals are data driven).

function Set-Display {
    Invoke-Chat '/time set noon'
    Invoke-Chat '/fill -6 -60 -2 6 -55 4 air'
    Invoke-Chat '/setblock -4 -60 1 complementaries:goblet ["cm:fluid"=6]'
    Invoke-Chat '/setblock -3 -60 1 complementaries:goblet ["cm:fluid"=2]'
    Invoke-Chat '/setblock -2 -60 1 complementaries:flower_box ["cm:flower0"=2,"cm:flower1"=11,"minecraft:cardinal_direction"="north"]'
    Invoke-Chat '/setblock -1 -60 1 complementaries:book_pile ["cm:book0"=2,"cm:book1"=7,"cm:book2"=4,"cm:book3"=3,"minecraft:cardinal_direction"="north"]'
    Invoke-Chat '/setblock 0 -60 1 complementaries:book_pile_horizontal ["cm:book0"=1,"cm:book1"=2,"cm:book2"=1]'
    Invoke-Chat '/setblock 1 -60 1 complementaries:pancake ["cm:pancakes"=4,"cm:topping"=1]'
    Invoke-Chat '/setblock 2 -60 1 complementaries:urn'
    Invoke-Chat '/setblock 3 -60 1 complementaries:jar_boat ["minecraft:cardinal_direction"="north"]'
    Invoke-Chat '/setblock 4 -61 1 complementaries:planter'
    Invoke-Chat '/setblock 4 -60 1 wheat ["growth"=7]'
}

Invoke-Chat '/gamemode creative'
Set-Display
Invoke-Chat '/tp @s 0.5 -59.5 -3.5 0 20'
Start-Sleep -Seconds 2
Save-Screenshot 'display' | Out-Null
Invoke-Chat '/tp @s -1.5 -59.5 -1.2 0 35'
Start-Sleep -Seconds 2
Save-Screenshot 'display-close' | Out-Null

# Real right clicks (survival so items are used up).
Invoke-Chat '/fill -6 -60 -2 6 -55 4 air'
Invoke-Chat '/gamemode survival'
Invoke-Chat '/setblock 0 -60 1 complementaries:goblet'
Invoke-Chat '/clear @s'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 honey_bottle 1'
Press-Key 1
Invoke-Chat '/tp @s 0.5 -60 -0.7 0 29'
Start-Sleep -Seconds 1
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'tableware: goblet filled with honey' 15 -Since $mark | Out-Null
Start-Sleep -Seconds 1
Save-Screenshot 'goblet-filled' | Out-Null

Invoke-Chat '/setblock 0 -60 1 complementaries:flower_box ["minecraft:cardinal_direction"="north"]'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 poppy 1'
Press-Key 1
Invoke-Chat '/tp @s 0.5 -60 -0.7 0 32'
Start-Sleep -Seconds 1
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'tableware: planted minecraft:poppy in a flower box' 15 -Since $mark | Out-Null
Start-Sleep -Seconds 1
Save-Screenshot 'flower-planted' | Out-Null

Invoke-Chat '/setblock 0 -60 1 complementaries:pancake'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 complementaries:pancake 3'
Press-Key 1
Invoke-Chat '/tp @s 0.5 -60 -0.7 0 35'
Start-Sleep -Seconds 1
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'tableware: stacked a pancake \(2\)' 15 -Since $mark | Out-Null
Start-Sleep -Seconds 1
Save-Screenshot 'pancake' | Out-Null

Invoke-Chat '/fill -6 -60 -2 6 -55 4 air'
Invoke-Chat '/gamemode creative'

# The world's server (integrated or BDS host) runs the selftests too.
$r = Invoke-SelfTest 'tableware.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) tableware selftest(s) failed" }
