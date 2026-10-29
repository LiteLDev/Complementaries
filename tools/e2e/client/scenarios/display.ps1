# U12 display on a real client: right-click items onto a pedestal, a statue and an item shelf, put a
# helmet on a hat stand, and screenshot how the displayed items are rendered (data-driven helper
# entities, so the same visuals show on vanilla clients).
# Screenshots: out/display/{pedestal,statue,shelf,hat_stand,overview}.png
# Aim stands at (0.5, -60, 0.5) and looks from the eyes at the point (x, y + 1.62, z): every target
# y below is 1.62 below the point to look at.
function Aim([double]$x, [double]$y, [double]$z) {
    Invoke-Chat "/tp @s 0.5 -60 0.5 $(Get-Look ($x - 0.5) ($y + 60) ($z - 0.5))"
    Start-Sleep -Milliseconds 700
}
function Hold([string]$item) { Invoke-Chat "/replaceitem entity @s slot.weapon.mainhand 0 $item"; Start-Sleep -Milliseconds 400 }
# Press the right button without moving the mouse: a mouse move would turn the camera away from
# the target the /tp command aimed at.
function Use-Center {
    Focus-Game
    Assert-GameForeground
    [BsE2E.Native]::MouseButton(1, $false); Start-Sleep -Milliseconds 60; [BsE2E.Native]::MouseButton(1, $true)
    Start-Sleep -Milliseconds 900
}

Invoke-Chat '/gamemode survival'
Invoke-Chat '/tp @s 0.5 -60 0.5'
Start-Sleep -Milliseconds 800
Invoke-Chat '/fill -3 -60 2 5 -58 5 air'
Invoke-Chat '/setblock 0 -60 3 complementaries:pedestal'
Invoke-Chat '/setblock -2 -60 3 complementaries:statue'
Invoke-Chat '/setblock 2 -59 4 stone'
Invoke-Chat '/setblock 2 -59 3 complementaries:item_shelf'
Invoke-Chat '/summon complementaries:hat_stand 4.5 -60 3.5'
Start-Sleep -Seconds 1

# Pedestal: a sword sticks into it.
$mark = Get-LogMark
Hold 'diamond_sword'
Aim 0.5 -60.62 3.5
Use-Center
Wait-LogLine 'Placed minecraft:diamond_sword on the Pedestal' 15 -Since $mark | Out-Null
Start-Sleep -Seconds 1
Save-Screenshot 'pedestal' | Out-Null

# Statue: a candle lights it up.
$mark = Get-LogMark
Hold 'candle'
Aim -1.5 -61.02 3.5
Use-Center
Wait-LogLine 'Placed minecraft:candle on the Statue' 15 -Since $mark | Out-Null
Start-Sleep -Seconds 1
Save-Screenshot 'statue' | Out-Null

# Item shelf: a book stands on it.
$mark = Get-LogMark
Hold 'book'
Aim 2.5 -60.47 3.9
Use-Center
Wait-LogLine 'Placed minecraft:book on the Item Shelf' 15 -Since $mark | Out-Null
Start-Sleep -Seconds 1
Save-Screenshot 'shelf' | Out-Null

# Hat stand: wear a helmet.
$mark = Get-LogMark
Hold 'iron_helmet'
Aim 4.5 -61.12 3.5
Use-Center
Wait-LogLine 'put minecraft:iron_helmet on a hat stand' 15 -Since $mark | Out-Null
Start-Sleep -Seconds 1
Save-Screenshot 'hat_stand' | Out-Null

# Taking the sword back from the pedestal with an empty hand.
$mark = Get-LogMark
Hold 'air'
Aim 0.5 -60.62 3.5
Use-Center
Wait-LogLine 'Took minecraft:diamond_sword from the Pedestal' 15 -Since $mark | Out-Null
Hold 'end_crystal'
Use-Center
Wait-LogLine 'Placed minecraft:end_crystal on the Pedestal' 15 -Since $mark | Out-Null

Invoke-Chat "/tp @s 1.0 -60 -1.5 $(Get-Look 0 -1.3 5)"
Start-Sleep -Seconds 2
Save-Screenshot 'overview' | Out-Null

$r = Invoke-SelfTest 'display.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) display selftest(s) failed on the client" }
