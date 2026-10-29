# Textures of U4 `sign` (original pixel art): sign post items and arm textures for every wood,
# notice board faces and the doormat.
#
#   pwsh tools/textures/sign.ps1            # writes into assets/pack/rp/textures
#   pwsh tools/textures/sign.ps1 -Preview   # also writes 8x previews to tools/textures/preview/
param([switch]$Preview)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lib.ps1"
$rp = Join-Path $PSScriptRoot '../../assets/pack/rp/textures'

function Save([string[]]$Rows, [hashtable]$Pal, [string]$Rel) {
    Save-PixelArt -Rows $Rows -Palette $Pal -Path (Join-Path $rp $Rel)
    if ($Preview) { Save-PixelArt -Rows $Rows -Palette $Pal -Path (Join-Path $PSScriptRoot "preview/$($Rel -replace '[\\/]', '_')") -Scale 8 }
}

# A canvas of $w x $h filled with $fill; Set-Px writes one key.
function New-Canvas([int]$w, [int]$h, [char]$fill) { , @(for ($y = 0; $y -lt $h; $y++) { , ([char[]]([string]$fill * $w)) }) }
function Set-Px($c, [int]$x, [int]$y, [char]$k) { if ($y -ge 0 -and $y -lt $c.Count -and $x -ge 0 -and $x -lt $c[$y].Length) { $c[$y][$x] = $k } }
function To-Rows($c) { , @($c | ForEach-Object { -join $_ }) }

# plank base colour of each wood (own palette)
$woods = [ordered]@{
    oak = '#b8945f'; spruce = '#7a5a34'; birch = '#d7c185'; jungle = '#b88764'; acacia = '#ba6337'; dark_oak = '#4f3218'
    mangrove = '#773a35'; cherry = '#e2b2a6'; bamboo = '#d4bb59'; crimson = '#6b344b'; warped = '#2b6963'; pale_oak = '#e6dcd6'
}

foreach ($wood in $woods.Keys) {
    $base = $woods[$wood]
    $pal = @{
        o = Shade-Color $base 0.45   # outline
        d = Shade-Color $base 0.72   # shadow / seam
        m = $base                    # fill
        h = Shade-Color $base 1.18   # highlight
        s = Shade-Color $base 0.55   # stick
    }

    # ---- item icon: an arrow board on a stick -------------------------------------------------
    $c = New-Canvas 16 16 '.'
    for ($x = 1; $x -le 12; $x++) { for ($y = 3; $y -le 8; $y++) { Set-Px $c $x $y 'm' } }
    for ($x = 1; $x -le 12; $x++) { Set-Px $c $x 2 'o'; Set-Px $c $x 9 'o'; Set-Px $c $x 3 'h'; Set-Px $c $x 8 'd' }
    for ($y = 2; $y -le 9; $y++) { Set-Px $c 0 $y 'o' }
    for ($y = 3; $y -le 8; $y++) { Set-Px $c 1 $y 'h' }
    # arrow tip
    for ($i = 0; $i -lt 4; $i++) {
        for ($y = 3 + $i; $y -le 8 - $i; $y++) { Set-Px $c (13 + $i) $y 'm' }
        Set-Px $c (13 + $i) (2 + $i) 'o'; Set-Px $c (13 + $i) (9 - $i) 'o'
    }
    Set-Px $c 16 5 'o'
    for ($x = 2; $x -le 13; $x++) { if ($x % 5 -ne 1) { Set-Px $c $x 5 'd' } }  # plank seam
    Set-Px $c 7 4 'o'; Set-Px $c 7 7 'o'                                          # nails on the post
    # stick below the board
    for ($y = 10; $y -le 15; $y++) { Set-Px $c 6 $y 'o'; Set-Px $c 7 $y 's'; Set-Px $c 8 $y 'o' }
    Save (To-Rows $c) $pal "items/sign_post_$wood.png"

    # ---- arm texture (32x16, see models/entity/sign_post.geo.json for the UV layout) -----------
    $e = New-Canvas 32 16 'm'
    foreach ($top in 0, 6) {
        for ($x = 0; $x -lt 16; $x++) { Set-Px $e $x $top 'h'; Set-Px $e $x ($top + 5) 'd' }
        for ($y = $top; $y -le $top + 5; $y++) { Set-Px $e 0 $y 'd'; Set-Px $e 15 $y 'd' }
        for ($x = 2; $x -lt 14; $x++) { if ($x % 5 -ne 0) { Set-Px $e $x ($top + 3) 'd' } }  # grain line
        Set-Px $e 2 ($top + 2) 'o'; Set-Px $e 13 ($top + 2) 'o'                            # nails
        Set-Px $e 16 ($top + 1) 'd'; Set-Px $e 16 ($top + 4) 'd'
    }
    for ($y = 0; $y -lt 6; $y++) { Set-Px $e 18 $y 'd' }
    for ($x = 0; $x -lt 18; $x++) { Set-Px $e $x 12 'h'; Set-Px $e $x 13 'd' }
    Save (To-Rows $e) $pal "entity/sign_post/$wood.png"
}

# ---- notice board ----------------------------------------------------------------------------
$boardPal = @{
    f = '#5c3d1f'   # frame
    g = '#7a5530'   # frame highlight
    c = '#b07a45'   # cork / back board
    k = '#9a6838'   # cork speckle
    p = '#efe9d6'   # paper
    l = '#8f8a7a'   # paper lines
    r = '#b0302a'   # pin
    s = '#a07a4a'   # planks (sides)
    t = '#7f5f38'   # plank seams
}
function Board-Front([bool]$book) {
    $c = New-Canvas 16 16 'c'
    for ($i = 0; $i -lt 16; $i++) { Set-Px $c $i 0 'f'; Set-Px $c $i 15 'f'; Set-Px $c 0 $i 'f'; Set-Px $c 15 $i 'f' }
    for ($i = 1; $i -lt 15; $i++) { Set-Px $c $i 1 'g'; Set-Px $c 1 $i 'g' }
    foreach ($pt in @(@(4, 4), @(10, 3), @(6, 9), @(12, 11), @(3, 12), @(9, 7), @(13, 6), @(5, 6))) { Set-Px $c $pt[0] $pt[1] 'k' }
    if ($book) {
        for ($x = 3; $x -le 12; $x++) { for ($y = 3; $y -le 13; $y++) { Set-Px $c $x $y 'p' } }
        for ($y = 5; $y -le 12; $y += 2) { for ($x = 4; $x -le 11; $x++) { if (($x + $y) % 7 -ne 0) { Set-Px $c $x $y 'l' } } }
        Set-Px $c 7 3 'r'; Set-Px $c 8 3 'r'
    }
    To-Rows $c
}
Save (Board-Front $false) $boardPal 'blocks/notice_board_front.png'
Save (Board-Front $true) $boardPal 'blocks/notice_board_front_book.png'
$side = New-Canvas 16 16 's'
for ($y = 3; $y -lt 16; $y += 4) { for ($x = 0; $x -lt 16; $x++) { Set-Px $side $x $y 't' } }
foreach ($pt in @(@(5, 1), @(12, 5), @(3, 9), @(9, 13))) { Set-Px $side $pt[0] $pt[1] 't' }
Save (To-Rows $side) $boardPal 'blocks/notice_board_side.png'
$top = New-Canvas 16 16 's'
for ($x = 3; $x -lt 16; $x += 4) { for ($y = 0; $y -lt 16; $y++) { Set-Px $top $x $y 't' } }
Save (To-Rows $top) $boardPal 'blocks/notice_board_top.png'

# ---- doormat ---------------------------------------------------------------------------------
$matPal = @{ a = '#a4864b'; b = '#8c6f3a'; e = '#6e5228'; h = '#bea062' }
$m = New-Canvas 16 16 'a'
for ($y = 0; $y -lt 16; $y++) {
    for ($x = 0; $x -lt 16; $x++) {
        if (($x + 2 * $y) % 4 -eq 0) { Set-Px $m $x $y 'b' }
        elseif (($x * 3 + $y) % 7 -eq 0) { Set-Px $m $x $y 'h' }
    }
}
for ($i = 0; $i -lt 16; $i++) { Set-Px $m $i 0 'e'; Set-Px $m $i 15 'e'; Set-Px $m 0 $i 'e'; Set-Px $m 15 $i 'e' }
for ($i = 2; $i -lt 14; $i++) { Set-Px $m $i 2 'e'; Set-Px $m $i 13 'e'; Set-Px $m 2 $i 'e'; Set-Px $m 13 $i 'e' }
Save (To-Rows $m) $matPal 'blocks/doormat.png'
Write-Host "sign textures written"
