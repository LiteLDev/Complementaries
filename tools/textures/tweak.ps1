# U18 tweak: original pixel art for the placeable sticks / rods, gunpowder trails and hanging flower pots.
#
#   pwsh tools/textures/tweak.ps1            # writes assets/pack/rp/textures/blocks/tweak/*.png
#   pwsh tools/textures/tweak.ps1 -Preview   # also writes 8x previews next to them (not committed)
param([switch]$Preview)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lib.ps1"
$out = Join-Path $PSScriptRoot '../../assets/pack/rp/textures/blocks/tweak'

function Save($name, $rows, $pal) {
    Save-PixelArt -Rows $rows -Palette $pal -Path (Join-Path $out "$name.png")
    if ($Preview) { Save-PixelArt -Rows $rows -Palette $pal -Path (Join-Path $out "preview/$name.png") -Scale 8 }
}

# ---- sticks / rods (32x32 sheet) -----------------------------------------------------------------
# x 0-1, y 0-15: the side of a stick standing up; x 2-3, y 0-1: its end; y 16-17, x 0-15: the side of a
# stick lying down. The geometry (models/blocks/tweak_stick.geo.json) maps its faces onto these.
function Stick-Sheet([string]$dark, [string]$mid, [string]$light, [string]$knot) {
    $rows = New-Object System.Collections.Generic.List[string]
    # vertical side: two columns with a light edge, a dark edge and knots
    $vertical = @('lm', 'lm', 'mk', 'lm', 'ld', 'lm', 'lm', 'md', 'lm', 'mk', 'lm', 'lm', 'ld', 'lm', 'mk', 'lm')
    for ($y = 0; $y -lt 16; $y++) {
        $end = if ($y -eq 0) { 'kd' } elseif ($y -eq 1) { 'dk' } else { '..' }
        $rows.Add($vertical[$y] + $end + ('.' * 28))
    }
    $rows.Add('llmlllmllmlllmll' + ('.' * 16))
    $rows.Add('mmdmkmmdmmkmdmmm' + ('.' * 16))
    for ($y = 18; $y -lt 32; $y++) { $rows.Add('.' * 32) }
    $pal = @{ d = $dark; m = $mid; l = $light; k = $knot }
    return , @($rows.ToArray(), $pal)
}

$s = Stick-Sheet '#5a3d1e' '#7d5a2f' '#a07a44' '#4a3018'
Save 'stick' $s[0] $s[1]
$s = Stick-Sheet '#c8610f' '#f2a62b' '#ffe17a' '#a8470a'
Save 'blaze_rod' $s[0] $s[1]
$s = Stick-Sheet '#5b63a8' '#8f9fe0' '#d3defc' '#454b8a'
Save 'breeze_rod' $s[0] $s[1]

# ---- gunpowder trail (16x16): grey grains, the trail model uses the centre and the edges ---------
$trail = @(
    '......g..d......',
    '...d.....g..g...',
    '.g...gd.....d...',
    '....d...gg....g.',
    '..g...ddgdg.d...',
    '.....gdggdgd....',
    'd..g.dgddgdg..g.',
    '...gddgggddgd...',
    '.g..gdggdgdg.d..',
    '....dgdgddgg....',
    '..d..gdgdgd..g..',
    '.g....dgg.d.....',
    '....g.....g..d..',
    '..d...gd.g......',
    '.....g.....d..g.',
    '..g......g......'
)
Save 'gunpowder' $trail @{ g = '#6e6e6e'; d = '#3f3f3f' }
Save 'gunpowder_burning' $trail @{ g = '#ffb02e'; d = '#e2401a' }
Save 'gunpowder_embers' $trail @{ g = '#8a5a3a'; d = '#3a2a22' }

# ---- hanging flower pot (16x16) ------------------------------------------------------------------
# (0,0) 6x6 pot side, (6,0) 6x6 pot top (rim + soil), (0,6) 6x6 pot bottom, (12,0) 1x10 string.
$pot = @(
    'aaaaaabbbbbbw...',
    'bccccbbffffbw...',
    'ccccccbfeefbw...',
    'ceccecbfeffbw...',
    'cceccbbffffbw...',
    'eccccebbbbbbw...',
    'eeeeee......w...',
    'eceeee......w...',
    'eeceee......w...',
    'eeeeee......w...',
    'eceeee..........',
    'eeeeee..........',
    '................',
    '................',
    '................',
    '................'
)
Save 'hanging_flower_pot' $pot @{ a = '#9a4f33'; b = '#7a3b26'; c = '#b5643f'; e = '#8e4a2e'; f = '#4a3221'; w = '#d9cfb4' }
