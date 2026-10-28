# U14 flag: draws the original pixel art of flags, awnings, buntings and the flag pattern atlases,
# and writes the data-driven pack files of the unit (blocks, items, recipes, loot tables, helper
# entity, render controllers, animations, attachables, fragments).
#
#   pwsh -NoProfile -File tools/textures/flag.ps1
#
# Everything here is drawn/defined from scratch (no Supplementaries assets).

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lib.ps1"

$root = Resolve-Path "$PSScriptRoot/../.."
$bp = Join-Path $root 'assets/pack/bp'
$rp = Join-Path $root 'assets/pack/rp'

# ---- colours (Java dye order) -------------------------------------------------------------------
$Colors = [ordered]@{
    white = '#F9FFFE'; orange = '#F9801D'; magenta = '#C74EBD'; light_blue = '#3AB3DA'
    yellow = '#FED83D'; lime = '#80C71F'; pink = '#F38BAA'; gray = '#474F52'
    light_gray = '#9D9D97'; cyan = '#169C9C'; purple = '#8932B8'; blue = '#3C44AA'
    brown = '#835432'; green = '#5E7C16'; red = '#B02E26'; black = '#1D1D21'
}
$ColorNames = @($Colors.Keys)
$EnNames = @('White', 'Orange', 'Magenta', 'Light Blue', 'Yellow', 'Lime', 'Pink', 'Gray', 'Light Gray', 'Cyan', 'Purple', 'Blue', 'Brown', 'Green', 'Red', 'Black')
$ZhNames = @('白色', '橙色', '品红色', '淡蓝色', '黄色', '黄绿色', '粉红色', '灰色', '淡灰色', '青色', '紫色', '蓝色', '棕色', '绿色', '红色', '黑色')

function Write-Json($path, $obj) {
    $dir = Split-Path -Parent $path
    New-Item -ItemType Directory -Force $dir | Out-Null
    $json = $obj | ConvertTo-Json -Depth 40
    [IO.File]::WriteAllText($path, $json + "`n", [Text.UTF8Encoding]::new($false))
}

function New-Bitmap([int]$w, [int]$h) {
    return New-Object System.Drawing.Bitmap $w, $h, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
}
function Save-Bitmap($bmp, $path) {
    New-Item -ItemType Directory -Force (Split-Path -Parent $path) | Out-Null
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}
# Deterministic per-pixel noise in [-1, 1].
function Noise([int]$x, [int]$y, [int]$seed = 0) {
    $n = ($x * 374761393 + $y * 668265263 + $seed * 362437) -band 0x7fffffff
    $n = (($n -bxor ($n -shr 13)) * 1274126177) -band 0x7fffffff
    return (($n -band 0xff) / 127.5) - 1
}

# ---- banner patterns ----------------------------------------------------------------------------
# Bedrock pattern codes (index = position + 1 in the entity property), English and Chinese names.
$Patterns = @(
    @('bs', 'Base', '底部横条'), @('ts', 'Chief', '顶部横条'), @('ls', 'Pale Dexter', '左侧竖条'),
    @('rs', 'Pale Sinister', '右侧竖条'), @('cs', 'Pale', '中竖条'), @('ms', 'Fess', '中横条'),
    @('drs', 'Bend', '右斜条'), @('dls', 'Bend Sinister', '左斜条'), @('ss', 'Paly', '竖条纹'),
    @('cr', 'Saltire', '斜十字'), @('sc', 'Cross', '正十字'), @('bt', 'Chevron', '底部三角'),
    @('tt', 'Inverted Chevron', '顶部三角'), @('bts', 'Base Indented', '底部波纹'), @('tts', 'Chief Indented', '顶部波纹'),
    @('ld', 'Per Bend Sinister', '左上斜分'), @('rd', 'Per Bend Sinister Inverted', '右下斜分'), @('lud', 'Per Bend Inverted', '左下斜分'),
    @('rud', 'Per Bend', '右上斜分'), @('mc', 'Roundel', '圆形'), @('mr', 'Lozenge', '菱形'),
    @('vh', 'Per Pale', '左半边'), @('hh', 'Per Fess', '上半部分'), @('vhr', 'Per Pale Inverted', '右半边'),
    @('hhb', 'Per Fess Inverted', '下半部分'), @('bo', 'Bordure', '方框边'), @('cbo', 'Bordure Indented', '波纹边'),
    @('gra', 'Gradient', '渐变'), @('gru', 'Base Gradient', '底部渐变'), @('bri', 'Field Masoned', '砖纹'),
    @('glb', 'Globe', '地球'), @('cre', 'Creeper Charge', '苦力怕盾徽'), @('sku', 'Skull Charge', '头颅盾徽'),
    @('flo', 'Flower Charge', '花朵盾徽'), @('moj', 'Thing', 'Mojang徽标'), @('pig', 'Snout', '猪鼻'),
    @('bl', 'Base Dexter Canton', '左下方'), @('br', 'Base Sinister Canton', '右下方'), @('tl', 'Chief Dexter Canton', '左上方'),
    @('tr', 'Chief Sinister Canton', '右上方'), @('flw', 'Flow', '涡流'), @('gus', 'Guster', '旋风'),
    @('dragon', 'Dragon Charge', '龙形盾徽')
)

# 10x10 charges, drawn upright (x right, y down). '#' = pattern pixel.
$Charges = @{
    cre = @('..........', '.##....##.', '.##....##.', '..........', '....##....', '...####...', '...####...', '...#..#...', '..........', '..........')
    sku = @('..######..', '.########.', '.##.##.##.', '.#..##..#.', '.########.', '..##..##..', '..######..', '..#.##.#..', '...#..#...', '..........')
    flo = @('....##....', '...####...', '.##.##.##.', '##########', '.###..###.', '.###..###.', '##########', '.##.##.##.', '...####...', '....##....')
    moj = @('..........', '..####....', '.#....#...', '.#.##..##.', '.#.#.#.#..', '.#....#...', '..####....', '..........', '..........', '..........')
    glb = @('...####...', '..#.##.#..', '.##.#..##.', '#.##..#.##', '#..##.##.#', '##.#..#..#', '#.##.##.##', '.#..#.##..', '..##.#.#..', '...####...')
    pig = @('..........', '..........', '.########.', '#.#....#.#', '#.#....#.#', '#........#', '.########.', '..........', '..........', '..........')
    flw = @('..######..', '.#......#.', '#..####..#', '#.#....#.#', '#.#.##.#.#', '#.#..#.#.#', '#..##..#.#', '.#.....#..', '..######..', '..........')
    gus = @('..........', '.#######..', '........#.', '..#####..#', '.#.....#.#', '.#..##.#.#', '.#...#...#', '..###...#.', '.......#..', '..#####...')
    dragon = @('..#....#..', '.##....##.', '.########.', '##..##..##', '##########', '.########.', '..##..##..', '..#.##.#..', '...####...', '..........')
}

# Pattern mask in banner space: u in [0,1) left to right, v in [0,1) top to bottom (banner 20x40).
function Test-Pattern([string]$code, [double]$u, [double]$v) {
    $bx = $u * 20; $by = $v * 40
    switch ($code) {
        'bs' { return $v -ge 0.67 }
        'ts' { return $v -lt 0.33 }
        'ls' { return $u -lt 0.3 }
        'rs' { return $u -ge 0.7 }
        'cs' { return $u -ge 0.35 -and $u -lt 0.65 }
        'ms' { return $v -ge 0.4 -and $v -lt 0.6 }
        'drs' { return [Math]::Abs($u - $v) -lt 0.16 }
        'dls' { return [Math]::Abs($u + $v - 1) -lt 0.16 }
        'ss' { return ([Math]::Floor($u * 9) % 2) -eq 1 }
        'cr' { return ([Math]::Abs($u - $v) -lt 0.12) -or ([Math]::Abs($u + $v - 1) -lt 0.12) }
        'sc' { return ($u -ge 0.4 -and $u -lt 0.6) -or ($v -ge 0.42 -and $v -lt 0.58) }
        'bt' { return $v -ge (1 - (0.5 - [Math]::Abs($u - 0.5)) * 0.7) }
        'tt' { return $v -lt ((0.5 - [Math]::Abs($u - 0.5)) * 0.7) }
        'bts' { $l = ($u * 4) % 1; return $v -ge (0.82 + [Math]::Abs($l - 0.5) * 0.2) }
        'tts' { $l = ($u * 4) % 1; return $v -lt (0.18 - [Math]::Abs($l - 0.5) * 0.2) }
        'ld' { return ($u + $v) -lt 1 }
        'rd' { return ($u + $v) -ge 1 }
        'lud' { return $u -lt $v }
        'rud' { return $u -ge $v }
        'mc' { $dx = $bx - 10; $dy = ($by - 20) / 2; return ($dx * $dx + $dy * $dy) -lt 20 }
        'mr' { return ([Math]::Abs($bx - 10) / 6 + [Math]::Abs($by - 20) / 12) -le 1 }
        'vh' { return $u -lt 0.5 }
        'vhr' { return $u -ge 0.5 }
        'hh' { return $v -lt 0.5 }
        'hhb' { return $v -ge 0.5 }
        'bo' { return $bx -lt 1.6 -or $bx -ge 18.4 -or $by -lt 2.4 -or $by -ge 37.6 }
        'cbo' {
            $w = 1.6 + ((([Math]::Floor($bx) + [Math]::Floor($by)) % 2) * 1.2)
            return $bx -lt $w -or $bx -ge (20 - $w) -or $by -lt ($w * 1.5) -or $by -ge (40 - $w * 1.5)
        }
        'gra' { return (Noise ([int]$bx) ([int]$by) 7) * 0.5 + 0.5 -lt (1 - $v) }
        'gru' { return (Noise ([int]$bx) ([int]$by) 9) * 0.5 + 0.5 -lt $v }
        'bri' {
            $row = [Math]::Floor($by / 4); $inRow = $by - $row * 4
            $shift = if ($row % 2) { 2.5 } else { 0 }
            return $inRow -lt 0.9 -or ((($bx + $shift) % 5) -lt 0.9)
        }
        'bl' { return $u -lt 0.5 -and $v -ge 0.67 }
        'br' { return $u -ge 0.5 -and $v -ge 0.67 }
        'tl' { return $u -lt 0.5 -and $v -lt 0.33 }
        'tr' { return $u -ge 0.5 -and $v -lt 0.33 }
        default { return $false }
    }
}

# Flag texture (one colour row = 32x16, cloth = x 0..23, y 0..15). Geometric patterns follow the
# banner rotated so its top edge lies along the pole; charges are drawn upright in the middle.
function Get-FlagMask([string]$code) {
    $mask = New-Object 'bool[,]' 24, 16
    for ($x = 0; $x -lt 24; $x++) {
        for ($y = 0; $y -lt 16; $y++) {
            if ($Charges.ContainsKey($code)) {
                $cx = $x - 7; $cy = $y - 3
                $mask[$x, $y] = ($cx -ge 0 -and $cx -lt 10 -and $cy -ge 0 -and $cy -lt 10 -and $Charges[$code][$cy][$cx] -eq '#')
            } else {
                $u = 1 - ($y + 0.5) / 16; $v = ($x + 0.5) / 24
                $mask[$x, $y] = Test-Pattern $code $u $v
            }
        }
    }
    return , $mask
}

# One 32x16 texture per colour (cloth = x 0..23); `$mask` = $null draws the base cloth.
function Save-FlagTextures([string]$prefix, $mask) {
    for ($row = 0; $row -lt 16; $row++) {
        $bmp = New-Bitmap 32 16
        $col = ConvertTo-Color $Colors[$ColorNames[$row]]
        for ($x = 0; $x -lt 24; $x++) {
            for ($y = 0; $y -lt 16; $y++) {
                if ($mask -and -not $mask[$x, $y]) { continue }
                $shade = 1 + (Noise $x $y 3) * 0.035
                if (-not $mask) {
                    if ($y -eq 0 -or $y -eq 15) { $shade -= 0.08 }
                    if ($x -eq 23) { $shade -= 0.06 }
                    if ($x -lt 1) { $shade -= 0.1 }
                }
                $bmp.SetPixel($x, $y, (Shade-Color $col $shade))
            }
        }
        Save-Bitmap $bmp "$($prefix)_$($ColorNames[$row]).png"
    }
}

Write-Host 'flag pattern textures...'
$flagTexDir = Join-Path $rp 'textures/entity/flag'
Get-ChildItem $flagTexDir -Filter '*.png' -ErrorAction SilentlyContinue | Where-Object { $_.Name -like 'pattern_*' -or $_.Name -eq 'base.png' } | Remove-Item
Save-FlagTextures (Join-Path $flagTexDir 'base') $null
foreach ($p in $Patterns) {
    $m = Get-FlagMask $p[0]
    Save-FlagTextures (Join-Path $flagTexDir "p/$($p[0])") $m
}

# ---- block / item textures ----------------------------------------------------------------------
$wood = @{ o = '#4f3a20'; w = '#9c7a48'; l = '#b9945c'; d = '#6e5431' }

# Flag pole (block texture, 16x16; the pole uses x 7..8).
Save-PixelArt -Path (Join-Path $rp 'textures/blocks/flag_pole.png') -Palette $wood -Rows @(
    'dlwwdlwwdlwwdlww', 'dlwwdlwwdlwwdlww', 'dwwldwwldwwldwwl', 'dlwwdlwwdlwwdlww',
    'dwlwdwlwdwlwdwlw', 'dlwwdlwwdlwwdlww', 'dwwldwwldwwldwwl', 'dlwwdlwwdlwwdlww',
    'dlwwdlwwdlwwdlww', 'dwwldwwldwwldwwl', 'dlwwdlwwdlwwdlww', 'dwlwdwlwdwlwdwlw',
    'dlwwdlwwdlwwdlww', 'dwwldwwldwwldwwl', 'dlwwdlwwdlwwdlww', 'oooooooooooooooo')

function Get-Pal([string]$hex) {
    return @{ o = (Shade-Color $hex 0.45); d = (Shade-Color $hex 0.75); m = (ConvertTo-Color $hex); l = (Shade-Color $hex 1.18); s = '#6e5431'; t = '#9c7a48'; k = '#e8e2d0'; q = '#c9c1aa' }
}

$flagIcon = @(
    '................', '.tmmmmmmmmmmmo..', '.tmlmmmmmmmmmmo.', '.tmmmmmmmmmmmmmo', '.tmmmmmmmmmmmmdo', '.tmmmmmmmmmmmdo.',
    '.tdmmmmmmmmmddo.', '.tddddddddddoo..', '.to.oooooooo....', '.t..............', '.t..............', '.t..............',
    '.t..............', '.t..............', '.s..............', '................')
$buntingIcon = @(
    '................', '................', 'kq..............', '.kqqq......qqqk.', '..m.kqqqqqqk.m..', '..mm..m..m..mm..',
    '..mmmmmmmmmmmm..', '..dmmmlmmlmmmd..', '...dmmd..dmmd...', '...dmd....dmd...', '....d......d....', '................',
    '................', '................', '................', '................')
$awningIcon = @(
    '................', '................', '................', 'mkmkmkmkmkmkmkmk', 'mkmkmkmkmkmkmkmk', 'lklklklklklklklk',
    'mkmkmkmkmkmkmkmk', 'dqdqdqdqdqdqdqdq', 'm.m.m.m.m.m.m.m.', 'd.d.d.d.d.d.d.d.', '.s.............s', '.s.............s',
    '.s.............s', '.t.............t', '................', '................')
$awningTex = @(
    'mmmmkkkkmmmmkkkk', 'mmmmkkkkmmmmkkkk', 'mlmmkkqkmlmmkkqk', 'mmmmkkkkmmmmkkkk', 'mmmmkkkkmmmmkkkk', 'mmmdkkkqmmmdkkkq',
    'mmmmkkkkmmmmkkkk', 'mmmmkkkkmmmmkkkk', 'mlmmkkqkmlmmkkqk', 'mmmmkkkkmmmmkkkk', 'mmmmkkkkmmmmkkkk', 'mmmdkkkqmmmdkkkq',
    'mmmmkkkkmmmmkkkk', 'mmmmkkkkmmmmkkkk', 'dddddqqqddddqqqq', 'oooooooooooooooo')
# Bunting plane (16x16): string on top, two pennants hanging from it.
$buntingTex = @(
    'kq............qk', '.kqq........qqk.', '...kqqqqqqqqk...', '..mmmmm..mmmmm..', '..mlmmm..mlmmm..', '..mmmmm..mmmmm..',
    '..dmmmd..dmmmd..', '...mmm....mmm...', '...dmd....dmd...', '....m......m....', '....d......d....', '................',
    '................', '................', '................', '................')

$Awnings = @(, @('awning', '#d8c9a3', 'Awning', '遮阳篷')) + @(for ($i = 0; $i -lt 16; $i++) { , @("awning_$($ColorNames[$i])", $Colors[$ColorNames[$i]], "$($EnNames[$i]) Awning", "$($ZhNames[$i])遮阳篷") })

for ($i = 0; $i -lt 16; $i++) {
    $c = $ColorNames[$i]; $pal = Get-Pal $Colors[$c]
    if ($c -eq 'white') { $pal.k = '#c9c1aa'; $pal.q = '#a89f86' }
    Save-PixelArt -Path (Join-Path $rp "textures/items/flag_$c.png") -Palette $pal -Rows $flagIcon
    Save-PixelArt -Path (Join-Path $rp "textures/items/bunting_$c.png") -Palette $pal -Rows $buntingIcon
    Save-PixelArt -Path (Join-Path $rp "textures/blocks/bunting_$c.png") -Palette $pal -Rows $buntingTex
    # held flag: 32x16 cloth in the base colour
    $bmp = New-Bitmap 32 16
    $col = ConvertTo-Color $Colors[$c]
    for ($x = 0; $x -lt 24; $x++) { for ($y = 0; $y -lt 16; $y++) { $bmp.SetPixel($x, $y, (Shade-Color $col (1 + (Noise $x $y 3) * 0.035 - $(if ($y -eq 0 -or $y -eq 15) { 0.08 } else { 0 })))) } }
    for ($x = 24; $x -lt 32; $x++) { for ($y = 0; $y -lt 16; $y++) { $bmp.SetPixel($x, $y, (ConvertTo-Color $(if (($x + $y) % 3) { '#9c7a48' } else { '#6e5431' }))) } }
    Save-Bitmap $bmp (Join-Path $rp "textures/entity/flag/held_$c.png")
}
foreach ($a in $Awnings) {
    $pal = Get-Pal $a[1]
    if ($a[0] -eq 'awning') { $pal.k = '#bfae86'; $pal.q = '#a49372' }
    if ($a[0] -eq 'awning_white') { $pal.k = '#c9c1aa'; $pal.q = '#a89f86' }
    Save-PixelArt -Path (Join-Path $rp "textures/items/$($a[0]).png") -Palette $pal -Rows $awningIcon
    Save-PixelArt -Path (Join-Path $rp "textures/blocks/$($a[0]).png") -Palette $pal -Rows $awningTex
}
Save-PixelArt -Path (Join-Path $rp 'textures/items/dragon_banner_pattern.png') -Palette @{ p = '#e9dcc0'; e = '#b8a988'; d = '#3b2a4a'; m = '#6b4c86' } -Rows @(
    '................', '..pppppppppppe..', '..pppppppppppe..', '..ppdppppdpppe..', '..pdmpppdmdppe..', '..pdmmmmmmdppe..', '..ddmdmmdmddpe..',
    '..pdmmmmmmdppe..', '..ppdmddmdpppe..', '..pppdmmdppppe..', '..ppppddpppppe..', '..pppppppppppe..', '..pppppppppppe..', '..eeeeeeeeeeee..',
    '................', '................')

# ---- RP: fragments, geometry, entity -------------------------------------------------------------
$terrain = [ordered]@{ complementaries_flag_pole = @{ textures = 'textures/blocks/flag_pole' } }
$itemTex = [ordered]@{ complementaries_dragon_banner_pattern = @{ textures = 'textures/items/dragon_banner_pattern' } }
$blocksD = [ordered]@{}
$textsEn = New-Object System.Collections.Generic.List[string]
$textsZh = New-Object System.Collections.Generic.List[string]
function Add-Name([string]$id, [string]$en, [string]$zh) {
    foreach ($k in @("tile.complementaries:$id.name", "item.complementaries:$id.name")) { $textsEn.Add("$k=$en"); $textsZh.Add("$k=$zh") }
}

for ($i = 0; $i -lt 16; $i++) {
    $c = $ColorNames[$i]
    $itemTex["complementaries_flag_$c"] = @{ textures = "textures/items/flag_$c" }
    $itemTex["complementaries_bunting_$c"] = @{ textures = "textures/items/bunting_$c" }
    $terrain["complementaries_bunting_$c"] = @{ textures = "textures/blocks/bunting_$c" }
    $blocksD["complementaries:flag_$c"] = @{ sound = 'wood' }
    $blocksD["complementaries:bunting_$c"] = @{ sound = 'cloth' }
    $blocksD["complementaries:bunting_wall_$c"] = @{ sound = 'cloth' }
    Add-Name "flag_$c" "$($EnNames[$i]) Flag" "$($ZhNames[$i])旗帜"
    Add-Name "bunting_$c" "$($EnNames[$i]) Bunting" "$($ZhNames[$i])彩旗串"
    Add-Name "bunting_wall_$c" "$($EnNames[$i]) Bunting" "$($ZhNames[$i])彩旗串"
}
foreach ($a in $Awnings) {
    $itemTex["complementaries_$($a[0])"] = @{ textures = "textures/items/$($a[0])" }
    $terrain["complementaries_$($a[0])"] = @{ textures = "textures/blocks/$($a[0])" }
    $blocksD["complementaries:$($a[0])"] = @{ sound = 'cloth' }
    Add-Name $a[0] $a[2] $a[3]
}
Add-Name 'dragon_banner_pattern' 'Banner Pattern' '旗帜图案'
$textsEn.Add('entity.complementaries:flag_cloth.name=Flag'); $textsZh.Add('entity.complementaries:flag_cloth.name=旗帜')

Write-Json (Join-Path $rp 'terrain_texture.d/flag.json') $terrain
Write-Json (Join-Path $rp 'item_texture.d/flag.json') $itemTex
Write-Json (Join-Path $rp 'blocks.d/flag.json') $blocksD
New-Item -ItemType Directory -Force (Join-Path $rp 'texts.d/en_US'), (Join-Path $rp 'texts.d/zh_CN') | Out-Null
[IO.File]::WriteAllText((Join-Path $rp 'texts.d/en_US/flag.lang'), ($textsEn -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $rp 'texts.d/zh_CN/flag.lang'), ($textsZh -join "`n") + "`n", [Text.UTF8Encoding]::new($false))

# Sounds: awning bounce (vanilla cloth/slime sounds, no new audio files).
Write-Json (Join-Path $rp 'sounds.d/flag.json') ([ordered]@{
        'complementaries.block.awning.bounce' = [ordered]@{
            category = 'block'
            sounds   = @(
                [ordered]@{ name = 'sounds/step/cloth1'; pitch = 1.2; volume = 1.0 },
                [ordered]@{ name = 'sounds/step/cloth2'; pitch = 1.1; volume = 1.0 },
                [ordered]@{ name = 'sounds/step/cloth3'; pitch = 1.3; volume = 1.0 },
                [ordered]@{ name = 'sounds/step/cloth4'; pitch = 1.2; volume = 1.0 })
        }
    })

function Face([double[]]$uv, [double[]]$size) { return [ordered]@{ uv = $uv; uv_size = $size } }
function Geo([string]$id, [int]$tw, [int]$th, $bones) {
    return [ordered]@{ description = [ordered]@{ identifier = $id; texture_width = $tw; texture_height = $th; visible_bounds_width = 4; visible_bounds_height = 3; visible_bounds_offset = @(0, 1, 0) }; bones = $bones }
}

# Block geometries.
$blockGeos = @(
    (Geo 'geometry.complementaries.flag_pole' 16 16 @([ordered]@{ name = 'pole'; pivot = @(0, 0, 0); cubes = @([ordered]@{ origin = @(-1, 0, -1); size = @(2, 16, 2); uv = [ordered]@{
                            north = (Face @(7, 0) @(2, 16)); south = (Face @(7, 0) @(2, 16)); east = (Face @(7, 0) @(2, 16)); west = (Face @(7, 0) @(2, 16)); up = (Face @(7, 7) @(2, 2)); down = (Face @(7, 7) @(2, 2)) }
                    }) })),
    # awnings face north (front = -z, support behind = +z); bottom half y 0..8
    (Geo 'geometry.complementaries.awning_bottom' 16 16 @([ordered]@{ name = 'awning'; pivot = @(0, 0, 0); cubes = @(
                    [ordered]@{ origin = @(-8, 7, -8); size = @(16, 1, 16); uv = [ordered]@{ up = (Face @(0, 0) @(16, 16)); down = (Face @(0, 0) @(16, 16)); north = (Face @(0, 14) @(16, 1)); south = (Face @(0, 14) @(16, 1)); east = (Face @(0, 14) @(16, 1)); west = (Face @(0, 14) @(16, 1)) } },
                    [ordered]@{ origin = @(-8, 4, -8); size = @(16, 3, 1); uv = [ordered]@{ north = (Face @(0, 12) @(16, 3)); south = (Face @(0, 12) @(16, 3)); east = (Face @(0, 12) @(1, 3)); west = (Face @(0, 12) @(1, 3)); down = (Face @(0, 14) @(16, 1)) } }) })),
    (Geo 'geometry.complementaries.awning_top' 16 16 @([ordered]@{ name = 'awning'; pivot = @(0, 0, 0); cubes = @(
                    [ordered]@{ origin = @(-8, 15, -8); size = @(16, 1, 16); uv = [ordered]@{ up = (Face @(0, 0) @(16, 16)); down = (Face @(0, 0) @(16, 16)); north = (Face @(0, 14) @(16, 1)); south = (Face @(0, 14) @(16, 1)); east = (Face @(0, 14) @(16, 1)); west = (Face @(0, 14) @(16, 1)) } },
                    [ordered]@{ origin = @(-8, 12, -8); size = @(16, 3, 1); uv = [ordered]@{ north = (Face @(0, 12) @(16, 3)); south = (Face @(0, 12) @(16, 3)); east = (Face @(0, 12) @(1, 3)); west = (Face @(0, 12) @(1, 3)); down = (Face @(0, 14) @(16, 1)) } }) })),
    # slanted: the plate drops ~6.6 px from the back edge (+z) to the front edge (-z)
    (Geo 'geometry.complementaries.awning_bottom_slanted' 16 16 @([ordered]@{ name = 'awning'; pivot = @(0, 8, 8); rotation = @(-22.5, 0, 0); cubes = @(
                    [ordered]@{ origin = @(-8, 7, -9.3); size = @(16, 1, 17.3); uv = [ordered]@{ up = (Face @(0, 0) @(16, 16)); down = (Face @(0, 0) @(16, 16)); north = (Face @(0, 14) @(16, 1)); south = (Face @(0, 14) @(16, 1)); east = (Face @(0, 14) @(16, 1)); west = (Face @(0, 14) @(16, 1)) } }) })),
    (Geo 'geometry.complementaries.awning_top_slanted' 16 16 @([ordered]@{ name = 'awning'; pivot = @(0, 16, 8); rotation = @(-22.5, 0, 0); cubes = @(
                    [ordered]@{ origin = @(-8, 15, -9.3); size = @(16, 1, 17.3); uv = [ordered]@{ up = (Face @(0, 0) @(16, 16)); down = (Face @(0, 0) @(16, 16)); north = (Face @(0, 14) @(16, 1)); south = (Face @(0, 14) @(16, 1)); east = (Face @(0, 14) @(16, 1)); west = (Face @(0, 14) @(16, 1)) } }) })),
    # ceiling bunting: a plane across x in the middle of the block (axis z); rotated for axis x
    (Geo 'geometry.complementaries.bunting_ceiling' 16 16 @([ordered]@{ name = 'bunting'; pivot = @(0, 0, 0); cubes = @(
                    [ordered]@{ origin = @(-8, 0, 0); size = @(16, 16, 0); uv = [ordered]@{ north = (Face @(0, 0) @(16, 16)); south = (Face @(16, 0) @(-16, 16)) } }) })),
    # wall bunting facing north: a plane 0.5 px in front of the wall at +z
    (Geo 'geometry.complementaries.bunting_wall' 16 16 @([ordered]@{ name = 'bunting'; pivot = @(0, 0, 0); cubes = @(
                    [ordered]@{ origin = @(-8, 0, 6); size = @(16, 16, 0); uv = [ordered]@{ north = (Face @(0, 0) @(16, 16)); south = (Face @(16, 0) @(-16, 16)) } }) }))
)
Write-Json (Join-Path $rp 'models/blocks/flag.geo.json') ([ordered]@{ format_version = '1.12.0'; 'minecraft:geometry' = $blockGeos })

# Flag cloth: 8 chained segments of 3 px along +z (z 1..25), 16 px tall, 1 px thick (x -0.5..0.5).
# Layer n is inflated a little more than layer n-1 so the patterns sit on top of the base.
$SegLen = 3; $Segs = 8
function ClothBones([double]$inflate) {
    $bones = New-Object System.Collections.Generic.List[object]
    $bones.Add([ordered]@{ name = 'root'; pivot = @(0, 0, 0) })
    for ($s = 0; $s -lt $Segs; $s++) {
        $z0 = 1 + $s * $SegLen
        $u0 = $s * $SegLen
        $uv = [ordered]@{
            east  = (Face @($u0, 0) @($SegLen, 16))
            west  = (Face @(($u0 + $SegLen), 0) @((-$SegLen), 16))
            up    = (Face @($u0, 0) @($SegLen, 1))
            down  = (Face @($u0, 15) @($SegLen, 1))
        }
        if ($s -eq 0) { $uv.north = (Face @(0, 0) @(1, 16)) }
        if ($s -eq $Segs - 1) { $uv.south = (Face @(23, 0) @(1, 16)) }
        $bones.Add([ordered]@{
                name   = "seg$s"; parent = $(if ($s -eq 0) { 'root' } else { "seg$($s - 1)" }); pivot = @(0, 0, $z0)
                cubes  = @([ordered]@{ origin = @(-0.5, 0, $z0); size = @(1, 16, $SegLen); inflate = $inflate; uv = $uv })
            })
    }
    return $bones.ToArray()
}
$clothGeos = @(for ($l = 0; $l -le 6; $l++) { Geo "geometry.complementaries.flag_cloth.l$l" 32 16 (ClothBones (0.002 + $l * 0.012)) })
# Held flag: a 2-block pole with the cloth on top.
$heldBones = @(
    [ordered]@{ name = 'held'; pivot = @(0, 0, 0); binding = 'q.item_slot_to_bone_name(c.item_slot)' },
    [ordered]@{ name = 'pole'; parent = 'held'; pivot = @(0, 0, 0); cubes = @([ordered]@{ origin = @(-1, -12, -1); size = @(2, 32, 2); uv = [ordered]@{
                    north = (Face @(24, 0) @(2, 16)); south = (Face @(26, 0) @(2, 16)); east = (Face @(28, 0) @(2, 16)); west = (Face @(30, 0) @(2, 16)); up = (Face @(24, 0) @(2, 2)); down = (Face @(24, 0) @(2, 2)) }
            }) },
    [ordered]@{ name = 'cloth'; parent = 'held'; pivot = @(0, 4, 1); cubes = @([ordered]@{ origin = @(-0.5, 4, 1); size = @(1, 16, 24); uv = [ordered]@{
                    east = (Face @(0, 0) @(24, 16)); west = (Face @(24, 0) @(-24, 16)); up = (Face @(0, 0) @(24, 1)); down = (Face @(0, 15) @(24, 1)); south = (Face @(23, 0) @(1, 16)); north = (Face @(0, 0) @(1, 16)) }
            }) }
)
$clothGeos += (Geo 'geometry.complementaries.flag_held' 32 16 $heldBones)
Write-Json (Join-Path $rp 'models/entity/flag_cloth.geo.json') ([ordered]@{ format_version = '1.12.0'; 'minecraft:geometry' = $clothGeos })

# Wave (Supplementaries client defaults: slowness 100, wavyness 4, intensity 1, increment 0.3).
# ang(d) = (1 + 0.3 d) * sin(d / 4 rad - 360 t deg); t from position and time, so neighbours differ.
function AngExpr([int]$d) { return "(1 + 0.3 * $d) * math.sin($([Math]::Round($d / 4 * 180 / [Math]::PI, 4)) - 360 * v.cm_t)" }
$bonesAnim = [ordered]@{ root = [ordered]@{ rotation = @(0, "90 * (2 - q.property('complementaries:facing')) + q.body_y_rotation", 0) } }
for ($s = 0; $s -lt $Segs; $s++) {
    $d = $s * $SegLen
    $e = if ($s -eq 0) { AngExpr 0 } else { "$(AngExpr $d) - ($(AngExpr ($d - $SegLen)))" }
    $bonesAnim["seg$s"] = [ordered]@{ rotation = @(0, $e, 0) }
}
$heldAnim = [ordered]@{ cloth = [ordered]@{ rotation = @(0, "3 * math.sin(q.life_time * 216)", 0) } }
Write-Json (Join-Path $rp 'animations/flag.animation.json') ([ordered]@{
        format_version = '1.8.0'
        animations     = [ordered]@{
            'animation.complementaries.flag_cloth.wave'   = [ordered]@{ loop = $true; bones = $bonesAnim }
            'animation.complementaries.flag_held.wave'    = [ordered]@{ loop = $true; bones = $heldAnim }
            'animation.complementaries.flag_held.first'   = [ordered]@{ loop = $true; bones = [ordered]@{ held = [ordered]@{ position = @(-4, -6, 2); rotation = @(0, 90, 0); scale = 0.6 } } }
            'animation.complementaries.flag_held.third'   = [ordered]@{ loop = $true; bones = [ordered]@{ held = [ordered]@{ position = @(0, 6, -2); rotation = @(90, 0, 0) } } }
        }
    })

# Textures: base_<colour> and p/<pattern>_<colour>; a layer's property value is (pattern + 1) * 16 + colour.
$texMap = [ordered]@{}
$baseArray = New-Object System.Collections.Generic.List[string]
for ($c = 0; $c -lt 16; $c++) { $texMap["b$c"] = "textures/entity/flag/base_$($ColorNames[$c])"; $baseArray.Add("Texture.b$c") }
$patArray = New-Object System.Collections.Generic.List[string]
for ($i = 0; $i -lt $Patterns.Count; $i++) {
    for ($c = 0; $c -lt 16; $c++) { $texMap["p$($i)_$c"] = "textures/entity/flag/p/$($Patterns[$i][0])_$($ColorNames[$c])"; $patArray.Add("Texture.p$($i)_$c") }
}
$rcs = [ordered]@{
    'controller.render.complementaries.flag_base' = [ordered]@{
        arrays    = [ordered]@{ textures = [ordered]@{ 'Array.base' = $baseArray.ToArray() } }
        geometry  = 'Geometry.l0'; materials = @(@{ '*' = 'Material.default' }); textures = @("Array.base[q.property('complementaries:base')]")
    }
}
$renderList = New-Object System.Collections.Generic.List[object]
$renderList.Add('controller.render.complementaries.flag_base')
for ($l = 0; $l -lt 6; $l++) {
    $prop = "q.property('complementaries:l$l')"
    $rcs["controller.render.complementaries.flag_l$l"] = [ordered]@{
        arrays    = [ordered]@{ textures = [ordered]@{ 'Array.patterns' = $patArray.ToArray() } }
        geometry  = "Geometry.l$($l + 1)"; materials = @(@{ '*' = 'Material.alpha' })
        textures  = @("Array.patterns[$prop - 16]")
    }
    $renderList.Add([ordered]@{ "controller.render.complementaries.flag_l$l" = "$prop >= 16" })
}
$rcs['controller.render.complementaries.flag_held'] = [ordered]@{ geometry = 'Geometry.default'; materials = @(@{ '*' = 'Material.default' }); textures = @('Texture.default') }
Write-Json (Join-Path $rp 'render_controllers/flag.render_controllers.json') ([ordered]@{ format_version = '1.8.0'; render_controllers = $rcs })

$geoMap = [ordered]@{}
for ($l = 0; $l -le 6; $l++) { $geoMap["l$l"] = "geometry.complementaries.flag_cloth.l$l" }
Write-Json (Join-Path $rp 'entity/flag_cloth.entity.json') ([ordered]@{
        format_version          = '1.10.0'
        'minecraft:client_entity' = [ordered]@{
            description = [ordered]@{
                identifier        = 'complementaries:flag_cloth'
                materials         = [ordered]@{ default = 'entity'; alpha = 'entity_alphatest' }
                textures          = $texMap
                geometry          = $geoMap
                animations        = [ordered]@{ wave = 'animation.complementaries.flag_cloth.wave' }
                scripts           = [ordered]@{
                    pre_animation = @("v.cm_t = math.mod(q.position(0) * 7 + q.position(2) * 13 + q.life_time * 20, 100) / 100;")
                    animate       = @('wave')
                }
                render_controllers = $renderList.ToArray()
                enable_attachables = $false
            }
        }
    })

for ($i = 0; $i -lt 16; $i++) {
    $c = $ColorNames[$i]
    Write-Json (Join-Path $rp "attachables/flag_$c.json") ([ordered]@{
            format_version         = '1.10.0'
            'minecraft:attachable' = [ordered]@{
                description = [ordered]@{
                    identifier         = "complementaries:flag_$c"
                    materials          = [ordered]@{ default = 'entity_alphatest' }
                    textures           = [ordered]@{ default = "textures/entity/flag/held_$c" }
                    geometry           = [ordered]@{ default = 'geometry.complementaries.flag_held' }
                    animations         = [ordered]@{ wave = 'animation.complementaries.flag_held.wave'; first = 'animation.complementaries.flag_held.first'; third = 'animation.complementaries.flag_held.third' }
                    scripts            = [ordered]@{ animate = @('wave', [ordered]@{ first = 'c.is_first_person' }, [ordered]@{ third = '!c.is_first_person' }) }
                    render_controllers = @('controller.render.complementaries.flag_held')
                }
            }
        })
}

# ---- BP: helper entity -----------------------------------------------------------------------------
$props = [ordered]@{
    'complementaries:base'   = [ordered]@{ type = 'int'; range = @(0, 15); default = 0; client_sync = $true }
    'complementaries:facing' = [ordered]@{ type = 'int'; range = @(0, 3); default = 2; client_sync = $true }
}
for ($l = 0; $l -lt 6; $l++) { $props["complementaries:l$l"] = [ordered]@{ type = 'int'; range = @(0, 1023); default = 0; client_sync = $true } }
Write-Json (Join-Path $bp 'entities/flag_cloth.json') ([ordered]@{
        format_version     = '1.21.50'
        'minecraft:entity' = [ordered]@{
            description = [ordered]@{ identifier = 'complementaries:flag_cloth'; is_spawnable = $false; is_summonable = $true; properties = $props }
            components  = [ordered]@{
                'minecraft:type_family'                  = @{ family = @('complementaries_flag_cloth', 'inanimate') }
                'minecraft:collision_box'                = @{ width = 0.1; height = 0.1 }
                'minecraft:physics'                      = @{ has_gravity = $false; has_collision = $false }
                'minecraft:pushable'                     = @{ is_pushable = $false; is_pushable_by_piston = $false }
                'minecraft:damage_sensor'                = @{ triggers = @(@{ cause = 'all'; deals_damage = 'no' }) }
                'minecraft:health'                       = @{ value = 1; max = 1 }
                'minecraft:knockback_resistance'         = @{ value = 1.0 }
                'minecraft:fire_immune'                  = @{}
                'minecraft:persistent'                   = @{}
                'minecraft:conditional_bandwidth_optimization' = @{}
                'minecraft:nameable'                     = @{ always_show = $false; allow_name_tag_renaming = $false }
            }
        }
    })

# ---- BP: blocks, items, loot, recipes -------------------------------------------------------------
$FacingRot = @{ 0 = @(0, 180, 0); 1 = @(0, 90, 0); 2 = @(0, 0, 0); 3 = @(0, -90, 0) } # Java 2D: S W N E
function Block-Json([string]$id, $components, $states, $permutations, [string]$group) {
    $desc = [ordered]@{ identifier = "complementaries:$id"; menu_category = [ordered]@{ category = 'items'; group = $group } }
    if ($states) { $desc.states = $states }
    $b = [ordered]@{ description = $desc; components = $components }
    if ($permutations) { $b.permutations = $permutations }
    return [ordered]@{ format_version = '1.21.60'; 'minecraft:block' = $b }
}
function Loot([string]$item) {
    return [ordered]@{ pools = @([ordered]@{ rolls = 1; entries = @([ordered]@{ type = 'item'; name = "complementaries:$item" }); conditions = @(@{ condition = 'survives_explosion' }) }) }
}
function Item-Json([string]$id, [string]$icon, [string]$group, [int]$stack, [string[]]$tags, [bool]$placer, [bool]$restricted) {
    $c = [ordered]@{ 'minecraft:icon' = [ordered]@{ textures = [ordered]@{ default = $icon } }; 'minecraft:max_stack_size' = $stack }
    if ($placer) {
        $placerDef = [ordered]@{ block = "complementaries:$id"; replace_block_item = $true }
        # placement handled by the mod (states depend on the click): never place on the client
        if ($restricted) { $placerDef.use_on = @('minecraft:end_portal') }
        $c['minecraft:block_placer'] = $placerDef
    }
    if ($tags) { $c['minecraft:tags'] = [ordered]@{ tags = $tags } }
    return [ordered]@{ format_version = '1.21.60'; 'minecraft:item' = [ordered]@{ description = [ordered]@{ identifier = "complementaries:$id"; menu_category = [ordered]@{ category = 'items'; group = $group } }; components = $c } }
}
# Unlocks the recipe when the player gets the first plain item ingredient.
function Unlock-Of($ingredients) {
    foreach ($i in $ingredients) { if ($i -is [System.Collections.IDictionary] -and $i.Contains('item')) { return [ordered]@{ item = $i['item'] } } }
    return [ordered]@{ context = 'AlwaysUnlocked' }
}
function Recipe-Shaped([string]$name, $pattern, $key, [string]$result, [int]$count, [string]$group) {
    return [ordered]@{ format_version = '1.21.50'; 'minecraft:recipe_shaped' = [ordered]@{ description = [ordered]@{ identifier = "complementaries:$name" }; tags = @('crafting_table'); group = $group; unlock = @(Unlock-Of @($key.Values)); pattern = $pattern; key = $key; result = [ordered]@{ item = "complementaries:$result"; count = $count } } }
}
function Recipe-Shapeless([string]$name, $ingredients, [string]$result, [int]$count, [string]$group) {
    return [ordered]@{ format_version = '1.21.50'; 'minecraft:recipe_shapeless' = [ordered]@{ description = [ordered]@{ identifier = "complementaries:$name" }; tags = @('crafting_table'); group = $group; unlock = @(Unlock-Of $ingredients); ingredients = $ingredients; result = [ordered]@{ item = "complementaries:$result"; count = $count } } }
}
$destroy = @{ seconds_to_destroy = 1.0 }
$water = [ordered]@{ detection_rules = @([ordered]@{ liquid_type = 'water'; can_contain_liquid = $true; on_liquid_touches = 'no_reaction' }) }

for ($i = 0; $i -lt 16; $i++) {
    $c = $ColorNames[$i]
    # Flag: pole + selection 8x16x8; the cloth is the helper entity.
    $comp = [ordered]@{
        'minecraft:geometry'                  = 'geometry.complementaries.flag_pole'
        'minecraft:material_instances'        = @{ '*' = [ordered]@{ texture = 'complementaries_flag_pole'; render_method = 'alpha_test' } }
        'minecraft:collision_box'             = [ordered]@{ origin = @(-4, 0, -4); size = @(8, 16, 8) }
        'minecraft:selection_box'             = [ordered]@{ origin = @(-4, 0, -4); size = @(8, 16, 8) }
        'minecraft:destructible_by_mining'    = $destroy
        'minecraft:destructible_by_explosion' = @{ explosion_resistance = 1.0 }
        'minecraft:light_dampening'           = 0
        'minecraft:map_color'                 = $Colors[$c]
        'minecraft:loot'                      = "loot_tables/blocks/flag_$c.json"
        'minecraft:liquid_detection'          = $water
        'minecraft:flammable'                 = @{ catch_chance_modifier = 5; destroy_chance_modifier = 20 }
        'tag:complementaries:flags'           = @{}
    }
    Write-Json (Join-Path $bp "blocks/flag_$c.json") (Block-Json "flag_$c" $comp $null $null 'minecraft:itemGroup.name.banner')
    Write-Json (Join-Path $bp "items/flag_$c.json") (Item-Json "flag_$c" "complementaries_flag_$c" 'minecraft:itemGroup.name.banner' 16 @('complementaries:flags') $true $false)
    Write-Json (Join-Path $bp "loot_tables/blocks/flag_$c.json") (Loot "flag_$c")
    Write-Json (Join-Path $bp "recipes/flag_$c.json") (Recipe-Shaped "flag_$c" @('###', '###', '/  ') ([ordered]@{ '#' = @{ item = "minecraft:$($c)_wool" }; '/' = @{ item = 'minecraft:stick' } }) "flag_$c" 1 'flag')

    # Buntings: ceiling (axis 0 = z: plane across x, 1 = x) and wall (facing away from the wall).
    $bcomp = [ordered]@{
        'minecraft:geometry'                  = 'geometry.complementaries.bunting_ceiling'
        'minecraft:material_instances'        = @{ '*' = [ordered]@{ texture = "complementaries_bunting_$c"; render_method = 'alpha_test'; ambient_occlusion = $false; face_dimming = $false } }
        'minecraft:collision_box'             = $false
        'minecraft:selection_box'             = [ordered]@{ origin = @(-8, 0, -1); size = @(16, 16, 2) }
        'minecraft:destructible_by_mining'    = @{ seconds_to_destroy = 0.2 }
        'minecraft:destructible_by_explosion' = @{ explosion_resistance = 0.5 }
        'minecraft:light_dampening'           = 0
        'minecraft:map_color'                 = $Colors[$c]
        'minecraft:loot'                      = "loot_tables/blocks/bunting_$c.json"
        'minecraft:flammable'                 = @{ catch_chance_modifier = 30; destroy_chance_modifier = 60 }
        'tag:complementaries:buntings'        = @{}
    }
    $bperm = @(
        [ordered]@{ condition = "q.block_state('complementaries:axis') == 1"; components = [ordered]@{ 'minecraft:transformation' = @{ rotation = @(0, 90, 0) } } }
    )
    Write-Json (Join-Path $bp "blocks/bunting_$c.json") (Block-Json "bunting_$c" $bcomp ([ordered]@{ 'complementaries:axis' = @(0, 1) }) $bperm 'minecraft:itemGroup.name.banner')
    $wcomp = [ordered]@{}
    foreach ($k in $bcomp.Keys) { $wcomp[$k] = $bcomp[$k] }
    $wcomp['minecraft:geometry'] = 'geometry.complementaries.bunting_wall'
    $wcomp['minecraft:selection_box'] = [ordered]@{ origin = @(-8, 0, 7); size = @(16, 16, 1) }
    $wcomp['minecraft:loot'] = "loot_tables/blocks/bunting_wall_$c.json"
    $wperm = @(foreach ($f in 0..3) { [ordered]@{ condition = "q.block_state('complementaries:facing') == $f"; components = [ordered]@{ 'minecraft:transformation' = @{ rotation = $FacingRot[$f] } } } })
    $wdef = Block-Json "bunting_wall_$c" $wcomp ([ordered]@{ 'complementaries:facing' = @(0, 1, 2, 3) }) $wperm 'minecraft:itemGroup.name.banner'
    $wdef.'minecraft:block'.description.menu_category = [ordered]@{ category = 'none'; is_hidden_in_commands = $false }
    Write-Json (Join-Path $bp "blocks/bunting_wall_$c.json") $wdef
    Write-Json (Join-Path $bp "items/bunting_$c.json") (Item-Json "bunting_$c" "complementaries_bunting_$c" 'minecraft:itemGroup.name.banner' 64 @('complementaries:buntings') $true $true)
    Write-Json (Join-Path $bp "loot_tables/blocks/bunting_$c.json") (Loot "bunting_$c")
    Write-Json (Join-Path $bp "loot_tables/blocks/bunting_wall_$c.json") (Loot "bunting_$c")
    Write-Json (Join-Path $bp "recipes/bunting_$($c)_2.json") (Recipe-Shaped "bunting_$($c)_2" @('sss', '###', ' # ') ([ordered]@{ 's' = @{ item = 'minecraft:string' }; '#' = @{ item = "minecraft:$($c)_wool" } }) "bunting_$c" 6 'bunting')
    Write-Json (Join-Path $bp "recipes/bunting_$c.json") (Recipe-Shapeless "bunting_$c" @(@{ tag = 'complementaries:buntings' }, @{ item = "minecraft:$($c)_dye" }) "bunting_$c" 1 'bunting')
}

foreach ($a in $Awnings) {
    $id = $a[0]
    $comp = [ordered]@{
        'minecraft:geometry'                  = 'geometry.complementaries.awning_top'
        'minecraft:material_instances'        = @{ '*' = [ordered]@{ texture = "complementaries_$id"; render_method = 'alpha_test' } }
        'minecraft:collision_box'             = [ordered]@{ origin = @(-8, 9, -8); size = @(16, 7, 16) }
        'minecraft:selection_box'             = [ordered]@{ origin = @(-8, 8, -8); size = @(16, 8, 16) }
        'minecraft:destructible_by_mining'    = @{ seconds_to_destroy = 0.6 }
        'minecraft:destructible_by_explosion' = @{ explosion_resistance = 0.5 }
        'minecraft:light_dampening'           = 0
        'minecraft:map_color'                 = $a[1]
        'minecraft:loot'                      = "loot_tables/blocks/$id.json"
        'minecraft:liquid_detection'          = $water
        'minecraft:flammable'                 = @{ catch_chance_modifier = 30; destroy_chance_modifier = 60 }
        'tag:complementaries:awnings'         = @{}
    }
    $perms = New-Object System.Collections.Generic.List[object]
    foreach ($f in 0..3) { $perms.Add([ordered]@{ condition = "q.block_state('complementaries:facing') == $f"; components = [ordered]@{ 'minecraft:transformation' = @{ rotation = $FacingRot[$f] } } }) }
    $perms.Add([ordered]@{ condition = "q.block_state('complementaries:bottom') == 1"; components = [ordered]@{
                'minecraft:collision_box' = [ordered]@{ origin = @(-8, 1, -8); size = @(16, 7, 16) }; 'minecraft:selection_box' = [ordered]@{ origin = @(-8, 0, -8); size = @(16, 8, 16) } }
        })
    foreach ($bt in 0..1) { foreach ($sl in 0..1) {
            $g = 'geometry.complementaries.awning_' + $(if ($bt) { 'bottom' } else { 'top' }) + $(if ($sl) { '_slanted' } else { '' })
            $perms.Add([ordered]@{ condition = "q.block_state('complementaries:bottom') == $bt && q.block_state('complementaries:slanted') == $sl"; components = [ordered]@{ 'minecraft:geometry' = $g } })
        } }
    $states = [ordered]@{ 'complementaries:facing' = @(0, 1, 2, 3); 'complementaries:bottom' = @(0, 1); 'complementaries:slanted' = @(0, 1) }
    Write-Json (Join-Path $bp "blocks/$id.json") (Block-Json $id $comp $states $perms.ToArray() 'minecraft:itemGroup.name.wool')
    Write-Json (Join-Path $bp "items/$id.json") (Item-Json $id "complementaries_$id" 'minecraft:itemGroup.name.wool' 64 @('complementaries:awnings') $true $true)
    Write-Json (Join-Path $bp "loot_tables/blocks/$id.json") (Loot $id)
}
# The natural awning is crafted from flax (`FFF / S S` -> 2): that recipe belongs with the flax unit
# (complementaries:flax), a recipe naming a missing item fails to load.
Remove-Item -ErrorAction SilentlyContinue (Join-Path $bp 'recipes/awning.json')
for ($i = 0; $i -lt 16; $i++) {
    $c = $ColorNames[$i]
    Write-Json (Join-Path $bp "recipes/awning_$($c)_2.json") (Recipe-Shaped "awning_$($c)_2" @('###', '/ /') ([ordered]@{ '#' = @{ item = "minecraft:$($c)_carpet" }; '/' = @{ item = 'minecraft:stick' } }) "awning_$c" 2 'awning')
    Write-Json (Join-Path $bp "recipes/awning_$c.json") (Recipe-Shapeless "awning_$c" @(@{ item = 'complementaries:awning' }, @{ item = "minecraft:$($c)_dye" }) "awning_$c" 1 'awning')
}

Write-Json (Join-Path $bp 'items/dragon_banner_pattern.json') (Item-Json 'dragon_banner_pattern' 'complementaries_dragon_banner_pattern' 'minecraft:itemGroup.name.banner_pattern' 1 @('complementaries:banner_patterns') $false $false)
Write-Json (Join-Path $bp 'recipes/dragon_banner_pattern.json') (Recipe-Shapeless 'dragon_banner_pattern' @(@{ item = 'minecraft:paper' }, @{ item = 'minecraft:dragon_head' }) 'dragon_banner_pattern' 1 '')

Write-Host 'flag assets written'
