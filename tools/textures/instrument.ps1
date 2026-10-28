# U6 `instrument`: textures, block geometries and block definitions of the globes, the wind vane, the clock
# block, the hourglass and the altimeter. Everything here is original work (pixel art drawn from code, our own
# globe map generator); nothing is taken from Supplementaries.
#
#   pwsh -NoProfile -File tools/textures/instrument.ps1
#
# Writes:
#   assets/pack/rp/textures/blocks/instrument/**.png, assets/pack/rp/textures/items/altimeter.png
#   assets/pack/rp/models/blocks/{globe,clock_block,hourglass,wind_vane}.geo.json
#   assets/pack/rp/terrain_texture.d/instrument.json
#   assets/pack/bp/blocks/{globe,globe_sepia,clock_block,hourglass,wind_vane}.json
#
# The C++ side (src/feature/instrument) relies on the state names and value meanings defined here (see
# docs/spec/instrument.md, section 1).

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lib.ps1"
[System.Threading.Thread]::CurrentThread.CurrentCulture = [System.Globalization.CultureInfo]::InvariantCulture

$Root = Resolve-Path "$PSScriptRoot/../.."
$Rp = Join-Path $Root 'assets/pack/rp'
$Bp = Join-Path $Root 'assets/pack/bp'
$TexDir = Join-Path $Rp 'textures/blocks/instrument'
$GlobeDir = Join-Path $TexDir 'globe'
New-Item -ItemType Directory -Force $TexDir, $GlobeDir, (Join-Path $Rp 'models/blocks'), (Join-Path $Bp 'blocks'), (Join-Path $Rp 'terrain_texture.d'), (Join-Path $Rp 'textures/items') | Out-Null

$Terrain = [ordered]@{}
function Add-Terrain([string]$name, [string]$path) { $script:Terrain[$name] = [ordered]@{ textures = $path } }

function Save-Json($obj, [string]$path, [switch]$Compress) {
    $json = $obj | ConvertTo-Json -Depth 64 -Compress:$Compress
    [IO.File]::WriteAllText($path, $json + "`n", [Text.UTF8Encoding]::new($false))
}

# ------------------------------------------------------------------------------------------------
# Deterministic random numbers (Park-Miller; exact in doubles)
# ------------------------------------------------------------------------------------------------
class Rng {
    [double]$s
    Rng([double]$seed) {
        $v = [Math]::Abs($seed) % 2147483646
        $this.s = $v + 1
    }
    [double] Next() {
        $this.s = ($this.s * 48271) % 2147483647
        return ($this.s - 1) / 2147483646
    }
    [double] Range([double]$a, [double]$b) { return $a + ($b - $a) * $this.Next() }
    [int] Int([int]$n) { return [int][Math]::Floor($this.Next() * $n) % $n }
    [double[]] UnitVector() {
        $z = $this.Range(-1, 1); $t = $this.Range(0, 2 * [Math]::PI); $r = [Math]::Sqrt(1 - $z * $z)
        return @(($r * [Math]::Cos($t)), $z, ($r * [Math]::Sin($t)))
    }
}

# ------------------------------------------------------------------------------------------------
# Globe maps. The sphere is an 8x8x8 cube with box UV at (0, 0) on a 32x16 texture:
#   up (8..16, 0..8), down (16..24, 0..8), east (0..8, 8..16), north (8..16, 8..16), west (16..24, 8..16),
#   south (24..32, 8..16). Each texel is projected onto the unit sphere and coloured from a seeded
#   "planet": continents are spherical caps with wavy borders, latitude decides ice / cold / hot land.
# Palette indices: 0 none, 1 water, 2 water shaded, 3 deep water, 4 shallow water, 5 land, 6 land shaded,
#   7 hot shaded, 8 hot, 9 cold, 10 cold shaded, 11 iceberg, 12 mushroom island.
# ------------------------------------------------------------------------------------------------
function Get-TexelDirection([int]$u, [int]$v) {
    $i = $u % 8 + 0.5; $j = $v % 8 + 0.5
    if ($v -lt 8) {
        if ($u -ge 8 -and $u -lt 16) { $p = @((-4 + $i), 4, (-4 + $j)) }       # up
        elseif ($u -ge 16 -and $u -lt 24) { $p = @((-4 + $i), -4, (4 - $j)) }  # down
        else { return $null }
    } else {
        $y = 4 - $j
        if ($u -lt 8) { $p = @(4, $y, (4 - $i)) }                                # east
        elseif ($u -lt 16) { $p = @((4 - $i), $y, -4) }                          # north
        elseif ($u -lt 24) { $p = @(-4, $y, (-4 + $i)) }                         # west
        else { $p = @((-4 + $i), $y, 4) }                                        # south
    }
    $len = [Math]::Sqrt($p[0] * $p[0] + $p[1] * $p[1] + $p[2] * $p[2])
    return @(($p[0] / $len), ($p[1] / $len), ($p[2] / $len))
}

function Get-Angle($a, $b) {
    $d = $a[0] * $b[0] + $a[1] * $b[1] + $a[2] * $b[2]
    return [Math]::Acos([Math]::Max([double]-1, [Math]::Min([double]1, [double]$d)))
}

function From-LatLon([double]$lat, [double]$lon) {
    $la = $lat * [Math]::PI / 180; $lo = $lon * [Math]::PI / 180
    return @(([Math]::Cos($la) * [Math]::Sin($lo)), [Math]::Sin($la), ([Math]::Cos($la) * [Math]::Cos($lo)))
}

# Returns a 32x16 array (rows of palette indices) for a planet description.
function New-PlanetMap($rng, $continents, [bool]$features) {
    $waves = @(for ($k = 0; $k -lt 4; $k++) { @{ f = $rng.UnitVector(); a = $rng.Range(0.04, 0.1); w = $rng.Range(2.5, 5.5); p = $rng.Range(0, 6.3) } })
    $polar = $rng.Range(60, 68)
    $deserts = @(for ($k = 0; $k -lt 5 + $rng.Int(4); $k++) { $c = $rng.UnitVector(); $c[1] *= 0.35; @{ c = $c; r = $rng.Range(0.2, 0.45) } })
    $map = New-Object 'int[,]' 32, 16
    $water = New-Object System.Collections.Generic.List[object]
    for ($v = 0; $v -lt 16; $v++) {
        for ($u = 0; $u -lt 32; $u++) {
            $d = Get-TexelDirection $u $v
            if ($null -eq $d) { $map[$u, $v] = 0; continue }
            $score = -10.0
            foreach ($c in $continents) { $score = [Math]::Max([double]$score, [double]($c.r - (Get-Angle $d $c.c))) }
            $wobble = 0.0
            foreach ($w in $waves) { $wobble += $w.a * [Math]::Sin($w.w * ($d[0] * $w.f[0] + $d[1] * $w.f[1] + $d[2] * $w.f[2]) * 3 + $w.p) }
            $score += $wobble
            $lat = [Math]::Asin($d[1]) * 180 / [Math]::PI
            $alat = [Math]::Abs($lat)
            $shade = $rng.Next() -lt 0.3
            if ($alat -gt $polar + 40 * $wobble) {
                $c = if ($shade) { 10 } else { 9 }
            } elseif ($score -gt 0) {
                if ($alat -gt 46 + 30 * $wobble) { $c = if ($shade) { 10 } else { 9 } }
                elseif ($alat -lt 34 -and @($deserts | Where-Object { (Get-Angle $d $_.c) -lt $_.r }).Count -gt 0) { $c = if ($shade) { 7 } else { 8 } }
                else { $c = if ($shade) { 6 } else { 5 } }
            } elseif ($score -gt -0.05) {
                $c = 4
            } elseif ($score -lt -0.5) {
                $c = 3
            } else {
                $c = if ($rng.Next() -lt 0.1) { 2 } else { 1 }
                $water.Add(@($u, $v, $alat, $score))
            }
            $map[$u, $v] = $c
        }
    }
    if ($features -and $water.Count -gt 0) {
        $bergs = 1 + $rng.Int(3); $tries = 0
        while ($bergs -gt 0 -and $tries -lt 200) {
            $t = $water[$rng.Int($water.Count)]; $tries++
            if ($t[2] -gt 40) { $map[$t[0], $t[1]] = 11; $bergs-- }
        }
        $shrooms = $rng.Int(3)
        for ($k = 0; $k -lt $shrooms; $k++) { $t = $water[$rng.Int($water.Count)]; $map[$t[0], $t[1]] = 12 }
    }
    return , $map
}

function New-SeededPlanet([int]$seed) {
    $rng = [Rng]::new(7919 * $seed + 104729)
    $n = 6 + $rng.Int(5)
    $continents = @(for ($k = 0; $k -lt $n; $k++) { @{ c = $rng.UnitVector(); r = $rng.Range(0.32, 0.72) } })
    return , (New-PlanetMap $rng $continents $true)
}

function New-EarthPlanet {
    $rng = [Rng]::new(1969)
    $spec = @(
        @(48, -100, 0.42), @(62, -115, 0.33), @(15, -90, 0.11), @(-12, -58, 0.38), @(-35, -65, 0.22),
        @(50, 15, 0.24), @(52, 90, 0.55), @(30, 78, 0.28), @(15, 103, 0.17), @(6, 20, 0.42), @(-20, 26, 0.28),
        @(23, 45, 0.14), @(-25, 134, 0.27), @(73, -40, 0.17), @(-84, 0, 0.42), @(-3, 116, 0.09), @(37, 138, 0.07),
        @(54, -3, 0.05), @(-19, 47, 0.05), @(65, 100, 0.4)
    )
    $continents = @(foreach ($s in $spec) { @{ c = (From-LatLon $s[0] $s[1]); r = $s[2] * 1.5 } })
    return , (New-PlanetMap $rng $continents $false)
}

$Palettes = @{
    ow     = @('#000000', '#3d63d4', '#3555bf', '#2a45a0', '#5a8fe2', '#55a13d', '#438533', '#c5a457', '#dcc47c', '#eef4f7', '#cddde8', '#f7fcff', '#9a7aa0')
    nether = @('#000000', '#d4581a', '#bd4b14', '#9e3c0e', '#ec8a26', '#7d2c2c', '#652222', '#5a4434', '#6f5642', '#4c4b52', '#3b3b42', '#f2c65c', '#a71d3a')
    end    = @('#000000', '#140e1f', '#100b19', '#0a0711', '#2d2142', '#dfe4a4', '#c9cd8c', '#a37aa5', '#b68db6', '#3a2a52', '#2c2040', '#dac4e8', '#7c4f90')
    sepia  = @('#000000', '#b89e6f', '#ab9163', '#9b8156', '#c7af80', '#7c5c37', '#6b4e2e', '#8e6b41', '#9f7c4d', '#dcc9a0', '#cbb78e', '#ead9b4', '#5d4427')
}

function Save-PaletteMap($map, $palette, [string]$path) {
    $pal = @{}
    $keys = '.abcdefghijkl'
    for ($k = 1; $k -lt 13; $k++) { $pal[[string]$keys[$k]] = $palette[$k] }
    $rows = for ($v = 0; $v -lt 16; $v++) { -join $(for ($u = 0; $u -lt 32; $u++) { $keys[$map[$u, $v]] }) }
    Save-PixelArt -Rows $rows -Palette $pal -Path $path
}

Write-Host 'globe maps...'
for ($m = 0; $m -lt 16; $m++) {
    $map = New-SeededPlanet $m
    foreach ($p in 'ow', 'nether', 'end', 'sepia') {
        Save-PaletteMap $map $Palettes[$p] (Join-Path $GlobeDir "map_${p}_$m.png")
        Add-Terrain "cm_globe_map_${p}_$m" "textures/blocks/instrument/globe/map_${p}_$m"
    }
}
$earth = New-EarthPlanet
Save-PaletteMap $earth $Palettes.ow (Join-Path $GlobeDir 'earth.png')
Save-PaletteMap $earth $Palettes.sepia (Join-Path $GlobeDir 'earth_sepia.png')
Add-Terrain 'cm_globe_earth' 'textures/blocks/instrument/globe/earth'
Add-Terrain 'cm_globe_earth_sepia' 'textures/blocks/instrument/globe/earth_sepia'

# Moon and sun: shaded from the same sphere projection.
function Save-SphereShade([string]$path, [scriptblock]$color) {
    $bmpRows = @(); $pal = @{}; $next = 0; $chars = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789'
    $lookup = @{}
    for ($v = 0; $v -lt 16; $v++) {
        $row = ''
        for ($u = 0; $u -lt 32; $u++) {
            $d = Get-TexelDirection $u $v
            if ($null -eq $d) { $row += '.'; continue }
            $hex = & $color $d $u $v
            if (-not $lookup.ContainsKey($hex)) { $lookup[$hex] = [string]$chars[$next]; $pal[[string]$chars[$next]] = $hex; $next++ }
            $row += $lookup[$hex]
        }
        $bmpRows += $row
    }
    Save-PixelArt -Rows $bmpRows -Palette $pal -Path $path
}
$moonRng = [Rng]::new(4242)
$craters = @(for ($k = 0; $k -lt 14; $k++) { @{ c = $moonRng.UnitVector(); r = $moonRng.Range(0.25, 0.55) } })
Save-SphereShade (Join-Path $GlobeDir 'moon.png') {
    param($d, $u, $v)
    $base = 0
    foreach ($c in $craters) { $a = Get-Angle $d $c.c; if ($a -lt $c.r) { $base = if ($a -gt $c.r * 0.72) { [Math]::Max($base, 2) } else { 1 } } }
    $n = $moonRng.Int(3)
    switch ($base) { 2 { @('#d2d2cc', '#c8c8c2', '#cdcdc7')[$n] } 1 { @('#86868a', '#7c7c80', '#8c8c90')[$n] } default { @('#acaca8', '#a2a29e', '#b4b4b0')[$n] } }
}
Save-SphereShade (Join-Path $GlobeDir 'sun.png') {
    param($d, $u, $v)
    $t = [Math]::Sin($d[0] * 9 + 1.3) * [Math]::Sin($d[1] * 7 + 0.4) * [Math]::Sin($d[2] * 8 + 2.1)
    if ($t -gt 0.25) { '#fff3a8' } elseif ($t -gt 0) { '#ffd64a' } elseif ($t -gt -0.3) { '#ffb52e' } else { '#f2861c' }
}
Add-Terrain 'cm_globe_moon' 'textures/blocks/instrument/globe/moon'
Add-Terrain 'cm_globe_sun' 'textures/blocks/instrument/globe/sun'

# Inside of a sheared globe: crust, mantle, core rings.
$coreRows = @(
    'bbbbbbbbbbbbbbbb', 'bccccccccccccccb', 'bcddddddddddddcb', 'bcdeeeeeeeeeedcb', 'bcdeffffffffedcb', 'bcdefggggggfedcb', 'bcdefghhhhgfedcb', 'bcdefghiihgfedcb',
    'bcdefghiihgfedcb', 'bcdefghhhhgfedcb', 'bcdefggggggfedcb', 'bcdeffffffffedcb', 'bcdeeeeeeeeeedcb', 'bcddddddddddddcb', 'bccccccccccccccb', 'bbbbbbbbbbbbbbbb')
Save-PixelArt -Rows $coreRows -Palette @{ b = '#55a13d'; c = '#7a5a3a'; d = '#6b4a2e'; e = '#b4421c'; f = '#d0581f'; g = '#ec7a22'; h = '#f7a52c'; i = '#ffe066' } -Path (Join-Path $GlobeDir 'core.png')
Save-PixelArt -Rows $coreRows -Palette @{ b = '#7c5c37'; c = '#8f6f48'; d = '#7f6240'; e = '#a17c4e'; f = '#b08a5a'; g = '#c09b69'; h = '#d0ae7c'; i = '#e4cb9c' } -Path (Join-Path $GlobeDir 'core_sepia.png')
Add-Terrain 'cm_globe_core' 'textures/blocks/instrument/globe/core'
Add-Terrain 'cm_globe_core_sepia' 'textures/blocks/instrument/globe/core_sepia'

# Stand: wooden base, brass meridian.
Save-PixelArt -Path (Join-Path $GlobeDir 'stand.png') -Palette @{ a = '#6b4a2b'; b = '#85603a'; c = '#9c7448'; d = '#c9a23e'; e = '#e3c25a'; f = '#a17f2a' } -Rows @(
    'abcbabcbabcbabcb', 'bcbabcbabcbabcba', 'cbabcbabcbabcbab', 'babcbabcbabcbabc', 'abcbabcbabcbabcb', 'bcbabcbabcbabcba', 'cbabcbabcbabcbab', 'babcbabcbabcbabc',
    'deedeedeedeedeed', 'eddeddeddeddedde', 'fdefdefdefdefdef', 'deedeedeedeedeed', 'eddeddeddeddedde', 'fdefdefdefdefdef', 'deedeedeedeedeed', 'eddeddeddeddedde')
Add-Terrain 'cm_globe_stand' 'textures/blocks/instrument/globe/stand'

# ------------------------------------------------------------------------------------------------
# Other block textures (16x16)
# ------------------------------------------------------------------------------------------------
Write-Host 'block textures...'
Save-PixelArt -Path (Join-Path $TexDir 'clock_face.png') -Palette @{ a = '#5e3f22'; b = '#7a5530'; c = '#efe6cf'; d = '#d9cfb4'; e = '#2b2b2b'; f = '#c9a23e' } -Rows @(
    'aaaaaaaaaaaaaaaa',
    'abbbbbbbbbbbbbba',
    'abbbbfcecfbbbbba',
    'abbbccccccccbbba',
    'abbcccccccccdbba',
    'abfccccccccccfba',
    'abccccccccccccba',
    'abecccccccccceba',
    'abecccccccccceba',
    'abccccccccccccba',
    'abfccccccccccfba',
    'abbdcccccccccbba',
    'abbbdcccccccbbba',
    'abbbbfdcedfbbbba',
    'abbbbbbbbbbbbbba',
    'aaaaaaaaaaaaaaaa')
Save-PixelArt -Path (Join-Path $TexDir 'clock_side.png') -Palette @{ a = '#5e3f22'; b = '#7a5530'; c = '#8a6239' } -Rows @(
    'aaaaaaaaaaaaaaaa', 'abbbbbbbbbbbbbba', 'abcccbcccbcccbba', 'abbbbbbbbbbbbbba', 'abccbcccbcccbcba', 'abbbbbbbbbbbbbba', 'abcccbcccbcccbba', 'abbbbbbbbbbbbbba',
    'abccbcccbcccbcba', 'abbbbbbbbbbbbbba', 'abcccbcccbcccbba', 'abbbbbbbbbbbbbba', 'abccbcccbcccbcba', 'abbbbbbbbbbbbbba', 'abbbbbbbbbbbbbba', 'aaaaaaaaaaaaaaaa')
Save-PixelArt -Path (Join-Path $TexDir 'clock_hands.png') -Palette @{ a = '#1d1d1f'; b = '#3a3a40' } -Rows @(
    @(for ($k = 0; $k -lt 16; $k++) { 'aaaaaaaabbbbbbbb' }))
Add-Terrain 'cm_clock_face' 'textures/blocks/instrument/clock_face'
Add-Terrain 'cm_clock_side' 'textures/blocks/instrument/clock_side'
Add-Terrain 'cm_clock_hands' 'textures/blocks/instrument/clock_hands'

Save-PixelArt -Path (Join-Path $TexDir 'hourglass_frame.png') -Palette @{ a = '#a7801f'; b = '#d6ae3c'; c = '#f2d76a'; d = '#7e5f12' } -Rows @(
    'bbbbbbbbbbbbbbbb', 'bccbccbccbccbccb', 'baabaabaabaabaab', 'dddddddddddddddd', 'bccbccbccbccbccb', 'baabaabaabaabaab', 'bbbbbbbbbbbbbbbb', 'dddddddddddddddd',
    'bbbbbbbbbbbbbbbb', 'bccbccbccbccbccb', 'baabaabaabaabaab', 'dddddddddddddddd', 'bccbccbccbccbccb', 'baabaabaabaabaab', 'bbbbbbbbbbbbbbbb', 'dddddddddddddddd')
Save-PixelArt -Path (Join-Path $TexDir 'hourglass_glass.png') -Palette @{ a = '#cfe7ef'; b = '#9fc6d4'; c = '#ffffff' } -Rows @(
    'aaaaaaaaaaaaaaaa', 'a.c............a', 'ac.............a', 'a..............a', 'a..............a', 'a..............a', 'a..............a', 'a..............b',
    'a..............b', 'a..............b', 'a..............b', 'a..............b', 'a..............b', 'a.............bb', 'a............b.b', 'abbbbbbbbbbbbbbb')
Add-Terrain 'cm_hourglass_frame' 'textures/blocks/instrument/hourglass_frame'
Add-Terrain 'cm_hourglass_glass' 'textures/blocks/instrument/hourglass_glass'

# Dust textures for the sand inside the hourglass (vanilla block textures are used for sand-like blocks).
function Save-Speckle([string]$name, [string[]]$colors, [int]$seed) {
    $r = [Rng]::new($seed); $pal = @{}; $keys = 'abcdef'
    for ($k = 0; $k -lt $colors.Count; $k++) { $pal[[string]$keys[$k]] = $colors[$k] }
    $rows = for ($y = 0; $y -lt 16; $y++) { -join $(for ($x = 0; $x -lt 16; $x++) { $keys[$r.Int($colors.Count)] }) }
    Save-PixelArt -Rows $rows -Palette $pal -Path (Join-Path $TexDir "$name.png")
    Add-Terrain "cm_$name" "textures/blocks/instrument/$name"
}
Save-Speckle 'hourglass_sugar' @('#f4f4f4', '#e6e6ea', '#ffffff', '#d9d9df') 11
Save-Speckle 'hourglass_ash' @('#8a8580', '#7a7570', '#9b9691', '#6c6762') 12
Save-Speckle 'hourglass_gunpowder' @('#5c5c5c', '#4a4a4a', '#6e6e6e', '#3c3c3c') 13
Save-Speckle 'hourglass_glowstone' @('#ffd36a', '#f2b746', '#fff0a6', '#d99a33') 14
Save-Speckle 'hourglass_blaze' @('#ffb12e', '#f28a16', '#ffd75a', '#d96c0f') 15
Save-Speckle 'hourglass_redstone' @('#c41a0e', '#9e1209', '#e8321e', '#7a0d06') 16
Save-Speckle 'hourglass_dust' @('#b8b2a6', '#a59f94', '#c9c3b6', '#968f84') 17
$VanillaDust = [ordered]@{
    hourglass_sand = 'textures/blocks/sand'; hourglass_red_sand = 'textures/blocks/red_sand'; hourglass_suspicious_sand = 'textures/blocks/suspicious_sand_0'
    hourglass_soul_sand = 'textures/blocks/soul_sand'; hourglass_slime = 'textures/blocks/slime'; hourglass_honey = 'textures/blocks/honey_top'
}
foreach ($k in $VanillaDust.Keys) { Add-Terrain "cm_$k" $VanillaDust[$k] }
$Concrete = @('white', 'orange', 'magenta', 'light_blue', 'yellow', 'lime', 'pink', 'gray', 'silver', 'cyan', 'purple', 'blue', 'brown', 'green', 'red', 'black')
foreach ($c in $Concrete) { Add-Terrain "cm_hourglass_concrete_$c" "textures/blocks/concrete_powder_$c" }

Save-PixelArt -Path (Join-Path $TexDir 'wind_vane.png') -Palette @{ a = '#2f2f33'; b = '#4a4a52'; c = '#6a6a74'; d = '#c9a23e' } -Rows @(
    'abcbabcbabcbabcb', 'bcbabcbabcbabcba', 'cbabcbabcbabcbab', 'babcbabcbabcbabc', 'abcbabcbabcbabcb', 'bcbabcbabcbabcba', 'cbabcbabcbabcbab', 'babcbabcbabcbabc',
    'dddddddddddddddd', 'dddddddddddddddd', 'abcbabcbabcbabcb', 'bcbabcbabcbabcba', 'cbabcbabcbabcbab', 'babcbabcbabcbabc', 'abcbabcbabcbabcb', 'bcbabcbabcbabcba')
# The rooster (11x11 in the top-left corner), facing +z on the east side of the plane.
Save-PixelArt -Path (Join-Path $TexDir 'wind_vane_rooster.png') -Palette @{ a = '#26262a'; b = '#3c3c44'; c = '#b8322a'; d = '#c9a23e' } -Rows @(
    '...cc...........',
    '..cccc..........',
    '..aaa...........',
    '.daaa...........',
    '..aaa.......b...',
    '..aaaa.....bb...',
    '..aaaaaaaabbb...',
    '...aaaaaaaab....',
    '....aaaaaaa.....',
    '.....a..a.......',
    '.aaaaaaaaaaa....',
    '................',
    '................',
    '................',
    '................',
    '................')
Add-Terrain 'cm_wind_vane' 'textures/blocks/instrument/wind_vane'
Add-Terrain 'cm_wind_vane_rooster' 'textures/blocks/instrument/wind_vane_rooster'

# Altimeter item: copper frame, glass window over a depth strip, red needle.
Save-PixelArt -Path (Join-Path $Rp 'textures/items/altimeter.png') -Palette @{ a = '#6d3a22'; b = '#b4673f'; c = '#e09a6a'; d = '#7fb5e8'; e = '#55a13d'; f = '#7a5a3a'; g = '#4c4c55'; h = '#c41a0e'; i = '#f4d16a' } -Rows @(
    '................',
    '......aaaa......',
    '.....abccba.....',
    '....abbbbbba....',
    '....abdddda.....',
    '....abdddda.....',
    '...habeeeeah....',
    '....abffffa.....',
    '....abffffa.....',
    '....abgggga.....',
    '....abgggga.....',
    '....abbbbbba....',
    '.....abccba.....',
    '......aiia......',
    '.......aa.......',
    '................')

Save-Json $Terrain (Join-Path $Rp 'terrain_texture.d/instrument.json')

# ------------------------------------------------------------------------------------------------
# Geometry helpers (pixel units; x -8..8, y 0..16, z -8..8; north = -z)
# ------------------------------------------------------------------------------------------------
function Rnd([double]$x) { return [Math]::Round($x, 4) }
function Face([double]$u, [double]$v, [double]$w, [double]$h, [string]$mat) {
    $f = [ordered]@{ uv = @((Rnd $u), (Rnd $v)); uv_size = @((Rnd $w), (Rnd $h)) }
    if ($mat) { $f.material_instance = $mat }
    return $f
}
function Cube($origin, $size, $faces) {
    return [ordered]@{ origin = @($origin | ForEach-Object { Rnd $_ }); size = @($size | ForEach-Object { Rnd $_ }); uv = $faces }
}
function Bone([string]$name, [string]$parent, $pivot, $rotation, $cubes) {
    $b = [ordered]@{ name = $name }
    if ($parent) { $b.parent = $parent }
    $b.pivot = @($pivot | ForEach-Object { Rnd $_ })
    if ($rotation) { $b.rotation = @($rotation | ForEach-Object { Rnd $_ }) }
    if ($cubes) { $b.cubes = @($cubes) }
    return $b
}
function Save-Geometry([string]$id, [int]$tw, [int]$th, $bones, [string]$file) {
    Save-Json ([ordered]@{
            format_version       = '1.12.0'
            'minecraft:geometry' = @([ordered]@{
                    description = [ordered]@{ identifier = $id; texture_width = $tw; texture_height = $th; visible_bounds_width = 2; visible_bounds_height = 2; visible_bounds_offset = @(0, 0.5, 0) }
                    bones       = @($bones)
                })
        }) (Join-Path $Rp "models/blocks/$file") -Compress
}
# Faces of a box using a texture region scaled to the face size (sx, sy: texture px per geometry px).
function Plain-Faces($size, [string]$mat, [double]$sx = 1, [double]$sy = 1, [string[]]$only = $null) {
    $w = $size[0]; $h = $size[1]; $d = $size[2]
    $all = [ordered]@{
        north = (Face 0 0 ($w * $sx) ($h * $sy) $mat); south = (Face 0 0 ($w * $sx) ($h * $sy) $mat)
        east = (Face 0 0 ($d * $sx) ($h * $sy) $mat); west = (Face 0 0 ($d * $sx) ($h * $sy) $mat)
        up = (Face 0 0 ($w * $sx) ($d * $sy) $mat); down = (Face 0 0 ($w * $sx) ($d * $sy) $mat)
    }
    if ($only) { $o = [ordered]@{}; foreach ($k in $only) { $o[$k] = $all[$k] }; return $o }
    return $all
}

# ------------------------------------------------------------------------------------------------
# Globe geometry: stand + 16 yaw steps of four sphere models (globe, flat, round, sheared)
# ------------------------------------------------------------------------------------------------
Write-Host 'geometry...'
$CY = 9  # sphere center height
# UV of the part (x0..x1, y0..y1, z0..z1; sphere-local -4..4) of a face of the 8x8x8 box UV at (0, 0).
function Sphere-Face([string]$face, $x0, $x1, $y0, $y1, $z0, $z1, [string]$mat = 'sphere') {
    switch ($face) {
        'north' { return Face (8 + 4 - $x1) (8 + 4 - $y1) ($x1 - $x0) ($y1 - $y0) $mat }
        'south' { return Face (24 + $x0 + 4) (8 + 4 - $y1) ($x1 - $x0) ($y1 - $y0) $mat }
        'east' { return Face (0 + 4 - $z1) (8 + 4 - $y1) ($z1 - $z0) ($y1 - $y0) $mat }
        'west' { return Face (16 + $z0 + 4) (8 + 4 - $y1) ($z1 - $z0) ($y1 - $y0) $mat }
        'up' { return Face (8 + $x0 + 4) ($z0 + 4) ($x1 - $x0) ($z1 - $z0) $mat }
        'down' { return Face (16 + $x0 + 4) (4 - $z0) ($x1 - $x0) (-($z1 - $z0)) $mat }
    }
}
function Sphere-Cube($x0, $x1, $y0, $y1, $z0, $z1, [string[]]$faces = @('north', 'south', 'east', 'west', 'up', 'down'), $override = @{}) {
    $f = [ordered]@{}
    foreach ($k in $faces) { $f[$k] = if ($override.ContainsKey($k)) { $override[$k] } else { Sphere-Face $k $x0 $x1 $y0 $y1 $z0 $z1 } }
    return Cube @($x0, ($CY + $y0), $z0) @(($x1 - $x0), ($y1 - $y0), ($z1 - $z0)) $f
}
$core = Face 0 0 32 16 'core'
$sphereModels = [ordered]@{
    globe   = { @(Sphere-Cube -4 4 -4 4 -4 4) }
    flat    = { @(Sphere-Cube 0 0 -4 4 -4 4 @('east', 'west') @{ east = (Sphere-Face 'east' 4 4 -4 4 -4 4); west = (Sphere-Face 'west' -4 -4 -4 4 -4 4) }) }
    round   = { @((Sphere-Cube -4 4 -3 3 -3 3), (Sphere-Cube -3 3 -3 3 -4 4), (Sphere-Cube -3 3 -4 4 -3 3)) }
    sheared = { @((Sphere-Cube -4 4 -4 4 -4 0 @('north', 'east', 'west', 'up', 'down', 'south') @{ south = $core }),
            (Sphere-Cube 0 4 -4 4 0 4 @('south', 'east', 'up', 'down', 'west') @{ west = $core })) }
}
# skin state: 0..3 globe, 4..7 flat, 8..11 round (palette = skin % 4: overworld, nether, end, earth), 12 moon,
# 13 sun, 14 sheared. Keep in sync with Globe.cpp.
$skinCond = @{
    globe   = "(q.block_state('complementaries:skin') <= 3 || q.block_state('complementaries:skin') == 12 || q.block_state('complementaries:skin') == 13)"
    flat    = "(q.block_state('complementaries:skin') >= 4 && q.block_state('complementaries:skin') <= 7)"
    round   = "(q.block_state('complementaries:skin') >= 8 && q.block_state('complementaries:skin') <= 11)"
    sheared = "q.block_state('complementaries:skin') == 14"
}
$S = 2 # the stand texture is 16 px wide on a 32 px wide geometry texture
$bones = @()
$bones += Bone 'stand' $null @(0, 0, 0) $null @(
    (Cube @(-4, 0, -4) @(8, 1, 8) ([ordered]@{ north = (Face 0 0 16 1 $null); south = (Face 0 0 16 1 $null); east = (Face 0 0 16 1 $null); west = (Face 0 0 16 1 $null); up = (Face 0 0 16 8 $null); down = (Face 0 0 16 8 $null) })),
    (Cube @(-1, 1, -1) @(2, 2.5, 2) (Plain-Faces @(2, 2.5, 2) $null $S 1)))
function Brass-Faces($size) {
    $o = [ordered]@{}
    foreach ($k in 'north', 'south', 'east', 'west', 'up', 'down') { $o[$k] = Face 0 8 ([Math]::Max(1, $size[0]) * $S) ([Math]::Max(1, $size[1])) $null }
    return $o
}
$bones += Bone 'meridian' $null @(0, $CY, 0) @(-22.5, 0, 0) @(
    (Cube @(-0.5, ($CY - 6.5), -0.5) @(1, 1, 1) (Brass-Faces @(1, 1, 1))),
    (Cube @(-0.5, ($CY + 5.5), -0.5) @(1, 1, 1) (Brass-Faces @(1, 1, 1))),
    (Cube @(-0.5, ($CY - 6.5), 5.5) @(1, 13, 1) (Brass-Faces @(1, 13, 1))),
    (Cube @(-0.5, ($CY - 6.5), 0.5) @(1, 1, 5) (Brass-Faces @(1, 1, 5))),
    (Cube @(-0.5, ($CY + 5.5), 0.5) @(1, 1, 5) (Brass-Faces @(1, 1, 5))))
$bones += Bone 'tilt' $null @(0, $CY, 0) @(-22.5, 0, 0) $null
$globeVis = [ordered]@{}
foreach ($model in $sphereModels.Keys) {
    for ($k = 0; $k -lt 16; $k++) {
        $name = "${model}_$k"
        $bones += Bone $name 'tilt' @(0, $CY, 0) @(0, ($k * 22.5), 0) (& $sphereModels[$model])
        $globeVis[$name] = "q.block_state('complementaries:angle') == $k && $($skinCond[$model])"
    }
}
Save-Geometry 'geometry.complementaries.globe' 32 16 $bones 'globe.geo.json'

# ------------------------------------------------------------------------------------------------
# Clock block: body (single / two-faced) + 12 hour and 12 minute hand positions per face (pixel lines)
# ------------------------------------------------------------------------------------------------
function Hand-Cubes([double]$deg, [double]$len, [bool]$back, [double]$u) {
    $t = $deg * [Math]::PI / 180
    $seen = @{}; $cubes = @()
    for ($s = 0.5; $s -le $len; $s += 0.75) {
        $x = [Math]::Round((-[Math]::Sin($t) * $s) * 2) / 2
        if ($back) { $x = -$x }
        $y = [Math]::Round((8 + [Math]::Cos($t) * $s) * 2) / 2
        $key = "$x,$y"
        if ($seen.ContainsKey($key)) { continue }
        $seen[$key] = 1
        if ($back) { $cubes += Cube @(($x - 0.5), ($y - 0.5), 7.0) @(1, 1, 0.1) ([ordered]@{ south = (Face $u 0 1 1 'hands') }) }
        else { $cubes += Cube @(($x - 0.5), ($y - 0.5), -7.1) @(1, 1, 0.1) ([ordered]@{ north = (Face $u 0 1 1 'hands') }) }
    }
    return $cubes
}
$bones = @()
$bones += Bone 'body_single' $null @(0, 0, 0) $null @(Cube @(-8, 0, -7) @(16, 16, 15) ([ordered]@{
            north = (Face 0 0 16 16 'dial'); south = (Face 0 0 16 16 $null); east = (Face 0 0 15 16 $null); west = (Face 0 0 15 16 $null); up = (Face 0 0 16 15 $null); down = (Face 0 0 16 15 $null) }))
$bones += Bone 'body_double' $null @(0, 0, 0) $null @(Cube @(-8, 0, -7) @(16, 16, 14) ([ordered]@{
            north = (Face 0 0 16 16 'dial'); south = (Face 0 0 16 16 'dial'); east = (Face 0 0 14 16 $null); west = (Face 0 0 14 16 $null); up = (Face 0 0 16 14 $null); down = (Face 0 0 16 14 $null) }))
$clockVis = [ordered]@{ body_single = "q.block_state('complementaries:two_faced') == 0"; body_double = "q.block_state('complementaries:two_faced') == 1" }
for ($i = 0; $i -lt 12; $i++) {
    foreach ($side in 'front', 'back') {
        $back = $side -eq 'back'
        $extra = if ($back) { " && q.block_state('complementaries:two_faced') == 1" } else { '' }
        $bones += Bone "hour_${side}_$i" $null @(0, 8, 0) $null (Hand-Cubes ($i * 30) 4 $back 0)
        $clockVis["hour_${side}_$i"] = "q.block_state('complementaries:hour') == $i$extra"
        $bones += Bone "minute_${side}_$i" $null @(0, 8, 0) $null (Hand-Cubes ($i * 30) 6 $back 8)
        $clockVis["minute_${side}_$i"] = "q.block_state('complementaries:minute') == $i$extra"
    }
}
Save-Geometry 'geometry.complementaries.clock_block' 16 16 $bones 'clock_block.geo.json'

# ------------------------------------------------------------------------------------------------
# Hourglass: gold frame, glass bulbs, 16 sand levels (level = fill of the lower bulb in 15ths)
# ------------------------------------------------------------------------------------------------
$bones = @()
$bones += Bone 'frame' $null @(0, 0, 0) $null @(
    (Cube @(-4, 0, -4) @(8, 2, 8) (Plain-Faces @(8, 2, 8) $null)),
    (Cube @(-4, 14, -4) @(8, 2, 8) (Plain-Faces @(8, 2, 8) $null)),
    (Cube @(-4, 2, -4) @(1, 12, 1) (Plain-Faces @(1, 12, 1) $null)),
    (Cube @(3, 2, -4) @(1, 12, 1) (Plain-Faces @(1, 12, 1) $null)),
    (Cube @(-4, 2, 3) @(1, 12, 1) (Plain-Faces @(1, 12, 1) $null)),
    (Cube @(3, 2, 3) @(1, 12, 1) (Plain-Faces @(1, 12, 1) $null)))
$bones += Bone 'glass' $null @(0, 0, 0) $null @(
    (Cube @(-3, 2, -3) @(6, 5, 6) (Plain-Faces @(6, 5, 6) 'glass' (16 / 6) (16 / 6))),
    (Cube @(-1, 7, -1) @(2, 2, 2) (Plain-Faces @(2, 2, 2) 'glass' 1 1 @('north', 'south', 'east', 'west'))),
    (Cube @(-3, 9, -3) @(6, 5, 6) (Plain-Faces @(6, 5, 6) 'glass' (16 / 6) (16 / 6))))
$hasSand = "(q.block_state('complementaries:dust') + q.block_state('complementaries:dust_page')) > 0"
$vertical = "(q.block_state('minecraft:block_face') == 'up' || q.block_state('minecraft:block_face') == 'down')"
$hourVis = [ordered]@{}
for ($l = 0; $l -le 15; $l++) {
    $f = $l / 15
    if ($l -gt 0) {
        $h = 4.5 * $f
        $bones += Bone "sand_low_$l" $null @(0, 0, 0) $null @(Cube @(-2.5, 2.05, -2.5) @(5, $h, 5) (Plain-Faces @(5, $h, 5) 'sand'))
        $hourVis["sand_low_$l"] = "q.block_state('complementaries:level') == $l && $hasSand"
    }
    if ($l -lt 15) {
        $h = 4.5 * (1 - $f)
        $bones += Bone "sand_high_$l" $null @(0, 0, 0) $null @(Cube @(-2.5, 9.05, -2.5) @(5, $h, 5) (Plain-Faces @(5, $h, 5) 'sand'))
        $hourVis["sand_high_$l"] = "q.block_state('complementaries:level') == $l && $hasSand"
        $top = 2.05 + 4.5 * $f
        $bones += Bone "stream_$l" $null @(0, 0, 0) $null @(Cube @(-0.5, $top, -0.5) @(1, (9.05 - $top), 1) (Plain-Faces @(1, (9.05 - $top), 1) 'sand' 1 1 @('north', 'south', 'east', 'west')))
        $hourVis["stream_$l"] = "q.block_state('complementaries:level') == $l && $hasSand && $vertical"
    }
}
Save-Geometry 'geometry.complementaries.hourglass' 16 16 $bones 'hourglass.geo.json'

# ------------------------------------------------------------------------------------------------
# Wind vane: pole and cross arms + the rooster at 64 yaw steps
# ------------------------------------------------------------------------------------------------
$bones = @()
$bones += Bone 'pole' $null @(0, 0, 0) $null @(
    (Cube @(-2, 0, -2) @(4, 1, 4) (Plain-Faces @(4, 1, 4) $null)),
    (Cube @(-0.5, 1, -0.5) @(1, 15, 1) (Plain-Faces @(1, 15, 1) $null)),
    (Cube @(-5, 3, -0.5) @(10, 1, 1) ([ordered]@{ north = (Face 0 8 10 1 $null); south = (Face 0 8 10 1 $null); up = (Face 0 8 10 1 $null); down = (Face 0 8 10 1 $null); east = (Face 0 8 1 1 $null); west = (Face 0 8 1 1 $null) })),
    (Cube @(-0.5, 3, -5) @(1, 1, 10) ([ordered]@{ east = (Face 0 8 10 1 $null); west = (Face 0 8 10 1 $null); up = (Face 0 8 1 10 $null); down = (Face 0 8 1 10 $null); north = (Face 0 8 1 1 $null); south = (Face 0 8 1 1 $null) })))
$vaneVis = [ordered]@{}
for ($k = 0; $k -lt 64; $k++) {
    $bones += Bone "vane_$k" $null @(0, 0, 0) @(0, ($k * 5.625), 0) @(Cube @(0, 5, -5.5) @(0, 11, 11) ([ordered]@{ east = (Face 0 0 11 11 'rooster'); west = (Face 11 0 -11 11 'rooster') }))
    $vaneVis["vane_$k"] = "q.block_state('complementaries:yaw') * 4 + q.block_state('complementaries:yaw_fine') == $k"
}
Save-Geometry 'geometry.complementaries.wind_vane' 16 16 $bones 'wind_vane.geo.json'

# ------------------------------------------------------------------------------------------------
# Block definitions
# ------------------------------------------------------------------------------------------------
Write-Host 'blocks...'
function Mat([string]$tex, [string]$method = 'alpha_test') { return [ordered]@{ texture = $tex; render_method = $method } }
function Perm([string]$cond, $components) { return [ordered]@{ condition = $cond; components = $components } }
function Cardinal-Perms {
    $rot = [ordered]@{ north = 0; west = 90; south = 180; east = -90 }
    return @(foreach ($d in $rot.Keys) { Perm "q.block_state('minecraft:cardinal_direction') == '$d'" ([ordered]@{ 'minecraft:transformation' = [ordered]@{ rotation = @(0, $rot[$d], 0) } }) })
}
function Block-Json([string]$id, $states, $traits, $components, $perms) {
    $desc = [ordered]@{ identifier = $id; menu_category = [ordered]@{ category = 'items' }; states = $states }
    if ($traits) { $desc.traits = $traits }
    return [ordered]@{ format_version = '1.26.20'; 'minecraft:block' = [ordered]@{ description = $desc; components = $components; permutations = @($perms) } }
}
function Range-State([int]$max) { return [ordered]@{ values = [ordered]@{ min = 0; max = $max } } }
$cardinalTrait = [ordered]@{ 'minecraft:placement_direction' = [ordered]@{ enabled_states = @('minecraft:cardinal_direction'); y_rotation_offset = 180 } }
$water = [ordered]@{ detection_rules = @([ordered]@{ liquid_type = 'water'; can_contain_liquid = $true }) }
$immovable = [ordered]@{ movement_type = 'immovable' }

function Globe-Mats([string]$sphere, [string]$coreTex) { return [ordered]@{ '*' = (Mat 'cm_globe_stand'); sphere = (Mat $sphere); core = (Mat $coreTex) } }
foreach ($sepia in $false, $true) {
    $id = if ($sepia) { 'globe_sepia' } else { 'globe' }
    $suffix = if ($sepia) { '_sepia' } else { '' }
    $perms = @(Cardinal-Perms)
    $palNames = @('ow', 'nether', 'end')
    for ($p = 0; $p -lt 3; $p++) {
        $skins = "(q.block_state('complementaries:skin') == $p || q.block_state('complementaries:skin') == $($p + 4) || q.block_state('complementaries:skin') == $($p + 8))"
        for ($m = 0; $m -lt 16; $m++) {
            $tex = if ($sepia) { "cm_globe_map_sepia_$m" } else { "cm_globe_map_$($palNames[$p])_$m" }
            $perms += Perm "$skins && q.block_state('complementaries:map') == $m" ([ordered]@{ 'minecraft:material_instances' = (Globe-Mats $tex "cm_globe_core$suffix") })
        }
    }
    $perms += Perm "(q.block_state('complementaries:skin') == 3 || q.block_state('complementaries:skin') == 7 || q.block_state('complementaries:skin') == 11 || q.block_state('complementaries:skin') == 14)" ([ordered]@{ 'minecraft:material_instances' = (Globe-Mats "cm_globe_earth$suffix" "cm_globe_core$suffix") })
    $perms += Perm "q.block_state('complementaries:skin') == 12" ([ordered]@{ 'minecraft:material_instances' = (Globe-Mats 'cm_globe_moon' "cm_globe_core$suffix") })
    $perms += Perm "q.block_state('complementaries:skin') == 13" ([ordered]@{ 'minecraft:material_instances' = (Globe-Mats 'cm_globe_sun' "cm_globe_core$suffix") })
    $base = if ($sepia) { 'cm_globe_map_sepia_0' } else { 'cm_globe_map_ow_0' }
    $components = [ordered]@{
        'minecraft:geometry'                  = [ordered]@{ identifier = 'geometry.complementaries.globe'; bone_visibility = $globeVis }
        'minecraft:material_instances'        = (Globe-Mats $base "cm_globe_core$suffix")
        'minecraft:collision_box'             = [ordered]@{ origin = @(-6, 0, -6); size = @(12, 15, 12) }
        'minecraft:selection_box'             = [ordered]@{ origin = @(-6, 0, -6); size = @(12, 15, 12) }
        'minecraft:destructible_by_mining'    = [ordered]@{ seconds_to_destroy = 0.75 }
        'minecraft:destructible_by_explosion' = [ordered]@{ explosion_resistance = 2 }
        'minecraft:light_dampening'           = 0
        'minecraft:map_color'                 = $(if ($sepia) { '#9b8156' } else { '#3d63d4' })
        'minecraft:loot'                      = "loot_tables/blocks/$id.json"
        'minecraft:redstone_consumer'         = [ordered]@{ min_power = 1 }
        'minecraft:liquid_detection'          = $water
        'minecraft:movable'                   = $immovable
    }
    $states = [ordered]@{ 'complementaries:angle' = (Range-State 15); 'complementaries:skin' = (Range-State 14); 'complementaries:map' = (Range-State 15) }
    Save-Json (Block-Json "complementaries:$id" $states $cardinalTrait $components $perms) (Join-Path $Bp "blocks/$id.json")
}

# Clock block
$perms = @(Cardinal-Perms)
$perms += Perm "q.block_state('complementaries:two_faced') == 1" ([ordered]@{
        'minecraft:collision_box' = [ordered]@{ origin = @(-8, 0, -7); size = @(16, 16, 14) }
        'minecraft:selection_box' = [ordered]@{ origin = @(-8, 0, -7); size = @(16, 16, 14) }
        'minecraft:loot'          = 'loot_tables/blocks/clock_block_two_faced.json'
    })
$components = [ordered]@{
    'minecraft:geometry'                  = [ordered]@{ identifier = 'geometry.complementaries.clock_block'; bone_visibility = $clockVis }
    'minecraft:material_instances'        = [ordered]@{ '*' = (Mat 'cm_clock_side' 'opaque'); dial = (Mat 'cm_clock_face' 'opaque'); hands = (Mat 'cm_clock_hands' 'opaque') }
    'minecraft:collision_box'             = [ordered]@{ origin = @(-8, 0, -7); size = @(16, 16, 15) }
    'minecraft:selection_box'             = [ordered]@{ origin = @(-8, 0, -7); size = @(16, 16, 15) }
    'minecraft:destructible_by_mining'    = [ordered]@{ seconds_to_destroy = 1.5 }
    'minecraft:destructible_by_explosion' = [ordered]@{ explosion_resistance = 3 }
    'minecraft:light_dampening'           = 0
    'minecraft:map_color'                 = '#7a5530'
    'minecraft:loot'                      = 'loot_tables/blocks/clock_block.json'
    'minecraft:flammable'                 = [ordered]@{ catch_chance_modifier = 5; destroy_chance_modifier = 20 }
    'minecraft:liquid_detection'          = $water
    'minecraft:movable'                   = $immovable
}
$states = [ordered]@{ 'complementaries:hour' = (Range-State 11); 'complementaries:minute' = (Range-State 11); 'complementaries:two_faced' = (Range-State 1) }
Save-Json (Block-Json 'complementaries:clock_block' $states $cardinalTrait $components $perms) (Join-Path $Bp 'blocks/clock_block.json')

# Hourglass: dust index = dust_page * 16 + dust (0 = empty). Keep in sync with Hourglass.cpp.
$DustTextures = @('cm_hourglass_sand', 'cm_hourglass_sand', 'cm_hourglass_red_sand', 'cm_hourglass_suspicious_sand', 'cm_hourglass_sugar', 'cm_hourglass_ash',
    'cm_hourglass_gunpowder', 'cm_hourglass_glowstone', 'cm_hourglass_soul_sand', 'cm_hourglass_blaze', 'cm_hourglass_redstone', 'cm_hourglass_slime',
    'cm_hourglass_honey', 'cm_hourglass_dust', 'cm_hourglass_dust', 'cm_hourglass_dust') + @($Concrete | ForEach-Object { "cm_hourglass_concrete_$_" })
$DustLight = @{ 7 = 10; 9 = 6; 10 = 4 }
$perms = @()
$faceRot = [ordered]@{ north = @(90, 0, 0); south = @(90, 0, 0); east = @(0, 0, 90); west = @(0, 0, 90) }
foreach ($d in $faceRot.Keys) { $perms += Perm "q.block_state('minecraft:block_face') == '$d'" ([ordered]@{ 'minecraft:transformation' = [ordered]@{ rotation = $faceRot[$d] } }) }
for ($i = 1; $i -lt 32; $i++) {
    $comp = [ordered]@{ 'minecraft:material_instances' = [ordered]@{ '*' = (Mat 'cm_hourglass_frame'); glass = (Mat 'cm_hourglass_glass'); sand = (Mat $DustTextures[$i]) } }
    if ($DustLight.ContainsKey($i)) { $comp['minecraft:light_emission'] = $DustLight[$i] }
    $perms += Perm "q.block_state('complementaries:dust_page') == $([Math]::Floor($i / 16)) && q.block_state('complementaries:dust') == $($i % 16)" $comp
}
$components = [ordered]@{
    'minecraft:geometry'                  = [ordered]@{ identifier = 'geometry.complementaries.hourglass'; bone_visibility = $hourVis }
    'minecraft:material_instances'        = [ordered]@{ '*' = (Mat 'cm_hourglass_frame'); glass = (Mat 'cm_hourglass_glass'); sand = (Mat 'cm_hourglass_sand') }
    'minecraft:collision_box'             = [ordered]@{ origin = @(-4, 0, -4); size = @(8, 16, 8) }
    'minecraft:selection_box'             = [ordered]@{ origin = @(-4, 0, -4); size = @(8, 16, 8) }
    'minecraft:destructible_by_mining'    = [ordered]@{ seconds_to_destroy = 0.5 }
    'minecraft:destructible_by_explosion' = [ordered]@{ explosion_resistance = 1 }
    'minecraft:light_dampening'           = 0
    'minecraft:map_color'                 = '#d6ae3c'
    'minecraft:loot'                      = 'loot_tables/blocks/hourglass.json'
    'minecraft:liquid_detection'          = $water
    'minecraft:movable'                   = $immovable
}
$states = [ordered]@{ 'complementaries:level' = (Range-State 15); 'complementaries:dust' = (Range-State 15); 'complementaries:dust_page' = (Range-State 1) }
$traits = [ordered]@{ 'minecraft:placement_position' = [ordered]@{ enabled_states = @('minecraft:block_face') } }
Save-Json (Block-Json 'complementaries:hourglass' $states $traits $components $perms) (Join-Path $Bp 'blocks/hourglass.json')

# Wind vane
$perms = @()
for ($w = 1; $w -le 3; $w++) {
    $perms += Perm "q.block_state('complementaries:wind') == $w" ([ordered]@{ 'minecraft:redstone_producer' = [ordered]@{ power = $w; strongly_powered_face = 'down'; connected_faces = @('down', 'up', 'north', 'south', 'east', 'west') } })
}
$components = [ordered]@{
    'minecraft:geometry'                  = [ordered]@{ identifier = 'geometry.complementaries.wind_vane'; bone_visibility = $vaneVis }
    'minecraft:material_instances'        = [ordered]@{ '*' = (Mat 'cm_wind_vane'); rooster = (Mat 'cm_wind_vane_rooster') }
    'minecraft:collision_box'             = [ordered]@{ origin = @(-6, 0, -6); size = @(12, 16, 12) }
    'minecraft:selection_box'             = [ordered]@{ origin = @(-6, 0, -6); size = @(12, 16, 12) }
    'minecraft:destructible_by_mining'    = [ordered]@{ seconds_to_destroy = 2.5 }
    'minecraft:destructible_by_explosion' = [ordered]@{ explosion_resistance = 6 }
    'minecraft:light_dampening'           = 0
    'minecraft:map_color'                 = '#4a4a52'
    'minecraft:loot'                      = 'loot_tables/blocks/wind_vane.json'
    'minecraft:redstone_producer'         = [ordered]@{ power = 0; strongly_powered_face = 'down'; connected_faces = @('down', 'up', 'north', 'south', 'east', 'west') }
    'minecraft:liquid_detection'          = $water
    'minecraft:movable'                   = $immovable
}
$states = [ordered]@{ 'complementaries:wind' = (Range-State 3); 'complementaries:yaw' = (Range-State 15); 'complementaries:yaw_fine' = (Range-State 3) }
Save-Json (Block-Json 'complementaries:wind_vane' $states $null $components $perms) (Join-Path $Bp 'blocks/wind_vane.json')

Write-Host 'done'
