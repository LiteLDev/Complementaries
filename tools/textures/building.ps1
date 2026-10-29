# Original pixel art of unit U10 `building` (16x16 block and item textures).
#
#   pwsh tools/textures/building.ps1
#
# Every texture is built from a deterministic generator (fixed seeds) on a grid of palette keys and
# saved with Save-PixelArt (tools/textures/lib.ps1).

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lib.ps1"
$root = Resolve-Path (Join-Path $PSScriptRoot '../..')
$blocks = Join-Path $root 'assets/pack/rp/textures/blocks/building'
$items = Join-Path $root 'assets/pack/rp/textures/items/building'

# ---- grid helpers --------------------------------------------------------------------------------
function New-Grid([char]$Fill = '.') { $g = New-Object 'char[,]' 16, 16; for ($y = 0; $y -lt 16; $y++) { for ($x = 0; $x -lt 16; $x++) { $g[$y, $x] = $Fill } }; return , $g }
function Grid-Rows($G) { $rows = for ($y = 0; $y -lt 16; $y++) { -join (0..15 | ForEach-Object { $G[$y, $_] }) }; return , $rows }
function Copy-Grid($G) { $c = New-Object 'char[,]' 16, 16; [Array]::Copy($G, $c, 256); return , $c }
function Flip-Grid($G) { $c = New-Grid; for ($y = 0; $y -lt 16; $y++) { for ($x = 0; $x -lt 16; $x++) { $c[$y, $x] = $G[$y, (15 - $x)] } }; return , $c }
function Rotate-Grid($G) {
    # 90 degrees clockwise
    $c = New-Grid; for ($y = 0; $y -lt 16; $y++) { for ($x = 0; $x -lt 16; $x++) { $c[$y, $x] = $G[(15 - $x), $y] } }; return , $c
}
function Overlay-Grid($Base, $Top) { $c = Copy-Grid $Base; for ($y = 0; $y -lt 16; $y++) { for ($x = 0; $x -lt 16; $x++) { if ($Top[$y, $x] -ne '.') { $c[$y, $x] = $Top[$y, $x] } } }; return , $c }

# Noise of shade keys '1'..'5' (3 = base) with a fixed seed.
function Noise-Grid([int]$Seed, [int[]]$Weights = @(1, 3, 8, 3, 1)) {
    $rnd = [Random]::new($Seed); $g = New-Grid
    $total = ($Weights | Measure-Object -Sum).Sum
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            $r = $rnd.Next($total); $acc = 0
            for ($i = 0; $i -lt 5; $i++) { $acc += $Weights[$i]; if ($r -lt $acc) { $g[$y, $x] = [char](49 + $i); break } }
        }
    }
    return , $g
}
function Shades($Base, [double]$Step = 0.12) {
    return @{ '1' = (Shade-Color $Base (1 - 2 * $Step)); '2' = (Shade-Color $Base (1 - $Step)); '3' = $Base; '4' = (Shade-Color $Base (1 + $Step)); '5' = (Shade-Color $Base (1 + 2 * $Step)) }
}
function Save-Grid($G, [hashtable]$Palette, [string]$Path) { Save-PixelArt -Rows (Grid-Rows $G) -Palette $Palette -Path $Path }

# ---- ash -----------------------------------------------------------------------------------------
$ash = Noise-Grid 101 @(1, 4, 7, 3, 1)
$rnd = [Random]::new(7)
for ($i = 0; $i -lt 10; $i++) { $ash[$rnd.Next(16), $rnd.Next(16)] = 'k' }  # charcoal bits
$ashPal = (Shades '#9b9a98' 0.08) + @{ k = '#4a4846' }
Save-Grid $ash $ashPal "$blocks/ash.png"

# ash item: a small heap
$heap = @(
    '................',
    '................',
    '................',
    '................',
    '................',
    '.......44.......',
    '.....445333.....',
    '....4533k333....',
    '...453333332....',
    '..45333k333322..',
    '..4333333332k2..',
    '.433k333333332..',
    '.33333332k33322.',
    '.2333333333322..',
    '..222222222221..',
    '................'
)
Save-PixelArt -Rows $heap -Palette $ashPal -Path "$items/ash.png"

# ash brick item
$brickItem = @(
    '................',
    '................',
    '................',
    '................',
    '................',
    '..........5544..',
    '.......554433o..',
    '....5544333332o.',
    '..55443333k3322o',
    '..o4333333332211',
    '..o33k3333221111',
    '..o333322111111.',
    '...o2221111111..',
    '....o111111.....',
    '................',
    '................'
)
Save-PixelArt -Rows $brickItem -Palette ((Shades '#6e6d72' 0.12) + @{ k = '#3c3b40'; o = '#2b2a2e' }) -Path "$items/ash_brick.png"

# ---- bricks --------------------------------------------------------------------------------------
# Brick courses 4 px high, bricks 8 px wide, offset by 4 every other course; mortar 'm'.
function Brick-Grid([int]$Seed, [int]$CourseH = 4, [int]$BrickW = 8) {
    $g = Noise-Grid $Seed @(1, 3, 8, 3, 1)
    for ($y = 0; $y -lt 16; $y++) {
        $course = [Math]::Floor($y / $CourseH)
        $off = if ($course % 2 -eq 0) { 0 } else { $BrickW / 2 }
        for ($x = 0; $x -lt 16; $x++) {
            if ($y % $CourseH -eq ($CourseH - 1) -or (($x + $off) % $BrickW) -eq ($BrickW - 1)) { $g[$y, $x] = 'm' }
            elseif ($y % $CourseH -eq 0 -and $g[$y, $x] -lt '4') { $g[$y, $x] = '4' }        # lit top edge
        }
    }
    return , $g
}
Save-Grid (Brick-Grid 201) ((Shades '#5f5e63' 0.1) + @{ m = '#3a393d' }) "$blocks/ash_bricks.png"
$lapis = Brick-Grid 301 4 8
$rnd = [Random]::new(31)
for ($i = 0; $i -lt 7; $i++) { $x = $rnd.Next(16); $y = $rnd.Next(16); if ($lapis[$y, $x] -ne 'm') { $lapis[$y, $x] = 'g' } }
Save-Grid $lapis ((Shades '#2d58b8' 0.12) + @{ m = '#1b2f6b'; g = '#d9b23a' }) "$blocks/lapis_bricks.png"

# ---- tiles ---------------------------------------------------------------------------------------
# 2x2 tiles of 8 px with a bevel: light top/left edge, dark bottom/right edge.
function Tile-Grid([int]$Seed) {
    $g = Noise-Grid $Seed @(0, 2, 10, 2, 0)
    for ($y = 0; $y -lt 16; $y++) {
        for ($x = 0; $x -lt 16; $x++) {
            $ty = $y % 8; $tx = $x % 8
            if ($ty -eq 7 -or $tx -eq 7) { $g[$y, $x] = '1' }
            elseif ($ty -eq 0 -or $tx -eq 0) { $g[$y, $x] = '5' }
            elseif ($ty -eq 6 -or $tx -eq 6) { $g[$y, $x] = '2' }
        }
    }
    return , $g
}
Save-Grid (Tile-Grid 401) (Shades '#8c8c8c' 0.14) "$blocks/stone_tile.png"
Save-Grid (Tile-Grid 501) (Shades '#3a3036' 0.22) "$blocks/blackstone_tile.png"

# checker: 8 px squares, white and black
$checker = Noise-Grid 601 @(0, 2, 12, 2, 0)
for ($y = 0; $y -lt 16; $y++) {
    for ($x = 0; $x -lt 16; $x++) {
        $dark = ([Math]::Floor($y / 8) + [Math]::Floor($x / 8)) % 2 -eq 1
        $c = [string]$checker[$y, $x]
        $checker[$y, $x] = if ($dark) { @{ '2' = 'a'; '3' = 'b'; '4' = 'c' }[$c] } else { @{ '2' = 'x'; '3' = 'y'; '4' = 'z' }[$c] }
    }
}
Save-Grid $checker @{ a = '#1c1a1e'; b = '#262428'; c = '#302e33'; x = '#d6d2cc'; y = '#e4e0da'; z = '#efebe5' } "$blocks/checker_block.png"

# ---- wood ----------------------------------------------------------------------------------------
# fine wood: smooth boards with long horizontal grain, a seam every 8 rows
$fine = New-Grid '3'
$rnd = [Random]::new(701)
for ($y = 0; $y -lt 16; $y++) {
    if ($y % 8 -eq 7) { for ($x = 0; $x -lt 16; $x++) { $fine[$y, $x] = '1' }; continue }
    $start = $rnd.Next(16); $len = 4 + $rnd.Next(9); $shade = if ($rnd.Next(2) -eq 0) { '2' } else { '4' }
    for ($i = 0; $i -lt $len; $i++) { $fine[$y, (($start + $i) % 16)] = $shade }
}
Save-Grid $fine (Shades '#b8915c' 0.08) "$blocks/fine_wood.png"

# timber frames: a 2 px wooden border (transparent inside), braces add diagonals
$wood = (Shades '#7a5631' 0.1)
function Frame-Grid([switch]$Brace, [switch]$Cross) {
    $g = New-Grid
    for ($i = 0; $i -lt 16; $i++) {
        foreach ($p in @(@(0, $i), @(1, $i), @(14, $i), @(15, $i), @($i, 0), @($i, 1), @($i, 14), @($i, 15))) {
            $g[$p[0], $p[1]] = if ($p[0] -eq 0 -or $p[1] -eq 0) { '4' } elseif ($p[0] -eq 15 -or $p[1] -eq 15) { '2' } else { '3' }
        }
    }
    if ($Brace -or $Cross) {
        # bottom-left to top-right
        for ($i = 1; $i -lt 15; $i++) { $g[(15 - $i), $i] = '3'; if ($i -lt 14) { $g[(15 - $i), ($i + 1)] = '2' }; if ($i -gt 1) { $g[(16 - $i), $i] = '4' } }
    }
    if ($Cross) {
        for ($i = 1; $i -lt 15; $i++) { $g[$i, $i] = '3'; if ($i -lt 14) { $g[$i, ($i + 1)] = '2' }; if ($i -gt 1) { $g[($i - 1), $i] = '4' } }
    }
    return , $g
}
$frame = Frame-Grid; $brace = Frame-Grid -Brace; $cross = Frame-Grid -Cross
Save-Grid $frame $wood "$blocks/timber_frame.png"
Save-Grid $brace $wood "$blocks/timber_brace.png"
Save-Grid (Flip-Grid $brace) $wood "$blocks/timber_brace_flipped.png"
Save-Grid $cross $wood "$blocks/timber_cross_brace.png"

# ---- daub ----------------------------------------------------------------------------------------
$daub = Noise-Grid 801 @(1, 3, 9, 3, 0)
$rnd = [Random]::new(81)
for ($i = 0; $i -lt 6; $i++) { $daub[$rnd.Next(16), $rnd.Next(16)] = 's' }   # straw bits
$daubPal = @{ '1' = '#b9ae95'; '2' = '#cbc1a8'; '3' = '#d9d0b9'; '4' = '#e5dec9'; '5' = '#efe9d8'; s = '#c9a85a' }
Save-Grid $daub $daubPal "$blocks/daub.png"
# frames over daub: wood keys renamed so the palettes don't clash
function Wood-Keys($G) { $c = Copy-Grid $G; for ($y = 0; $y -lt 16; $y++) { for ($x = 0; $x -lt 16; $x++) { $c[$y, $x] = @{ '.' = '.'; '2' = 'p'; '3' = 'q'; '4' = 'r' }[[string]$G[$y, $x]] } }; return , $c }
$daubFramePal = $daubPal + @{ p = $wood['2']; q = $wood['3']; r = $wood['4'] }
Save-Grid (Overlay-Grid $daub (Wood-Keys $frame)) $daubFramePal "$blocks/daub_frame.png"
Save-Grid (Overlay-Grid $daub (Wood-Keys $brace)) $daubFramePal "$blocks/daub_brace.png"
Save-Grid (Overlay-Grid $daub (Wood-Keys (Flip-Grid $brace))) $daubFramePal "$blocks/daub_brace_flipped.png"
Save-Grid (Overlay-Grid $daub (Wood-Keys $cross)) $daubFramePal "$blocks/daub_cross_brace.png"

# ---- misc blocks ---------------------------------------------------------------------------------
# soap: smooth pink with a few bubbles
$soap = Noise-Grid 901 @(0, 2, 12, 2, 0)
foreach ($b in @(@(3, 4), @(10, 9), @(6, 12), @(12, 3))) {
    $y = $b[0]; $x = $b[1]
    $soap[$y, $x] = 'w'; $soap[$y, ($x + 1)] = '5'; $soap[($y + 1), $x] = '5'; $soap[($y + 1), ($x + 1)] = '4'
}
Save-Grid $soap ((Shades '#e9a3c9' 0.07) + @{ w = '#ffffff' }) "$blocks/soap_block.png"

# flint: dark knapped facets
$flint = Noise-Grid 1001 @(2, 5, 6, 2, 1)
$rnd = [Random]::new(1003)
for ($i = 0; $i -lt 9; $i++) {
    $y = $rnd.Next(15); $x = $rnd.Next(14)
    $flint[$y, $x] = '5'; $flint[$y, ($x + 1)] = '4'; $flint[($y + 1), ($x + 1)] = '1'
}
Save-Grid $flint (Shades '#45454b' 0.16) "$blocks/flint_block.png"

# feather block: white with grey vane strokes
$feather = Noise-Grid 1101 @(0, 1, 10, 4, 1)
for ($row = 0; $row -lt 16; $row += 4) {
    for ($x = 0; $x -lt 16; $x++) { $y = ($row + [Math]::Floor($x / 4)) % 16; $feather[$y, $x] = '1' }
}
Save-Grid $feather (Shades '#e8e6e0' 0.06) "$blocks/feather_block.png"

# sugar cube: grainy white with a faint grid
$sugar = Noise-Grid 1201 @(0, 2, 10, 3, 1)
for ($i = 0; $i -lt 16; $i++) { if ($i % 4 -eq 3) { for ($j = 0; $j -lt 16; $j++) { if ($sugar[$i, $j] -gt '2') { $sugar[$i, $j] = '2' }; if ($sugar[$j, $i] -gt '2') { $sugar[$j, $i] = '2' } } } }
Save-Grid $sugar (Shades '#f2f1ee' 0.04) "$blocks/sugar_cube.png"

# ---- raked gravel --------------------------------------------------------------------------------
# Top textures: texture up = north, right = east. Grooves 'g' every 4 px.
function Raked-Grid([scriptblock]$IsGroove, [int]$Seed) {
    $g = Noise-Grid $Seed @(2, 4, 6, 3, 1)
    for ($y = 0; $y -lt 16; $y++) { for ($x = 0; $x -lt 16; $x++) { if (& $IsGroove $x $y) { $g[$y, $x] = 'g' } elseif (& $IsGroove ($x - 1) $y) { $g[$y, $x] = '5' } } }
    return , $g
}
$gravelPal = (Shades '#857f7c' 0.14) + @{ g = '#5a5553' }
$straight = Raked-Grid { param($x, $y) ($x % 4) -eq 1 } 1301
$corner = Raked-Grid { param($x, $y) $d = [Math]::Sqrt([Math]::Pow($x - 15.5, 2) + [Math]::Pow($y + 0.5, 2)); ([Math]::Floor($d) % 4) -eq 1 } 1302
# 0 north-south, 1 east-west, 2 north-east, 3 north-west, 4 south-east, 5 south-west
Save-Grid $straight $gravelPal "$blocks/raked_gravel_0.png"
Save-Grid (Rotate-Grid $straight) $gravelPal "$blocks/raked_gravel_1.png"
Save-Grid $corner $gravelPal "$blocks/raked_gravel_2.png"
Save-Grid (Rotate-Grid (Rotate-Grid (Rotate-Grid $corner))) $gravelPal "$blocks/raked_gravel_3.png"
Save-Grid (Rotate-Grid $corner) $gravelPal "$blocks/raked_gravel_4.png"
Save-Grid (Rotate-Grid (Rotate-Grid $corner)) $gravelPal "$blocks/raked_gravel_5.png"

Write-Host 'building textures written'
