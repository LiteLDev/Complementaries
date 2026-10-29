# U17 trap on a real client: the block models (spikes in every direction, tipped tips, fodder
# layers), catching a chicken with a cage, placing the full cage (the chicken shows inside),
# releasing it, and tipping spikes with a lingering potion.
# Screenshots: out/trap/{gallery,caught,cage-full,tipped}.png

function Mouse([int]$button) {
    Assert-GameForeground
    [BsE2E.Native]::MouseButton($button, $false); Start-Sleep -Milliseconds 80; [BsE2E.Native]::MouseButton($button, $true)
    Start-Sleep -Milliseconds 400
}

function SneakUse {
    Assert-GameForeground
    [BsE2E.Native]::Key([uint16]0x10, $false); Start-Sleep -Milliseconds 400
    [BsE2E.Native]::MouseButton(1, $false); Start-Sleep -Milliseconds 80; [BsE2E.Native]::MouseButton(1, $true)
    Start-Sleep -Milliseconds 400
    [BsE2E.Native]::Key([uint16]0x10, $true); Start-Sleep -Milliseconds 300
}

Invoke-Chat '/gamemode creative'
Invoke-Chat '/clear @s'
Invoke-Chat '/kill @e[type=!player]'
# a fixed spot: face south over the flat ground
Invoke-Chat '/tp @s 0 -60 0 0 20'
Start-Sleep -Seconds 2

# ---- gallery ---------------------------------------------------------------------------------
Invoke-Chat '/fill -5 -60 3 4 -57 6 air'
Invoke-Chat '/setblock -2 -60 4 complementaries:bamboo_spikes ["minecraft:block_face"="up"]'
Invoke-Chat '/setblock -1 -60 4 complementaries:bamboo_spikes ["minecraft:block_face"="up","complementaries:tip"=1]'
Invoke-Chat '/setblock 0 -59 5 stone'
Invoke-Chat '/setblock 0 -59 4 complementaries:bamboo_spikes ["minecraft:block_face"="north"]'
Invoke-Chat '/setblock 1 -60 4 complementaries:fodder'
Invoke-Chat '/setblock 2 -60 4 complementaries:fodder ["complementaries:eaten"=5]'
Invoke-Chat '/setblock -3 -59 4 stone'
Invoke-Chat '/setblock -3 -59 3 complementaries:bamboo_spikes ["minecraft:block_face"="north","complementaries:tip"=2]'
Invoke-Chat '/setblock 4 -59 4 stone'
Invoke-Chat '/setblock 3 -59 4 complementaries:bamboo_spikes ["minecraft:block_face"="west","complementaries:tip"=8]'
Invoke-Chat '/setblock -5 -59 4 stone'
Invoke-Chat '/setblock -4 -59 4 complementaries:bamboo_spikes ["minecraft:block_face"="east"]'
Invoke-Chat '/setblock 1 -57 6 stone'
Invoke-Chat '/setblock 1 -58 6 complementaries:bamboo_spikes ["minecraft:block_face"="down"]'
Start-Sleep -Seconds 2
Save-Screenshot 'gallery' | Out-Null

# ---- cage: catch -----------------------------------------------------------------------------
Invoke-Chat '/give @s complementaries:cage 1'
Press-Key '1'
# the chicken waits in a glass ring in front of the player, who looks at it from above
Invoke-Chat '/fill 5 -60 1 7 -60 3 glass'
Invoke-Chat '/setblock 6 -60 2 air'
Invoke-Chat '/summon chicken 6.5 -60 2.5'
Invoke-Chat '/effect @e[type=chicken] slowness 60 255 true'
Invoke-Chat '/tp @s 6.5 -58 0.5 0 55'
Start-Sleep -Milliseconds 500
$mark = Get-LogMark
Mouse 0  # hit the chicken with the cage
$caught = $false
try { Wait-LogLine 'caught minecraft:chicken in a cage' 5 -Since $mark | Out-Null; $caught = $true } catch {}
if (-not $caught) {
    Mouse 1  # or use it on the chicken
    Wait-LogLine 'caught minecraft:chicken in a cage' 10 -Since $mark | Out-Null
}
Start-Sleep -Seconds 1
Save-Screenshot 'caught' | Out-Null

# ---- cage: place (sneaking) with the chicken inside -------------------------------------------
Invoke-Chat '/tp @s 9.5 -60 0.5 0 60'
Start-Sleep -Milliseconds 500
$mark = Get-LogMark
SneakUse
Wait-LogLine 'cage with Chicken placed at 0:9:-60:1' 10 -Since $mark | Out-Null
Invoke-Chat '/tp @s 9.5 -59.5 -1.2 0 25'
Start-Sleep -Seconds 2
Save-Screenshot 'cage-full' | Out-Null

# ---- cage: release (not sneaking); creative keeps the mob in the item --------------------------
Invoke-Chat '/tp @s 12.5 -60 0.5 0 60'
Start-Sleep -Milliseconds 500
$mark = Get-LogMark
Mouse 1
Wait-LogLine 'released minecraft:chicken from a cage' 10 -Since $mark | Out-Null
Start-Sleep -Seconds 1
$releases = @(Get-LeviLog -Since $mark | Where-Object { $_ -match 'released minecraft:chicken' }).Count
if ($releases -ne 1) { throw "one click released $releases chickens" }

# ---- tipped spikes: use a lingering poison potion on plain spikes ------------------------------
Invoke-Chat '/clear @s'
Invoke-Chat '/setblock 15 -60 2 complementaries:bamboo_spikes ["minecraft:block_face"="up"]'
Invoke-Chat '/give @s lingering_potion 1 25'
Press-Key '1'
Invoke-Chat '/tp @s 15.5 -60 0.5 0 35'
Start-Sleep -Milliseconds 500
$mark = Get-LogMark
Mouse 1
Wait-LogLine 'tipped with potion 25' 10 -Since $mark | Out-Null
Start-Sleep -Seconds 1
Save-Screenshot 'tipped' | Out-Null
# ---- selftests on the integrated server ---------------------------------------------------------
$r = Invoke-SelfTest 'trap.*'
if ($r.Pass -lt 9) { throw "expected at least 9 trap selftests to pass, got $($r.Pass)" }
