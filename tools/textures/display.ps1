# Draws the textures of the display unit (pedestal, item shelf, statue, hat stand).
# Original pixel art: run `pwsh tools/textures/display.ps1` to regenerate.
. "$PSScriptRoot/lib.ps1"

$root = Join-Path $PSScriptRoot '../../assets/pack/rp/textures'
$blocks = Join-Path $root 'blocks'
$items = Join-Path $root 'items'
$entity = Join-Path $root 'entity/complementaries'

# Deterministic noise: a small LCG so the art never changes between runs.
$script:seed = 12345
function Next-Rand([int]$mod) {
    $script:seed = ($script:seed * 1103515245 + 12345) % 2147483648
    return [int]($script:seed % $mod)
}

# Fills a w x h grid with `fill`, sprinkling `alt` keys with the given chance (percent).
function New-Grid([int]$w, [int]$h, [char]$fill, [char[]]$alt = @(), [int]$chance = 0) {
    $rows = for ($y = 0; $y -lt $h; $y++) {
        $line = for ($x = 0; $x -lt $w; $x++) {
            if ($alt.Count -gt 0 -and (Next-Rand 100) -lt $chance) { $alt[(Next-Rand $alt.Count)] } else { $fill }
        }
        -join $line
    }
    return , [string[]]$rows
}

function Set-Px([string[]]$rows, [int]$x, [int]$y, [char]$c) {
    $a = $rows[$y].ToCharArray(); $a[$x] = $c; $rows[$y] = -join $a
}

function Fill-Rect([string[]]$rows, [int]$x0, [int]$y0, [int]$w, [int]$h, [char]$c) {
    for ($y = $y0; $y -lt $y0 + $h; $y++) { for ($x = $x0; $x -lt $x0 + $w; $x++) { Set-Px $rows $x $y $c } }
}

# ---- stone palette (pedestal, statue) ----------------------------------------------------------
$stone = @{
    s = '#8f8f8f'; t = '#9c9c9c'; d = '#7a7a7a'; k = '#5f5f5f'; l = '#b0b0b0'; m = '#6c6c6c'
}

# Pedestal side: cap band (rows 0-2), pillar (rows 3-12, x 3-12), base band (rows 13-15).
$side = New-Grid 16 16 's' @('t', 'd') 22
Fill-Rect $side 0 0 16 1 'l'
Fill-Rect $side 0 2 16 1 'k'
Fill-Rect $side 0 13 16 1 'l'
Fill-Rect $side 0 15 16 1 'k'
for ($y = 3; $y -le 12; $y++) {
    Set-Px $side 3 $y 'l'; Set-Px $side 12 $y 'k'
    Set-Px $side 6 $y 'm'; Set-Px $side 9 $y 'm'
    Set-Px $side 0 $y 'k'; Set-Px $side 15 $y 'k'
}
Set-Px $side 4 7 'k'; Set-Px $side 10 5 'd'; Set-Px $side 7 10 'd'
Save-PixelArt -Rows $side -Palette $stone -Path (Join-Path $blocks 'pedestal_side.png')

$top = New-Grid 16 16 't' @('s', 'l') 18
Fill-Rect $top 1 1 14 1 'l'; Fill-Rect $top 1 14 14 1 'k'
for ($y = 1; $y -le 14; $y++) { Set-Px $top 1 $y 'l'; Set-Px $top 14 $y 'k' }
Fill-Rect $top 4 4 8 1 'd'; Fill-Rect $top 4 11 8 1 'd'
for ($y = 4; $y -le 11; $y++) { Set-Px $top 4 $y 'd'; Set-Px $top 11 $y 'd' }
Save-PixelArt -Rows $top -Palette $stone -Path (Join-Path $blocks 'pedestal_top.png')

$bottom = New-Grid 16 16 'd' @('s', 'm') 25
Fill-Rect $bottom 1 1 14 1 'm'; Fill-Rect $bottom 1 14 14 1 'k'
Save-PixelArt -Rows $bottom -Palette $stone -Path (Join-Path $blocks 'pedestal_bottom.png')

# Statue base (smooth stone slab look).
$base = New-Grid 16 16 't' @('s') 15
Fill-Rect $base 4 4 8 1 'l'; Fill-Rect $base 4 11 8 1 'k'
for ($y = 4; $y -le 11; $y++) { Set-Px $base 4 $y 'l'; Set-Px $base 11 $y 'k' }
Fill-Rect $base 4 12 8 1 'd'
Save-PixelArt -Rows $base -Palette $stone -Path (Join-Path $blocks 'statue_base.png')

# ---- item shelf (oak) ------------------------------------------------------------------------------
$wood = @{ w = '#a2834f'; v = '#b8945f'; d = '#7e6237'; k = '#5b4524'; l = '#c9a66d' }
$shelf = New-Grid 16 16 'w' @('v', 'd') 15
Fill-Rect $shelf 0 0 16 1 'l'; Fill-Rect $shelf 0 1 16 1 'd'           # front edge
Fill-Rect $shelf 0 3 16 1 'k'
for ($x = 0; $x -lt 14; $x += 5) { Set-Px $shelf $x 5 'd'; Set-Px $shelf ($x + 2) 8 'd' }
Fill-Rect $shelf 0 10 6 2 'd'; Set-Px $shelf 0 10 'l'; Set-Px $shelf 2 10 'l'
Save-PixelArt -Rows $shelf -Palette $wood -Path (Join-Path $blocks 'item_shelf.png')

# ---- invisible texture of the display helper entity ------------------------------------------------
New-Item -ItemType Directory -Force $entity | Out-Null
Save-PixelArt -Rows (New-Grid 16 16 '.') -Palette @{} -Path (Join-Path $entity 'item_display.png')

# ---- statue skin (64x64, standard player skin layout, carved stone) ------------------------------------
$sk = @{ s = '#a6a6a6'; t = '#b5b5b5'; d = '#8a8a8a'; k = '#6b6b6b'; l = '#c8c8c8'; e = '#585858' }
$skin = New-Grid 64 64 '.'
function Paint-Box([string[]]$rows, [int]$u, [int]$v, [int]$w, [int]$h, [int]$d) {
    # Box UV: top/bottom (w x d) at (u+d, v), sides (d|w|d|w x h) at (u, v+d).
    for ($y = $v; $y -lt $v + $d; $y++) { for ($x = $u + $d; $x -lt $u + $d + 2 * $w; $x++) { Set-Px $rows $x $y ([char]('tsl'[(Next-Rand 3)])) } }
    for ($y = $v + $d; $y -lt $v + $d + $h; $y++) {
        for ($x = $u; $x -lt $u + 2 * $d + 2 * $w; $x++) {
            $c = 's'; $r = Next-Rand 100
            if ($r -lt 15) { $c = 't' } elseif ($r -lt 27) { $c = 'd' }
            if ($y -eq $v + $d + $h - 1) { $c = 'k' }
            Set-Px $rows $x $y ([char]$c)
        }
    }
}
Paint-Box $skin 0 0 8 8 8      # head
Paint-Box $skin 16 16 8 12 4   # body
Paint-Box $skin 40 16 4 12 4   # right arm
Paint-Box $skin 32 48 4 12 4   # left arm
Paint-Box $skin 0 16 4 12 4    # right leg
Paint-Box $skin 16 48 4 12 4   # left leg
# face (front of the head: x 8-15, y 8-15): brows, eyes, nose, mouth carved darker
Fill-Rect $skin 9 11 2 1 'e'; Fill-Rect $skin 13 11 2 1 'e'
Fill-Rect $skin 9 10 2 1 'd'; Fill-Rect $skin 13 10 2 1 'd'
Fill-Rect $skin 11 12 2 2 'l'; Fill-Rect $skin 10 14 4 1 'k'
Fill-Rect $skin 8 8 8 2 'd'      # hair line
# belt and tunic hem on the body front (x 20-27, y 20-31)
Fill-Rect $skin 20 26 8 1 'k'; Fill-Rect $skin 23 20 2 6 'd'
Save-PixelArt -Rows $skin -Palette $sk -Path (Join-Path $entity 'statue.png')

# ---- hat stand entity (64x32): head (box uv 0,0), neck (32,0), base plate (0,16) -------------------------
$hs = @{ w = '#b8945f'; v = '#a2834f'; d = '#7e6237'; k = '#5b4524'; l = '#d0ad74'; s = '#9c9c9c'; t = '#b0b0b0'; m = '#7a7a7a' }
$hat = New-Grid 64 32 '.'
for ($y = 0; $y -lt 16; $y++) { for ($x = 0; $x -lt 32; $x++) {
    if ($y -lt 8 -and ($x -lt 8 -or $x -ge 24)) { continue }
    $c = 'w'; $r = Next-Rand 100; if ($r -lt 18) { $c = 'v' } elseif ($r -lt 26) { $c = 'l' }
    if (($y -ge 8) -and (($y - 8) % 4 -eq 3)) { $c = 'd' }
    Set-Px $hat $x $y ([char]$c)
} }
for ($y = 0; $y -lt 5; $y++) { for ($x = 32; $x -lt 40; $x++) {
    if ($y -lt 2 -and ($x -lt 34 -or $x -ge 38)) { continue }
    Set-Px $hat $x $y ([char]('dk'[($x + $y) % 2]))
} }
for ($y = 16; $y -lt 29; $y++) { for ($x = 0; $x -lt 48; $x++) {
    if ($y -lt 28 -and ($x -lt 12 -or $x -ge 36)) { continue }   # base plate box uv: 12x12 top/bottom, 1-pixel sides
    $c = 's'; $r = Next-Rand 100; if ($r -lt 20) { $c = 't' } elseif ($r -lt 30) { $c = 'm' }
    Set-Px $hat $x $y ([char]$c)
} }
Save-PixelArt -Rows $hat -Palette $hs -Path (Join-Path $entity 'hat_stand.png')

# ---- item icons (16x16) -----------------------------------------------------------------------------------
$icon = @(
    '................',
    '.....oooooo.....',
    '....owwvwwvwo...',
    '....owvwwwwwo...',
    '....owwwlwwvo...',
    '....owvwwwwwo...',
    '....owwwvwwwo...',
    '.....oooooo.....',
    '.......od.......',
    '.......od.......',
    '.......od.......',
    '.......od.......',
    '...kkkkkkkkkk...',
    '..kssttssttssk..',
    '..kmmmmmmmmmmk..',
    '................'
)
Save-PixelArt -Rows ([string[]]($icon | ForEach-Object { $_.Replace('o', 'k') })) -Palette $hs -Path (Join-Path $items 'hat_stand.png')

$statueIcon = @(
    '......kkkk......',
    '.....kttttk.....',
    '.....ktekek.....',
    '.....kttttk.....',
    '......kttk......',
    '....kkssssk.k...',
    '...ktkssssktk...',
    '...ktksddskttk..',
    '...k.ksssskk....',
    '.....ksssk......',
    '.....kssssk.....',
    '.....ks.ksk.....',
    '.....ks.ksk.....',
    '....kkkkkkkk....',
    '...klllllllllk..',
    '...kddddddddk...'
)
Save-PixelArt -Rows ([string[]]($statueIcon | ForEach-Object { $_.PadRight(16, '.').Substring(0, 16) })) -Palette $sk -Path (Join-Path $items 'statue.png')
