# U1 jar: draws the jar / faucet textures (original pixel art) and generates the block, geometry,
# terrain atlas and helper-entity JSON whose permutations follow the fluid table.
#
#   pwsh tools/textures/jar.ps1            # writes into assets/pack/{bp,rp}
#
# The `content` value of a jar / faucet (0..63) is split in two block states:
#   complementaries:kind = content / 16, complementaries:fluid = content % 16.
# Content values must match src/feature/jar/Fluids.h (contentFor, kContent*).

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lib.ps1"

$root = (Resolve-Path "$PSScriptRoot/../..").Path
$bp = Join-Path $root 'assets/pack/bp'
$rp = Join-Path $root 'assets/pack/rp'
$tex = Join-Path $rp 'textures/blocks/jar'

function Write-Json($path, $obj) {
    $dir = Split-Path -Parent $path
    New-Item -ItemType Directory -Force $dir | Out-Null
    $json = $obj | ConvertTo-Json -Depth 64
    [IO.File]::WriteAllText($path, $json + "`n")
}

# ---- fluid colours (content value -> colour, alpha) ----------------------------------------------
$fluids = @{
    1  = @('water', '#3f76e4', 'b8')
    2  = @('lava', '#e2611a', 'ff')
    3  = @('honey', '#f2a51c', 'f0')
    4  = @('milk', '#f4f4ef', 'ff')
    5  = @('mushroom_stew', '#b7835a', 'ff')
    6  = @('beetroot_soup', '#a33a33', 'ff')
    7  = @('rabbit_stew', '#b8743c', 'ff')
    8  = @('suspicious_stew', '#8fae52', 'ff')
    9  = @('experience', '#b6f046', 'e0')
    10 = @('dragon_breath', '#dc6fe0', 'c8')
    11 = @('ghast_tear', '#d9f3f3', 'd8')
    12 = @('magma_cream', '#e8901f', 'ff')
    13 = @('slime', '#77c15a', 'e8')
    14 = @('powder_snow', '#eef3f8', 'ff')
    15 = @('ominous', '#2b4549', 'e8')
    16 = @('lumisene', '#f0e59a', 'e8')
    17 = @('potion', '#385dc6', 'c8')
    18 = @('potion_night_vision', '#c2ff66', 'c8')
    19 = @('potion_invisibility', '#f6f6f6', 'c8')
    20 = @('potion_leaping', '#fdff84', 'c8')
    21 = @('potion_fire_resistance', '#ff9900', 'c8')
    22 = @('potion_swiftness', '#33ebff', 'c8')
    23 = @('potion_slowness', '#8bafe0', 'c8')
    24 = @('potion_water_breathing', '#98dac0', 'c8')
    25 = @('potion_healing', '#f82423', 'c8')
    26 = @('potion_harming', '#a9656a', 'c8')
    27 = @('potion_poison', '#87a363', 'c8')
    28 = @('potion_regeneration', '#cd5cab', 'c8')
    29 = @('potion_strength', '#ffc700', 'c8')
    30 = @('potion_weakness', '#484d48', 'c8')
    31 = @('potion_decay', '#736156', 'c8')
    32 = @('potion_turtle_master', '#8e7ee8', 'c8')
    33 = @('potion_slow_falling', '#f3cfb9', 'c8')
    34 = @('potion_wind_charging', '#bdc9ff', 'c8')
    35 = @('potion_weaving', '#78695a', 'c8')
    36 = @('potion_oozing', '#99ffa3', 'c8')
    37 = @('potion_infestation', '#8c9b8c', 'c8')
}
# jar light per content (luminosity), faucet light = clamp(lum / 2, 1, 7)
$jarLight = @{ 2 = 15; 9 = 5; 10 = 3; 16 = 15; 62 = 5; 63 = 3 }
$cookieContent = @{ 48 = 'cookie'; 49 = 'candy'; 50 = 'item' }

function Get-Noise([int]$x, [int]$y, [int]$seed) {
    $h = ($x * 374761393 + $y * 668265263 + $seed * 1442695041) -band 0x7fffffff
    $h = (($h -bxor ($h -shr 13)) * 1274126177) -band 0x7fffffff
    return ($h % 100) / 100.0
}

function Save-Fluid([string]$name, [string]$hex, [string]$alpha, [int]$seed) {
    $base = ConvertTo-Color $hex
    $pal = @{
        a = '#' + $alpha + $hex.Substring(1)
        b = '#' + $alpha + (Shade-Color $base 1.12).Name.Substring(2)
        c = '#' + $alpha + (Shade-Color $base 0.88).Name.Substring(2)
        d = '#' + $alpha + (Shade-Color $base 1.3).Name.Substring(2)
    }
    $rows = for ($y = 0; $y -lt 16; $y++) {
        -join (0..15 | ForEach-Object {
                $n = Get-Noise $_ $y $seed
                if ($y -eq 0) { if ($n -lt 0.5) { 'd' } else { 'b' } }
                elseif ($n -lt 0.18) { 'b' } elseif ($n -gt 0.86) { 'c' } else { 'a' }
            })
    }
    Save-PixelArt -Rows $rows -Palette $pal -Path (Join-Path $tex "fluid_$name.png")
}

# ---- textures ------------------------------------------------------------------------------------
New-Item -ItemType Directory -Force $tex | Out-Null
$seed = 1
foreach ($k in $fluids.Keys | Sort-Object) { Save-Fluid $fluids[$k][0] $fluids[$k][1] $fluids[$k][2] ($seed++) }

# jar glass: faint blue tint, bright rim and two highlight streaks
$glass = @(
    'wwwwwwwwwwwwwwww',
    'w..............w',
    'w.h............w',
    'w.h..........s.w',
    'w.h..........s.w',
    'w.h............w',
    'w.h............w',
    'w..............w',
    'w..............w',
    'w..............w',
    'w............s.w',
    'w............s.w',
    'w..............w',
    'w..............w',
    'w..............w',
    'wwwwwwwwwwwwwwww'
)
$glassPal = @{ w = '#c0d8eef4'; h = '#a0ffffff'; s = '#70ffffff'; '.' = $null }
# fill the inside with a faint tint instead of full transparency
$glassRows = $glass | ForEach-Object { $_.Replace('.', 't') }
$glassPal['t'] = '#26c8e4f4'
Save-PixelArt -Rows $glassRows -Palette $glassPal -Path (Join-Path $tex 'jar_side.png')
$top = @(
    'wwwwwwwwwwwwwwww',
    'wttttttttttttttw',
    'wttttttttttttttw',
    'wtthhttttttttttw',
    'wtthtttttttttttw',
    'wttttttttttttttw',
    'wttttttttttttttw',
    'wttttttttttttttw',
    'wttttttttttttttw',
    'wttttttttttttttw',
    'wttttttttttttttw',
    'wttttttttttttttw',
    'wttttttttttttttw',
    'wttttttttttttttw',
    'wttttttttttttttw',
    'wwwwwwwwwwwwwwww'
)
Save-PixelArt -Rows $top -Palette $glassPal -Path (Join-Path $tex 'jar_top.png')
$cork = for ($y = 0; $y -lt 16; $y++) {
    -join (0..15 | ForEach-Object {
            $n = Get-Noise $_ $y 77
            if ($y -eq 0 -or $y -eq 15 -or $_ -eq 0 -or $_ -eq 15) { 'o' } elseif ($n -lt 0.2) { 'l' } elseif ($n -gt 0.85) { 'd' } else { 'm' }
        })
}
Save-PixelArt -Rows $cork -Palette @{ o = '#6b4423'; m = '#a87a4b'; l = '#c49a68'; d = '#8a6038' } -Path (Join-Path $tex 'jar_cork.png')

# cookie / candy / generic item seen from the top (a flat disc)
$disc = @(
    '................',
    '................',
    '.....oooooo.....',
    '....oaaaaaao....',
    '...oaacaaaaao...',
    '..oaaaaaacaaao..',
    '..oaaaaaaaaaao..',
    '..oacaaaaaaaao..',
    '..oaaaaacaaaao..',
    '..oaaaaaaaacao..',
    '..oaacaaaaaaao..',
    '...oaaaaacaao...',
    '....oaaaaaao....',
    '.....oooooo.....',
    '................',
    '................'
)
Save-PixelArt -Rows $disc -Palette @{ o = '#8a5a2b'; a = '#c88b47'; c = '#4a2a14' } -Path (Join-Path $tex 'cookie.png')
Save-PixelArt -Rows $disc -Palette @{ o = '#d8508a'; a = '#f6f0f2'; c = '#ef6fa6' } -Path (Join-Path $tex 'candy.png')
Save-PixelArt -Rows $disc -Palette @{ o = '#6e6e6e'; a = '#a8a8a8'; c = '#8a8a8a' } -Path (Join-Path $tex 'item.png')

# faucet: brushed iron with a red valve
$iron = for ($y = 0; $y -lt 16; $y++) {
    -join (0..15 | ForEach-Object {
            $n = Get-Noise $_ $y 31
            if ($y -eq 0 -or $_ -eq 0) { 'l' } elseif ($y -eq 15 -or $_ -eq 15) { 'd' } elseif ($n -lt 0.25) { 'l' } elseif ($n -gt 0.8) { 'd' } else { 'm' }
        })
}
Save-PixelArt -Rows $iron -Palette @{ l = '#d8d8d8'; m = '#a9a9a9'; d = '#6f6f6f' } -Path (Join-Path $tex 'faucet.png')
$valve = for ($y = 0; $y -lt 16; $y++) {
    -join (0..15 | ForEach-Object { if ($y -eq 0 -or $_ -eq 0) { 'l' } elseif ($y -eq 15 -or $_ -eq 15) { 'd' } else { 'm' } })
}
Save-PixelArt -Rows $valve -Palette @{ l = '#e05a4a'; m = '#b8261c'; d = '#7a1610' } -Path (Join-Path $tex 'faucet_valve.png')

# ---- terrain atlas fragment ----------------------------------------------------------------------
$atlas = [ordered]@{
    cm_jar_side         = @{ textures = 'textures/blocks/jar/jar_side' }
    cm_jar_top          = @{ textures = 'textures/blocks/jar/jar_top' }
    cm_jar_cork         = @{ textures = 'textures/blocks/jar/jar_cork' }
    cm_jar_cookie       = @{ textures = 'textures/blocks/jar/cookie' }
    cm_jar_candy        = @{ textures = 'textures/blocks/jar/candy' }
    cm_jar_item         = @{ textures = 'textures/blocks/jar/item' }
    cm_faucet           = @{ textures = 'textures/blocks/jar/faucet' }
    cm_faucet_valve     = @{ textures = 'textures/blocks/jar/faucet_valve' }
}
foreach ($k in $fluids.Keys | Sort-Object) { $atlas["cm_jar_fluid_$k"] = @{ textures = "textures/blocks/jar/fluid_$($fluids[$k][0])" } }
Write-Json (Join-Path $rp 'terrain_texture.d/jar.json') $atlas

# ---- geometry helpers ----------------------------------------------------------------------------
function New-Cube([double[]]$origin, [double[]]$size, [string]$mat, [hashtable]$mats = @{}) {
    $sx, $sy, $sz = $size
    $face = {
        param($w, $h, $name)
        $m = if ($mats.ContainsKey($name)) { $mats[$name] } else { $mat }
        @{ uv = @(0, 0); uv_size = @([Math]::Max($w, 1), [Math]::Max($h, 1)); material_instance = $m }
    }
    [ordered]@{
        origin = $origin
        size   = $size
        uv     = [ordered]@{
            north = & $face $sx $sy 'north'
            south = & $face $sx $sy 'south'
            east  = & $face $sz $sy 'east'
            west  = & $face $sz $sy 'west'
            up    = & $face $sx $sz 'up'
            down  = & $face $sx $sz 'down'
        }
    }
}
function New-Bone([string]$name, $cubes, [double[]]$pivot = @(0, 0, 0), [double[]]$rotation = $null) {
    $b = [ordered]@{ name = $name; pivot = $pivot; cubes = @($cubes) }
    if ($rotation) { $b.rotation = $rotation }
    $b
}
function New-Geo([string]$id, $bones) {
    [ordered]@{
        format_version      = '1.12.0'
        'minecraft:geometry' = @(
            [ordered]@{
                description = [ordered]@{ identifier = $id; texture_width = 16; texture_height = 16; visible_bounds_width = 2; visible_bounds_height = 2.5; visible_bounds_offset = @(0, 0.75, 0) }
                bones       = @($bones)
            }
        )
    }
}
function Content-Cond([int]$c) { "q.block_state('complementaries:kind') == $([Math]::Floor($c / 16)) && q.block_state('complementaries:fluid') == $($c % 16)" }

# ---- jar -----------------------------------------------------------------------------------------
$jarBones = @()
$jarBones += New-Bone 'jar' (New-Cube @(-5, 0, -5) @(10, 14, 10) 'glass' @{ up = 'glass_top'; down = 'glass_top' })
$jarBones += New-Bone 'lid' (New-Cube @(-3, 14, -3) @(6, 2, 6) 'cork')
for ($n = 1; $n -le 12; $n++) { $jarBones += New-Bone "fluid_$n" (New-Cube @(-4, 1, -4) @(8, $n, 8) 'fluid') }
for ($n = 1; $n -le 12; $n++) {
    $y = $n
    $rot = @(0, ((($n * 37) % 90) - 45), 0)
    $jarBones += New-Bone "cookie_$n" (New-Cube @(-3, $y, -3) @(6, 1, 6) 'cookie') @(0, $y, 0) $rot
}
Write-Json (Join-Path $rp 'models/blocks/jar.geo.json') (New-Geo 'geometry.complementaries.jar' $jarBones)

$isFluid = "(q.block_state('complementaries:kind') < 3 && (q.block_state('complementaries:kind') > 0 || q.block_state('complementaries:fluid') > 0))"
$isCookie = "(q.block_state('complementaries:kind') == 3 && q.block_state('complementaries:fluid') <= 2)"
$vis = [ordered]@{}
for ($n = 1; $n -le 12; $n++) { $vis["fluid_$n"] = "$isFluid && q.block_state('complementaries:level') == $n" }
for ($n = 1; $n -le 12; $n++) { $vis["cookie_$n"] = "$isCookie && q.block_state('complementaries:level') >= $n" }

function Jar-Materials([string]$fluidTex, [string]$cookieTex) {
    [ordered]@{
        '*'       = @{ texture = 'cm_jar_side'; render_method = 'blend' }
        glass     = @{ texture = 'cm_jar_side'; render_method = 'blend' }
        glass_top = @{ texture = 'cm_jar_top'; render_method = 'blend' }
        cork      = @{ texture = 'cm_jar_cork'; render_method = 'blend' }
        fluid     = @{ texture = $fluidTex; render_method = 'blend' }
        cookie    = @{ texture = $cookieTex; render_method = 'blend' }
    }
}

$jarPerms = @()
foreach ($k in $fluids.Keys | Sort-Object) {
    $comp = [ordered]@{ 'minecraft:material_instances' = (Jar-Materials "cm_jar_fluid_$k" 'cm_jar_cookie') }
    if ($jarLight.ContainsKey([int]$k)) { $comp['minecraft:light_emission'] = $jarLight[[int]$k] }
    $jarPerms += [ordered]@{ condition = (Content-Cond $k); components = $comp }
}
foreach ($k in $cookieContent.Keys | Sort-Object) {
    $jarPerms += [ordered]@{ condition = (Content-Cond $k); components = [ordered]@{ 'minecraft:material_instances' = (Jar-Materials 'cm_jar_fluid_1' "cm_jar_$($cookieContent[$k])") } }
}
foreach ($k in 62, 63) {
    $jarPerms += [ordered]@{ condition = (Content-Cond $k); components = [ordered]@{ 'minecraft:light_emission' = $jarLight[$k] } }
}

$jar = [ordered]@{
    format_version    = '1.21.100'
    'minecraft:block' = [ordered]@{
        description  = [ordered]@{
            identifier      = 'complementaries:jar'
            menu_category   = @{ category = 'items'; group = 'minecraft:itemGroup.name.glass' }
            states          = [ordered]@{
                'complementaries:kind'  = @{ values = @{ min = 0; max = 3 } }
                'complementaries:fluid' = @{ values = @{ min = 0; max = 15 } }
                'complementaries:level' = @{ values = @{ min = 0; max = 12 } }
            }
        }
        components   = [ordered]@{
            'minecraft:geometry'                  = [ordered]@{ identifier = 'geometry.complementaries.jar'; bone_visibility = $vis }
            'minecraft:material_instances'        = (Jar-Materials 'cm_jar_fluid_1' 'cm_jar_cookie')
            'minecraft:collision_box'             = @{ origin = @(-5, 0, -5); size = @(10, 16, 10) }
            'minecraft:selection_box'             = @{ origin = @(-5, 0, -5); size = @(10, 16, 10) }
            'minecraft:destructible_by_mining'    = @{ seconds_to_destroy = 0.5 }
            'minecraft:destructible_by_explosion' = @{ explosion_resistance = 1 }
            'minecraft:light_dampening'           = 0
            'minecraft:map_color'                 = '#c8e0f0'
            'minecraft:loot'                      = 'loot_tables/blocks/jar.json'
            'minecraft:movable'                   = @{ movement_type = 'popped' }
        }
        permutations = $jarPerms
    }
}
Write-Json (Join-Path $bp 'blocks/jar.json') $jar

# ---- faucet --------------------------------------------------------------------------------------
# Modelled pointing north (-z); the wall it hangs on is at +z. `_c` bones: sitting on a jar below.
$fb = @()
$fb += New-Bone 'pipe' (New-Cube @(-2, 10, -3) @(4, 4, 11) 'iron')
$fb += New-Bone 'spout' (New-Cube @(-2, 5, -3) @(4, 5, 4) 'iron')
$fb += New-Bone 'stem' (New-Cube @(-1, 14, 1) @(2, 1, 2) 'iron')
$fb += New-Bone 'valve' (New-Cube @(-3, 15, 1) @(6, 1, 2) 'valve')
$fb += New-Bone 'stream' (New-Cube @(-1, 0, -2) @(2, 5, 2) 'stream')
$fb += New-Bone 'pipe_c' (New-Cube @(-2, 5, -3) @(4, 4, 11) 'iron')
$fb += New-Bone 'spout_c' (New-Cube @(-2, 0, -3) @(4, 5, 4) 'iron')
$fb += New-Bone 'stem_c' (New-Cube @(-1, 9, 1) @(2, 1, 2) 'iron')
$fb += New-Bone 'valve_c' (New-Cube @(-3, 10, 1) @(6, 1, 2) 'valve')
Write-Json (Join-Path $rp 'models/blocks/faucet.geo.json') (New-Geo 'geometry.complementaries.faucet' $fb)

$open = "q.block_state('complementaries:enabled') != q.block_state('complementaries:powered')"
$hasFluid = "(q.block_state('complementaries:kind') > 0 || q.block_state('complementaries:fluid') > 0)"
$conn = "q.block_state('complementaries:connected') == 1"
$fvis = [ordered]@{
    pipe    = "!($conn)"
    spout   = "!($conn)"
    stem    = "!($conn)"
    valve   = "!($conn)"
    stream  = "!($conn) && $open && $hasFluid"
    pipe_c  = $conn
    spout_c = $conn
    stem_c  = $conn
    valve_c = $conn
}
function Faucet-Materials([string]$streamTex) {
    [ordered]@{
        '*'    = @{ texture = 'cm_faucet'; render_method = 'alpha_test' }
        iron   = @{ texture = 'cm_faucet'; render_method = 'alpha_test' }
        valve  = @{ texture = 'cm_faucet_valve'; render_method = 'alpha_test' }
        stream = @{ texture = $streamTex; render_method = 'alpha_test' }
    }
}
$fPerms = @()
$rot = @{ 0 = 180; 1 = 90; 2 = 0; 3 = 270 }
foreach ($f in 0..3) {
    $fPerms += [ordered]@{ condition = "q.block_state('complementaries:facing') == $f"; components = [ordered]@{ 'minecraft:transformation' = @{ rotation = @(0, $rot[$f], 0) } } }
}
foreach ($k in $fluids.Keys | Sort-Object) {
    $comp = [ordered]@{ 'minecraft:material_instances' = (Faucet-Materials "cm_jar_fluid_$k") }
    if ($jarLight.ContainsKey([int]$k)) { $comp['minecraft:light_emission'] = [Math]::Max(1, [Math]::Min(7, [Math]::Floor($jarLight[[int]$k] / 2))) }
    $fPerms += [ordered]@{ condition = (Content-Cond $k); components = $comp }
}
$fPerms += [ordered]@{
    condition  = $conn
    components = [ordered]@{
        'minecraft:collision_box' = @{ origin = @(-3, 0, -3); size = @(6, 10, 11) }
        'minecraft:selection_box' = @{ origin = @(-3, 0, -3); size = @(6, 10, 11) }
    }
}
$faucet = [ordered]@{
    format_version    = '1.21.40'
    'minecraft:block' = [ordered]@{
        description  = [ordered]@{
            identifier    = 'complementaries:faucet'
            menu_category = @{ category = 'items' }
            states        = [ordered]@{
                'complementaries:facing'    = @{ values = @{ min = 0; max = 3 } }
                'complementaries:enabled'   = @{ values = @{ min = 0; max = 1 } }
                'complementaries:powered'   = @{ values = @{ min = 0; max = 1 } }
                'complementaries:connected' = @{ values = @{ min = 0; max = 1 } }
                'complementaries:kind'      = @{ values = @{ min = 0; max = 3 } }
                'complementaries:fluid'     = @{ values = @{ min = 0; max = 15 } }
            }
        }
        components   = [ordered]@{
            'minecraft:geometry'                  = [ordered]@{ identifier = 'geometry.complementaries.faucet'; bone_visibility = $fvis }
            'minecraft:material_instances'        = (Faucet-Materials 'cm_jar_fluid_1')
            'minecraft:collision_box'             = @{ origin = @(-3, 5, -3); size = @(6, 10, 11) }
            'minecraft:selection_box'             = @{ origin = @(-3, 5, -3); size = @(6, 10, 11) }
            'minecraft:destructible_by_mining'    = @{ seconds_to_destroy = 3 }
            'minecraft:destructible_by_explosion' = @{ explosion_resistance = 4.8 }
            'minecraft:light_dampening'           = 0
            'minecraft:map_color'                 = '#a7a7a7'
        }
        permutations = $fPerms
    }
}
Write-Json (Join-Path $bp 'blocks/faucet.json') $faucet

# ---- helper entity that draws a jar's mob --------------------------------------------------------
# kind -> name, geometry, texture, scale, animation (vanilla resources of the client)
$mobs = @{
    1  = @('bee', 'geometry.bee', 'textures/entity/bee/bee', 0.55, 'animation.bee.flying')
    2  = @('slime', 'geometry.slime', 'textures/entity/slime/slime', 0.7, $null)
    3  = @('magma_cube', 'geometry.magma_cube_v2', 'textures/entity/slime/magmacube_v2', 0.7, $null)
    4  = @('allay', 'geometry.allay', 'textures/entity/allay/allay', 0.7, $null)
    5  = @('frog_temperate', 'geometry.frog', 'textures/entity/frog/temperate_frog', 0.6, $null)
    6  = @('frog_cold', 'geometry.frog', 'textures/entity/frog/cold_frog', 0.6, $null)
    7  = @('frog_warm', 'geometry.frog', 'textures/entity/frog/warm_frog', 0.6, $null)
    8  = @('vex', 'geometry.vex', 'textures/entity/vex/vex', 0.3, $null)
    9  = @('silverfish', 'geometry.silverfish', 'textures/entity/silverfish', 0.6, $null)
    10 = @('endermite', 'geometry.endermite', 'textures/entity/endermite', 0.8, $null)
    11 = @('rabbit_brown', 'geometry.rabbit.baby', 'textures/entity/rabbit/rabbit_brown_baby', 1.0, $null)
    12 = @('rabbit_white', 'geometry.rabbit.baby', 'textures/entity/rabbit/rabbit_white_baby', 1.0, $null)
    13 = @('rabbit_black', 'geometry.rabbit.baby', 'textures/entity/rabbit/rabbit_black_baby', 1.0, $null)
    14 = @('rabbit_splotched', 'geometry.rabbit.baby', 'textures/entity/rabbit/rabbit_white_splotched_baby', 1.0, $null)
    15 = @('rabbit_gold', 'geometry.rabbit.baby', 'textures/entity/rabbit/rabbit_gold_baby', 1.0, $null)
    16 = @('rabbit_salt', 'geometry.rabbit.baby', 'textures/entity/rabbit/rabbit_salt_baby', 1.0, $null)
    20 = @('cod', 'geometry.cod', 'textures/entity/fish/cod', 0.6, 'animation.cod.swim')
    21 = @('salmon', 'geometry.salmon', 'textures/entity/fish/salmon', 0.5, 'animation.salmon.swim')
    22 = @('pufferfish', 'geometry.pufferfish.small', 'textures/entity/fish/pufferfish', 0.8, 'animation.pufferfish.swim')
    23 = @('tropical_fish', 'geometry.tropicalfish_a', 'textures/entity/fish/tropical_a', 0.7, 'animation.tropicalfish.swim')
    24 = @('axolotl_lucy', 'geometry.axolotl', 'textures/entity/axolotl/axolotl_lucy', 0.45, $null)
    25 = @('tadpole', 'geometry.tadpole', 'textures/entity/tadpole/tadpole', 1.0, $null)
    26 = @('axolotl_cyan', 'geometry.axolotl', 'textures/entity/axolotl/axolotl_cyan', 0.45, $null)
    27 = @('axolotl_gold', 'geometry.axolotl', 'textures/entity/axolotl/axolotl_gold', 0.45, $null)
    28 = @('axolotl_wild', 'geometry.axolotl', 'textures/entity/axolotl/axolotl_wild', 0.45, $null)
    29 = @('axolotl_blue', 'geometry.axolotl', 'textures/entity/axolotl/axolotl_blue', 0.45, $null)
}
$maxKind = 31

$events = [ordered]@{}
foreach ($k in $mobs.Keys | Sort-Object) { $events["complementaries:display_$k"] = @{ set_property = @{ 'complementaries:mob' = [int]$k } } }
$entity = [ordered]@{
    format_version     = '1.21.40'
    'minecraft:entity' = [ordered]@{
        description = [ordered]@{
            identifier     = 'complementaries:jar_mob'
            is_spawnable   = $false
            is_summonable  = $true
            properties     = @{ 'complementaries:mob' = [ordered]@{ type = 'int'; range = @(0, $maxKind); default = 0; client_sync = $true } }
        }
        components  = [ordered]@{
            'minecraft:type_family'          = @{ family = @('complementaries_jar_mob', 'inanimate') }
            'minecraft:collision_box'        = @{ width = 0.01; height = 0.01 }
            'minecraft:physics'              = @{ has_gravity = $false; has_collision = $false }
            'minecraft:pushable'             = @{ is_pushable = $false; is_pushable_by_piston = $false }
            'minecraft:damage_sensor'        = @{ triggers = @(@{ cause = 'all'; deals_damage = 'no' }) }
            'minecraft:health'               = @{ value = 1; max = 1 }
            'minecraft:knockback_resistance' = @{ value = 1.0 }
            'minecraft:fire_immune'          = @{}
            'minecraft:breathable'           = [ordered]@{ total_supply = 15; suffocate_time = 0; breathes_air = $true; breathes_water = $true; generates_bubbles = $false }
            'minecraft:persistent'           = @{}
            'minecraft:nameable'             = @{ always_show = $false; allow_name_tag_renaming = $false }
        }
        events      = $events
    }
}
Write-Json (Join-Path $bp 'entities/jar_mob.json') $entity

$q = "q.property('complementaries:mob')"
$geoMap = [ordered]@{ none = 'geometry.complementaries.jar_mob_none'; slime_outer = 'geometry.slime.armor' }
$texMap = [ordered]@{ none = 'textures/blocks/jar/item' }
$anims = [ordered]@{}
$animate = @()
$geoArr = @(); $texArr = @()
$scaleExpr = '1.0'
for ($k = $maxKind; $k -ge 0; $k--) {
    if ($mobs.Contains($k)) { $scaleExpr = "($q == $k ? $($mobs[$k][3]) : $scaleExpr)" }
}
for ($k = 0; $k -le $maxKind; $k++) {
    if ($mobs.Contains($k)) {
        $m = $mobs[$k]
        $geoMap[$m[0]] = $m[1]
        $texMap[$m[0]] = $m[2]
        $geoArr += "Geometry.$($m[0])"
        $texArr += "Texture.$($m[0])"
        if ($m[4]) { $anims["swim_$($m[0])"] = $m[4]; $animate += @{ "swim_$($m[0])" = "$q == $k" } }
    } else {
        $geoArr += 'Geometry.none'
        $texArr += 'Texture.none'
    }
}
$client = [ordered]@{
    format_version            = '1.10.0'
    'minecraft:client_entity' = [ordered]@{
        description = [ordered]@{
            identifier         = 'complementaries:jar_mob'
            materials          = @{ default = 'entity_alphatest'; outer = 'entity_alphablend' }
            textures           = $texMap
            geometry           = $geoMap
            animations         = $anims
            scripts            = [ordered]@{ scale = $scaleExpr; animate = $animate }
            render_controllers = @('controller.render.complementaries.jar_mob', @{ 'controller.render.complementaries.jar_mob.slime_outer' = "$q == 2" })
        }
    }
}
Write-Json (Join-Path $rp 'entity/complementaries_jar_mob.entity.json') $client

$rc = [ordered]@{
    format_version     = '1.8.0'
    render_controllers = [ordered]@{
        'controller.render.complementaries.jar_mob'             = [ordered]@{
            arrays    = [ordered]@{
                geometries = @{ 'Array.geo' = $geoArr }
                textures   = @{ 'Array.tex' = $texArr }
            }
            geometry  = "Array.geo[$q]"
            materials = @(@{ '*' = 'Material.default' })
            textures  = @("Array.tex[$q]")
        }
        'controller.render.complementaries.jar_mob.slime_outer' = [ordered]@{
            geometry  = 'Geometry.slime_outer'
            materials = @(@{ '*' = 'Material.outer' })
            textures  = @('Texture.slime')
        }
    }
}
Write-Json (Join-Path $rp 'render_controllers/complementaries_jar_mob.render_controllers.json') $rc

$none = [ordered]@{
    format_version       = '1.12.0'
    'minecraft:geometry' = @(
        [ordered]@{
            description = [ordered]@{ identifier = 'geometry.complementaries.jar_mob_none'; texture_width = 16; texture_height = 16 }
            bones       = @([ordered]@{ name = 'root'; pivot = @(0, 0, 0) })
        }
    )
}
Write-Json (Join-Path $rp 'models/entity/complementaries_jar_mob_none.geo.json') $none

Write-Host "jar assets written"
