# Original pixel art of the tool unit (U16): item icons, the bubble block and particle sheets.
#   pwsh tools/textures/tool.ps1 [-Preview]
param([switch]$Preview)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lib.ps1"
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$rp = Join-Path $root 'assets/pack/rp/textures'

function Out-Texture([string]$rel, [string[]]$rows, [hashtable]$pal) {
    Save-PixelArt -Rows $rows -Palette $pal -Path (Join-Path $rp $rel)
    if ($Preview) { Save-PixelArt -Rows $rows -Palette $pal -Scale 16 -Path (Join-Path $root "tools/textures/preview/$($rel -replace '[\\/]', '_')") }
}

# ---- slingshot: forked wooden frame, string band with a leather pouch -------------------------
Out-Texture 'items/slingshot.png' @(
    '................',
    '.oo.........oo..',
    'ohwo.......ohwo.',
    'owwo.......owwo.',
    'owwosslllssowwo.',
    '.owwo..l..owwo..',
    '..owwo...owwo...',
    '...owwo.owwo....',
    '....owwwwwo.....',
    '.....owwwo......',
    '......owo.......',
    '......ohw.......',
    '......owo.......',
    '......owo.......',
    '......ooo.......',
    '................'
) @{ o = '#4a2e14'; w = '#9b6a35'; h = '#c08a4e'; s = '#e8e1d0'; l = '#7a4a26' }

# ---- bubble blower: wire ring with a soap film on a stick -------------------------------------
Out-Texture 'items/bubble_blower.png' @(
    '................',
    '.........mmmm...',
    '........mbbbbm..',
    '.......mbbhbbbm.',
    '.......mbhbbbbm.',
    '.......mbbbbbbm.',
    '........mbbbbm..',
    '........mmmm....',
    '.......ss.......',
    '......ss........',
    '.....ss.........',
    '....ss..........',
    '...ss...........',
    '..dd............',
    '.dd.............',
    '................'
) @{ m = '#8d9aa3'; b = '#7fd6f2'; h = '#f2fcff'; s = '#a57841'; d = '#6b4a26' }

# ---- soap: a pink bar -------------------------------------------------------------------------
Out-Texture 'items/soap.png' @(
    '................',
    '................',
    '................',
    '................',
    '................',
    '....oooooooo....',
    '...ohhhhhhhwo...',
    '..ohwwwwwwwwso..',
    '..owwwwwwwwwso..',
    '..owwwwwwwwsso..',
    '..ossssssssso...',
    '...ooooooooo....',
    '................',
    '................',
    '................',
    '................'
) @{ o = '#8a4b5f'; w = '#f3c6d3'; h = '#fff0f5'; s = '#d99aaf' }

# ---- wrench: copper spanner, open jaw top right ------------------------------------------------
Out-Texture 'items/wrench.png' @(
    '................',
    '...........oo.o.',
    '..........ohhoho',
    '..........ohhhho',
    '.........ohlhho.',
    '........ohlhoo..',
    '.......ohlo.....',
    '......ohlo......',
    '.....ohlo.......',
    '....ohho........',
    '...ohho.........',
    '..ohhho.........',
    '.ohhhho.........',
    '.ohhho..........',
    '..ooo...........',
    '................'
) @{ o = '#5e3119'; h = '#c8794a'; l = '#e9a774' }

# ---- flute: diagonal wooden pipe with finger holes --------------------------------------------
Out-Texture 'items/flute.png' @(
    '................',
    '.............oo.',
    '............owwo',
    '...........owwo.',
    '..........odwo..',
    '.........owwo...',
    '........owdo....',
    '.......owwo.....',
    '......odwo......',
    '.....owwo.......',
    '....owdo........',
    '...owwo.........',
    '..owwo..........',
    '.owwo...........',
    '.oo.............',
    '................'
) @{ o = '#6e4a1e'; w = '#d8b46a'; d = '#3a2410' }

# ---- confetti popper: striped cone shooting confetti ------------------------------------------
Out-Texture 'items/confetti_popper.png' @(
    '..r....y........',
    '.....b.....g....',
    '...y......r.....',
    '.........oo..b..',
    '..g.....oppo....',
    '.......oppwpo...',
    '......oppwppo...',
    '.....oppwppo....',
    '....oppwppo.....',
    '...opwppo.......',
    '..opwppo........',
    '..owppo.........',
    '.oppo...........',
    '.ooo............',
    '................',
    '................'
) @{ o = '#5a1f45'; p = '#d14fa0'; w = '#f2c94c'; r = '#e8453c'; y = '#f7d23e'; b = '#3f8de0'; g = '#4cc66a' }

# ---- antique ink: corked bottle of sepia ink -------------------------------------------------
Out-Texture 'items/antique_ink.png' @(
    '................',
    '......cccc......',
    '......cccc......',
    '......gwwg......',
    '......gwwg......',
    '.....gwwwwg.....',
    '....gwiiiiwg....',
    '...giiiiiiiig...',
    '...giiihiiiig...',
    '...giiiiiiiig...',
    '...giiiiiiiig...',
    '...giiiiiiiig...',
    '....giiiiiig....',
    '.....gggggg.....',
    '................',
    '................'
) @{ c = '#9a7044'; g = '#3c4b52'; w = '#cfe3ea'; i = '#5b3a1e'; h = '#8a6038' }

# ---- empty slice map: paper with horizontal strata --------------------------------------------
$inner = @('pppppppppppp', 'pssssssssssp', 'pppppppppppp', 'pkkkkkkkkkkp', 'pppppppppppp', 'pssssssssssp',
    'pppppppppppp', 'pkkkkkkkkkkp', 'pppppppppppp', 'pssssssssssp', 'pppppppppppp', 'pppppppppppp')
$rows = @('................', '.oooooooooooooo.') + ($inner | ForEach-Object { ".o$($_)o." }) + @('.oooooooooooooo.', '................')
Out-Texture 'items/empty_slice_map.png' $rows @{ o = '#7d6a4a'; p = '#efe4c8'; s = '#9fb6c9'; k = '#6e7f8c' }

# ---- bubble block: translucent film with a rim and a glint -------------------------------------
$bubble = for ($y = 0; $y -lt 16; $y++) {
    -join (0..15 | ForEach-Object {
        $x = $_
        if ($x -eq 0 -or $y -eq 0 -or $x -eq 15 -or $y -eq 15) { 'r' }
        elseif (($x -eq 2 -and $y -ge 2 -and $y -le 5) -or ($y -eq 2 -and $x -ge 2 -and $x -le 5)) { 'h' }
        elseif (($x + $y) % 7 -eq 0) { 'c' }
        else { 'b' }
    })
}
Out-Texture 'blocks/bubble_block.png' $bubble @{ r = '#a0bfeefa'; b = '#38a8e4f6'; c = '#50d0b0f0'; h = '#e0ffffff' }

# ---- particle sheets ---------------------------------------------------------------------------
Out-Texture 'particle/cm_confetti.png' @(
    'rrrryyyybbbbgggg',
    'rrrryyyybbbbgggg',
    'rrrryyyybbbbgggg',
    'rrrryyyybbbbgggg'
) @{ r = '#ef4d6b'; y = '#ffd23f'; b = '#4aa3ff'; g = '#5ad17a' }
Out-Texture 'particle/cm_bubble.png' @(
    '..oooo..',
    '.ohhbbo.',
    'ohhbbbbo',
    'ohbbbbbo',
    'obbbbbbo',
    'obbbbbbo',
    '.obbbbo.',
    '..oooo..'
) @{ o = '#d0e6f6ff'; h = '#f0ffffff'; b = '#4096dcf0' }
Write-Host 'tool textures written'
