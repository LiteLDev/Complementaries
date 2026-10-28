# Draws the blackboard textures and generates the data-driven drawing renderer (U5 blackboard).
#
#   pwsh tools/textures/blackboard.ps1            # writes into assets/pack/rp
#   pwsh tools/textures/blackboard.ps1 -Preview   # also writes 8x previews to tools/textures/preview/
#
# Outputs (all original art / generated files):
#   textures/blocks/blackboard_front.png, blackboard_side.png      the block
#   textures/entity/blackboard/c1..c15.png                         chalk of each pixel value
#   models/entity/blackboard_display.geo.json                      one quad per pixel (2 halves)
#   render_controllers/blackboard_display.render_controllers.json  one controller per colour (+ glow)
#   entity/blackboard_display.entity.json                          client entity
#
# Pixel values follow Supplementaries: 0 blank, 1 white, 15 orange, other dyes their dye id.
# The helper entity keeps 6 pixels (4 bits each) per int property `complementaries:p<k>`; each
# entity draws 8 rows (`complementaries:half` 0 = top, 1 = bottom), so 128 pixels in 22 properties.
param([switch]$Preview)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lib.ps1"
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$rp = Join-Path $root 'assets/pack/rp'

function Save-Tex([string[]]$Rows, [hashtable]$Pal, [string]$Rel) {
    Save-PixelArt -Rows $Rows -Palette $Pal -Path (Join-Path $rp $Rel)
    if ($Preview) {
        Save-PixelArt -Rows $Rows -Palette $Pal -Path (Join-Path $PSScriptRoot ("preview/" + ($Rel -replace '[\\/]', '_'))) -Scale 8
    }
}

# Deterministic noise so re-running the script gives the same files.
$script:seed = 1234567
function Next-Rand([int]$n) {
    $script:seed = ($script:seed * 1103515245 + 12345) % 2147483648
    return [int](($script:seed / 65536) % $n)
}

# ---- block: front (the slate) -------------------------------------------------------------------
$front = for ($y = 0; $y -lt 16; $y++) {
    -join (0..15 | ForEach-Object {
            $x = $_
            if ($x -eq 0 -or $y -eq 0 -or $x -eq 15 -or $y -eq 15) { 'e' }
            else {
                $r = Next-Rand 20
                if ($r -lt 2) { 's' } elseif ($r -lt 6) { 'd' } elseif ($r -lt 7) { 'l' } else { 'b' }
            }
        })
}
Save-Tex $front @{ b = '#2b302d'; d = '#272b29'; l = '#343a36'; s = '#3b413d'; e = '#1e2220' } 'textures/blocks/blackboard_front.png'

# ---- block: sides / back (dark wooden frame around blackstone) ----------------------------------
$side = @(
    'oooooooooooooooo',
    'oWWWWWWWWWWWWWWo',
    'oWkkkkkkkkkkkkWo',
    'oWkmmkkmkkkmmkWo',
    'oWkkkmkkkmkkkkWo',
    'oWkmkkkkmkkkmkWo',
    'oWkkkkmkkkkmkkWo',
    'oWkmkkkkkmkkkkWo',
    'oWkkkmkkkkkkmkWo',
    'oWkkkkkmkkmkkkWo',
    'oWkmkkkkkkkkkkWo',
    'oWkkkkmkkmkkmkWo',
    'oWkmkkkkkkkkkkWo',
    'oWkkkkkmkkkmkkWo',
    'oWWWWWWWWWWWWWWo',
    'oooooooooooooooo'
)
Save-Tex $side @{ o = '#3d2a17'; W = '#5a3e22'; k = '#2a2628'; m = '#353033' } 'textures/blocks/blackboard_side.png'

# ---- chalk textures (one per pixel value) -------------------------------------------------------
$colors = [ordered]@{
    1  = '#f4f4f0' # white chalk
    2  = '#b24cd8' # magenta
    3  = '#6699d8' # light blue
    4  = '#e5e533' # yellow
    5  = '#7fcc19' # lime
    6  = '#f27fa5' # pink
    7  = '#4c4c4c' # gray
    8  = '#999999' # light gray
    9  = '#4c7f99' # cyan
    10 = '#7f3fb2' # purple
    11 = '#334cb2' # blue
    12 = '#664c33' # brown
    13 = '#667f33' # green
    14 = '#993333' # red
    15 = '#d87f33' # orange
}
foreach ($v in $colors.Keys) {
    $base = $colors[$v]
    $rows = for ($y = 0; $y -lt 16; $y++) {
        -join (0..15 | ForEach-Object { $r = Next-Rand 10; if ($r -lt 2) { 'a' } elseif ($r -lt 4) { 'c' } else { 'b' } })
    }
    Save-Tex $rows @{ b = $base; a = (Shade-Color $base 0.88); c = (Shade-Color $base 1.08) } "textures/entity/blackboard/c$v.png"
}

# ---- display geometry -------------------------------------------------------------------------
# Local frame: the board front faces north (-z) at z = -3 px (the board is 5 px thick at the back of
# the block); the entity sits at the block's bottom centre. Column 0 is on the viewer's left
# (east, +x, for a viewer looking south at a north-facing board). An animation turns the root
# bone to the board's facing.
$frontZ = -3.05
function New-HalfGeometry([int]$half) {
    $bones = [System.Collections.Generic.List[object]]::new()
    $bones.Add([ordered]@{ name = 'root'; pivot = @(0, 0, 0) })
    for ($n = 0; $n -lt 128; $n++) {
        $row = $half * 8 + [int][Math]::Floor($n / 16)
        $col = $n % 16
        $x = 7 - $col
        $y = 15 - $row
        $bones.Add([ordered]@{
                name   = "p$n"
                parent = 'root'
                pivot  = @(0, 0, 0)
                cubes  = @([ordered]@{
                        origin = @($x, $y, $frontZ)
                        size   = @(1, 1, 0)
                        uv     = [ordered]@{ north = [ordered]@{ uv = @($col, $row); uv_size = @(1, 1) } }
                    })
            })
    }
    return [ordered]@{
        description = [ordered]@{
            identifier            = "geometry.complementaries.blackboard_display_$(@('top', 'bottom')[$half])"
            texture_width         = 16
            texture_height        = 16
            visible_bounds_width  = 2
            visible_bounds_height = 2
            visible_bounds_offset = @(0, 0.5, 0)
        }
        bones       = $bones
    }
}
$geo = [ordered]@{ format_version = '1.16.0'; 'minecraft:geometry' = @((New-HalfGeometry 0), (New-HalfGeometry 1)) }
$geoPath = Join-Path $rp 'models/entity/blackboard_display.geo.json'
New-Item -ItemType Directory -Force (Split-Path $geoPath) | Out-Null
$geo | ConvertTo-Json -Depth 20 -Compress | Set-Content -NoNewline -Encoding utf8NoBOM $geoPath

# ---- render controllers -------------------------------------------------------------------------
$controllers = [ordered]@{}
$entityControllers = [System.Collections.Generic.List[object]]::new()
foreach ($glow in @($false, $true)) {
    foreach ($v in $colors.Keys) {
        $vis = [System.Collections.Generic.List[object]]::new()
        $vis.Add([ordered]@{ '*' = $false })
        for ($n = 0; $n -lt 128; $n++) {
            $k = [int][Math]::Floor($n / 6)
            $div = [int][Math]::Pow(16, $n % 6)
            $vis.Add([ordered]@{ "p$n" = "math.mod(math.floor(q.property('complementaries:p$k') / $div), 16) == $v" })
        }
        $name = "controller.render.complementaries.blackboard_c$v" + $(if ($glow) { '_glow' } else { '' })
        $rc = [ordered]@{
            arrays          = [ordered]@{ geometries = [ordered]@{ 'Array.halves' = @('Geometry.top', 'Geometry.bottom') } }
            geometry        = "Array.halves[q.property('complementaries:half')]"
            materials       = @([ordered]@{ '*' = 'Material.default' })
            textures        = @("Texture.c$v")
            part_visibility = $vis
        }
        if ($glow) { $rc['ignore_lighting'] = $true }
        $controllers[$name] = $rc
        $bit = [int][Math]::Pow(2, $v)
        $cond = "math.mod(math.floor(q.property('complementaries:colors') / $bit), 2) == 1 && " + $(if ($glow) { "q.property('complementaries:glow')" } else { "!q.property('complementaries:glow')" })
        $entityControllers.Add([ordered]@{ $name = $cond })
    }
}
$rcPath = Join-Path $rp 'render_controllers/blackboard_display.render_controllers.json'
New-Item -ItemType Directory -Force (Split-Path $rcPath) | Out-Null
[ordered]@{ format_version = '1.10.0'; render_controllers = $controllers } | ConvertTo-Json -Depth 20 -Compress |
    Set-Content -NoNewline -Encoding utf8NoBOM $rcPath

# ---- client entity --------------------------------------------------------------------------------
$textures = [ordered]@{}
foreach ($v in $colors.Keys) { $textures["c$v"] = "textures/entity/blackboard/c$v" }
$client = [ordered]@{
    format_version             = '1.10.0'
    'minecraft:client_entity' = [ordered]@{
        description = [ordered]@{
            identifier         = 'complementaries:blackboard_display'
            materials          = [ordered]@{ default = 'entity_alphatest' }
            textures           = $textures
            geometry           = [ordered]@{
                top    = 'geometry.complementaries.blackboard_display_top'
                bottom = 'geometry.complementaries.blackboard_display_bottom'
            }
            animations         = [ordered]@{ facing = 'animation.complementaries.blackboard_display.facing' }
            scripts            = [ordered]@{ animate = @('facing') }
            render_controllers = $entityControllers
            enable_attachables = $false
            hide_armor         = $true
        }
    }
}
$cePath = Join-Path $rp 'entity/blackboard_display.entity.json'
New-Item -ItemType Directory -Force (Split-Path $cePath) | Out-Null
$client | ConvertTo-Json -Depth 20 | Set-Content -Encoding utf8NoBOM $cePath

Write-Host 'blackboard textures and renderer written'
