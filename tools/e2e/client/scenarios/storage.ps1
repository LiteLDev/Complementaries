# U2 storage on a real client: the sack screen (vanilla container on the view entity, shown as a
# dispenser grid) stores an item moved into it, the quiver screen, the present form and the block
# models. Works in every mode (local, remote-mod,
# remote-vanilla): all screens are vanilla containers or forms driven by the server.
# Screenshots: out/storage/{sack-placed,sack-screen,sack-moved,sack-closed,quiver-screen,present-form,present-packed}.png
#
# The camera is not turned (teleport rotations do not reach a headless client reliably): blocks are
# placed in a ring around the player at eye height, so the crosshair hits one whatever the yaw is.

# Chat with a longer wait for the chat box (the first characters can get lost otherwise).
function Send-StorageChat([string]$text) {
    Focus-Game
    Press-Key T; Start-Sleep -Milliseconds 1500
    Type-Text $text
    Press-Key ENTER; Start-Sleep -Milliseconds 900
}
$ring = @('~2 ~1 ~', '~-2 ~1 ~', '~ ~1 ~2', '~ ~1 ~-2')
function Set-Ring([string]$block) { foreach ($at in $ring) { Send-StorageChat "/setblock $at $block" } }

Send-StorageChat 'hello'
Send-StorageChat '/clear @s'
Send-StorageChat '/gamemode survival @s'
Send-StorageChat '/effect @s clear'

# ---- sack: open (dispenser grid titled "Sack"), move an item in, close: the sack stores it ----
Send-StorageChat '/give @s gold_ingot 3'
Set-Ring 'complementaries:sack'
Start-Sleep -Milliseconds 800
Save-Screenshot 'sack-placed' | Out-Null
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'storage: .* opened the sack at' 20 -Since $mark | Out-Null
Start-Sleep -Milliseconds 1200
Save-Screenshot 'sack-screen' | Out-Null
Click 0.3648 0.7292 -Relative   # hotbar slot 1: pick up the gold
Start-Sleep -Milliseconds 400
Click 0.4664 0.3042 -Relative   # first sack slot (top left of the 3x3 grid): put it down
Start-Sleep -Milliseconds 800
Save-Screenshot 'sack-moved' | Out-Null
Close-Screen
Wait-LogLine 'storage: the sack at .* was closed \(1 stacks inside\)' 20 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
Save-Screenshot 'sack-closed' | Out-Null
Set-Ring 'air'
Send-StorageChat '/kill @e[type=item]'
Send-StorageChat '/clear @s'

# ---- quiver: use in the air opens its screen ----
Send-StorageChat '/give @s complementaries:quiver 1'
Start-Sleep -Milliseconds 500
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'storage: .* opened the complementaries:quiver screen' 20 -Since $mark | Out-Null
Start-Sleep -Milliseconds 1200
Save-Screenshot 'quiver-screen' | Out-Null
Close-Screen
Send-StorageChat '/clear @s'

# ---- present: a packed public present unpacks and shows the packing form ----
# (packed presents are 14 px tall, so the horizontal crosshair hits them)
Set-Ring 'complementaries:present_red["complementaries:packed"=true]'
Start-Sleep -Milliseconds 800
Save-Screenshot 'present-packed' | Out-Null
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'storage: .* opened the present at' 20 -Since $mark | Out-Null
Start-Sleep -Milliseconds 2500
Save-Screenshot 'present-form' | Out-Null
Close-Screen
Start-Sleep -Milliseconds 800
Save-Screenshot 'present-opened' | Out-Null
Set-Ring 'air'
Send-StorageChat '/kill @e[type=item]'

# ---- the server-side behaviour on this world ----
$r = Invoke-SelfTest 'storage.*'
if ($r.Pass -lt 11) { throw "expected at least 11 storage selftests to pass, got $($r.Pass)" }
