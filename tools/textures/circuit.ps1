# Textures (and the crystal display model) of the circuit unit (U8): relayer, speaker block,
# crystal display, redstone illuminator, minecart with dispenser.
#
#   pwsh tools/textures/circuit.ps1            # writes assets/pack/rp/textures/... + the display geometry
#   pwsh tools/textures/circuit.ps1 -Preview   # also writes 8x previews to tools/textures/preview/
#
# Original pixel art, drawn from rows of palette keys (see lib.ps1).
param([switch]$Preview)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lib.ps1"
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$blocks = Join-Path $root 'assets/pack/rp/textures/blocks'
$items = Join-Path $root 'assets/pack/rp/textures/items'
$entity = Join-Path $root 'assets/pack/rp/textures/entity'

function Save([string]$dir, [string]$name, [string[]]$rows, [hashtable]$pal) {
    Save-PixelArt -Rows $rows -Palette $pal -Path (Join-Path $dir "$name.png")
    if ($Preview) { Save-PixelArt -Rows $rows -Palette $pal -Path (Join-Path $PSScriptRoot "preview/$name.png") -Scale 8 }
}

# Builds a w x h grid filled by a scriptblock { param($x, $y) -> palette key }.
function Grid([int]$w, [int]$h, [scriptblock]$fn) {
    $rows = for ($y = 0; $y -lt $h; $y++) { -join (0..($w - 1) | ForEach-Object { & $fn $_ $y }) }
    return , $rows
}

# ---- shared stone frame (relayer) ------------------------------------------------------------------
$stone = @{ k = '#3a3a3e'; d = '#56565c'; m = '#6e6e75'; l = '#8a8a92'; h = '#a3a3ab'; r = '#5c0b07'; x = '#d61e14'; o = '#ff6a4d'; q = '#e8e2d6' }
function StoneBase([int]$x, [int]$y) {
    if ($x -eq 0 -or $y -eq 0) { return 'l' }
    if ($x -eq 15 -or $y -eq 15) { return 'k' }
    if ($x -eq 1 -or $y -eq 1) { return 'h' }
    if ($x -eq 14 -or $y -eq 14) { return 'd' }
    if ((($x * 7 + $y * 3) % 11) -eq 0) { return 'd' }
    if ((($x * 5 + $y * 13) % 17) -eq 0) { return 'l' }
    return 'm'
}

# relayer front: an arrow-shaped output port; side: a redstone line along the direction of travel
foreach ($on in $false, $true) {
    $suffix = if ($on) { 'on' } else { 'off' }
    $lit = if ($on) { 'x' } else { 'r' }
    $front = Grid 16 16 {
        param($x, $y)
        $dx = [Math]::Abs($x - 7.5); $dy = [Math]::Abs($y - 7.5)
        if ($dx -le 2 -and $dy -le 2) { if ($on -and $dx -le 1 -and $dy -le 1) { return 'o' } else { return $lit } }
        if ($dx -le 3.5 -and $dy -le 3.5) { return 'k' }
        StoneBase $x $y
    }
    Save $blocks "relayer_front_$suffix" $front $stone
    # side faces (they rotate with the block, so the art is symmetric): a redstone channel ring
    $side = Grid 16 16 {
        param($x, $y)
        $dx = [Math]::Abs($x - 7.5); $dy = [Math]::Abs($y - 7.5)
        $ring = [Math]::Max($dx, $dy)
        if ($ring -ge 3 -and $ring -le 4) { return $lit }
        if ($ring -ge 2 -and $ring -le 5) { return 'd' }
        if ($dx -le 1 -and $dy -le 1) { return 'k' }
        StoneBase $x $y
    }
    Save $blocks "relayer_side_$suffix" $side $stone
}
$back = Grid 16 16 {
    param($x, $y)
    $dx = [Math]::Abs($x - 7.5); $dy = [Math]::Abs($y - 7.5)
    if ($dx -le 1 -and $dy -le 1) { return 'q' }
    if ($dx -le 2.5 -and $dy -le 2.5) { return 'k' }
    if (($dx -le 0.5 -or $dy -le 0.5) -and $dx -le 5.5 -and $dy -le 5.5) { return 'd' }
    StoneBase $x $y
}
Save $blocks 'relayer_back' $back $stone

# ---- speaker block -----------------------------------------------------------------------------------
$wood = @{ k = '#3b2615'; d = '#5e3d22'; m = '#7d5532'; l = '#9b6e43'; h = '#b8895a'; g = '#2b2b30'; n = '#4a4a52'; c = '#c27a3a'; y = '#e39a55' }
function Plank([int]$x, [int]$y) {
    if ($x -eq 0 -or $x -eq 15 -or $y -eq 0 -or $y -eq 15) { return 'k' }
    if (($y % 5) -eq 0) { return 'd' }
    if (((($x + [Math]::Floor($y / 5) * 6) % 9)) -eq 0) { return 'd' }
    if ((($x * 3 + $y) % 7) -eq 0) { return 'l' }
    return 'm'
}
$speakerFront = Grid 16 16 {
    param($x, $y)
    $r = [Math]::Sqrt([Math]::Pow($x - 7.5, 2) + [Math]::Pow($y - 7.5, 2))
    if ($r -le 2) { return 'y' }
    if ($r -le 3) { return 'c' }
    if ($r -le 6) { if ((($x + $y) % 2) -eq 0) { return 'g' } else { return 'n' } }
    if ($r -le 6.8) { return 'k' }
    Plank $x $y
}
Save $blocks 'speaker_block_front' $speakerFront $wood
Save $blocks 'speaker_block_side' (Grid 16 16 { param($x, $y) Plank $x $y }) $wood
$speakerTop = Grid 16 16 {
    param($x, $y)
    if ($x -ge 4 -and $x -le 11 -and $y -ge 4 -and $y -le 11) { if ((($x + $y) % 3) -eq 0) { return 'h' } else { return 'l' } }
    Plank $x $y
}
Save $blocks 'speaker_block_top' $speakerTop $wood

# ---- crystal display ---------------------------------------------------------------------------------
$crystal = @{ k = '#26203a'; d = '#3d3357'; m = '#5b4b80'; l = '#8a73b8'; h = '#c2aef0'; s = '#14121c'; t = '#1d1a28'; p = '#ff3b2f'; w = '#ff8a75' }
$displaySide = Grid 16 16 {
    param($x, $y)
    if ($x -eq 0 -or $y -eq 0) { return 'l' }
    if ($x -eq 15 -or $y -eq 15) { return 'k' }
    if ((($x + $y) % 6) -eq 0) { return 'h' }
    if ((($x * 3 + $y * 5) % 7) -eq 0) { return 'd' }
    return 'm'
}
Save $blocks 'crystal_display' $displaySide $crystal
# the screen: dark glass with the unlit "88" ghost
$ghost = @{}
function MarkDigit([int]$u0, [int]$v0, [int]$w, [int]$h) {
    $mid = [int](($h - 1) / 2)
    for ($i = 0; $i -lt $w; $i++) { $ghost["$($u0 + $i),$v0"] = 1; $ghost["$($u0 + $i),$($v0 + $mid)"] = 1; $ghost["$($u0 + $i),$($v0 + $h - 1)"] = 1 }
    for ($j = 0; $j -lt $h; $j++) { $ghost["$u0,$($v0 + $j)"] = 1; $ghost["$($u0 + $w - 1),$($v0 + $j)"] = 1 }
}
MarkDigit 3 4 4 7
MarkDigit 9 4 4 7
$displayFront = Grid 16 16 {
    param($x, $y)
    if ($x -eq 0 -or $y -eq 0 -or $x -eq 15 -or $y -eq 15) { return 'k' }
    if ($x -eq 1 -or $y -eq 1 -or $x -eq 14 -or $y -eq 14) { return 'd' }
    if ($ghost.ContainsKey("$x,$y")) { return 't' }
    return 's'
}
Save $blocks 'crystal_display_front' $displayFront $crystal
Save $blocks 'crystal_display_segment' (Grid 16 16 { param($x, $y) if ((($x + $y) % 4) -eq 0) { 'w' } else { 'p' } }) $crystal

# ---- redstone illuminator ----------------------------------------------------------------------------
$levels = @(
    @{ name = 'redstone_illuminator_0'; a = '#4a1410'; b = '#2e0c09'; c = '#6b2a20' },
    @{ name = 'redstone_illuminator_1'; a = '#8f2a17'; b = '#5c180c'; c = '#c25a2c' },
    @{ name = 'redstone_illuminator_2'; a = '#d14a1f'; b = '#8f2a14'; c = '#ff9a4a' },
    @{ name = 'redstone_illuminator_3'; a = '#ff7a2e'; b = '#d14a1f'; c = '#ffe08a' }
)
foreach ($lv in $levels) {
    $pal = @{ f = '#5a5a60'; e = '#86868e'; a = $lv.a; b = $lv.b; c = $lv.c }
    $rows = Grid 16 16 {
        param($x, $y)
        if ($x -eq 0 -or $y -eq 0 -or $x -eq 15 -or $y -eq 15) { return 'f' }
        if (($x -eq 5 -or $x -eq 10) -or ($y -eq 5 -or $y -eq 10)) { return 'e' }
        $cx = ($x % 5); $cy = ($y % 5)
        if ($cx -ge 2 -and $cx -le 3 -and $cy -ge 2 -and $cy -le 3) { return 'c' }
        if ((($x + $y) % 3) -eq 0) { return 'b' }
        return 'a'
    }
    Save $blocks $lv.name $rows $pal
}

# ---- minecart with dispenser -------------------------------------------------------------------------
# entity texture (64x16): [0..15] cart metal, [16..31] dispenser front, [32..47] dispenser side, [48..63] top
$cartPal = @{ k = '#2c2f33'; d = '#474c52'; m = '#666c73'; l = '#8a9098'; h = '#b4bac2'; s = '#3a3a3e'; v = '#56565c'; n = '#6e6e75'; u = '#8a8a92'; o = '#111111' }
$metal = { param($x, $y) if ($x -eq 0 -or $y -eq 0) { 'l' } elseif ($x -eq 15 -or $y -eq 15) { 'k' } elseif ((($x + $y * 3) % 8) -eq 0) { 'h' } elseif ((($x * 2 + $y) % 9) -eq 0) { 'd' } else { 'm' } }
$cobble = { param($x, $y) if ($x -eq 0 -or $y -eq 0 -or $x -eq 15 -or $y -eq 15) { 's' } elseif ((($x * 7 + $y * 3) % 11) -eq 0) { 's' } elseif ((($x * 5 + $y * 13) % 7) -eq 0) { 'u' } elseif ((($x + $y) % 5) -eq 0) { 'v' } else { 'n' } }
$cart = Grid 64 16 {
    param($x, $y)
    $t = [int][Math]::Floor($x / 16); $u = $x % 16
    switch ($t) {
        0 { return (& $metal $u $y) }
        1 {
            $dx = [Math]::Abs($u - 7.5); $dy = [Math]::Abs($y - 7.5)
            if ($dx -le 2 -and $dy -le 2) { return 'o' }
            if ($dx -le 3 -and $dy -le 3) { return 's' }
            return (& $cobble $u $y)
        }
        2 { return (& $cobble $u $y) }
        default { if ($u -ge 3 -and $u -le 12 -and $y -ge 3 -and $y -le 12 -and ($u -eq 3 -or $u -eq 12 -or $y -eq 3 -or $y -eq 12)) { return 'v' } else { return (& $cobble $u $y) } }
    }
}
Save $entity 'dispenser_minecart' $cart $cartPal
$icon = Grid 16 16 {
    param($x, $y)
    # the cart (rows 9..14) with the dispenser on top (rows 1..9)
    if ($y -ge 9 -and $y -le 14 -and $x -ge 1 -and $x -le 14) {
        if ($y -eq 14 -and ($x -le 2 -or $x -ge 13)) { return '.' }
        if ($y -eq 9 -or $x -eq 1 -or $x -eq 14) { return 'k' }
        if ($y -eq 13) { return 'd' }
        if ((($x + $y) % 5) -eq 0) { return 'h' }
        return 'm'
    }
    if ($y -ge 1 -and $y -le 8 -and $x -ge 4 -and $x -le 11) {
        if ($y -eq 1 -or $x -eq 4 -or $x -eq 11) { return 's' }
        if ($x -ge 6 -and $x -le 9 -and $y -ge 3 -and $y -le 6) { if ($x -ge 7 -and $x -le 8 -and $y -ge 4 -and $y -le 5) { return 'o' } else { return 's' } }
        if ((($x + $y) % 3) -eq 0) { return 'u' }
        return 'n'
    }
    if ($y -eq 15 -and ($x -eq 3 -or $x -eq 4 -or $x -eq 11 -or $x -eq 12)) { return 'k' }
    return '.'
}
Save $items 'dispenser_minecart' $icon $cartPal

# ---- crystal display geometry (seven-segment bones driven by block states) -----------------------------
# Segment rectangles of a w x h digit (u right, v down): a top, b top right, c bottom right, d bottom,
# e bottom left, f top left, g middle.
function Segments([int]$w, [int]$h) {
    $m = [int](($h - 1) / 2)
    return [ordered]@{
        a = @(0, 0, $w, 1); b = @(($w - 1), 0, 1, ($m + 1)); c = @(($w - 1), $m, 1, ($h - $m)); d = @(0, ($h - 1), $w, 1)
        e = @(0, $m, 1, ($h - $m)); f = @(0, 0, 1, ($m + 1)); g = @(0, $m, $w, 1)
    }
}
$digits = [ordered]@{ l = @(3, 4, 4, 7); r = @(9, 4, 4, 7); big = @(5, 2, 6, 11) }
$segMask = @(0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F)
$segBit = @{ a = 0; b = 1; c = 2; d = 3; e = 4; f = 5; g = 6 }
$bones = [System.Collections.Generic.List[object]]::new()
$bones.Add([ordered]@{ name = 'body'; pivot = @(0, 0, 0); cubes = @(
            [ordered]@{ origin = @(-8, 0, -8); size = @(16, 16, 16); uv = [ordered]@{
                    north = [ordered]@{ uv = @(0, 0); uv_size = @(16, 16); material_instance = 'screen' }
                    south = [ordered]@{ uv = @(0, 0); uv_size = @(16, 16) }; east = [ordered]@{ uv = @(0, 0); uv_size = @(16, 16) }
                    west = [ordered]@{ uv = @(0, 0); uv_size = @(16, 16) }; up = [ordered]@{ uv = @(0, 0); uv_size = @(16, 16) }
                    down = [ordered]@{ uv = @(0, 0); uv_size = @(16, 16) }
                }
            })
    })
$visibility = [ordered]@{}
$power = "q.block_state('complementaries:power')"
$chain = "q.block_state('complementaries:chain')"
foreach ($dname in $digits.Keys) {
    $d = $digits[$dname]
    $segs = Segments $d[2] $d[3]
    foreach ($s in $segs.Keys) {
        $r = $segs[$s]
        $u = $d[0] + $r[0]; $v = $d[1] + $r[1]
        # front face = north (z = -8). Bedrock geometry mirrors x (model +x is world west), so the
        # viewer's left (world east) is model -x
        $x0 = $u - 8; $y0 = 16 - ($v + $r[3])
        $face = [ordered]@{ uv = @(0, 0); uv_size = @(1, 1); material_instance = 'segment' }
        $bones.Add([ordered]@{ name = "${dname}_$s"; parent = 'body'; pivot = @(0, 0, 0); cubes = @(
                    [ordered]@{ origin = @($x0, $y0, -8.05); size = @($r[2], $r[3], 0.05); uv = [ordered]@{ north = $face; south = $face; east = $face; west = $face; up = $face; down = $face } }
                ) })
        # powers (0..15) for which this segment is lit
        $lit = @()
        for ($p = 0; $p -le 15; $p++) {
            $digit = switch ($dname) { 'l' { if ($p -ge 10) { 1 } else { -1 } } 'r' { $p % 10 } default { [Math]::Min($p, 9) } }
            if ($digit -ge 0 -and (($segMask[$digit] -shr $segBit[$s]) -band 1)) { $lit += $p }
        }
        $mode = if ($dname -eq 'big') { "$chain != 0" } else { "$chain == 0" }
        $cond = if ($lit.Count -eq 0) { 'false' } elseif ($lit.Count -eq 16) { 'true' } else { '(' + (($lit | ForEach-Object { "$power == $_" }) -join ' || ') + ')' }
        $visibility["${dname}_$s"] = "$mode && $cond"
    }
}
$geo = [ordered]@{
    format_version = '1.16.0'
    'minecraft:geometry' = @([ordered]@{
            description = [ordered]@{ identifier = 'geometry.complementaries.crystal_display'; texture_width = 16; texture_height = 16; visible_bounds_width = 2; visible_bounds_height = 2; visible_bounds_offset = @(0, 0.5, 0) }
            bones = $bones
        })
}
$geoPath = Join-Path $root 'assets/pack/rp/models/blocks/crystal_display.geo.json'
New-Item -ItemType Directory -Force (Split-Path $geoPath) | Out-Null
$geo | ConvertTo-Json -Depth 20 | Set-Content $geoPath -Encoding utf8NoBOM
# the bone visibility map goes into the block definition (BP)
$blockPath = Join-Path $root 'assets/pack/bp/blocks/crystal_display.json'
$block = Get-Content $blockPath -Raw | ConvertFrom-Json -AsHashtable
$block['minecraft:block']['components']['minecraft:geometry']['bone_visibility'] = $visibility
$block | ConvertTo-Json -Depth 30 | Set-Content $blockPath -Encoding utf8NoBOM
Write-Host 'circuit textures and display geometry written'
