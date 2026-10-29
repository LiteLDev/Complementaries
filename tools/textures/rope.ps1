# Textures of U11 `rope`: rope, rope arrow, pulley, flax (original pixel art).
#
#   pwsh -NoProfile -File tools/textures/rope.ps1 [-Preview]
#
# Writes assets/pack/rp/textures/{blocks,items,entity}/complementaries/*.png
param([switch]$Preview)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lib.ps1"

$root = (Resolve-Path "$PSScriptRoot/../..").Path
$blocks = Join-Path $root 'assets/pack/rp/textures/blocks/complementaries'
$items = Join-Path $root 'assets/pack/rp/textures/items/complementaries'
$entity = Join-Path $root 'assets/pack/rp/textures/entity/complementaries'
$previewDir = Join-Path $root 'tools/textures/preview'

function Save($rows, $pal, $path) {
    Save-PixelArt -Rows $rows -Palette $pal -Path $path
    if ($Preview) { Save-PixelArt -Rows $rows -Palette $pal -Path (Join-Path $previewDir (Split-Path -Leaf $path)) -Scale 8 }
}

# An empty w x h canvas (array of char arrays) and a helper to turn it into rows.
function New-Canvas([int]$w = 16, [int]$h = 16) {
    $c = New-Object 'char[][]' $h
    for ($y = 0; $y -lt $h; $y++) { $c[$y] = ('.' * $w).ToCharArray() }
    return , $c
}
function To-Rows($canvas) { return , @($canvas | ForEach-Object { -join $_ }) }
function Put($canvas, [int]$x, [int]$y, [char]$ch) {
    if ($y -ge 0 -and $y -lt $canvas.Count -and $x -ge 0 -and $x -lt $canvas[$y].Length) { $canvas[$y][$x] = $ch }
}

# ---- rope ----------------------------------------------------------------------------------------
# Twisted strand: diagonal light/dark stripes, darker outline. Vertical strand in columns 6..9 and
# horizontal strand in rows 6..9 (the block model maps its faces onto these).
$ropePal = @{ l = '#d8b77e'; m = '#b38d55'; d = '#86653a'; o = '#5e4426' }
function Rope-Pixel([int]$across, [int]$along) {
    if ($across -eq 0 -or $across -eq 3) { if ((($along + $across) % 4) -lt 2) { return 'd' } else { return 'o' } }
    $k = ($along + $across) % 4
    if ($k -eq 0) { return 'l' } elseif ($k -eq 1) { return 'l' } elseif ($k -eq 2) { return 'm' } else { return 'd' }
}
$c = New-Canvas
for ($t = 0; $t -lt 16; $t++) {
    for ($a = 0; $a -lt 4; $a++) {
        Put $c (6 + $a) $t (Rope-Pixel $a $t)
        Put $c $t (6 + $a) (Rope-Pixel $a $t)
    }
}
Save (To-Rows $c) $ropePal (Join-Path $blocks 'rope.png')

# Rope item: a coil with a hanging end.
$ropeItem = @(
    '................',
    '.....oooooo.....',
    '...oodllllddo...',
    '..odlmmddmmldo..',
    '..olmo....omlo..',
    '.odmo......omdo.',
    '.olm........mlo.',
    '.odm........mdo.',
    '.olmo......omlo.',
    '..odmo....omdo..',
    '..oldmmddmmlmo..',
    '...oodllllddlo..',
    '.....oooooomdo..',
    '...........olo..',
    '...........odo..',
    '............o...'
)
Save $ropeItem $ropePal (Join-Path $items 'rope.png')

# ---- rope arrow ------------------------------------------------------------------------------------
$arrowPal = @{ l = '#d8b77e'; m = '#b38d55'; o = '#5e4426'; s = '#6b4a2b'; t = '#cfcfcf'; g = '#8f8f8f'; f = '#f0f0f0'; e = '#b9b9b9' }
$arrowItem = @(
    '................',
    '...........ggt..',
    '..........gttg..',
    '.........sgtg...',
    '........ollo....',
    '.......olmlo....',
    '......olmlo.....',
    '.....olmlo......',
    '....olmlo.......',
    '...ollmo........',
    '...ooso.........',
    '..ffso..........',
    '.fef.o..........',
    'fef.............',
    'ef..............',
    '................'
)
Save $arrowItem $arrowPal (Join-Path $items 'rope_arrow.png')
# Projectile texture (16x16): shaft along x in row 0..1, rope wrap, head and fletching; the entity
# model maps its two crossed planes onto rows 0..4 and the back square onto 0..4 x 5..9.
$arrowEntity = @(
    'ef.ollmlollmogtt',
    'fefolmlmolmlsggt',
    'ef.ollmlollmogtt',
    '....ooooooo.....',
    '................',
    '.ss.............',
    'sees............',
    'sees............',
    '.ss.............',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................'
)
Save $arrowEntity $arrowPal (Join-Path $entity 'rope_arrow.png')

# ---- pulley ----------------------------------------------------------------------------------------
$pulPal = @{ p = '#9c7a4a'; q = '#7f6038'; r = '#5d4426'; i = '#a9a9a9'; j = '#6f6f6f'; k = '#d4d4d4'; l = '#d8b77e'; m = '#b38d55'; d = '#86653a'; o = '#5e4426'; c = '#4f4f57'; n = '#8a8a96' }
$frame = @(
    'rrrrrrrrrrrrrrrr',
    'rppppqppppppqppr',
    'rpqqqqqqqqqqqqpr',
    'rpqppppppppppqpr',
    'rqqp........pqqr',
    'rpqp........pqpr',
    'rpqp........pqpr',
    'rpqp........pqpr',
    'rpqp........pqpr',
    'rpqp........pqpr',
    'rpqp........pqpr',
    'rqqp........pqqr',
    'rpqppppppppppqpr',
    'rpqqqqqqqqqqqqpr',
    'rppqppppppqppppr',
    'rrrrrrrrrrrrrrrr'
)
$wheel = @(
    '................',
    '................',
    '................',
    '................',
    '.....jjjjjj.....',
    '....jiiiiiij....',
    '....jikkjjij....',
    '....jijrrjij....',
    '....jijrrjij....',
    '....jiijjkij....',
    '....jiiiiiij....',
    '.....jjjjjj.....',
    '................',
    '................',
    '................',
    '................'
)
$inner = @(
    '................',
    '................',
    '................',
    '................',
    '....rrrrrrrr....',
    '....rqqqqqqr....',
    '....rqqqqqqr....',
    '....rqqqqqqr....',
    '....rqqqqqqr....',
    '....rqqqqqqr....',
    '....rqqqqqqr....',
    '....rrrrrrrr....',
    '................',
    '................',
    '................',
    '................'
)
Save (Merge-Rows (Merge-Rows $frame $inner) $wheel) $pulPal (Join-Path $blocks 'pulley_block_front.png')
$sideBase = Merge-Rows $frame $inner
Save $sideBase $pulPal (Join-Path $blocks 'pulley_block_side.png')
# Side with rope / chain wound around the axle (a band in rows 5..10).
$band = New-Canvas
for ($x = 4; $x -lt 12; $x++) { for ($y = 5; $y -lt 11; $y++) { Put $band $x $y (Rope-Pixel (($y - 5) % 4) $x) } }
Save (Merge-Rows $sideBase (To-Rows $band)) $pulPal (Join-Path $blocks 'pulley_block_side_rope.png')
$chain = New-Canvas
for ($x = 4; $x -lt 12; $x++) {
    for ($y = 5; $y -lt 11; $y++) {
        $link = (($x + [int][Math]::Floor($y / 2)) % 3)
        Put $chain $x $y ([char]($(if ($link -eq 0) { 'c' } elseif ($link -eq 1) { 'n' } else { 'i' })))
    }
}
Save (Merge-Rows $sideBase (To-Rows $chain)) $pulPal (Join-Path $blocks 'pulley_block_side_chain.png')

# ---- flax --------------------------------------------------------------------------------------------
$flaxPal = @{ g = '#5f9a3a'; h = '#447a2a'; d = '#2f5a1e'; b = '#6f8fd8'; c = '#4b67b8'; w = '#dfe6ff'; y = '#d9c36a'; z = '#b89a42'; s = '#8b6a3a'; t = '#6a5030' }
# Deterministic stalks: each stalk is a column with a height; leaves alternate sides.
function Flax-Stage([int[]]$cols, [int]$height, [int]$bloom, [int]$ripe, [switch]$Top) {
    $c = New-Canvas
    foreach ($x in $cols) {
        $hgt = [Math]::Min(16, $height + (($x * 7) % 3))
        for ($i = 0; $i -lt $hgt; $i++) {
            $y = 15 - $i
            $ch = if ($ripe) { if ($i % 3 -eq 0) { 'z' } else { 'y' } } elseif ($i % 4 -eq 1) { 'h' } else { 'g' }
            Put $c $x $y $ch
            if ($i -gt 1 -and (($i + $x * 2) % 5) -eq 0) {
                $side = if (((($x * 3) + $i) % 2) -eq 0) { -1 } else { 1 }
                Put $c ($x + $side) ($y) $(if ($ripe) { 'z' } else { 'd' })
                Put $c ($x + 2 * $side) ($y - 1) $(if ($ripe) { 'y' } else { 'h' })
            }
        }
        if ($bloom -and $hgt -gt 2) {
            $ty = 16 - $hgt
            if ($ripe) { Put $c $x $ty 's'; Put $c ($x - 1) ($ty + 1) 't'; Put $c ($x + 1) ($ty + 1) 's' }
            else { Put $c $x $ty 'w'; Put $c ($x - 1) ($ty + 1) 'b'; Put $c ($x + 1) ($ty + 1) 'c'; Put $c $x ($ty + 1) 'b' }
        }
    }
    return , (To-Rows $c)
}
$cols = @(2, 5, 8, 11, 14)
$heights = @(3, 5, 7, 10, 16, 16, 16, 16)
for ($age = 0; $age -lt 8; $age++) {
    $rows = Flax-Stage $cols $heights[$age] 0 0
    Save $rows $flaxPal (Join-Path $blocks "flax_crop_stage_$age.png")
}
$topHeights = @{ 4 = 3; 5 = 7; 6 = 11; 7 = 12 }
foreach ($age in 4..7) {
    $rows = Flax-Stage $cols $topHeights[$age] ($age -ge 6) ($age -eq 7) -Top
    Save $rows $flaxPal (Join-Path $blocks "flax_crop_top_stage_$age.png")
}
Save (Flax-Stage @(3, 7, 12) 11 1 0) $flaxPal (Join-Path $blocks 'wild_flax.png')

# Flax block (bale): straw-like fibres with a band.
$balePal = @{ a = '#d2c08a'; b = '#bba66d'; c = '#9e8a52'; e = '#7b6a3c'; f = '#5d8a3a' }
$side = New-Canvas
for ($y = 0; $y -lt 16; $y++) {
    for ($x = 0; $x -lt 16; $x++) {
        $k = (($x * 5 + $y * 3) % 7)
        $ch = if ($x % 4 -eq 3) { 'c' } elseif ($k -lt 3) { 'a' } else { 'b' }
        if ($y -eq 4 -or $y -eq 11) { $ch = 'e' }
        if ($y -eq 3 -or $y -eq 12) { $ch = 'f' }
        Put $side $x $y ([char]$ch)
    }
}
Save (To-Rows $side) $balePal (Join-Path $blocks 'flax_block_side.png')
$top = New-Canvas
for ($y = 0; $y -lt 16; $y++) {
    for ($x = 0; $x -lt 16; $x++) {
        $edge = ($x -eq 0 -or $y -eq 0 -or $x -eq 15 -or $y -eq 15)
        $ring = [Math]::Max([Math]::Abs($x - 7.5), [Math]::Abs($y - 7.5))
        $ch = if ($edge) { 'e' } elseif ([int]$ring % 3 -eq 0) { 'c' } elseif ((($x + $y) % 3) -eq 0) { 'a' } else { 'b' }
        Put $top $x $y ([char]$ch)
    }
}
Save (To-Rows $top) $balePal (Join-Path $blocks 'flax_block_top.png')

# Items: flax (a bundle of stalks) and flax seeds.
$flaxItem = @(
    '................',
    '..........w.....',
    '.........bcb....',
    '......w...g.....',
    '.....bcb..g.....',
    '......g..g..w...',
    '......g..g.bcb..',
    '.......g.g..g...',
    '.......g.g.g....',
    '.......ggggg....',
    '......zyyyyz....',
    '.......ggg......',
    '.......ghg......',
    '......gh.hg.....',
    '.....h.....h....',
    '................'
)
Save $flaxItem $flaxPal (Join-Path $items 'flax.png')
$seedPal = @{ s = '#8b6a3a'; t = '#6a5030'; u = '#b08c55' }
$seeds = @(
    '................',
    '................',
    '................',
    '................',
    '.......u........',
    '......sst....u..',
    '.......t....sst.',
    '.............t..',
    '...u.....u......',
    '..sst...sst.....',
    '...t.....t......',
    '.......u........',
    '......sst.......',
    '.......t........',
    '................',
    '................'
)
Save $seeds $seedPal (Join-Path $items 'flax_seeds.png')

Write-Host "rope textures written"
