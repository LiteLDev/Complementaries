# U10 building on a real client: right-click interactions of a real player (hoe on gravel, slab on
# slab, ash on ash, daub into a timber frame), then a showcase of every block for the screenshots.
# Works in every mode (local, remote-mod, remote-vanilla: the blocks are data-driven and the
# mechanics run on the server).
# Screenshots: out/building/{interactions,showcase-front,showcase-side,showcase-top}.png

function Use-On([string]$Item, [string]$Block, [double[]]$Look, [string]$Pattern, [string]$Name) {
    # target block two blocks south of the (centred) player, at foot level
    Invoke-Chat "/setblock ~ ~ ~2 $Block"
    Invoke-Chat "/replaceitem entity @s slot.hotbar 0 $Item"
    Face-Offset $Look[0] $Look[1] $Look[2]
    Start-Sleep -Milliseconds 600
    $mark = Get-LogMark
    Click 0.5 0.5 -Relative -Right
    Wait-LogLine $Pattern 15 -Since $mark | Out-Null
    Start-Sleep -Milliseconds 500
    Save-Screenshot $Name | Out-Null
}

Invoke-Chat '/gamemode creative'
Invoke-Chat '/time set noon'
Invoke-Chat '/weather clear'
# stand in the middle of a block and clear some room
Invoke-Chat '/execute as @s at @s align xz run tp @s ~0.5 ~ ~0.5'
Invoke-Chat '/fill ~-9 ~ ~-2 ~9 ~4 ~9 air'
Start-Sleep -Milliseconds 800

# hoe on gravel (north face) -> raked gravel
Use-On 'minecraft:iron_hoe' 'minecraft:gravel' @(0, 0.75, 2.5) 'building: raked gravel at' 'use-hoe'
# slab on the top face of a bottom slab -> double slab
Use-On 'complementaries:stone_tile_slab 4' 'complementaries:stone_tile_slab' @(0, 0.25, 2.5) 'building: double slab' 'use-slab'
# ash on ash -> one more layer
Use-On 'complementaries:ash 4' 'complementaries:ash ["complementaries:layers"=2]' @(0, 0.1, 2.5) 'building: ash layer added' 'use-ash'
# daub into a timber brace
Use-On 'complementaries:daub 4' 'complementaries:timber_brace' @(0, 0.5, 2.5) 'building: filled complementaries:timber_brace with daub' 'use-daub'
Invoke-Chat '/setblock ~ ~ ~2 air'
# a stair placed by the player faces the way the player looks (south here)
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 complementaries:stone_tile_stairs 4'
Face-Offset 0 -0.5 2.5
Start-Sleep -Milliseconds 600
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
$line = Wait-LogLine 'building: placed complementaries:stone_tile_stairs facing' 15 -Since $mark
Write-Step "stairs: $line"
if ($line -notmatch 'facing 0 ') { throw "the stairs should face south (0): $line" }
Start-Sleep -Milliseconds 500
Save-Screenshot 'use-stairs' | Out-Null
Invoke-Chat '/fill ~-1 ~ ~1 ~1 ~1 ~3 air'

# ---- showcase ----------------------------------------------------------------------------------
# back row: every full block
$full = @('ash_bricks', 'stone_tile', 'blackstone_tile', 'lapis_bricks', 'checker_block', 'fine_wood', 'daub', 'daub_frame',
    'daub_brace', 'daub_cross_brace', 'timber_frame', 'timber_brace', 'timber_cross_brace', 'soap_block', 'flint_block', 'feather_block', 'sugar_cube')
$x = -8
foreach ($b in $full) { Invoke-Chat "/setblock ~$x ~ ~7 complementaries:$b"; $x++ }
# middle row: stairs (straight, inner and outer corners), slabs, walls
Invoke-Chat '/setblock ~-8 ~ ~5 complementaries:ash_bricks_stairs ["minecraft:cardinal_direction"="south"]'
Invoke-Chat '/setblock ~-7 ~ ~5 complementaries:ash_bricks_stairs ["minecraft:cardinal_direction"="south"]'
Invoke-Chat '/setblock ~-6 ~ ~5 complementaries:ash_bricks_stairs ["minecraft:cardinal_direction"="west"]'
Invoke-Chat '/setblock ~-6 ~ ~4 complementaries:ash_bricks_stairs ["minecraft:cardinal_direction"="west"]'
Invoke-Chat '/setblock ~-4 ~ ~5 complementaries:stone_tile_stairs ["minecraft:cardinal_direction"="north"]'
Invoke-Chat '/setblock ~-3 ~ ~5 complementaries:lapis_bricks_stairs ["minecraft:cardinal_direction"="north","minecraft:vertical_half"="top"]'
Invoke-Chat '/setblock ~-2 ~ ~5 complementaries:fine_wood_stairs ["minecraft:cardinal_direction"="east"]'
Invoke-Chat '/setblock ~0 ~ ~5 complementaries:checker_slab'
Invoke-Chat '/setblock ~1 ~ ~5 complementaries:blackstone_tile_slab ["minecraft:vertical_half"="top"]'
Invoke-Chat '/setblock ~2 ~ ~5 complementaries:fine_wood_slab ["complementaries:double"=true]'
Invoke-Chat '/setblock ~4 ~ ~5 complementaries:stone_tile_wall'
Invoke-Chat '/setblock ~5 ~ ~5 complementaries:stone_tile_wall'
Invoke-Chat '/setblock ~6 ~ ~5 complementaries:ash_bricks_wall'
Invoke-Chat '/setblock ~6 ~ ~6 complementaries:lapis_bricks_wall'
Invoke-Chat '/setblock ~7 ~ ~5 minecraft:cobblestone_wall'
Invoke-Chat '/setblock ~8 ~ ~5 complementaries:blackstone_tile_wall'
Invoke-Chat '/setblock ~8 ~1 ~5 complementaries:blackstone_tile_wall'
# front row: ash layers 1..8 and raked gravel patterns
for ($i = 1; $i -le 8; $i++) { Invoke-Chat "/setblock ~$($i - 9) ~ ~3 complementaries:ash [`"complementaries:layers`"=$i]" }
Invoke-Chat '/fill ~1 ~-1 ~3 ~4 ~-1 ~3 minecraft:stone'
Invoke-Chat '/setblock ~1 ~ ~3 complementaries:raked_gravel'
Invoke-Chat '/setblock ~2 ~ ~3 complementaries:raked_gravel'
Invoke-Chat '/setblock ~3 ~ ~3 complementaries:raked_gravel'
Invoke-Chat '/setblock ~3 ~ ~2 complementaries:raked_gravel'
Start-Sleep -Seconds 2

Invoke-Chat "/tp @s ~ ~3 ~-3 $(Get-Look 0 -4.62 8)"
Start-Sleep -Seconds 2
Save-Screenshot 'showcase-front' | Out-Null
Invoke-Chat "/tp @s ~-11 ~1 ~8 $(Get-Look 11 -5.62 0)"
Start-Sleep -Seconds 2
Save-Screenshot 'showcase-side' | Out-Null
Invoke-Chat "/tp @s ~11 ~6 ~-1 $(Get-Look 0 -11.62 1)"
Start-Sleep -Seconds 2
Save-Screenshot 'showcase-top' | Out-Null

# the server-side tests on this world too (local: the integrated server; remote: the BDS host)
$r = Invoke-SelfTest 'building.*' -TimeoutSeconds 900
if ($r.Fail -gt 0 -or $r.Pass -lt 13) { throw "building selftests: pass=$($r.Pass) fail=$($r.Fail)" }
