# Draws the textures and writes the models / block definitions of the U13 `tableware` unit
# (goblet, urn, planter, flower box, book piles, pancake, candy, jar boat).
#
#   pwsh -NoProfile -File tools/textures/tableware.ps1
#
# Everything here is original pixel art and geometry; the flower box shows vanilla flower textures
# by reference (resource pack paths of the vanilla pack, nothing is copied).

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lib.ps1"

$root  = Resolve-Path "$PSScriptRoot/../.."
$rp    = Join-Path $root 'assets/pack/rp'
$bp    = Join-Path $root 'assets/pack/bp'
$tex   = Join-Path $rp 'textures/blocks/tableware'
$items = Join-Path $rp 'textures/items/tableware'
$geo   = Join-Path $rp 'models/blocks/tableware'
New-Item -ItemType Directory -Force $tex, $items, $geo, (Join-Path $bp 'blocks') | Out-Null

# ---- canvas helpers ---------------------------------------------------------------------------

function New-Canvas([int]$W = 16, [int]$H = 16, [string]$Fill = '.') {
    $c = New-Object 'char[][]' $H
    for ($y = 0; $y -lt $H; $y++) { $c[$y] = ($Fill * $W).ToCharArray() }
    return , $c
}
function Set-Px($C, [int]$X, [int]$Y, [string]$Ch) {
    if ($Y -ge 0 -and $Y -lt $C.Count -and $X -ge 0 -and $X -lt $C[0].Count) { $C[$Y][$X] = $Ch[0] }
}
function Fill-Rect($C, [int]$X0, [int]$Y0, [int]$X1, [int]$Y1, [string]$Ch) {
    for ($y = $Y0; $y -le $Y1; $y++) { for ($x = $X0; $x -le $X1; $x++) { Set-Px $C $x $y $Ch } }
}
# Random pick among $Keys (weights optional) in a rectangle, deterministic per seed.
function Noise-Rect($C, [int]$X0, [int]$Y0, [int]$X1, [int]$Y1, [string[]]$Keys, [int]$Seed, [int[]]$Weights) {
    $rnd = [System.Random]::new($Seed)
    if (-not $Weights) { $Weights = @(1) * $Keys.Count }
    $total = ($Weights | Measure-Object -Sum).Sum
    for ($y = $Y0; $y -le $Y1; $y++) {
        for ($x = $X0; $x -le $X1; $x++) {
            $r = $rnd.Next($total); $acc = 0
            for ($i = 0; $i -lt $Keys.Count; $i++) { $acc += $Weights[$i]; if ($r -lt $acc) { Set-Px $C $x $y $Keys[$i]; break } }
        }
    }
}
function Rows($C) { return , @($C | ForEach-Object { -join $_ }) }
function Save-Tex([string]$Name, $C, [hashtable]$Pal, [string]$Dir = $tex) {
    Save-PixelArt -Rows (Rows $C) -Palette $Pal -Path (Join-Path $Dir "$Name.png")
}
function Save-Json($Obj, [string]$Path) {
    New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
    $json = ($Obj | ConvertTo-Json -Depth 50) -replace "`r`n", "`n"
    [IO.File]::WriteAllText($Path, $json + "`n")
}

# ---- textures ---------------------------------------------------------------------------------

# Goblet: brushed silver.
$c = New-Canvas
$cols = 'mmllwlmmdmmllmdd'
for ($x = 0; $x -lt 16; $x++) { Fill-Rect $c $x 0 $x 15 $cols[$x] }
Noise-Rect $c 0 0 15 15 @('.', 'd', 'w') 1301 @(40, 2, 2)
for ($y = 0; $y -lt 16; $y++) { for ($x = 0; $x -lt 16; $x++) { if ($c[$y][$x] -eq '.') { $c[$y][$x] = $cols[$x] } } }
Save-Tex 'goblet' $c @{ d = '#5d636b'; m = '#8f979f'; l = '#bcc4cb'; w = '#e9eff3' }

# Fluids (cm:fluid 1..15). Water is translucent (blend).
$fluids = [ordered]@{
    water           = '#c03f76e4'
    honey           = '#f2a215'
    milk            = '#f4f2ee'
    dragon_breath   = '#d77fd1'
    experience      = '#b8f03a'
    potion_red      = '#f82423'
    potion_darkred  = '#6b1212'
    potion_orange   = '#e49a3a'
    potion_yellow   = '#f2d23a'
    potion_green    = '#4e9331'
    potion_lightblue = '#7cafc6'
    potion_blue     = '#2e5299'
    potion_pink     = '#cd5cab'
    potion_gray     = '#7f8392'
    potion_black    = '#352a27'
}
$seed = 1310
foreach ($k in $fluids.Keys) {
    $base = $fluids[$k]
    $c = New-Canvas
    Noise-Rect $c 0 0 15 15 @('b', 'd', 'l') ($seed++) @(10, 3, 2)
    Save-Tex "fluid_$k" $c @{ b = $base; d = (Shade-Color $base 0.85); l = (Shade-Color $base 1.18) }
}

# Urn: glazed clay pot with a zig-zag band.
$c = New-Canvas
Noise-Rect $c 0 0 15 15 @('b', 'd', 'l') 1320 @(8, 2, 2)
Fill-Rect $c 0 2 15 2 'k'; Fill-Rect $c 0 13 15 13 'k'
for ($x = 0; $x -lt 16; $x++) {
    $y = 6 + [Math]::Abs(($x % 4) - 2)
    Set-Px $c $x $y 'p'; Set-Px $c $x ($y + 1) 'p'
}
Fill-Rect $c 0 14 15 15 'd'
Save-Tex 'urn_side' $c @{ b = '#b0663f'; d = '#8a4c2c'; l = '#c47a4f'; k = '#4f2a18'; p = '#e2c79a' }
$c = New-Canvas
Noise-Rect $c 0 0 15 15 @('b', 'd', 'l') 1321 @(8, 2, 2)
Fill-Rect $c 4 4 11 11 'k'; Fill-Rect $c 5 5 10 10 'h'
Save-Tex 'urn_top' $c @{ b = '#b0663f'; d = '#8a4c2c'; l = '#c47a4f'; k = '#4f2a18'; h = '#22140c' }

# Planter: terracotta box with a soil top.
$terra = @{ b = '#9c5b3b'; d = '#7e4630'; l = '#b26e4a'; r = '#c98760'; s = '#5b3a22'; o = '#704a2c'; g = '#4a2f1b' }
$c = New-Canvas
Noise-Rect $c 0 0 15 15 @('b', 'd', 'l') 1330 @(9, 2, 2)
Fill-Rect $c 0 0 15 1 'r'; Fill-Rect $c 0 2 15 2 'd'; Fill-Rect $c 0 15 15 15 'd'
Save-Tex 'planter_side' $c $terra
$c = New-Canvas
Noise-Rect $c 0 0 15 15 @('s', 'o', 'g') 1331 @(6, 3, 2)
Fill-Rect $c 0 0 15 0 'r'; Fill-Rect $c 0 15 15 15 'r'; Fill-Rect $c 0 0 0 15 'r'; Fill-Rect $c 15 0 15 15 'r'
Save-Tex 'planter_top' $c $terra
$c = New-Canvas
Noise-Rect $c 0 0 15 15 @('b', 'd', 'l') 1332 @(6, 4, 1)
Save-Tex 'planter_bottom' $c $terra

# Flower box: dark planks, soil on top.
$wood = @{ b = '#8b6438'; d = '#6c4b29'; l = '#a27845'; n = '#3d2a17'; s = '#5b3a22'; o = '#704a2c' }
$c = New-Canvas
Noise-Rect $c 0 0 15 15 @('b', 'l', 'd') 1340 @(10, 2, 1)
Fill-Rect $c 0 5 15 5 'd'; Fill-Rect $c 0 10 15 10 'd'; Fill-Rect $c 0 15 15 15 'd'
Set-Px $c 1 2 'n'; Set-Px $c 14 2 'n'; Set-Px $c 1 7 'n'; Set-Px $c 14 7 'n'; Set-Px $c 1 12 'n'; Set-Px $c 14 12 'n'
Save-Tex 'flower_box_side' $c $wood
$c = New-Canvas
Noise-Rect $c 0 0 15 15 @('s', 'o') 1341 @(5, 3)
Fill-Rect $c 0 0 15 0 'b'; Fill-Rect $c 0 15 15 15 'b'
Save-Tex 'flower_box_top' $c $wood

# Books: covers 1-6 (plain colours), 7 enchanted, 8 written, 9 book and quill, plus pages.
$covers = [ordered]@{
    1 = '#6b3e21'; 2 = '#8e2222'; 3 = '#2f6b2a'; 4 = '#27427c'; 5 = '#2b2b31'; 6 = '#22706e'
    7 = '#6f3aa0'; 8 = '#5b3a1f'; 9 = '#7a5230'
}
foreach ($n in $covers.Keys) {
    $base = $covers[$n]
    $c = New-Canvas
    Noise-Rect $c 0 0 15 15 @('b', 'l') (1350 + $n) @(12, 2)
    Fill-Rect $c 0 0 15 0 'd'; Fill-Rect $c 0 15 15 15 'd'; Fill-Rect $c 0 0 0 15 'd'; Fill-Rect $c 15 0 15 15 'd'
    $band = if ($n -ge 7) { 'g' } else { 'e' }
    Fill-Rect $c 1 3 14 3 $band; Fill-Rect $c 1 12 14 12 $band
    if ($n -eq 7) { Fill-Rect $c 6 6 9 9 'g'; Fill-Rect $c 7 7 8 8 'p' }
    if ($n -eq 9) { Fill-Rect $c 10 5 10 10 'w'; Set-Px $c 11 5 'w'; Set-Px $c 9 10 'k' }
    Save-Tex "book_$n" $c @{ b = $base; l = (Shade-Color $base 1.2); d = (Shade-Color $base 0.6); e = (Shade-Color $base 1.45); g = '#e3bf45'; p = '#b77ce0'; w = '#f2f2f2'; k = '#1b1b1b' }
}
$c = New-Canvas
for ($y = 0; $y -lt 16; $y++) { Fill-Rect $c 0 $y 15 $y ($(if ($y % 2) { 'l' } else { 'p' })) }
Save-Tex 'book_pages' $c @{ p = '#f2ead6'; l = '#d8ccb0' }

# Pancake: golden sides, browned top, toppings.
$c = New-Canvas
Noise-Rect $c 0 0 15 15 @('b', 'l') 1360 @(6, 3)
Fill-Rect $c 0 0 15 0 'd'; Fill-Rect $c 0 15 15 15 'd'
Save-Tex 'pancake_side' $c @{ b = '#e0a14d'; l = '#efbd6b'; d = '#b5732f' }
$c = New-Canvas
Noise-Rect $c 0 0 15 15 @('b', 'd', 'l') 1361 @(8, 3, 2)
Save-Tex 'pancake_top' $c @{ b = '#d18c3e'; d = '#b06e2a'; l = '#e3a957' }
$toppings = [ordered]@{ honey = '#f0a316'; syrup = '#93451a'; chocolate = '#4b2a17' }
$seed = 1365
foreach ($k in $toppings.Keys) {
    $base = $toppings[$k]
    $c = New-Canvas
    Noise-Rect $c 0 0 15 15 @('b', 'l', 'd') ($seed++) @(8, 2, 2)
    Save-Tex "topping_$k" $c @{ b = $base; l = (Shade-Color $base 1.25); d = (Shade-Color $base 0.8) }
}

# Jar boat: glass jar, cork lid, little boat with a sail.
$c = New-Canvas
Fill-Rect $c 0 0 15 0 'e'; Fill-Rect $c 0 15 15 15 'e'; Fill-Rect $c 0 0 0 15 'e'; Fill-Rect $c 15 0 15 15 'e'
Set-Px $c 2 2 'h'; Set-Px $c 3 3 'h'; Set-Px $c 2 3 'h'; Set-Px $c 4 4 'h'
Save-Tex 'jar_glass' $c @{ e = '#a0d8eef2'; h = '#d0ffffff' }
$c = New-Canvas
Noise-Rect $c 0 0 15 15 @('b', 'd', 'l') 1370 @(7, 3, 2)
Save-Tex 'jar_lid' $c @{ b = '#a87b4f'; d = '#8a6038'; l = '#c09366' }
$c = New-Canvas
Noise-Rect $c 0 0 15 15 @('b', 'l') 1371 @(8, 2)
Fill-Rect $c 0 4 15 4 'd'; Fill-Rect $c 0 9 15 9 'd'; Fill-Rect $c 0 14 15 14 'd'
Save-Tex 'boat_hull' $c @{ b = '#9b6c3c'; l = '#b38250'; d = '#6c4824' }
$c = New-Canvas
Noise-Rect $c 0 0 15 15 @('w', 'g') 1372 @(10, 1)
Save-Tex 'sail' $c @{ w = '#f3f1ea'; g = '#d9d6cc' }

# ---- item icons -------------------------------------------------------------------------------

Save-PixelArt -Path (Join-Path $items 'pancake.png') -Palette @{
    h = '#f0a316'; j = '#c47c0c'; p = '#efbd6b'; d = '#b5732f'; k = '#8a5320'
} -Rows @(
    '................',
    '................',
    '................',
    '................',
    '.....hhhhhh.....',
    '...hhhhhhhhhh...',
    '..pphhhhhhhhpp..',
    '..dddhdddjdddd..',
    '..pppjpppppppp..',
    '..ddddddddhddd..',
    '..pppppppppppp..',
    '..dddddddddddd..',
    '..pppppppppppp..',
    '...kkkkkkkkkk...',
    '................',
    '................'
)
Save-PixelArt -Path (Join-Path $items 'candy.png') -Palette @{
    r = '#d8343a'; w = '#f5f1ea'; d = '#9c1f25'; y = '#e8c84a'; o = '#c49a2c'
} -Rows @(
    '................',
    '................',
    '..yo........oy..',
    '..yyo......oyy..',
    '...yyo....oyy...',
    '....ydrrrrdy....',
    '....drwwrrrd....',
    '....rwrrwwrr....',
    '....rrwwrrwr....',
    '....drrrwwrd....',
    '....ydrrrrdy....',
    '...yyo....oyy...',
    '..yyo......oyy..',
    '..yo........oy..',
    '................',
    '................'
)

# ---- geometry ---------------------------------------------------------------------------------

# Face UVs follow the cube's position in the block (like auto-UV in a 16x16 texture), or the
# whole texture with -Full (flowers, books).
function Face-Uv([string]$Face, $O, $S, [bool]$Full) {
    if ($Full) { return @(0, 0), @(16, 16) }
    $x = $O[0] + 8; $y = $O[1]; $z = $O[2] + 8
    switch ($Face) {
        { $_ -in 'north', 'south' } { return @([Math]::Max(0, $x), [Math]::Max(0, 16 - $y - $S[1])), @($S[0], $S[1]) }
        { $_ -in 'east', 'west' } { return @([Math]::Max(0, $z), [Math]::Max(0, 16 - $y - $S[1])), @($S[2], $S[1]) }
        default { return @([Math]::Max(0, $x), [Math]::Max(0, $z)), @($S[0], $S[2]) }
    }
}
# $Mat: one material for every face, or a hashtable face -> material (missing faces are culled).
function Cube($O, $S, $Mat, [switch]$Full, $Rotation, $Pivot) {
    $uv = [ordered]@{}
    foreach ($f in 'north', 'south', 'east', 'west', 'up', 'down') {
        $m = if ($Mat -is [hashtable]) { $Mat[$f] } else { $Mat }
        if (-not $m) { continue }
        $u = Face-Uv $f $O $S $Full.IsPresent
        $uv[$f] = [ordered]@{ uv = $u[0]; uv_size = $u[1]; material_instance = $m }
    }
    $cube = [ordered]@{ origin = $O; size = $S; uv = $uv }
    if ($Rotation) { $cube.rotation = $Rotation; $cube.pivot = $Pivot }
    return $cube
}
function Bone([string]$Name, $Cubes, [string]$Parent = 'root') {
    $b = [ordered]@{ name = $Name; pivot = @(0, 0, 0); cubes = @($Cubes) }
    if ($Parent) { $b.parent = $Parent }
    return $b
}
function Save-Geo([string]$Id, $Bones) {
    $all = @([ordered]@{ name = 'root'; pivot = @(0, 0, 0) }) + @($Bones)
    Save-Json ([ordered]@{
            format_version     = '1.16.0'
            'minecraft:geometry' = @([ordered]@{
                    description = [ordered]@{
                        identifier            = "geometry.complementaries.$Id"
                        texture_width         = 16
                        texture_height        = 16
                        visible_bounds_width  = 2
                        visible_bounds_height = 2.5
                        visible_bounds_offset = @(0, 0.75, 0)
                    }
                    bones       = $all
                })
        }) (Join-Path $geo "$Id.geo.json")
}
# Two crossed planes (a plant) centred on (cx, cz), standing on y, $W wide and tall.
function Cross([double]$Cx, [double]$Y, [double]$Cz, [double]$W, [string]$Mat) {
    $face = @{ north = $Mat; south = $Mat }
    return @(
        (Cube @(($Cx - $W / 2), $Y, $Cz) @($W, $W, 0) $face -Full -Rotation @(0, 45, 0) -Pivot @($Cx, $Y, $Cz)),
        (Cube @(($Cx - $W / 2), $Y, $Cz) @($W, $W, 0) $face -Full -Rotation @(0, -45, 0) -Pivot @($Cx, $Y, $Cz))
    )
}

# Goblet: foot, stem, cup (front = north), fluid surface.
Save-Geo 'goblet' @(
    (Bone 'body' @(
            (Cube @(-3, 0, -3) @(6, 1, 6) 'goblet'),
            (Cube @(-1, 1, -1) @(2, 4, 2) 'goblet'),
            (Cube @(-3, 5, -3) @(6, 1, 6) 'goblet'),
            (Cube @(-3, 6, -3) @(6, 3, 1) 'goblet'),
            (Cube @(-3, 6, 2) @(6, 3, 1) 'goblet'),
            (Cube @(-3, 6, -2) @(1, 3, 4) 'goblet'),
            (Cube @(2, 6, -2) @(1, 3, 4) 'goblet')
        )),
    (Bone 'fluid' @((Cube @(-2, 6, -2) @(4, 2.5, 4) @{ up = 'fluid' })))
)

# Urn: body, shoulder, neck, rim.
$urnMat = @{ north = 'side'; south = 'side'; east = 'side'; west = 'side'; up = 'top'; down = 'top' }
Save-Geo 'urn' @(
    (Bone 'body' @(
            (Cube @(-5, 0, -5) @(10, 9, 10) $urnMat),
            (Cube @(-4, 9, -4) @(8, 2, 8) $urnMat),
            (Cube @(-3, 11, -3) @(6, 2, 6) $urnMat),
            (Cube @(-4, 13, -4) @(8, 2, 8) $urnMat)
        ))
)

# Flower box: the box (front = north, slots left to right seen from the front = +x to -x) and
# 15 plants per slot.
$flowerNames = @('dandelion', 'poppy', 'blue_orchid', 'allium', 'azure_bluet', 'red_tulip', 'orange_tulip',
    'white_tulip', 'pink_tulip', 'oxeye_daisy', 'cornflower', 'lily_of_the_valley', 'wither_rose', 'oak_sapling', 'birch_sapling')
$flowerTextures = @('flower_dandelion', 'flower_rose', 'flower_blue_orchid', 'flower_allium', 'flower_houstonia',
    'flower_tulip_red', 'flower_tulip_orange', 'flower_tulip_white', 'flower_tulip_pink', 'flower_oxeye_daisy',
    'flower_cornflower', 'flower_lily_of_the_valley', 'flower_wither_rose', 'sapling_oak', 'sapling_birch')
$boxSide = @{ north = 'box'; south = 'box'; east = 'box'; west = 'box'; down = 'box' }
$fbBones = @(
    (Bone 'box' @(
            (Cube @(-8, 0, -3) @(16, 5, 1) $boxSide),
            (Cube @(-8, 0, 2) @(16, 5, 1) $boxSide),
            (Cube @(-8, 0, -2) @(1, 5, 4) $boxSide),
            (Cube @(7, 0, -2) @(1, 5, 4) $boxSide),
            (Cube @(-7, 0, -2) @(14, 4, 4) @{ up = 'soil'; down = 'box' })
        ))
)
$slotX = @(5.333, 0, -5.333)
$fbVis = [ordered]@{}
for ($s = 0; $s -lt 3; $s++) {
    for ($f = 1; $f -le 15; $f++) {
        $name = "slot${s}_$f"
        $fbBones += Bone $name (Cross $slotX[$s] 4 0 8 "flower_$f")
        $fbVis[$name] = "q.block_state('cm:flower$s') == $f"
    }
}
Save-Geo 'flower_box' $fbBones

# Book piles. Standing books side by side (spines to the front), lying books stacked.
$bookX = @(-7, -3.5, 0, 3.5)
$bookH = @(10, 11, 9, 10)
$bpBones = @(); $bpVis = [ordered]@{}
for ($s = 0; $s -lt 4; $s++) {
    for ($t = 1; $t -le 7; $t++) {
        $mat = @{ north = "cover_$t"; west = "cover_$t"; east = "cover_$t"; south = 'pages'; up = 'pages'; down = 'pages' }
        $name = "book${s}_$t"
        $bpBones += Bone $name @((Cube @($bookX[$s], 0, -4) @(3, $bookH[$s], 8) $mat -Full))
        $bpVis[$name] = "q.block_state('cm:book$s') == $t"
    }
}
Save-Geo 'book_pile' $bpBones
$bhX = @(-5, -4, -6, -5)
$bhBones = @(); $bhVis = [ordered]@{}
for ($s = 0; $s -lt 4; $s++) {
    for ($t = 1; $t -le 2; $t++) {
        $m = "cover_$($t + 7)"
        $mat = @{ up = $m; down = $m; north = $m; south = 'pages'; east = 'pages'; west = 'pages' }
        $name = "book${s}_$t"
        $bhBones += Bone $name @((Cube @($bhX[$s], (3 * $s), -6) @(10, 3, 12) $mat -Full))
        $bhVis[$name] = "q.block_state('cm:book$s') == $t"
    }
}
Save-Geo 'book_pile_horizontal' $bhBones

# Pancake: 8 layers of 2 px, a topping slab on the top layer.
$pcBones = @(); $pcVis = [ordered]@{}
$pcMat = @{ north = 'side'; south = 'side'; east = 'side'; west = 'side'; up = 'top'; down = 'top' }
for ($n = 1; $n -le 8; $n++) {
    $pcBones += Bone "layer_$n" @((Cube @(-6, (2 * ($n - 1)), -6) @(12, 2, 12) $pcMat))
    $pcVis["layer_$n"] = "q.block_state('cm:pancakes') >= $($n - 1)"
    $pcBones += Bone "topping_$n" @(
        (Cube @(-5.5, (2 * $n), -5.5) @(11, 0.5, 11) @{ up = 'topping'; north = 'topping'; south = 'topping'; east = 'topping'; west = 'topping' }),
        (Cube @(-6.25, (2 * $n - 1.5), -2) @(0.25, 2, 2) @{ west = 'topping'; north = 'topping'; south = 'topping' }),
        (Cube @(1, (2 * $n - 1), -6.25) @(2, 1.5, 0.25) @{ north = 'topping'; east = 'topping'; west = 'topping' })
    )
    $pcVis["topping_$n"] = "q.block_state('cm:pancakes') == $($n - 1) && q.block_state('cm:topping') > 0"
}
Save-Geo 'pancake' $pcBones

# Jar boat: glass jar with water, a little boat and its sail, cork lid.
Save-Geo 'jar_boat' @(
    (Bone 'jar' @(
            (Cube @(-5, 0, -5) @(10, 12, 10) 'glass'),
            (Cube @(-3, 12, -3) @(6, 1, 6) 'glass')
        )),
    (Bone 'lid' @((Cube @(-2.5, 13, -2.5) @(5, 2, 5) 'lid'))),
    (Bone 'water' @((Cube @(-4.5, 0.5, -4.5) @(9, 3, 9) 'water'))),
    (Bone 'boat' @(
            (Cube @(-3, 2.5, -1.5) @(6, 1, 3) 'hull'),
            (Cube @(-3.5, 3.5, -1.5) @(7, 1, 0.5) 'hull'),
            (Cube @(-3.5, 3.5, 1) @(7, 1, 0.5) 'hull'),
            (Cube @(-0.25, 3.5, -0.25) @(0.5, 6, 0.5) 'hull'),
            (Cube @(-2.5, 5, 0) @(5, 4, 0) @{ north = 'sail'; south = 'sail' } -Full)
        ))
)

# ---- block definitions ------------------------------------------------------------------------

$dirs = [ordered]@{ north = 0; west = 90; south = 180; east = 270 }

function Mat([string]$Texture, [string]$Method = 'alpha_test') {
    return [ordered]@{ texture = $Texture; render_method = $Method; ambient_occlusion = $false; face_dimming = $true }
}
function Box($O, $S) { return [ordered]@{ origin = $O; size = $S } }
function Save-Block([string]$Id, $Description, $Components, $Permutations) {
    $block = [ordered]@{ description = $Description; components = $Components }
    if ($Permutations) { $block.permutations = @($Permutations) }
    Save-Json ([ordered]@{ format_version = '1.21.60'; 'minecraft:block' = $block }) (Join-Path $bp "blocks/$Id.json")
}
function Desc([string]$Id, [string]$Category, $States, [switch]$Directional, [switch]$Hidden) {
    $d = [ordered]@{ identifier = "complementaries:$Id" }
    $cat = if ($Hidden) { 'none' } else { $Category }
    $d.menu_category = [ordered]@{ category = $cat; is_hidden_in_commands = $false }
    if ($States) { $d.states = $States }
    if ($Directional) {
        $d.traits = [ordered]@{ 'minecraft:placement_direction' = [ordered]@{ enabled_states = @('minecraft:cardinal_direction'); y_rotation_offset = 180 } }
    }
    return $d
}
function Range([int]$Max) { return [ordered]@{ values = [ordered]@{ min = 0; max = $Max } } }
function Base([string]$Id, [string]$Geo, $Materials, $Box, [double]$Seconds, [double]$Resist, [string]$MapColor, $Vis) {
    $g = [ordered]@{ identifier = "geometry.complementaries.$Geo" }
    if ($Vis) { $g.bone_visibility = $Vis }
    $c = [ordered]@{
        'minecraft:display_name'               = "tile.complementaries:$Id.name"
        'minecraft:geometry'                   = $g
        'minecraft:material_instances'         = $Materials
        'minecraft:collision_box'              = $Box
        'minecraft:selection_box'              = $Box
        'minecraft:destructible_by_mining'     = [ordered]@{ seconds_to_destroy = $Seconds }
        'minecraft:destructible_by_explosion'  = [ordered]@{ explosion_resistance = $Resist }
        'minecraft:light_dampening'            = 0
        'minecraft:map_color'                  = $MapColor
        'minecraft:loot'                       = "loot_tables/blocks/$Id.json"
        'minecraft:placement_filter'           = [ordered]@{ conditions = @([ordered]@{ allowed_faces = @('up') }) }
    }
    return $c
}
function Turn([string]$Extra) {
    $p = @()
    foreach ($d in $dirs.Keys) {
        $cond = "q.block_state('minecraft:cardinal_direction') == '$d'"
        if ($Extra) { $cond = "$cond && $Extra" }
        $p += [ordered]@{ condition = $cond; components = [ordered]@{ 'minecraft:transformation' = [ordered]@{ rotation = @(0, $dirs[$d], 0) } } }
    }
    return $p
}

# Goblet
$fluidOrder = @($fluids.Keys)
# One render method per block (the game refuses mixed opaque / transparent instances): blend here.
$gobletMats = [ordered]@{ '*' = (Mat 'cm_tableware_goblet' 'blend'); goblet = (Mat 'cm_tableware_goblet' 'blend'); fluid = (Mat 'cm_tableware_fluid_water' 'blend') }
$gobletComp = Base 'goblet' 'goblet' $gobletMats (Box @(-3, 0, -3) @(6, 9, 6)) 0.75 3 '#a7a7a7' ([ordered]@{ fluid = "q.block_state('cm:fluid') > 0" })
$gobletPerms = @()
for ($i = 0; $i -lt $fluidOrder.Count; $i++) {
    $method = 'blend'
    $gobletPerms += [ordered]@{
        condition  = "q.block_state('cm:fluid') == $($i + 1)"
        components = [ordered]@{ 'minecraft:material_instances' = [ordered]@{
                '*' = (Mat 'cm_tableware_goblet' 'blend'); goblet = (Mat 'cm_tableware_goblet' 'blend'); fluid = (Mat "cm_tableware_fluid_$($fluidOrder[$i])" $method)
            }
        }
    }
}
Save-Block 'goblet' (Desc 'goblet' 'items' ([ordered]@{ 'cm:fluid' = (Range 15) })) $gobletComp $gobletPerms

# Urn
$urnMats = [ordered]@{ '*' = (Mat 'cm_tableware_urn_side' 'opaque'); side = (Mat 'cm_tableware_urn_side' 'opaque'); top = (Mat 'cm_tableware_urn_top' 'opaque') }
$urnComp = Base 'urn' 'urn' $urnMats (Box @(-5, 0, -5) @(10, 15, 10)) 0.5 0.5 '#b0663f' $null
$urnPerms = @([ordered]@{ condition = "q.block_state('cm:treasure') == 1"; components = [ordered]@{ 'minecraft:loot' = 'loot_tables/blocks/urn_treasure.json' } })
Save-Block 'urn' (Desc 'urn' 'items' ([ordered]@{ 'cm:treasure' = (Range 1) })) $urnComp $urnPerms

# Planter (full block, no placement filter)
$planterComp = [ordered]@{
    'minecraft:display_name'              = 'tile.complementaries:planter.name'
    'minecraft:geometry'                  = 'minecraft:geometry.full_block'
    'minecraft:material_instances'        = [ordered]@{
        '*'  = [ordered]@{ texture = 'cm_tableware_planter_side'; render_method = 'opaque' }
        up   = [ordered]@{ texture = 'cm_tableware_planter_top'; render_method = 'opaque' }
        down = [ordered]@{ texture = 'cm_tableware_planter_bottom'; render_method = 'opaque' }
    }
    'minecraft:destructible_by_mining'    = [ordered]@{ seconds_to_destroy = 1.25 }
    'minecraft:destructible_by_explosion' = [ordered]@{ explosion_resistance = 4.2 }
    'minecraft:map_color'                 = '#9c5b3b'
    'minecraft:loot'                      = 'loot_tables/blocks/planter.json'
}
Save-Block 'planter' (Desc 'planter' 'nature' $null) $planterComp $null

# Flower box
$fbMats = [ordered]@{ '*' = (Mat 'cm_tableware_flower_box_side'); box = (Mat 'cm_tableware_flower_box_side'); soil = (Mat 'cm_tableware_flower_box_top') }
for ($f = 1; $f -le 15; $f++) { $fbMats["flower_$f"] = Mat "cm_tableware_flower_$($flowerNames[$f - 1])" }
$fbComp = Base 'flower_box' 'flower_box' $fbMats (Box @(-8, 0, -8) @(16, 6, 16)) 1 3 '#8b6438' $fbVis
$fbComp['minecraft:placement_filter'] = [ordered]@{ conditions = @([ordered]@{ allowed_faces = @('up', 'side') }) }
$fbPerms = @()
$wallShift = @{ north = @(0, 0, 0.3125); south = @(0, 0, -0.3125); west = @(0.3125, 0, 0); east = @(-0.3125, 0, 0) }
foreach ($d in $dirs.Keys) {
    foreach ($wall in 0, 1) {
        $t = [ordered]@{ rotation = @(0, $dirs[$d], 0) }
        if ($wall -eq 1) { $t.translation = $wallShift[$d] }
        $fbPerms += [ordered]@{
            condition  = "q.block_state('minecraft:cardinal_direction') == '$d' && q.block_state('cm:wall') == $wall"
            components = [ordered]@{ 'minecraft:transformation' = $t }
        }
    }
}
$fbStates = [ordered]@{ 'cm:wall' = (Range 1); 'cm:flower0' = (Range 15); 'cm:flower1' = (Range 15); 'cm:flower2' = (Range 15) }
$fbDesc = Desc 'flower_box' 'items' $fbStates -Directional
Save-Block 'flower_box' $fbDesc $fbComp $fbPerms

# Book piles
$bpMats = [ordered]@{ '*' = (Mat 'cm_tableware_book_pages' 'opaque'); pages = (Mat 'cm_tableware_book_pages' 'opaque') }
for ($t = 1; $t -le 7; $t++) { $bpMats["cover_$t"] = Mat "cm_tableware_book_$t" 'opaque' }
$bpComp = Base 'book_pile' 'book_pile' $bpMats (Box @(-7, 0, -7) @(14, 11, 14)) 0.5 1 '#6b3e21' $bpVis
$bpComp.Remove('minecraft:collision_box')
$bpComp['minecraft:collision_box'] = Box @(-7, 0, -7) @(14, 11, 14)
$bpStates = [ordered]@{ 'cm:book0' = (Range 7); 'cm:book1' = (Range 7); 'cm:book2' = (Range 7); 'cm:book3' = (Range 7) }
Save-Block 'book_pile' (Desc 'book_pile' 'items' $bpStates -Directional -Hidden) $bpComp (Turn $null)

$bhMats = [ordered]@{ '*' = (Mat 'cm_tableware_book_pages' 'opaque'); pages = (Mat 'cm_tableware_book_pages' 'opaque'); cover_8 = (Mat 'cm_tableware_book_8' 'opaque'); cover_9 = (Mat 'cm_tableware_book_9' 'opaque') }
$bhComp = Base 'book_pile_horizontal' 'book_pile_horizontal' $bhMats (Box @(-6, 0, -6) @(12, 3, 12)) 0.5 1 '#5b3a1f' $bhVis
$bhPerms = @(Turn $null)
for ($n = 1; $n -le 3; $n++) {
    $bhPerms += [ordered]@{
        condition  = "q.block_state('cm:book$n') > 0"
        components = [ordered]@{ 'minecraft:collision_box' = (Box @(-6, 0, -6) @(12, (3 * ($n + 1)), 12)); 'minecraft:selection_box' = (Box @(-6, 0, -6) @(12, (3 * ($n + 1)), 12)) }
    }
}
$bhStates = [ordered]@{ 'cm:book0' = (Range 2); 'cm:book1' = (Range 2); 'cm:book2' = (Range 2); 'cm:book3' = (Range 2) }
Save-Block 'book_pile_horizontal' (Desc 'book_pile_horizontal' 'items' $bhStates -Directional -Hidden) $bhComp $bhPerms

# Pancake
$pcMats = [ordered]@{ '*' = (Mat 'cm_tableware_pancake_side' 'opaque'); side = (Mat 'cm_tableware_pancake_side' 'opaque'); top = (Mat 'cm_tableware_pancake_top' 'opaque'); topping = (Mat 'cm_tableware_topping_honey' 'opaque') }
$pcComp = Base 'pancake' 'pancake' $pcMats (Box @(-6, 0, -6) @(12, 2, 12)) 0.5 0.5 '#e0a14d' $pcVis
$pcComp['minecraft:loot'] = 'loot_tables/blocks/pancake_1.json'
$pcPerms = @()
for ($n = 1; $n -lt 8; $n++) {
    $pcPerms += [ordered]@{
        condition  = "q.block_state('cm:pancakes') == $n"
        components = [ordered]@{
            'minecraft:collision_box' = (Box @(-6, 0, -6) @(12, (2 * ($n + 1)), 12))
            'minecraft:selection_box' = (Box @(-6, 0, -6) @(12, (2 * ($n + 1)), 12))
            'minecraft:loot'          = "loot_tables/blocks/pancake_$($n + 1).json"
        }
    }
}
$top = @('honey', 'syrup', 'chocolate')
for ($i = 1; $i -le 3; $i++) {
    $m = [ordered]@{}; foreach ($k in $pcMats.Keys) { $m[$k] = $pcMats[$k] }
    $m.topping = Mat "cm_tableware_topping_$($top[$i - 1])" 'opaque'
    $pcPerms += [ordered]@{ condition = "q.block_state('cm:topping') == $i"; components = [ordered]@{ 'minecraft:material_instances' = $m } }
}
$pcStates = [ordered]@{ 'cm:pancakes' = (Range 7); 'cm:topping' = (Range 3) }
Save-Block 'pancake' (Desc 'pancake' 'items' $pcStates -Hidden) $pcComp $pcPerms

# Jar boat
$jbMats = [ordered]@{
    '*'   = (Mat 'cm_tableware_jar_glass' 'blend'); glass = (Mat 'cm_tableware_jar_glass' 'blend'); lid = (Mat 'cm_tableware_jar_lid' 'blend')
    water = (Mat 'cm_tableware_fluid_water' 'blend'); hull = (Mat 'cm_tableware_boat_hull' 'blend'); sail = (Mat 'cm_tableware_sail' 'blend')
}
$jbComp = Base 'jar_boat' 'jar_boat' $jbMats (Box @(-5, 0, -5) @(10, 15, 10)) 0.5 1 '#a0d8ee' $null
Save-Block 'jar_boat' (Desc 'jar_boat' 'items' $null -Directional) $jbComp (Turn $null)

# ---- texture atlas fragment -------------------------------------------------------------------

$terrain = [ordered]@{}
foreach ($n in (Get-ChildItem $tex -Filter *.png | Sort-Object Name)) {
    $terrain["cm_tableware_$($n.BaseName)"] = [ordered]@{ textures = "textures/blocks/tableware/$($n.BaseName)" }
}
for ($f = 0; $f -lt 15; $f++) {
    $terrain["cm_tableware_flower_$($flowerNames[$f])"] = [ordered]@{ textures = "textures/blocks/$($flowerTextures[$f])" }
}
Save-Json $terrain (Join-Path $rp 'terrain_texture.d/tableware.json')
Save-Json ([ordered]@{
        cm_tableware_pancake = [ordered]@{ textures = 'textures/items/tableware/pancake' }
        cm_tableware_candy   = [ordered]@{ textures = 'textures/items/tableware/candy' }
    }) (Join-Path $rp 'item_texture.d/tableware.json')

Write-Host 'tableware: textures, models and blocks written'
