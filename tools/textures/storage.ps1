# Textures of the storage unit (U2): sack, presents (17 colours), quiver and lunch basket.
# Original pixel art, drawn procedurally on character grids (see lib.ps1).
#
#   pwsh tools/textures/storage.ps1            # writes assets/pack/rp/textures/...
#   pwsh tools/textures/storage.ps1 -Preview   # also writes 8x previews to tools/textures/preview/
param([switch]$Preview)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lib.ps1"
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$blocks = Join-Path $root 'assets/pack/rp/textures/blocks'
$items = Join-Path $root 'assets/pack/rp/textures/items'

# ---- grid helpers ---------------------------------------------------------------------------------
function New-Grid([int]$w, [int]$h) {
    $g = New-Object 'char[][]' $h
    for ($y = 0; $y -lt $h; $y++) { $g[$y] = ('.' * $w).ToCharArray() }
    return , $g
}
function Set-Px($g, [int]$x, [int]$y, [char]$c) {
    if ($y -ge 0 -and $y -lt $g.Count -and $x -ge 0 -and $x -lt $g[$y].Length) { $g[$y][$x] = $c }
}
# Fills a rectangle; $f is a scriptblock (x, y) -> char with x/y relative to the rectangle.
function Fill-Rect($g, [int]$x0, [int]$y0, [int]$w, [int]$h, [scriptblock]$f) {
    for ($y = 0; $y -lt $h; $y++) { for ($x = 0; $x -lt $w; $x++) { Set-Px $g ($x0 + $x) ($y0 + $y) ([char](& $f $x $y)) } }
}
function Get-Rows($g) { return , ([string[]]($g | ForEach-Object { -join $_ })) }
function Save-Grid($g, [hashtable]$pal, [string]$path, [string]$name) {
    $rows = Get-Rows $g
    Save-PixelArt -Rows $rows -Palette $pal -Path $path
    if ($Preview) { Save-PixelArt -Rows $rows -Palette $pal -Path (Join-Path $root "tools/textures/preview/$name.png") -Scale 8 }
}

# ---- sack (32x32, uv layout of models/blocks/sack.geo.json) ----------------------------------------
$burlap = '#b08854'
$sackPal = @{
    a = $burlap; b = (Shade-Color $burlap 1.15); c = (Shade-Color $burlap 0.82); d = (Shade-Color $burlap 0.65)
    r = '#d9c89a'; s = '#a9946a'; i = '#3a2a17'; j = '#4c3820'
}
$weave = { param($x, $y) if (($x + $y) % 4 -eq 0) { 'b' } elseif (($x * 3 + $y) % 7 -eq 0) { 'c' } else { 'a' } }
$g = New-Grid 32 32
# body side 10x9: weave, darker edges and a bulging bottom
Fill-Rect $g 0 0 10 9 { param($x, $y)
    if ($y -eq 8) { 'd' } elseif ($x -eq 0 -or $x -eq 9) { 'c' } elseif ($y -eq 0) { 'b' } else { & $weave $x $y } }
# body top 10x10: gathered towards the neck
Fill-Rect $g 10 0 10 10 { param($x, $y)
    $dx = [Math]::Abs($x - 4.5); $dy = [Math]::Abs($y - 4.5)
    if ($dx -lt 3 -and $dy -lt 3) { 'c' } elseif ((($x + $y) % 3) -eq 0) { 'b' } else { 'a' } }
# body bottom 10x10
Fill-Rect $g 20 0 10 10 { param($x, $y) if ((($x + $y) % 2) -eq 0) { 'c' } else { 'd' } }
# neck side 6x2 / top 6x6: rope
Fill-Rect $g 0 10 6 2 { param($x, $y) if ((($x + $y) % 2) -eq 0) { 'r' } else { 's' } }
Fill-Rect $g 6 10 6 6 { param($x, $y) if ((($x + $y) % 3) -eq 0) { 's' } else { 'r' } }
# open rim top 8x8: burlap rim around the dark inside
Fill-Rect $g 16 10 8 8 { param($x, $y)
    if ($x -eq 0 -or $y -eq 0 -or $x -eq 7 -or $y -eq 7) { 'a' } elseif ((($x * $y) % 5) -eq 1) { 'j' } else { 'i' } }
# tuft side 8x2 / top 8x8
Fill-Rect $g 0 16 8 2 { param($x, $y) if ($y -eq 0) { 'b' } else { & $weave $x $y } }
Fill-Rect $g 8 16 8 8 { param($x, $y)
    $dx = [Math]::Abs($x - 3.5); $dy = [Math]::Abs($y - 3.5)
    if ($dx -lt 1 -and $dy -lt 1) { 'd' } elseif ($dx -lt 2 -and $dy -lt 2) { 'c' } else { & $weave $x $y } }
Save-Grid $g $sackPal (Join-Path $blocks 'sack.png') 'sack'

# ---- presents (32x32, uv layout of models/blocks/present.geo.json) --------------------------------
$dyes = [ordered]@{
    '' = '#c9a46a'
    white = '#e9ecec'; orange = '#f07613'; magenta = '#bd44b3'; light_blue = '#3aafd9'; yellow = '#f8c527'
    lime = '#70b919'; pink = '#ed8dac'; gray = '#3e4447'; light_gray = '#8e8e86'; cyan = '#158991'
    purple = '#792aac'; blue = '#35399d'; brown = '#724728'; green = '#546d1b'; red = '#a12722'; black = '#1d1c21'
}
foreach ($color in $dyes.Keys) {
    $paper = $dyes[$color]
    $ribbon = switch ($color) {
        '' { '#c22a2a' }
        { $_ -in 'white', 'light_gray', 'yellow', 'lime', 'pink', 'orange' } { '#c22a2a' }
        default { '#f2e7c4' }
    }
    $pal = @{
        p = $paper; q = (Shade-Color $paper 0.8); h = (Shade-Color $paper 1.12)
        w = $ribbon; v = (Shade-Color $ribbon 0.78); i = '#2d241a'; k = (Shade-Color $paper 0.55)
    }
    $g = New-Grid 32 32
    # body side 10x9: paper, vertical ribbon in the middle, shade at the bottom
    Fill-Rect $g 0 0 10 9 { param($x, $y)
        if ($x -eq 4 -or $x -eq 5) { if ($y -eq 8) { 'v' } else { 'w' } }
        elseif ($y -eq 8 -or $x -eq 0) { 'q' } elseif ($y -eq 0) { 'h' } else { 'p' } }
    # open box seen from above 10x10: paper rim, dark inside
    Fill-Rect $g 10 0 10 10 { param($x, $y)
        if ($x -eq 0 -or $y -eq 0 -or $x -eq 9 -or $y -eq 9) { 'p' } elseif ($x -eq 1 -or $y -eq 1) { 'k' } else { 'i' } }
    # bottom 10x10
    Fill-Rect $g 20 0 10 10 { param($x, $y) 'q' }
    # lid side 12x3
    Fill-Rect $g 0 10 12 3 { param($x, $y)
        if ($x -eq 5 -or $x -eq 6) { 'w' } elseif ($y -eq 2) { 'q' } elseif ($y -eq 0) { 'h' } else { 'p' } }
    # lid top 12x12: crossed ribbon
    Fill-Rect $g 0 13 12 12 { param($x, $y)
        $rx = ($x -eq 5 -or $x -eq 6); $ry = ($y -eq 5 -or $y -eq 6)
        if ($rx -and $ry) { 'v' } elseif ($rx -or $ry) { 'w' } elseif ($x -eq 0 -or $y -eq 11) { 'q' } elseif ($x -eq 11 -or $y -eq 0) { 'h' } else { 'p' } }
    # bow 6x3 side / 6x6 top
    Fill-Rect $g 24 16 6 3 { param($x, $y) if ($y -eq 2 -or $x -eq 0 -or $x -eq 5) { 'v' } else { 'w' } }
    Fill-Rect $g 24 20 6 6 { param($x, $y) if ((($x + $y) % 4) -eq 0) { 'v' } else { 'w' } }
    $file = if ($color) { "present_$color" } else { 'present' }
    Save-Grid $g $pal (Join-Path $blocks "$file.png") $file
}

# ---- quiver (16x16 item) ---------------------------------------------------------------------------
$quiverRows = @(
    '..........f.f...',
    '.........fsf.f..',
    '........fsfsf...',
    '.......sfsf.....',
    '......omms......',
    '.....ommmmo.....',
    '....ommmmno.....',
    '...ommmmnno.....',
    '..ommmmnno......',
    '..ombbmno.......',
    '.ommbbnno.......',
    '.ommmnno........',
    'ommmnno.........',
    'odmnno..........',
    '.oddo...........',
    '..oo............'
)
$quiverPal = @{ o = '#3b2412'; m = '#8b5a2b'; n = '#a8713a'; d = '#5e3a1a'; b = '#c9a33f'; f = '#e8e8e8'; s = '#7a6a55' }
Save-PixelArt -Rows $quiverRows -Palette $quiverPal -Path (Join-Path $items 'quiver.png')
if ($Preview) { Save-PixelArt -Rows $quiverRows -Palette $quiverPal -Path (Join-Path $root 'tools/textures/preview/quiver.png') -Scale 8 }

# ---- lunch basket (16x16 item) ---------------------------------------------------------------------
$basketRows = @(
    '................',
    '.....hhhhhh.....',
    '....h......h....',
    '...h........h...',
    '...h........h...',
    '.oooooooooooooo.',
    '.ozzzzzzzzzzzzo.',
    '.orzrzrzrzrzrzo.',
    '.oxyxyxyxyxyxyo.',
    '.oyxyxyxyxyxyxo.',
    '.oxyxyxyxyxyxyo.',
    '.oyxyxyxyxyxyxo.',
    '.oxyxyxyxyxyxyo.',
    '..oyxyxyxyxyxo..',
    '...oooooooooo...',
    '................'
)
$basketPal = @{ o = '#4a3016'; h = '#7d5a2e'; x = '#c79a55'; y = '#a87b3c'; z = '#d9d2c3'; r = '#b8463f' }
Save-PixelArt -Rows $basketRows -Palette $basketPal -Path (Join-Path $items 'lunch_basket.png')
if ($Preview) { Save-PixelArt -Rows $basketRows -Palette $basketPal -Path (Join-Path $root 'tools/textures/preview/lunch_basket.png') -Scale 8 }

Write-Host 'storage textures written'
