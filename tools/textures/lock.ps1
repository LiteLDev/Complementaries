# Textures of the `lock` unit (keys, lock block, safe, metal gates, gold / netherite doors).
# Original pixel art, 16x16. Run: pwsh tools/textures/lock.ps1 [-Preview]
param([switch]$Preview)
. "$PSScriptRoot/lib.ps1"

$root   = (Resolve-Path "$PSScriptRoot/../..").Path
$blocks = Join-Path $root 'assets/pack/rp/textures/blocks'
$items  = Join-Path $root 'assets/pack/rp/textures/items'
$prev   = Join-Path $PSScriptRoot 'preview'

function New-Grid([char]$fill = '.') { return , (0..15 | ForEach-Object { , ([char[]]($fill.ToString() * 16)) }) }
function Grid-Rows($grid) { return , ($grid | ForEach-Object { -join $_ }) }
function Set-Px($grid, [int]$x, [int]$y, [char]$c) { if ($x -ge 0 -and $x -lt 16 -and $y -ge 0 -and $y -lt 16) { $grid[$y][$x] = $c } }
function Fill-Rect($grid, [int]$x0, [int]$y0, [int]$x1, [int]$y1, [char]$c) {
    for ($y = $y0; $y -le $y1; $y++) { for ($x = $x0; $x -le $x1; $x++) { Set-Px $grid $x $y $c } }
}
function Frame-Rect($grid, [int]$x0, [int]$y0, [int]$x1, [int]$y1, [char]$light, [char]$dark) {
    for ($x = $x0; $x -le $x1; $x++) { Set-Px $grid $x $y0 $light; Set-Px $grid $x $y1 $dark }
    for ($y = $y0; $y -le $y1; $y++) { Set-Px $grid $x0 $y $light; Set-Px $grid $x1 $y $dark }
}
function With($pal, $over) { $h = $pal.Clone(); foreach ($k in $over.Keys) { $h[$k] = $over[$k] }; return $h }
function Save($rows, $pal, $dir, $name) {
    Save-PixelArt -Rows $rows -Palette $pal -Path (Join-Path $dir "$name.png")
    if ($Preview) { Save-PixelArt -Rows $rows -Palette $pal -Path (Join-Path $prev "$name.png") -Scale 16 }
}

# Metal palettes: h highlight, l light, m mid, d dark, k darkest, a accent
$gold = @{ h = '#fffbd0'; l = '#fbe36a'; m = '#eab52a'; d = '#b9821a'; k = '#7d5410'; a = '#fff3a0' }
$iron = @{ h = '#ffffff'; l = '#dedede'; m = '#bdbdbd'; d = '#8b8b8b'; k = '#575757'; a = '#eeeeee' }
$neth = @{ h = '#7d7476'; l = '#5e5759'; m = '#4a4446'; d = '#36302f'; k = '#211d1e'; a = '#8f7f72' }

# ---- door halves ---------------------------------------------------------------------------------
function Door-Half([bool]$top, [bool]$window) {
    $g = New-Grid 'm'
    Frame-Rect $g 0 0 15 15 'l' 'k'
    # vertical seams
    foreach ($x in 5, 10) { for ($y = 1; $y -le 14; $y++) { Set-Px $g $x $y 'd'; Set-Px $g ($x + 1) $y 'l' } }
    if ($top) {
        if ($window) { Fill-Rect $g 2 2 4 6 '.'; Fill-Rect $g 7 2 9 6 '.'; Fill-Rect $g 12 2 13 6 '.' }
        for ($x = 1; $x -le 14; $x++) { Set-Px $g $x 8 'd'; Set-Px $g $x 9 'h' }
        Set-Px $g 2 12 'a'; Set-Px $g 13 12 'a'
    } else {
        for ($x = 1; $x -le 14; $x++) { Set-Px $g $x 6 'd'; Set-Px $g $x 7 'h' }
        Set-Px $g 2 3 'a'; Set-Px $g 13 3 'a'; Set-Px $g 2 12 'a'; Set-Px $g 13 12 'a'
        # handle
        Fill-Rect $g 12 1 13 1 'k'; Set-Px $g 13 2 'k'
    }
    return Grid-Rows $g
}
Save (Door-Half $false $false) $gold $blocks 'gold_door_lower'
Save (Door-Half $true $true) $gold $blocks 'gold_door_upper'
Save (Door-Half $false $false) $neth $blocks 'netherite_door_lower'
Save (Door-Half $true $false) $neth $blocks 'netherite_door_upper'

# ---- trapdoors -------------------------------------------------------------------------------------
function Trapdoor([bool]$holes) {
    $g = New-Grid 'm'
    Frame-Rect $g 0 0 15 15 'l' 'k'
    Frame-Rect $g 3 3 12 12 'd' 'h'
    if ($holes) { foreach ($p in @(@(5, 5), @(9, 5), @(5, 9), @(9, 9))) { Fill-Rect $g $p[0] $p[1] ($p[0] + 1) ($p[1] + 1) '.' } }
    else { Fill-Rect $g 6 6 9 9 'd'; Fill-Rect $g 7 7 8 8 'a' }
    Set-Px $g 1 1 'a'; Set-Px $g 14 1 'a'; Set-Px $g 1 14 'a'; Set-Px $g 14 14 'a'
    return Grid-Rows $g
}
Save (Trapdoor $true) $gold $blocks 'gold_trapdoor'
Save (Trapdoor $false) $neth $blocks 'netherite_trapdoor'

# ---- gates (bars + frame, used on every face of the gate geometry) ---------------------------------
function Gate-Texture {
    $g = New-Grid '.'
    foreach ($x in 1, 4, 7, 10, 13) { Fill-Rect $g $x 0 ($x + 1) 15 'm'; for ($y = 0; $y -le 15; $y++) { Set-Px $g ($x + 1) $y 'd' } }
    for ($x = 0; $x -le 15; $x++) { Set-Px $g $x 1 'l'; Set-Px $g $x 2 'd'; Set-Px $g $x 13 'l'; Set-Px $g $x 14 'd' }
    foreach ($x in 1, 4, 7, 10, 13) { Set-Px $g $x 1 'h'; Set-Px $g $x 13 'h' }
    return Grid-Rows $g
}
Save (Gate-Texture) $iron $blocks 'iron_gate'
Save (Gate-Texture) $gold $blocks 'gold_gate'

# ---- lock block ------------------------------------------------------------------------------------
$lock = New-Grid 'm'
Frame-Rect $lock 0 0 15 15 'l' 'k'
Frame-Rect $lock 2 2 13 13 'd' 'h'
Fill-Rect $lock 3 3 12 12 'l'
Fill-Rect $lock 6 5 9 7 'k'; Fill-Rect $lock 7 8 8 10 'k'
Set-Px $lock 6 8 'd'; Set-Px $lock 9 8 'd'
Set-Px $lock 1 1 'a'; Set-Px $lock 14 1 'a'; Set-Px $lock 1 14 'a'; Set-Px $lock 14 14 'a'
Save (Grid-Rows $lock) (With $iron @{ k = '#2b2b2b' }) $blocks 'lock_block'

# ---- safe (netherite box with a steel door and dial) ----------------------------------------------
$safePal = @{ h = '#8a8285'; l = '#6a6366'; m = '#524b4e'; d = '#3b3537'; k = '#1f1b1c'; a = '#c9c2b8'; g = '#e0b33c'; r = '#a3271f' }
$side = New-Grid 'm'
Frame-Rect $side 0 0 15 15 'l' 'k'
for ($y = 3; $y -le 12; $y += 3) { for ($x = 2; $x -le 13; $x++) { Set-Px $side $x $y 'd' } }
Set-Px $side 1 1 'h'; Set-Px $side 14 1 'h'; Set-Px $side 1 14 'd'; Set-Px $side 14 14 'd'
Save (Grid-Rows $side) $safePal $blocks 'safe_side'

$topt = New-Grid 'm'
Frame-Rect $topt 0 0 15 15 'l' 'k'
Frame-Rect $topt 2 2 13 13 'd' 'h'
Save (Grid-Rows $topt) $safePal $blocks 'safe_top'

$front = New-Grid 'm'
Frame-Rect $front 0 0 15 15 'l' 'k'
Frame-Rect $front 1 1 14 14 'k' 'h'
Fill-Rect $front 2 2 13 13 'l'
Frame-Rect $front 5 4 10 9 'd' 'h'
Fill-Rect $front 6 5 9 8 'a'; Set-Px $front 7 5 'r'; Set-Px $front 8 6 'k'; Set-Px $front 7 7 'k'
Fill-Rect $front 11 6 12 7 'g'; Fill-Rect $front 11 8 12 8 'd'
Fill-Rect $front 3 11 4 11 'd'; Fill-Rect $front 11 11 12 11 'd'
Save (Grid-Rows $front) $safePal $blocks 'safe_front'

$open = New-Grid 'm'
Frame-Rect $open 0 0 15 15 'l' 'k'
Fill-Rect $open 1 1 14 14 'k'
Fill-Rect $open 2 7 13 7 'd'; Fill-Rect $open 2 8 13 8 'm'
Fill-Rect $open 13 1 14 14 'l'; Fill-Rect $open 13 6 14 7 'g'
Save (Grid-Rows $open) $safePal $blocks 'safe_front_open'

# ---- key item --------------------------------------------------------------------------------------
$keyRows = @(
    '................',
    '..........kkk...',
    '.........klllk..',
    '........klhhmlk.',
    '........klh..mk.',
    '........klm..mk.',
    '.........kmmmdk.',
    '........kmkddk..',
    '.......kmk.kk...',
    '......kmk.......',
    '.....kmk........',
    '....kmkk........',
    '...kmkmk........',
    '..kmk.kk........',
    '..kdk...........',
    '...k............'
)
Save $keyRows (With $gold @{ k = '#5a3c0a' }) $items 'key'

# ---- item icons for 2-high doors and gates ---------------------------------------------------------
function Door-Item($pal, [bool]$window) {
    $g = New-Grid '.'
    Fill-Rect $g 4 0 11 15 'm'
    Frame-Rect $g 4 0 11 15 'l' 'k'
    for ($y = 1; $y -le 14; $y++) { Set-Px $g 7 $y 'd'; Set-Px $g 8 $y 'l' }
    for ($x = 5; $x -le 10; $x++) { Set-Px $g $x 7 'd'; Set-Px $g $x 8 'h' }
    if ($window) { Fill-Rect $g 5 2 6 4 '.'; Fill-Rect $g 9 2 10 4 '.' }
    Set-Px $g 10 10 'k'; Set-Px $g 10 11 'k'
    return Grid-Rows $g
}
Save (Door-Item $gold $true) $gold $items 'gold_door'
Save (Door-Item $neth $false) $neth $items 'netherite_door'

function Gate-Item {
    $g = New-Grid '.'
    Fill-Rect $g 0 3 1 14 'd'; Fill-Rect $g 14 3 15 14 'd'
    foreach ($x in 3, 6, 9, 12) { Fill-Rect $g $x 4 $x 13 'm' }
    for ($x = 2; $x -le 13; $x++) { Set-Px $g $x 5 'l'; Set-Px $g $x 11 'l' }
    Set-Px $g 0 3 'h'; Set-Px $g 14 3 'h'
    return Grid-Rows $g
}
Save (Gate-Item) $iron $items 'iron_gate'
Save (Gate-Item) $gold $items 'gold_gate'

Write-Host "lock textures written to $blocks and $items"
