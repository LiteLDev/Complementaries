# Draws the textures of the trap unit (bamboo spikes, tipped spikes, cage, fodder).
# Original pixel art. Run: pwsh tools/textures/trap.ps1

. "$PSScriptRoot/lib.ps1"

$root   = Join-Path $PSScriptRoot '../../assets/pack/rp/textures'
$blocks = Join-Path $root 'blocks'
$items  = Join-Path $root 'items'

# ---- bamboo spikes ---------------------------------------------------------------------------

# One plane of the crossed spike planes: three sharpened bamboo canes.
$spikeRows = @(
    '..p....p....p...',
    '.pTp..pTp..pTp..',
    '.TLG..LGD..TGD..',
    '.LGD..LGD..LGD..',
    '.LGD..NNN..LGD..',
    '.NNN..LGD..LGD..',
    '.LGD..LGD..NNN..',
    '.LGD..LGD..LGD..',
    '.LGD..LGD..LGD..',
    '.LGD..NNN..LGD..',
    '.NNN..LGD..LGD..',
    '.LGD..LGD..NNN..',
    '.LGD..LGD..LGD..',
    '.LGD..LGD..LGD..',
    '.LGD..NNN..LGD..',
    '.LGD..LGD..LGD..'
)
$spikePal = @{
    p = '#f2e7b5'; T = '#d9c98a'
    L = '#9fcf5a'; G = '#78aa3c'; D = '#557f28'; N = '#456a1f'
}
Save-PixelArt -Rows $spikeRows -Palette $spikePal -Path (Join-Path $blocks 'bamboo_spikes.png')

# Tip colours of tipped spikes (state complementaries:tip 1..8), matched to potion colours.
$tipColors = [ordered]@{
    1 = '#4e9331' # poison green
    2 = '#430a09' # harming dark red
    3 = '#5a6c81' # slowness grey blue
    4 = '#484d48' # weakness grey
    5 = '#352a27' # wither / decay
    6 = '#cd5cab' # pink (regeneration, healing)
    7 = '#3f76e4' # blue (speed, water, night vision)
    8 = '#8932b8' # purple (others)
}
foreach ($entry in $tipColors.GetEnumerator()) {
    $i = $entry.Key
    $c = $entry.Value
    $pal = $spikePal.Clone()
    $pal.p = Shade-Color $c 1.35
    $pal.T = $c
    Save-PixelArt -Rows $spikeRows -Palette $pal -Path (Join-Path $blocks "bamboo_spikes_tip_$i.png")
}

# Base plate: dried bamboo planks.
$baseRows = @(
    'aaaabbbbaaaabbbb',
    'abbbbaaabbbbaaab',
    'aaaabbbbaaaabbbb',
    'cccccccccccccccc',
    'bbbbaaaabbbbaaaa',
    'baaabbbbaaabbbba',
    'bbbbaaaabbbbaaaa',
    'cccccccccccccccc',
    'aaaabbbbaaaabbbb',
    'abbbbaaabbbbaaab',
    'aaaabbbbaaaabbbb',
    'cccccccccccccccc',
    'bbbbaaaabbbbaaaa',
    'baaabbbbaaabbbba',
    'bbbbaaaabbbbaaaa',
    'cccccccccccccccc'
)
$basePal = @{ a = '#c9b25a'; b = '#b59c47'; c = '#8a7333' }
Save-PixelArt -Rows $baseRows -Palette $basePal -Path (Join-Path $blocks 'bamboo_spikes_base.png')

$spikeItemRows = @(
    '................',
    '..p....p....p...',
    '.pTp..pTp..pTp..',
    '.LGD..LGD..LGD..',
    '.LGD..NNN..LGD..',
    '.NNN..LGD..LGD..',
    '.LGD..LGD..NNN..',
    '.LGD..LGD..LGD..',
    '.LGD..NNN..LGD..',
    '.NNN..LGD..LGD..',
    '.LGD..LGD..NNN..',
    '.LGD..LGD..LGD..',
    'aaaabbbbaaaabbbb',
    'abbbbaaabbbbaaab',
    'cccccccccccccccc',
    '................'
)
$itemPal = $spikePal + $basePal
Save-PixelArt -Rows $spikeItemRows -Palette $itemPal -Path (Join-Path $items 'bamboo_spikes.png')
$tippedPal = $itemPal.Clone()
$tippedPal.p = '#b6e39a'
$tippedPal.T = '#4e9331'
Save-PixelArt -Rows $spikeItemRows -Palette $tippedPal -Path (Join-Path $items 'bamboo_spikes_tipped.png')

# ---- cage ------------------------------------------------------------------------------------

$cageSide = @(
    'ffffffffffffffff',
    'fhhhhhhhhhhhhhhf',
    'fk.b..b..b..b.kf',
    'fk.b..b..b..b.kf',
    'fk.b..b..b..b.kf',
    'fk.b..b..b..b.kf',
    'fk.b..b..b..b.kf',
    'fk.b..b..b..b.kf',
    'fk.b..b..b..b.kf',
    'fk.b..b..b..b.kf',
    'fk.b..b..b..b.kf',
    'fk.b..b..b..b.kf',
    'fk.b..b..b..b.kf',
    'fhhhhhhhhhhhhhhf',
    'wwwwwwwwwwwwwwww',
    'xxxxxxxxxxxxxxxx'
)
$cageTop = @(
    'ffffffffffffffff',
    'fhhhhhhhhhhhhhhf',
    'fh.b..b..b..b.hf',
    'fbbbbbbbbbbbbbbf',
    'fh.b..b..b..b.hf',
    'fh.b..b..b..b.hf',
    'fbbbbbbbbbbbbbbf',
    'fh.b..b..b..b.hf',
    'fh.b..b..b..b.hf',
    'fbbbbbbbbbbbbbbf',
    'fh.b..b..b..b.hf',
    'fh.b..b..b..b.hf',
    'fbbbbbbbbbbbbbbf',
    'fh.b..b..b..b.hf',
    'fhhhhhhhhhhhhhhf',
    'ffffffffffffffff'
)
$cageBottom = @(
    'xxxxxxxxxxxxxxxx',
    'xwwwwwwwwwwwwwwx',
    'xwvvvvvvvvvvvvwx',
    'xwwwwwwwwwwwwwwx',
    'xwvvvvvvvvvvvvwx',
    'xwwwwwwwwwwwwwwx',
    'xwvvvvvvvvvvvvwx',
    'xwwwwwwwwwwwwwwx',
    'xwvvvvvvvvvvvvwx',
    'xwwwwwwwwwwwwwwx',
    'xwvvvvvvvvvvvvwx',
    'xwwwwwwwwwwwwwwx',
    'xwvvvvvvvvvvvvwx',
    'xwwwwwwwwwwwwwwx',
    'xwwwwwwwwwwwwwwx',
    'xxxxxxxxxxxxxxxx'
)
$cagePal = @{
    f = '#3d3f44'; h = '#8c9097'; k = '#5c5f66'; b = '#6f737a'
    w = '#9c7a48'; v = '#86683b'; x = '#5e4826'
}
Save-PixelArt -Rows $cageSide -Palette $cagePal -Path (Join-Path $blocks 'cage_side.png')
Save-PixelArt -Rows $cageTop -Palette $cagePal -Path (Join-Path $blocks 'cage_top.png')
Save-PixelArt -Rows $cageBottom -Palette $cagePal -Path (Join-Path $blocks 'cage_bottom.png')

$cageItem = @(
    '................',
    '..ffffffffffff..',
    '..fhhhhhhhhhhf..',
    '..fb.b.b.b.b.f..',
    '..fb.b.b.b.b.f..',
    '..fb.b.b.b.b.f..',
    '..fb.b.b.b.b.f..',
    '..fb.b.b.b.b.f..',
    '..fb.b.b.b.b.f..',
    '..fb.b.b.b.b.f..',
    '..fb.b.b.b.b.f..',
    '..fb.b.b.b.b.f..',
    '..fhhhhhhhhhhf..',
    '..wwwwwwwwwwww..',
    '..xxxxxxxxxxxx..',
    '................'
)
Save-PixelArt -Rows $cageItem -Palette $cagePal -Path (Join-Path $items 'cage.png')

# ---- fodder ----------------------------------------------------------------------------------

$fodderTop = @(
    'aabacaabdaacabaa',
    'abaaadabaacaabda',
    'caabaaacabaadaab',
    'aadaabaaabcaaaba',
    'baacaabdaaabaaca',
    'aabaadaacabaabaa',
    'acaabaabaadaacab',
    'aabdaacaabaabaaa',
    'daabaaabdaacaaba',
    'aacabaadaabaabca',
    'abaaacabaadacaab',
    'aadabaaabcaabdaa',
    'caabaadaabaacaba',
    'abaacaabdaabaaad',
    'aabaabcaaacaabaa',
    'baadaabaabadaaca'
)
$fodderSide = @(
    'aabacaabdaacabaa',
    'abaaadabaacaabda',
    'eeeeeeeeeeeeeeee',
    'aadaabaaabcaaaba',
    'baacaabdaaabaaca',
    'eeeeeeeeeeeeeeee',
    'acaabaabaadaacab',
    'aabdaacaabaabaaa',
    'eeeeeeeeeeeeeeee',
    'aacabaadaabaabca',
    'abaaacabaadacaab',
    'eeeeeeeeeeeeeeee',
    'caabaadaabaacaba',
    'abaacaabdaabaaad',
    'eeeeeeeeeeeeeeee',
    'baadaabaabadaaca'
)
$fodderPal = @{ a = '#b9a948'; b = '#9a8f35'; c = '#7f9a3a'; d = '#d8c768'; e = '#86782c' }
Save-PixelArt -Rows $fodderTop -Palette $fodderPal -Path (Join-Path $blocks 'fodder_top.png')
Save-PixelArt -Rows $fodderSide -Palette $fodderPal -Path (Join-Path $blocks 'fodder_side.png')

$fodderItem = @(
    '................',
    '................',
    '................',
    '.....dadaad.....',
    '...daabacaabd...',
    '..aabaadaacabaa.',
    '.abaacaabdaabaa.',
    '.eeeeeeeeeeeeee.',
    '.aadaabaaabcaab.',
    '.baacaabdaaabaa.',
    '.eeeeeeeeeeeeee.',
    '.acaabaabaadaac.',
    '.aabdaacaabaaba.',
    '.eeeeeeeeeeeeee.',
    '................',
    '................'
)
Save-PixelArt -Rows $fodderItem -Palette $fodderPal -Path (Join-Path $items 'fodder.png')

Write-Host 'trap textures written'
