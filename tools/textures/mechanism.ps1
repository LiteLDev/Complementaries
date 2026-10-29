# Textures of the mechanism unit (U7): spring launcher, bellows, turn table, cog block, crank.
# Original pixel art, drawn procedurally on 16x16 grids. Run: pwsh tools/textures/mechanism.ps1
. "$PSScriptRoot/lib.ps1"

$root = Resolve-Path "$PSScriptRoot/../.."
$out = Join-Path $root 'assets/pack/rp/textures/blocks'

# ---- tiny grid helpers --------------------------------------------------------------------------
function New-Grid([char]$fill = '.') { $g = New-Object 'char[,]' 16, 16; for ($y = 0; $y -lt 16; $y++) { for ($x = 0; $x -lt 16; $x++) { $g[$y, $x] = $fill } }; return , $g }
function Set-Px($g, [int]$x, [int]$y, [char]$c) { if ($x -ge 0 -and $x -lt 16 -and $y -ge 0 -and $y -lt 16) { $g[$y, $x] = $c } }
function Fill-Rect($g, [int]$x0, [int]$y0, [int]$x1, [int]$y1, [char]$c) { for ($y = $y0; $y -le $y1; $y++) { for ($x = $x0; $x -le $x1; $x++) { Set-Px $g $x $y $c } } }
function Frame-Rect($g, [int]$x0, [int]$y0, [int]$x1, [int]$y1, [char]$c) {
    for ($x = $x0; $x -le $x1; $x++) { Set-Px $g $x $y0 $c; Set-Px $g $x $y1 $c }
    for ($y = $y0; $y -le $y1; $y++) { Set-Px $g $x0 $y $c; Set-Px $g $x1 $y $c }
}
# Deterministic speckle: replaces `from` pixels with `to` where the hash says so.
function Speckle($g, [char]$from, [char]$to, [int]$seed, [int]$percent) {
    for ($y = 0; $y -lt 16; $y++) { for ($x = 0; $x -lt 16; $x++) {
        $h = (($x * 73856093) -bxor ($y * 19349663) -bxor ($seed * 83492791)) -band 0x7fffffff
        if ($g[$y, $x] -eq $from -and ($h % 100) -lt $percent) { $g[$y, $x] = $to }
    } }
}
function To-Rows($g) { $rows = for ($y = 0; $y -lt 16; $y++) { -join (0..15 | ForEach-Object { $g[$y, $_] }) }; return , $rows }
function Save($g, $pal, $name) { Save-PixelArt -Rows (To-Rows $g) -Palette $pal -Path (Join-Path $out "$name.png") }

# ---- palettes -----------------------------------------------------------------------------------
$iron = @{ a = '#ececec'; b = '#c6c6c6'; c = '#9a9a9a'; d = '#6b6b6b'; e = '#454545' }
$stone = @{ s = '#8c8c8c'; t = '#7b7b7b'; u = '#666666'; v = '#545454' }
$wood = @{ w = '#b48a55'; x = '#9a7344'; y = '#7c5a33'; z = '#5a4024' }
$leather = @{ l = '#9a5530'; m = '#7c4124'; n = '#b4683c'; k = '#5c2f1a' }
$copper = @{ o = '#f0a27c'; p = '#d67a50'; q = '#b25a36'; r = '#7e3c22' }
$red = @{ f = '#ff4a3a'; g = '#b81410'; h = '#6e0a08' }
$green = @{ i = '#8fd16b'; j = '#5b9c3e' }
function Pal { $h = @{}; foreach ($p in $args) { foreach ($k in $p.Keys) { $h[$k] = $p[$k] } }; return $h }

# ---- spring launcher ----------------------------------------------------------------------------
# side: an iron plate band on top (4 px), a stone body with a dark frame below it
$g = New-Grid 's'
Speckle $g 's' 't' 1 30; Speckle $g 's' 'u' 2 12
Frame-Rect $g 0 4 15 15 'v'
Fill-Rect $g 0 0 15 3 'b'; Fill-Rect $g 0 0 15 0 'a'; Fill-Rect $g 0 3 15 3 'd'
foreach ($x in 2, 13) { Set-Px $g $x 1 'e'; Set-Px $g $x 2 'c' }
Fill-Rect $g 6 5 9 14 'u'; Fill-Rect $g 7 5 8 14 'c'
foreach ($y in 6, 9, 12) { Set-Px $g 6 $y 'd'; Set-Px $g 9 ($y + 1) 'd' }
Save $g (Pal $iron $stone) 'piston_launcher_side'

# top: iron plate with rivets and a slime pad in the middle
$g = New-Grid 'b'
Speckle $g 'b' 'c' 3 15
Frame-Rect $g 0 0 15 15 'd'; Frame-Rect $g 1 1 14 14 'a'
foreach ($p in @(@(2, 2), @(13, 2), @(2, 13), @(13, 13))) { Set-Px $g $p[0] $p[1] 'e' }
Fill-Rect $g 5 5 10 10 'j'; Fill-Rect $g 6 6 9 9 'i'; Frame-Rect $g 4 4 11 11 'c'
Save $g (Pal $iron $green) 'piston_launcher_top'

# bottom: stone with a frame
$g = New-Grid 's'
Speckle $g 's' 't' 4 30; Speckle $g 's' 'u' 5 12
Frame-Rect $g 0 0 15 15 'v'
Save $g $stone 'piston_launcher_bottom'

# spring: a coil, diagonal light / dark turns
$g = New-Grid 'c'
for ($y = 0; $y -lt 16; $y++) { for ($x = 0; $x -lt 16; $x++) {
    $k = ($x + 2 * $y) % 8
    if ($k -lt 2) { Set-Px $g $x $y 'a' } elseif ($k -lt 4) { Set-Px $g $x $y 'b' } elseif ($k -lt 6) { Set-Px $g $x $y 'd' } else { Set-Px $g $x $y 'e' }
} }
Save $g $iron 'piston_launcher_spring'

# ---- bellows ------------------------------------------------------------------------------------
# board: planks along x
$g = New-Grid 'w'
foreach ($y in 3, 7, 11, 15) { Fill-Rect $g 0 $y 15 $y 'y' }
Speckle $g 'w' 'x' 6 25
foreach ($p in @(@(4, 1), @(11, 5), @(2, 9), @(9, 13))) { Set-Px $g $p[0] $p[1] 'z' }
Save $g $wood 'bellows_board'

# front: board with the nozzle (iron ring with a dark hole)
$g = New-Grid 'w'
foreach ($y in 3, 7, 11, 15) { Fill-Rect $g 0 $y 15 $y 'y' }
Speckle $g 'w' 'x' 7 25
Fill-Rect $g 5 5 10 10 'c'; Frame-Rect $g 5 5 10 10 'd'; Fill-Rect $g 6 6 9 9 'e'; Fill-Rect $g 7 7 8 8 'k'
Save $g (Pal $wood $iron $leather) 'bellows_front'

# leather: folds (horizontal pleats)
$g = New-Grid 'l'
for ($y = 0; $y -lt 16; $y++) { $c = switch ($y % 4) { 0 { 'n' } 1 { 'l' } 2 { 'm' } default { 'k' } }; Fill-Rect $g 0 $y 15 $y $c }
Speckle $g 'l' 'm' 8 15
Save $g $leather 'bellows_leather'

# ---- turn table ---------------------------------------------------------------------------------
# side: stone body, wooden top rim (3 px)
$g = New-Grid 's'
Speckle $g 's' 't' 9 30; Speckle $g 's' 'u' 10 10
Frame-Rect $g 0 0 15 15 'v'
Fill-Rect $g 0 0 15 2 'w'; Fill-Rect $g 0 2 15 2 'y'; Set-Px $g 3 1 'x'; Set-Px $g 11 0 'x'
Save $g (Pal $stone $wood) 'turn_table_side'

$g = New-Grid 's'
Speckle $g 's' 't' 11 30; Speckle $g 's' 'u' 12 10
Frame-Rect $g 0 0 15 15 'v'
Save $g $stone 'turn_table_bottom'

# top: wooden disc with a curved arrow; the arrow shows the direction, lit (red) while powered
function TurnTop([bool]$ccw, [bool]$lit) {
    $g = New-Grid 'y'
    Fill-Rect $g 1 1 14 14 'w'
    Speckle $g 'w' 'x' 13 22
    # ring of the disc
    foreach ($p in @(@(1, 1), @(14, 1), @(1, 14), @(14, 14), @(2, 1), @(1, 2), @(13, 1), @(14, 2), @(1, 13), @(2, 14), @(13, 14), @(14, 13))) { Set-Px $g $p[0] $p[1] 'y' }
    Fill-Rect $g 7 7 8 8 'z'
    $a = if ($lit) { 'f' } else { 'd' }
    $b = if ($lit) { 'g' } else { 'e' }
    # an arc over three quarters of a circle of radius 4 around (7.5, 7.5)
    $arc = @(@(6, 3), @(7, 3), @(8, 3), @(9, 3), @(10, 4), @(11, 5), @(12, 6), @(12, 7), @(12, 8), @(12, 9), @(11, 10), @(10, 11), @(9, 12), @(8, 12), @(7, 12), @(6, 12), @(5, 11), @(4, 10), @(3, 9), @(3, 8))
    foreach ($p in $arc) { Set-Px $g $p[0] $p[1] $a }
    # arrow head at the free end of the arc (clockwise: the arc runs from the top over the right)
    $head = @(@(2, 6), @(3, 6), @(4, 6), @(3, 7), @(1, 7), @(5, 7))
    foreach ($p in $head) { Set-Px $g $p[0] $p[1] $b }
    if ($ccw) {
        # mirror horizontally: the arrow runs the other way
        $m = New-Grid '.'
        for ($y = 0; $y -lt 16; $y++) { for ($x = 0; $x -lt 16; $x++) { $m[$y, (15 - $x)] = $g[$y, $x] } }
        $g = $m
    }
    return , $g
}
$tp = Pal $wood $iron $red
Save (TurnTop $false $false) $tp 'turn_table_top'
Save (TurnTop $true $false) $tp 'turn_table_top_inverted'
Save (TurnTop $false $true) $tp 'turn_table_top_on'
Save (TurnTop $true $true) $tp 'turn_table_top_inverted_on'

# ---- cog block ----------------------------------------------------------------------------------
function CogFace([int]$phase, [bool]$lit) {
    $g = New-Grid 'q'
    Speckle $g 'q' 'p' 14 20
    Frame-Rect $g 0 0 15 15 'r'
    # a gear: hub + 8 teeth; phase shifts the teeth by one pixel to suggest turning
    for ($y = 0; $y -lt 16; $y++) { for ($x = 0; $x -lt 16; $x++) {
        $dx = $x - 7.5; $dy = $y - 7.5; $d = [Math]::Sqrt($dx * $dx + $dy * $dy)
        $ang = [Math]::Atan2($dy, $dx) + $phase * [Math]::PI / 16
        $tooth = ([Math]::Floor(($ang + [Math]::PI) / ([Math]::PI / 4) * 2) % 2) -eq 0
        if ($d -le 4.6) { Set-Px $g $x $y 'o' }
        elseif ($d -le 6.4 -and $tooth) { Set-Px $g $x $y 'o' }
        elseif ($d -le 6.4) { Set-Px $g $x $y 'r' }
        if ($d -le 1.6) { Set-Px $g $x $y ($(if ($lit) { 'f' } else { 'r' })) }
        elseif ($d -le 2.6) { Set-Px $g $x $y ($(if ($lit) { 'g' } else { 'q' })) }
    } }
    return , $g
}
$cp = Pal $copper $red
Save (CogFace 0 $false) $cp 'cog_block'
Save (CogFace 1 $true) $cp 'cog_block_twisting'

# ---- crank --------------------------------------------------------------------------------------
# one texture for the whole model: iron base (rows 0-7), wooden handle (rows 8-15)
$g = New-Grid 'c'
Fill-Rect $g 0 0 15 7 'c'; Speckle $g 'c' 'b' 15 25; Frame-Rect $g 0 0 15 7 'd'
Fill-Rect $g 0 8 15 15 'w'; Speckle $g 'w' 'x' 16 30; Fill-Rect $g 0 15 15 15 'y'
Save $g (Pal $iron $wood) 'crank'

Write-Host "mechanism textures written to $out"
