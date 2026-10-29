# U9 lighting textures: sconces, candle holders, lamps, wall lantern bracket, item icons, flame particles.
# Original pixel art. Run from anywhere:  pwsh tools/textures/lighting.ps1
. "$PSScriptRoot/lib.ps1"

$root   = Resolve-Path "$PSScriptRoot/../.."
$blocks = Join-Path $root 'assets/pack/rp/textures/blocks'
$items  = Join-Path $root 'assets/pack/rp/textures/items'
$parts  = Join-Path $root 'assets/pack/rp/textures/particle'

# ---------------------------------------------------------------- palettes
$iron = @{ d = '#26262b'; m = '#3d3d44'; l = '#5d5d66'; h = '#82828c' }
$flames = [ordered]@{
    sconce        = @{ o = '#d8540c'; f = '#ffa21f'; c = '#fff4a8' }
    sconce_soul   = @{ o = '#1a7f96'; f = '#4fd4e0'; c = '#d2fcff' }
    sconce_ender  = @{ o = '#6a1fa8'; f = '#bf6dff'; c = '#f2ddff' }
    sconce_glow   = @{ o = '#14a08c'; f = '#6cf5d6'; c = '#e6fff8' }
    sconce_green  = @{ o = '#2f8a1c'; f = '#86e84a'; c = '#ecffd2' }
}

# Metal block texture: 0..7 x 0..7 riveted metal, 0..7 x 8..15 dark metal, flame at 8..11 x 0..6,
# burnt wick at 12..13 x 0..1. Everything else transparent.
$metalRows = @(
    'lmlmmlml', 'mhmmmmhm', 'lmmlmmmm', 'mmmmlmlm', 'mlmmmmmm', 'mmhmmlmh', 'lmmmmmmm', 'mmlmhmml'
)
$darkRows = @(
    'dmdddmdd', 'ddddmddd', 'mdddddmd', 'dddmdddd', 'dmddddmd', 'ddddmddd', 'mdmddddd', 'ddddddmd'
)
$flameRows = @(
    '..o.', '.of.', '.ffo', 'ofcf', 'fccf', 'ofco', '.ff.'
)

function New-HolderTexture([string[]]$flame, [int]$fw) {
    $rows = for ($y = 0; $y -lt 16; $y++) {
        $line = if ($y -lt 8) { $metalRows[$y] } else { $darkRows[$y - 8] }
        $fl = if ($y -lt $flame.Count) { $flame[$y] } else { '.' * $fw }
        $wick = if ($y -lt 2) { 'kk' } else { '..' }
        $pad = 16 - 8 - $fw - 2
        $line + $fl + $wick + ('.' * $pad)
    }
    return , $rows
}

foreach ($name in $flames.Keys) {
    $pal = @{} + $iron + $flames[$name] + @{ k = '#1b1410' }
    Save-PixelArt -Rows (New-HolderTexture $flameRows 4) -Palette $pal -Path "$blocks/$name.png"
}
$pal = @{} + $iron + $flames['sconce'] + @{ k = '#1b1410' }
Save-PixelArt -Rows (New-HolderTexture $flameRows 4) -Palette $pal -Path "$blocks/sconce_lever.png"

# Candle holder: brass-ish dark iron with a small candle flame (3x5) at 8..10 x 0..4.
$candleFlame = @('.o.', 'ofo', 'fcf', 'fcf', '.f.')
$pal = @{} + $iron + $flames['sconce'] + @{ k = '#1b1410' }
Save-PixelArt -Rows (New-HolderTexture $candleFlame 3) -Palette $pal -Path "$blocks/candle_holder.png"

# Wall lantern bracket: metal only.
Save-PixelArt -Rows (New-HolderTexture @() 4) -Palette ($iron + @{ k = '#1b1410' }) -Path "$blocks/wall_lantern.png"

# ---------------------------------------------------------------- lamps (full blocks)
$lampRows = @(
    'aaaaaaaaaaaaaaaa',
    'abbbbbbbbbbbbbba',
    'abccccccccccccda',
    'abcggggggggggcda',
    'abcgyyyyyyyygcda',
    'abcgyGGGGGGygcda',
    'abcgyGwwwwGygcda',
    'abcgyGwWWwGygcda',
    'abcgyGwWWwGygcda',
    'abcgyGwwwwGygcda',
    'abcgyGGGGGGygcda',
    'abcgyyyyyyyygcda',
    'abcggggggggggcda',
    'abddddddddddddda',
    'abbbbbbbbbbbbbba',
    'aaaaaaaaaaaaaaaa'
)
$lamps = [ordered]@{
    stone_lamp      = @{ a = '#5f5f5f'; b = '#8a8a8a'; c = '#9f9f9f'; d = '#6e6e6e' }
    blackstone_lamp = @{ a = '#1f1a20'; b = '#3a3238'; c = '#4a4148'; d = '#2a2429' }
    deepslate_lamp  = @{ a = '#2c2c30'; b = '#4f4f55'; c = '#626268'; d = '#3b3b40' }
    end_stone_lamp  = @{ a = '#b8b27e'; b = '#dcd8a0'; c = '#eae6b4'; d = '#c6c08a' }
}
$glow = @{ g = '#c7731c'; y = '#eba23a'; p = '#f8cf5a'; w = '#ffe993'; q = '#fffbe0' }
$glowEnd = @{ g = '#8e5fb8'; y = '#b98ae0'; p = '#dcbcf5'; w = '#f1e2ff'; q = '#ffffff' }
foreach ($name in $lamps.Keys) {
    # PowerShell hashtables are case-insensitive: the bright keys G / W become p / q.
    $rows = $lampRows | ForEach-Object { $_.Replace('G', 'p').Replace('W', 'q') }
    $g = if ($name -eq 'end_stone_lamp') { $glowEnd } else { $glow }
    $pal = @{} + $lamps[$name] + $g
    Save-PixelArt -Rows $rows -Palette $pal -Path "$blocks/$name.png"
}

# ---------------------------------------------------------------- item icons
$sconceIcon = @(
    '................',
    '.......o........',
    '......ofo.......',
    '......fcf.......',
    '......ofo.......',
    '.....dlllh......',
    '....dmmmmmh.....',
    '.....dmmmh......',
    '......dml.......',
    '.......m........',
    '.......m........',
    '.......m........',
    '......dml.......',
    '.....dmmmh......',
    '.....ddddd......',
    '................'
)
foreach ($name in $flames.Keys) {
    Save-PixelArt -Rows $sconceIcon -Palette (@{} + $iron + $flames[$name]) -Path "$items/$name.png"
}
$leverIcon = @(
    '................',
    '..........o.....',
    '.........ofo....',
    '.........fcf....',
    '........dlllh...',
    '.......dmmmmmh..',
    '........dmmmh...',
    '.......dml......',
    '......dm........',
    '.dhh.dm.........',
    '.dmmdm..........',
    '.dmmm...........',
    '.dmmh...........',
    '.dmmh...........',
    '.dddd...........',
    '................'
)
Save-PixelArt -Rows $leverIcon -Palette (@{} + $iron + $flames['sconce']) -Path "$items/sconce_lever.png"

$candleColors = [ordered]@{
    candle_holder = @('#e8cf9a', '#c9aa6c', '#a6874e')
    white = @('#f4f4f4', '#d6d6d6', '#b0b0b0'); orange = @('#f8a050', '#e2741c', '#b85810')
    magenta = @('#e27ad8', '#c04ab8', '#93308e'); light_blue = @('#8fd0f2', '#4aa8dc', '#2c80b4')
    yellow = @('#fbe36a', '#ecc22a', '#c49a14'); lime = @('#a6e45a', '#74c22a', '#4e9418')
    pink = @('#f7b4c8', '#ec88a8', '#c46284'); gray = @('#7a7f84', '#575c61', '#3e4245')
    light_gray = @('#c6c6c0', '#9d9d97', '#76766f'); cyan = @('#4ab8bc', '#1f8f94', '#146c70')
    purple = @('#a868d6', '#7f3cb4', '#5e2690'); blue = @('#5a6cd6', '#3646aa', '#232f82')
    brown = @('#a8764a', '#7f5230', '#5e3a20'); green = @('#7a9a3a', '#56721f', '#3d5214')
    red = @('#d24a42', '#a42a24', '#7c1a16'); black = @('#3d3d48', '#26262e', '#15151a')
}
$holderIcon = @(
    '................',
    '................',
    '.......o........',
    '......ofo.......',
    '.......c........',
    '.......k........',
    '......ABC.......',
    '......ABC.......',
    '......ABC.......',
    '......ABC.......',
    '......ABC.......',
    '...hlllllllh....',
    '....dmmmmmd.....',
    '......dmd.......',
    '.....dmmmd......',
    '....ddddddd.....'
)
foreach ($name in $candleColors.Keys) {
    $c = $candleColors[$name]
    $rows = $holderIcon | ForEach-Object { $_.Replace('A', 'x').Replace('B', 'y').Replace('C', 'z') }
    $pal = @{} + $iron + $flames['sconce'] + @{ k = '#1b1410'; x = $c[0]; y = $c[1]; z = $c[2] }
    $file = if ($name -eq 'candle_holder') { 'candle_holder' } else { "candle_holder_$name" }
    Save-PixelArt -Rows $rows -Palette $pal -Path "$items/$file.png"
}

# ---------------------------------------------------------------- particles: ender, glow, green flames (8x8 each)
$flameParticle = @(
    '........',
    '...o....',
    '..ofo...',
    '..ffo...',
    '.ofcfo..',
    '.fccf...',
    '.ofcfo..',
    '..ooo...'
)
$prow = for ($y = 0; $y -lt 8; $y++) {
    $flameParticle[$y].Replace('o', 'a').Replace('f', 'b').Replace('c', 'e') +
    $flameParticle[$y].Replace('o', 'g').Replace('f', 'i').Replace('c', 'j') +
    $flameParticle[$y].Replace('o', 'r').Replace('f', 's').Replace('c', 't')
}
$e = $flames['sconce_ender']; $g = $flames['sconce_glow']; $n = $flames['sconce_green']
$ppal = @{ a = $e.o; b = $e.f; e = $e.c; g = $g.o; i = $g.f; j = $g.c; r = $n.o; s = $n.f; t = $n.c }
Save-PixelArt -Rows $prow -Palette $ppal -Path "$parts/complementaries_flames.png"

Write-Host 'lighting textures written'
