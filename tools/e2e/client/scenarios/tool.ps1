# U16 tool on a real client: the tool.* selftests on the integrated server, then a real player
# charges and fires the slingshot, pops a confetti popper, blows bubbles and places a bubble block,
# and makes a slice map. Screenshots: out/tool/*.png
$r = Invoke-SelfTest 'tool.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 15) { throw "tool selftests: pass=$($r.Pass) fail=$($r.Fail)" }

# Holds the use button for $ms, then releases it. Headless runs do it in one request (the input pipe
# does not answer a second connection while the button is held); -Foreground uses SendInput.
function Use-Held([int]$ms) {
    Move-Mouse 0.5 0.5 -Relative
    if (Get-Command Send-HeadlessRaw -ErrorAction SilentlyContinue) {
        Send-HeadlessRaw -Action click -Button right -HoldMs $ms
    } else {
        [BsE2E.Native]::MouseButton(1, $false)
        Start-Sleep -Milliseconds $ms
        [BsE2E.Native]::MouseButton(1, $true)
    }
    Start-Sleep -Milliseconds 200
}

Invoke-Chat '/clear @s'
Invoke-Chat '/gamemode survival @s'
Invoke-Chat '/tp @s ~ ~ ~ 0 -5'
Invoke-Chat '/give @s complementaries:slingshot'
Invoke-Chat '/give @s cobblestone 16'
Invoke-Chat '/give @s complementaries:confetti_popper 4'
Invoke-Chat '/give @s complementaries:bubble_blower'
Invoke-Chat '/give @s complementaries:empty_slice_map'
Invoke-Chat '/give @s complementaries:wrench'
Start-Sleep -Milliseconds 500

# slingshot: hold to charge (bow pose), release to fire the cobblestone
Press-Key '1'
$mark = Get-LogMark
Use-Held 1400
Start-Sleep -Milliseconds 150
Save-Screenshot 'slingshot-flying' | Out-Null
Wait-LogLine 'slingshot fired 1 projectile' 10 -Since $mark | Out-Null
Wait-LogLine 'slingshot placed minecraft:cobblestone' 15 -Since $mark | Out-Null
Start-Sleep -Milliseconds 500
Save-Screenshot 'slingshot-landed' | Out-Null

# confetti popper
Press-Key '3'
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Start-Sleep -Milliseconds 250
Save-Screenshot 'confetti' | Out-Null
Wait-LogLine 'confetti popper used' 10 -Since $mark | Out-Null

# bubble blower: blow into the air, then place a bubble block on the ground
Press-Key '4'
Use-Held 1500
Save-Screenshot 'bubbles' | Out-Null
Invoke-Chat '/tp @s ~ ~ ~ 0 50'
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'bubble block placed' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 400
Save-Screenshot 'bubble-block' | Out-Null

# slice map: use the empty map, hold the filled one
Invoke-Chat '/tp @s ~ ~ ~ 0 60'
Press-Key '5'
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'slice map -?\d+ created' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 2500
Save-Screenshot 'slice-map' | Out-Null

# wrench in hand (item icon)
Press-Key '6'
Invoke-Chat '/tp @s ~ ~ ~ 0 0'
Start-Sleep -Milliseconds 400
Save-Screenshot 'wrench' | Out-Null
Assert-NoErrors
