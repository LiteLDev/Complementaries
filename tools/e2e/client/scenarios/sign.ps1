# U4 sign (client): a real player puts a sign post arm on a fence with the item, writes its text in
# the editor form, flips nothing, then fills a notice board (map) and a doormat (form). Screenshots
# show the arm geometry and the text: flat on the arm / board / mat with the client mod, as a
# floating name tag on a vanilla client (-Mode remote-vanilla).
#   out/sign/{editor,post-front,post-side,board,doormat-editor,overview}.png

# Fractions of the client area of the custom form (calibrated on 1600x900).
$FormFirstInput = 0.333   # centre of the first text box
$FormInputPitch = 0.143   # label + text box
$FormSubmitGap  = 0.115   # last text box -> Submit button

# Stands at (x, -60, z) looking at a point (`tp ... facing` does not turn the local camera, explicit
# rotations do).
function Look-At([double]$x, [double]$z, [double]$tx, [double]$ty, [double]$tz) {
    $eye   = -60 + 1.62
    $dx    = $tx - $x; $dy = $ty - $eye; $dz = $tz - $z
    $yaw   = [Math]::Atan2(-$dx, $dz) * 180 / [Math]::PI
    $pitch = -[Math]::Atan2($dy, [Math]::Sqrt($dx * $dx + $dz * $dz)) * 180 / [Math]::PI
    $inv   = [Globalization.CultureInfo]::InvariantCulture
    Invoke-Chat ('/tp @s {0} -60 {1} {2} {3}' -f $x.ToString($inv), $z.ToString($inv), $yaw.ToString('0.00', $inv), $pitch.ToString('0.00', $inv))
}

# Right click where the crosshair is. (Click moves the mouse first, which turns the camera in game.)
function Use-Item {
    if (Get-Command Send-HeadlessRaw -ErrorAction SilentlyContinue) {
        Send-HeadlessRaw -Action click -Button right | Out-Null   # raw: no cursor move, no camera turn
    } else {
        Focus-Game
        [BsE2E.Native]::MouseButton(1, $false); Start-Sleep -Milliseconds 80; [BsE2E.Native]::MouseButton(1, $true)
    }
    Start-Sleep -Milliseconds 400
}

function Fill-Form([string[]]$texts, [string]$shot) {
    Start-Sleep -Milliseconds 1500
    Save-Screenshot $shot | Out-Null
    $y = $FormFirstInput
    foreach ($t in $texts) {
        if ($t) {
            Click 0.5 $y -Relative
            Start-Sleep -Milliseconds 300
            Type-Text $t
            Start-Sleep -Milliseconds 200
        }
        $y += $FormInputPitch
    }
    Save-Screenshot "$shot-filled" | Out-Null
    Click 0.5 ($y - $FormInputPitch + $FormSubmitGap) -Relative
    Start-Sleep -Milliseconds 1200
}

Invoke-Chat '/gamemode creative'
Invoke-Chat '/time set noon'
Invoke-Chat '/clear @s'
# load the area first: /setblock fails in unloaded chunks
Invoke-Chat '/tp @s 100.5 -60 99.5'
Start-Sleep -Seconds 3
Invoke-Chat '/fill 95 -60 92 106 -56 100 air'
Invoke-Chat '/setblock 100 -60 95 oak_fence'
Invoke-Chat '/setblock 97 -60 95 complementaries:notice_board ["complementaries:facing"=0]'
Invoke-Chat '/setblock 103 -60 97 complementaries:doormat ["complementaries:facing"=0]'
Invoke-Chat '/give @s complementaries:sign_post_spruce 4'
Invoke-Chat '/give @s empty_map 1'
Invoke-Chat '/testforblock 100 -60 95 oak_fence'
Press-Key T; Start-Sleep -Milliseconds 800
Save-Screenshot 'setup-chat' | Out-Null
Press-Key ESC; Start-Sleep -Milliseconds 500
Press-Key 1

# ---- sign post: place an arm with the item, then write in the editor ----------------------------
Look-At 100.5 97.6 100.56 -59.25 95.5
Start-Sleep -Seconds 2
Save-Screenshot 'aim' | Out-Null
$mark = Get-LogMark
Use-Item
Wait-LogLine 'added a spruce sign post arm at' 20 -Since $mark | Out-Null
Wait-LogLine 'opened the sign post editor' 10 -Since $mark | Out-Null
Fill-Form @('Village') 'editor'
Wait-LogLine "sign post text at .* set to 'Village'" 20 -Since $mark | Out-Null

# a second, lower arm pointing the other way: click the lower half on the left of the post
$mark = Get-LogMark
Look-At 100.5 97.6 100.44 -59.8 95.5
Start-Sleep -Seconds 1
Use-Item
Wait-LogLine 'added a spruce sign post arm at' 20 -Since $mark | Out-Null
Wait-LogLine 'opened the sign post editor' 10 -Since $mark | Out-Null
Fill-Form @($null, 'Mine') 'editor-2'
Wait-LogLine "sign post text at .* set to 'Mine'" 20 -Since $mark | Out-Null

Look-At 100.5 98.6 100.5 -59.5 95.5
Start-Sleep -Seconds 2
Save-Screenshot 'post-front' | Out-Null
Look-At 103.4 95.5 100.5 -59.5 95.5
Start-Sleep -Seconds 2
Save-Screenshot 'post-side' | Out-Null

# ---- notice board: put the map on it ------------------------------------------------------------
$mark = Get-LogMark
Press-Key 2
Look-At 97.5 97.6 97.5 -59.5 95.5
Start-Sleep -Seconds 1
Use-Item
Wait-LogLine 'notice board .* holds minecraft:empty_map' 20 -Since $mark | Out-Null
Start-Sleep -Seconds 1
Save-Screenshot 'board' | Out-Null

# ---- doormat: write with an empty hand ----------------------------------------------------------
$mark = Get-LogMark
Press-Key 9
Look-At 103.5 99 103.5 -60 97.5
Start-Sleep -Seconds 1
Use-Item
Wait-LogLine 'opened the doormat editor' 20 -Since $mark | Out-Null
Fill-Form @('Welcome', 'to', 'Complementaries') 'doormat-editor'
Wait-LogLine "doormat text at .* set to 'Welcome'" 20 -Since $mark | Out-Null
Look-At 103.5 99.1 103.5 -60 97.5
Start-Sleep -Seconds 2
Save-Screenshot 'doormat' | Out-Null

Look-At 100.5 100.5 100.5 -59.6 95.5
Start-Sleep -Seconds 2
Save-Screenshot 'overview' | Out-Null

$r = Invoke-SelfTest 'sign.*'
if ($r.Pass -lt 7) { throw "expected 7 sign selftests to pass, got $($r.Pass)" }
