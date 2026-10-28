# Draws the U15 `cannon` textures (original pixel art): cannon block + barrel, cannonball, bombs,
# the red merchant and the particles.
#
#   pwsh -NoProfile -File tools/textures/cannon.ps1            # writes assets/pack/rp/textures/...
#   pwsh -NoProfile -File tools/textures/cannon.ps1 -Preview   # also writes x8 previews to tools/textures/preview/
param([switch]$Preview)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lib.ps1"

$root = (Resolve-Path "$PSScriptRoot/../..").Path
$rp = Join-Path $root 'assets/pack/rp/textures'

function Save-Tex([string[]]$Rows, [hashtable]$Pal, [string]$Rel) {
    Save-PixelArt -Rows $Rows -Palette $Pal -Path (Join-Path $rp $Rel)
    if ($Preview) {
        $name = ($Rel -replace '[\\/]', '_') -replace '\.png$', ''
        Save-PixelArt -Rows $Rows -Palette $Pal -Path (Join-Path $PSScriptRoot "preview/$name.x8.png") -Scale 8
    }
}

# ---- a character canvas for box-UV textures -----------------------------------------------------
function New-Canvas([int]$W, [int]$H) {
    $g = New-Object 'char[][]' $H
    for ($y = 0; $y -lt $H; $y++) { $g[$y] = ('.' * $W).ToCharArray() }
    return , $g
}
function Fill([char[][]]$G, [int]$X, [int]$Y, [int]$W, [int]$H, [string]$Keys, [int]$Seed = 1) {
    # Fills a rectangle with the palette keys of $Keys, picked by a small deterministic hash so the
    # surface gets a hand-made speckle (first key is the most common).
    for ($j = 0; $j -lt $H; $j++) {
        for ($i = 0; $i -lt $W; $i++) {
            $hh = (($X + $i) * 73856093 -bxor ($Y + $j) * 19349663 -bxor $Seed * 83492791) -band 0x7fffffff
            $r = $hh % 10
            $k = if ($Keys.Length -eq 1) { 0 } elseif ($r -lt 6) { 0 } elseif ($r -lt 8 -or $Keys.Length -lt 3) { 1 } else { 2 }
            $G[$Y + $j][$X + $i] = $Keys[$k]
        }
    }
}
function Border([char[][]]$G, [int]$X, [int]$Y, [int]$W, [int]$H, [char]$Key) {
    for ($i = 0; $i -lt $W; $i++) { $G[$Y][$X + $i] = $Key; $G[$Y + $H - 1][$X + $i] = $Key }
    for ($j = 0; $j -lt $H; $j++) { $G[$Y + $j][$X] = $Key; $G[$Y + $j][$X + $W - 1] = $Key }
}
# Box UV of a cube (w, h, d) at (u, v): top, bottom, then the four sides.
function Box([char[][]]$G, [int]$U, [int]$V, [int]$W, [int]$H, [int]$D, [string]$Top, [string]$Side, [string]$Bottom = $Top, [int]$Seed = 1) {
    Fill $G ($U + $D) $V $W $D $Top $Seed
    Fill $G ($U + $D + $W) $V $W $D $Bottom ($Seed + 1)
    Fill $G $U ($V + $D) (2 * ($W + $D)) $H $Side ($Seed + 2)
}
function Rows([char[][]]$G) { return , @($G | ForEach-Object { -join $_ }) }

# ---- cannon block (base plate + turntable), 64x32 --------------------------------------------
$iron = @{ a = '#4a4c54'; b = '#3d3f46'; c = '#5a5d66'; d = '#2b2c31'; w = '#6b4a2b'; x = '#57391f'; y = '#7d5733'; r = '#8f9099' }
$g = New-Canvas 64 32
Box $g 0 0 14 2 14 'wxy' 'xw' 'xw' 3              # oak plank base plate
Border $g 14 0 14 14 'x'
Box $g 0 16 8 1 8 'abc' 'ab' 'b' 5                # iron turntable
Border $g 8 16 8 8 'd'
$g[19][11] = 'r'; $g[20][12] = 'r'                # pivot bolt
Save-Tex (Rows $g) $iron 'blocks/cannon.png'

# ---- cannon barrel (entity), 64x32 -----------------------------------------------------------
$barrel = @{ w = '#6b4a2b'; x = '#57391f'; y = '#7d5733'; a = '#3b3d44'; b = '#33353b'; c = '#4b4e57'; d = '#202125'; e = '#121214'; r = '#6e717b'; g = '#a08a3a'; h = '#7b6a2c' }
$g = New-Canvas 64 32
Box $g 0 0 6 6 16 'abc' 'abc' 'b' 7               # tube
for ($x = 0; $x -lt 44; $x++) { $g[16][$x] = 'c'; $g[21][$x] = 'b' }
foreach ($x in 3, 4, 26, 27) { for ($y = 16; $y -lt 22; $y++) { $g[$y][$x] = 'r' } } # reinforcing bands
Box $g 44 0 8 8 2 'rc' 'rc' 'r' 9                 # muzzle ring
Fill $g 46 2 8 8 'rc' 11
Fill $g 48 4 4 4 'e' 1                             # bore (front face)
Box $g 44 10 4 4 2 'ab' 'ab' 'a' 13               # cascabel knob
Box $g 0 22 10 2 2 'gh' 'gh' 'h' 15               # brass trunnion axle
Box $g 44 16 2 8 6 'wx' 'wxy' 'x' 19              # wooden cheeks of the carriage
for ($y = 22; $y -lt 30; $y++) { $g[$y][50] = 'x'; $g[$y][58] = 'x' }
Save-Tex (Rows $g) $barrel 'entity/cannon_barrel.png'

# ---- cannonball (entity 6x6x6 cube) 32x16 and item 16x16 -------------------------------------
$ball = @{ a = '#2c2d31'; b = '#232428'; c = '#3a3c42'; h = '#5c5f69'; o = '#151518' }
$g = New-Canvas 32 16
Box $g 0 0 6 6 6 'abc' 'abc' 'b' 17
$g[7][7] = 'h'; $g[7][8] = 'h'; $g[8][7] = 'h'
Save-Tex (Rows $g) $ball 'entity/cannonball.png'
$g = New-Canvas 64 32                              # placed cannonball block: three crossed boxes
Box $g 0 0 6 8 6 'abc' 'abc' 'b' 41
Box $g 24 0 8 6 6 'abc' 'abc' 'b' 43
Box $g 0 14 6 6 8 'abc' 'abc' 'b' 45
$g[1][7] = 'h'; $g[1][8] = 'h'; $g[2][7] = 'h'
Save-Tex (Rows $g) $ball 'blocks/cannonball.png'

$ballItem = @(
    '................',
    '................',
    '................',
    '......oooo......',
    '....ooccaaoo....',
    '...ochhcaaabo...',
    '...ochcaaaabo...',
    '..occcaaaaabbo..',
    '..ocaaaaaaabbo..',
    '..oaaaaaaaabbo..',
    '...oaaaaaabbo...',
    '...obaaaabbbo...',
    '....oobbbboo....',
    '......oooo......',
    '................',
    '................'
)
Save-Tex $ballItem $ball 'items/cannonball.png'

# ---- bombs (item sprites, also used by the flying entities) ----------------------------------
$bombRows = @(
    '...........s....',
    '..........s.s...',
    '.........f......',
    '........f.......',
    '......ttft......',
    '......tqqt......',
    '....oooooooo....',
    '...occaaaaabo...',
    '..ochhcaaaaabo..',
    '..ochcaaaaaabo..',
    '..occaaaaaaabo..',
    '..oaaaaaaaaabo..',
    '..oaaaaaaaabbo..',
    '...oaaaaaabbo...',
    '....oobbbboo....',
    '......oooo......'
)
$bombPal = @{ o = '#101114'; a = '#2b2e3a'; b = '#1d1f27'; c = '#3d4152'; h = '#6d7388'; t = '#6b5a3a'; q = '#8a7650'; f = '#c9b48a'; s = '#ffcc33' }
Save-Tex $bombRows $bombPal 'items/bomb.png'
$bluePal = $bombPal.Clone(); $bluePal.a = '#1f3f8c'; $bluePal.b = '#162d66'; $bluePal.c = '#2f58b8'; $bluePal.h = '#7fa6ff'; $bluePal.s = '#66ccff'
Save-Tex $bombRows $bluePal 'items/bomb_blue.png'
$spiky = Merge-Rows $bombRows @(
    '................',
    '................',
    '................',
    '................',
    '................',
    '................',
    '..k....k...k....',
    '.k............k.',
    '................',
    'k..............k',
    '................',
    'k..............k',
    '................',
    '.k............k.',
    '..k...k...k..k..',
    '................'
)
$spikyPal = $bombPal.Clone(); $spikyPal.k = '#b8bcc8'
Save-Tex $spiky $spikyPal 'items/bomb_spiky.png'

# ---- cannon item icon --------------------------------------------------------------------------
$cannonItem = @(
    '................',
    '................',
    '.............oo.',
    '...........ooeeo',
    '.........ooaaeeo',
    '.......ooaacaoo.',
    '.....ooaacaao...',
    '...ooaacaao.....',
    '..obaacaagg.....',
    '..obbaaoggh.....',
    '...oob.ogh......',
    '..wwwwwwwwwwww..',
    '..xywwwyywwwyx..',
    '..xxxxxxxxxxxx..',
    '...r........r...',
    '................'
)
$cannonPal = @{ o = '#17181b'; a = '#3b3d44'; b = '#2b2c31'; c = '#5a5d66'; e = '#0c0c0e'; g = '#a08a3a'; h = '#7b6a2c'; w = '#7d5733'; x = '#57391f'; y = '#6b4a2b'; r = '#3d3f46' }
Save-Tex $cannonItem $cannonPal 'items/cannon.png'

# ---- red merchant (villager_v2 box UV), 64x64 ------------------------------------------------
$m = @{ s = '#b98563'; t = '#a3714f'; u = '#8c5f41'; e = '#2a1c14'; w = '#e8e0d0'; r = '#a3201c'; q = '#861914'; p = '#c23a2a'; k = '#2c1a12'; g = '#d6a13a'; h = '#5a1010'; z = '#3a2a1f'; y = '#4a3526' }
$g = New-Canvas 64 64
Box $g 0 0 8 10 8 'rq' 'stu' 'rq' 21              # head: hood on top, skin on the sides
Fill $g 0 8 32 3 'rqp' 22                          # hood rim around the head
Fill $g 8 11 8 2 'k' 1                             # beard / shadow under the hood (front face)
$g[13][9] = 'w'; $g[13][10] = 'e'; $g[13][13] = 'e'; $g[13][14] = 'w' # eyes
Fill $g 9 15 6 3 'yz' 23                           # beard
Box $g 24 0 2 4 2 'tu' 'tu' 'u' 25                 # nose
Box $g 16 20 8 12 6 'rq' 'rqp' 'q' 27              # body
for ($x = 16; $x -lt 44; $x++) { $g[30][$x] = 'g' }        # belt
Box $g 0 38 8 18 6 'rq' 'rqp' 'q' 29               # robe
for ($x = 0; $x -lt 28; $x++) { $g[54][$x] = 'h'; $g[55][$x] = 'g' } # robe hem
Box $g 40 38 8 4 4 'rq' 'rqp' 'q' 31               # crossed arms (middle)
Box $g 44 22 4 8 4 'rq' 'rqp' 's' 33               # arms
Box $g 0 22 4 12 4 'k' 'yz' 'k' 35                 # legs (boots)
Save-Tex (Rows $g) $m 'entity/red_merchant.png'

# ---- particles ---------------------------------------------------------------------------------
$dot = @('..ww..', '.wwww.', 'wwwwww', 'wwwwww', '.wwww.', '..ww..')
Save-Tex $dot @{ w = '#ffffff' } 'particle/cannon_trajectory.png'
$flame = @(
    '...b....',
    '..bb....',
    '..bcb...',
    '.bccb...',
    '.bcdcb..',
    'bcdddcb.',
    'bcdddcb.',
    '.bcccb..'
)
Save-Tex $flame @{ b = '#1d4fd6'; c = '#3f8cff'; d = '#bfe6ff' } 'particle/blue_flame.png'
$spike = @('......', '..s...', '.sss..', '..sss.', '...s..', '......')
Save-Tex $spike @{ s = '#c8ccd6' } 'particle/shrapnel.png'
Write-Host 'cannon textures written'
