# Writes the data-driven definitions of unit U10 `building` (see docs/spec/building.md):
#   BP blocks/, items/, recipes/, loot_tables/blocks/, features/, feature_rules/
#   RP models/blocks/*.geo.json and the fragments terrain_texture.d / item_texture.d / blocks.d /
#   texts.d/{en_US,zh_CN}/building.lang
# Textures are drawn by tools/textures/building.ps1.
#
#   pwsh tools/blocks/building.ps1

$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '../..')
$bp = Join-Path $root 'assets/pack/bp'
$rp = Join-Path $root 'assets/pack/rp'
$ns = 'complementaries'
$blockFormat = '1.26.20'

function Write-JsonFile([string]$Path, $Object) {
    $dir = Split-Path -Parent $Path
    New-Item -ItemType Directory -Force $dir | Out-Null
    $json = $Object | ConvertTo-Json -Depth 64
    [IO.File]::WriteAllText($Path, ($json -replace "`r`n", "`n") + "`n", [Text.UTF8Encoding]::new($false))
}

function Write-TextFile([string]$Path, [string[]]$Lines) {
    New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
    [IO.File]::WriteAllText($Path, ($Lines -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
}

# ---- shared state of the fragments ---------------------------------------------------------------
$terrain = [ordered]@{}   # texture key -> path
$itemTex = [ordered]@{}
$soundOf = [ordered]@{}   # block id -> blocks.json sound
$names = [System.Collections.Generic.List[object]]::new()  # @(key, en, zh)

function Add-Terrain([string]$Key) { $script:terrain[$Key] = [ordered]@{ textures = "textures/blocks/building/$($Key -replace '^cm_', '')" } }
function Add-Name([string]$Key, [string]$En, [string]$Zh) { $script:names.Add(@($Key, $En, $Zh)) }

# ---- geometry helpers ----------------------------------------------------------------------------
# World boxes are [x1, y1, z1, x2, y2, z2] in pixels with x, z in -8..8 (x east, z south) and y in 0..16.
# Block geometry has x mirrored compared to the world (+x of the model is west).
$MirrorX = $true

function Face-Uv([string]$Face, [double[]]$B) {
    $x1, $y1, $z1, $x2, $y2, $z2 = $B
    switch ($Face) {
        'north' { return @((8 - $x2), (16 - $y2), ($x2 - $x1), ($y2 - $y1)) }
        'south' { return @(($x1 + 8), (16 - $y2), ($x2 - $x1), ($y2 - $y1)) }
        'east' { return @((8 - $z2), (16 - $y2), ($z2 - $z1), ($y2 - $y1)) }
        'west' { return @(($z1 + 8), (16 - $y2), ($z2 - $z1), ($y2 - $y1)) }
        'up' { return @(($x1 + 8), ($z1 + 8), ($x2 - $x1), ($z2 - $z1)) }
        'down' { return @(($x1 + 8), (8 - $z2), ($x2 - $x1), ($z2 - $z1)) }
    }
}

function New-Cube([double[]]$B, [hashtable]$Materials = @{}) {
    $x1, $y1, $z1, $x2, $y2, $z2 = $B
    $gx = if ($MirrorX) { - $x2 } else { $x1 }
    $faces = [ordered]@{}
    foreach ($f in 'north', 'south', 'east', 'west', 'up', 'down') {
        $u = Face-Uv $f $B
        $face = [ordered]@{ uv = @($u[0], $u[1]); uv_size = @($u[2], $u[3]) }
        if ($Materials.ContainsKey($f)) { $face.material_instance = $Materials[$f] }
        $faces[$f] = $face
    }
    return [ordered]@{ origin = @($gx, $y1, $z1); size = @(($x2 - $x1), ($y2 - $y1), ($z2 - $z1)); uv = $faces }
}

function New-Bone([string]$Name, [object[]]$Cubes) { return [ordered]@{ name = $Name; pivot = @(0, 0, 0); cubes = $Cubes } }

function Write-Geometry([string]$File, [string]$Id, [object[]]$Bones) {
    Write-JsonFile (Join-Path $rp "models/blocks/$File.geo.json") ([ordered]@{
            format_version     = '1.12.0'
            'minecraft:geometry' = @([ordered]@{
                    description = [ordered]@{ identifier = $Id; texture_width = 16; texture_height = 16; visible_bounds_width = 2; visible_bounds_height = 2.5; visible_bounds_offset = @(0, 0.75, 0) }
                    bones       = $Bones
                })
        })
}

function Vis([bool]$On) { if ($On) { return '1.0' } return '0.0' }

function Box([double[]]$B) { return [ordered]@{ origin = @($B[0], $B[1], $B[2]); size = @(($B[3] - $B[0]), ($B[4] - $B[1]), ($B[5] - $B[2])) } }

# ---- geometries ----------------------------------------------------------------------------------
$quads = [ordered]@{ nw = @(-8, -8, 0, 0); ne = @(0, -8, 8, 0); sw = @(-8, 0, 0, 8); se = @(0, 0, 8, 8) }  # x1 z1 x2 z2
function Quad-Box([string]$Name, [int]$Y1, [int]$Y2) { $r = $quads[$Name]; return @($r[0], $Y1, $r[1], $r[2], $Y2, $r[3]) }

# slab: bottom + top halves
Write-Geometry 'cm_slab' 'geometry.complementaries.slab' @(
    (New-Bone 'bottom' @((New-Cube @(-8, 0, -8, 8, 8, 8)))),
    (New-Bone 'top' @((New-Cube @(-8, 8, -8, 8, 16, 8))))
)
# stairs: two half slabs and eight quarter blocks
$stairBones = @((New-Bone 'lower' @((New-Cube @(-8, 0, -8, 8, 8, 8)))), (New-Bone 'upper' @((New-Cube @(-8, 8, -8, 8, 16, 8)))))
foreach ($q in $quads.Keys) {
    $stairBones += New-Bone "lo_$q" @((New-Cube (Quad-Box $q 0 8)))
    $stairBones += New-Bone "hi_$q" @((New-Cube (Quad-Box $q 8 16)))
}
Write-Geometry 'cm_stairs' 'geometry.complementaries.stairs' $stairBones
# wall: post + low/tall arms
$wallArm = @{
    north = @(-3, -8, 3, -3); south = @(-3, 3, 3, 8); west = @(-8, -3, -3, 3); east = @(3, -3, 8, 3)
}
$wallBones = @((New-Bone 'post' @((New-Cube @(-4, 0, -4, 4, 16, 4)))), (New-Bone 'core' @((New-Cube @(-3, 0, -3, 3, 14, 3)))), (New-Bone 'core_tall' @((New-Cube @(-3, 0, -3, 3, 16, 3)))))
foreach ($d in 'north', 'east', 'south', 'west') {
    $a = $wallArm[$d]
    $wallBones += New-Bone "${d}_low" @((New-Cube @($a[0], 0, $a[1], $a[2], 14, $a[3])))
    $wallBones += New-Bone "${d}_tall" @((New-Cube @($a[0], 0, $a[1], $a[2], 16, $a[3])))
}
Write-Geometry 'cm_wall' 'geometry.complementaries.wall' $wallBones
# ash layers: one bone per layer count
$layerBones = @(for ($i = 1; $i -le 8; $i++) { New-Bone "layers_$i" @((New-Cube @(-8, 0, -8, 8, (2 * $i), 8))) })
Write-Geometry 'cm_layers' 'geometry.complementaries.layers' $layerBones
# raked gravel: 15/16 tall, own top material
Write-Geometry 'cm_raked_gravel' 'geometry.complementaries.raked_gravel' @(
    (New-Bone 'block' @((New-Cube @(-8, 0, -8, 8, 15, 8) @{ up = 'up' })))
)

# ---- block builders ------------------------------------------------------------------------------
function Material([string]$Tex, [string]$Render = 'opaque') { return [ordered]@{ texture = $Tex; render_method = $Render } }

function Base-Components($P) {
    $c = [ordered]@{}
    $c['minecraft:destructible_by_mining'] = [ordered]@{ seconds_to_destroy = $P.hard }
    $c['minecraft:destructible_by_explosion'] = [ordered]@{ explosion_resistance = $P.blast }
    $c['minecraft:map_color'] = $P.map
    $tags = @()
    if ($P.tool) { $tags += "minecraft:is_$($P.tool)_item_destructible" }
    if ($P.stone) { $tags += 'stone' }
    if ($P.wood) { $tags += 'wood' }
    if ($tags.Count -gt 0) { $c['minecraft:tags'] = $tags }
    if ($P.flammable) { $c['minecraft:flammable'] = [ordered]@{ catch_chance_modifier = 5; destroy_chance_modifier = 20 } }
    return $c
}

function Write-Block([string]$Path, $Description, $Components, [object[]]$Permutations = @()) {
    $block = [ordered]@{ description = $Description; components = $Components }
    if ($Permutations.Count -gt 0) { $block.permutations = $Permutations }
    Write-JsonFile (Join-Path $bp "blocks/$Path.json") ([ordered]@{ format_version = $blockFormat; 'minecraft:block' = $block })
}

function Describe([string]$Path, [string]$Category = 'construction', [string]$Group = $null) {
    $d = [ordered]@{ identifier = "${ns}:$Path"; menu_category = [ordered]@{ category = $Category } }
    if ($Group) { $d.menu_category.group = $Group }
    return $d
}

function Loot-Table([string]$File, [object[]]$Pools) {
    Write-JsonFile (Join-Path $bp "loot_tables/blocks/$File.json") ([ordered]@{ pools = $Pools })
}
function Simple-Pool([string]$Item, [int]$Count = 1, [string]$NeedsTag = $null) {
    $entry = [ordered]@{ type = 'item'; name = $Item; weight = 1 }
    if ($Count -ne 1) { $entry.functions = @([ordered]@{ function = 'set_count'; count = $Count }) }
    $pool = [ordered]@{ rolls = 1; entries = @($entry) }
    if ($NeedsTag) { $pool.conditions = @([ordered]@{ condition = 'match_tool'; item_tags_any = @($NeedsTag) }) }
    return $pool
}

function Write-FullBlock($P) {
    $c = Base-Components $P
    $c['minecraft:geometry'] = 'minecraft:geometry.full_block'
    $c['minecraft:material_instances'] = [ordered]@{ '*' = (Material $P.tex ($P.render ?? 'opaque')) }
    if ($P.render -eq 'alpha_test') { $c['minecraft:light_dampening'] = 0 }
    foreach ($k in $P.extra.Keys) { $c[$k] = $P.extra[$k] }
    if ($P.needsTool) {
        Loot-Table $P.path @((Simple-Pool "${ns}:$($P.path)" 1 'minecraft:is_pickaxe'))
        $c['minecraft:loot'] = "loot_tables/blocks/$($P.path).json"
    }
    $d = Describe $P.path ($P.category ?? 'construction')
    if ($P.states) { $d.states = $P.states }
    Write-Block $P.path $d $c $P.perms
    $script:soundOf["${ns}:$($P.path)"] = $P.sound
}

function Write-Slab($P, [string]$Path) {
    $c = Base-Components $P
    $c['minecraft:geometry'] = [ordered]@{ identifier = 'geometry.complementaries.slab' }
    $c['minecraft:material_instances'] = [ordered]@{ '*' = (Material $P.tex) }
    $c['minecraft:light_dampening'] = 0
    $loot1 = "loot_tables/blocks/$Path.json"
    $loot2 = "loot_tables/blocks/${Path}_double.json"
    $tag = if ($P.needsTool) { 'minecraft:is_pickaxe' } else { $null }
    Loot-Table $Path @((Simple-Pool "${ns}:$Path" 1 $tag))
    Loot-Table "${Path}_double" @((Simple-Pool "${ns}:$Path" 2 $tag))
    $c['minecraft:loot'] = $loot1
    $d = Describe $Path 'construction' 'minecraft:itemGroup.name.slab'
    $d.states = [ordered]@{ "${ns}:double" = @($false, $true) }
    $d.traits = [ordered]@{ 'minecraft:placement_position' = [ordered]@{ enabled_states = @('minecraft:vertical_half') } }
    $perms = @(
        [ordered]@{ condition = "q.block_state('${ns}:double')"; components = [ordered]@{
                'minecraft:geometry' = [ordered]@{ identifier = 'geometry.complementaries.slab'; bone_visibility = [ordered]@{ bottom = (Vis $true); top = (Vis $true) } }
                'minecraft:collision_box' = $true; 'minecraft:selection_box' = $true; 'minecraft:loot' = $loot2; 'minecraft:light_dampening' = 15
            }
        },
        [ordered]@{ condition = "!q.block_state('${ns}:double') && q.block_state('minecraft:vertical_half') == 'bottom'"; components = [ordered]@{
                'minecraft:geometry' = [ordered]@{ identifier = 'geometry.complementaries.slab'; bone_visibility = [ordered]@{ bottom = (Vis $true); top = (Vis $false) } }
                'minecraft:collision_box' = (Box @(-8, 0, -8, 8, 8, 8)); 'minecraft:selection_box' = (Box @(-8, 0, -8, 8, 8, 8))
            }
        },
        [ordered]@{ condition = "!q.block_state('${ns}:double') && q.block_state('minecraft:vertical_half') == 'top'"; components = [ordered]@{
                'minecraft:geometry' = [ordered]@{ identifier = 'geometry.complementaries.slab'; bone_visibility = [ordered]@{ bottom = (Vis $false); top = (Vis $true) } }
                'minecraft:collision_box' = (Box @(-8, 8, -8, 8, 16, 8)); 'minecraft:selection_box' = (Box @(-8, 8, -8, 8, 16, 8))
            }
        }
    )
    Write-Block $Path $d $c $perms
    $script:soundOf["${ns}:$Path"] = $P.sound
}

# Quarter blocks of stairs: Java shapes, facing = side of the tall back.
$cw = @{ north = 'east'; east = 'south'; south = 'west'; west = 'north' }
$ccw = @{ north = 'west'; west = 'south'; south = 'east'; east = 'north' }
$opp = @{ north = 'south'; south = 'north'; east = 'west'; west = 'east' }
function Quad-Of([string]$A, [string]$B) {
    # the quarter at the corner of two perpendicular sides
    $ns_ = if ($A -in 'north', 'south') { $A } else { $B }
    $ew = if ($A -in 'east', 'west') { $A } else { $B }
    return "$($ns_.Substring(0,1))$($ew.Substring(0,1))"
}
function Stair-Quads([string]$F, [int]$Shape) {
    $back = @((Quad-Of $F $cw[$F]), (Quad-Of $F $ccw[$F]))
    switch ($Shape) {
        0 { return $back }
        1 { return $back + @(Quad-Of $opp[$F] $ccw[$F]) }   # inner_left
        2 { return $back + @(Quad-Of $opp[$F] $cw[$F]) }    # inner_right
        3 { return @(Quad-Of $F $ccw[$F]) }                 # outer_left
        4 { return @(Quad-Of $F $cw[$F]) }                  # outer_right
    }
}

function Write-Stairs($P, [string]$Path) {
    $c = Base-Components $P
    $c['minecraft:geometry'] = [ordered]@{ identifier = 'geometry.complementaries.stairs' }
    $c['minecraft:material_instances'] = [ordered]@{ '*' = (Material $P.tex) }
    $c['minecraft:light_dampening'] = 0
    $c['minecraft:support'] = [ordered]@{ shape = 'stair' }
    if ($P.needsTool) {
        Loot-Table $Path @((Simple-Pool "${ns}:$Path" 1 'minecraft:is_pickaxe'))
        $c['minecraft:loot'] = "loot_tables/blocks/$Path.json"
    }
    $d = Describe $Path 'construction' 'minecraft:itemGroup.name.stairs'
    $d.states = [ordered]@{ "${ns}:shape" = [ordered]@{ values = [ordered]@{ min = 0; max = 4 } } }
    $d.traits = [ordered]@{
        'minecraft:placement_direction' = [ordered]@{ enabled_states = @('minecraft:cardinal_direction'); y_rotation_offset = 0 }
        'minecraft:placement_position'  = [ordered]@{ enabled_states = @('minecraft:vertical_half') }
    }
    $perms = @()
    foreach ($f in 'north', 'east', 'south', 'west') {
        foreach ($half in 'bottom', 'top') {
            for ($s = 0; $s -le 4; $s++) {
                $qs = Stair-Quads $f $s
                $vis = [ordered]@{ lower = (Vis ($half -eq 'bottom')); upper = (Vis ($half -eq 'top')) }
                $boxes = [System.Collections.Generic.List[object]]::new()
                if ($half -eq 'bottom') { $boxes.Add((Box @(-8, 0, -8, 8, 8, 8))) } else { $boxes.Add((Box @(-8, 8, -8, 8, 16, 8))) }
                foreach ($q in $quads.Keys) {
                    $on = $qs -contains $q
                    $vis["lo_$q"] = Vis (($half -eq 'top') -and $on)
                    $vis["hi_$q"] = Vis (($half -eq 'bottom') -and $on)
                    if ($on) {
                        if ($half -eq 'bottom') { $boxes.Add((Box (Quad-Box $q 8 16))) } else { $boxes.Add((Box (Quad-Box $q 0 8))) }
                    }
                }
                $perms += [ordered]@{
                    condition  = "q.block_state('minecraft:cardinal_direction') == '$f' && q.block_state('minecraft:vertical_half') == '$half' && q.block_state('${ns}:shape') == $s"
                    components = [ordered]@{
                        'minecraft:geometry'      = [ordered]@{ identifier = 'geometry.complementaries.stairs'; bone_visibility = $vis }
                        'minecraft:collision_box' = $boxes.ToArray()
                    }
                }
            }
        }
    }
    Write-Block $Path $d $c $perms
    $script:soundOf["${ns}:$Path"] = $P.sound
}

function Write-Wall($P, [string]$Path) {
    $c = Base-Components $P
    $vis = [ordered]@{
        post      = "q.block_state('${ns}:wall_post')"
        core      = "!q.block_state('${ns}:wall_post') && q.block_state('${ns}:wall_north') + q.block_state('${ns}:wall_south') + q.block_state('${ns}:wall_east') + q.block_state('${ns}:wall_west') < 5"
        core_tall = "!q.block_state('${ns}:wall_post') && q.block_state('${ns}:wall_north') + q.block_state('${ns}:wall_south') + q.block_state('${ns}:wall_east') + q.block_state('${ns}:wall_west') >= 5"
    }
    foreach ($dname in 'north', 'east', 'south', 'west') {
        $vis["${dname}_low"] = "q.block_state('${ns}:wall_$dname') == 1"
        $vis["${dname}_tall"] = "q.block_state('${ns}:wall_$dname') == 2"
    }
    $c['minecraft:geometry'] = [ordered]@{ identifier = 'geometry.complementaries.wall'; bone_visibility = $vis }
    $c['minecraft:material_instances'] = [ordered]@{ '*' = (Material $P.tex) }
    $c['minecraft:light_dampening'] = 0
    $c['minecraft:connection_rule'] = [ordered]@{ accepts_connections_from = 'all' }
    if ($P.needsTool) {
        Loot-Table $Path @((Simple-Pool "${ns}:$Path" 1 'minecraft:is_pickaxe'))
        $c['minecraft:loot'] = "loot_tables/blocks/$Path.json"
    }
    $d = Describe $Path 'construction' 'minecraft:itemGroup.name.walls'
    $d.states = [ordered]@{ "${ns}:wall_post" = @($true, $false) }
    foreach ($dname in 'north', 'east', 'south', 'west') { $d.states["${ns}:wall_$dname"] = [ordered]@{ values = [ordered]@{ min = 0; max = 2 } } }
    $perms = @()
    for ($m = 0; $m -lt 32; $m++) {
        $post = ($m -band 16) -ne 0
        $conds = @(if ($post) { "q.block_state('${ns}:wall_post')" } else { "!q.block_state('${ns}:wall_post')" })
        $boxes = [System.Collections.Generic.List[object]]::new()
        if ($post) { $boxes.Add((Box @(-4, 0, -4, 4, 16, 4))) } else { $boxes.Add((Box @(-3, 0, -3, 3, 16, 3))) }
        $minx = if ($post) { -4 } else { -3 }; $maxx = - $minx; $minz = $minx; $maxz = $maxx
        $i = 0
        foreach ($dname in 'north', 'east', 'south', 'west') {
            $on = ($m -band (1 -shl $i)) -ne 0
            $conds += if ($on) { "q.block_state('${ns}:wall_$dname') > 0" } else { "q.block_state('${ns}:wall_$dname') == 0" }
            if ($on) {
                $a = $wallArm[$dname]
                $boxes.Add((Box @($a[0], 0, $a[1], $a[2], 16, $a[3])))
                switch ($dname) { 'north' { $minz = -8 } 'south' { $maxz = 8 } 'west' { $minx = -8 } 'east' { $maxx = 8 } }
            }
            $i++
        }
        $perms += [ordered]@{
            condition  = ($conds -join ' && ')
            components = [ordered]@{ 'minecraft:collision_box' = $boxes.ToArray(); 'minecraft:selection_box' = (Box @($minx, 0, $minz, $maxx, 16, $maxz)) }
        }
    }
    Write-Block $Path $d $c $perms
    $script:soundOf["${ns}:$Path"] = $P.sound
}

# ---- recipes -------------------------------------------------------------------------------------
function Item-Ref([string]$I) { if ($I.StartsWith('#')) { return [ordered]@{ tag = $I.Substring(1) } } return [ordered]@{ item = $I } }

function Shaped([string]$Id, [string[]]$Pattern, [hashtable]$Key, [string]$Result, [int]$Count = 1) {
    $k = [ordered]@{}; foreach ($ch in ($Key.Keys | Sort-Object)) { $k[$ch] = Item-Ref $Key[$ch] }
    Write-JsonFile (Join-Path $bp "recipes/$Id.json") ([ordered]@{ format_version = '1.20.10'; 'minecraft:recipe_shaped' = [ordered]@{
                description = [ordered]@{ identifier = "${ns}:$Id" }; tags = @('crafting_table'); pattern = $Pattern; key = $k; unlock = @((Item-Ref $Key[($Key.Keys | Sort-Object | Select-Object -First 1)]))
                result = [ordered]@{ item = $Result; count = $Count }
            }
        })
}
function Shapeless([string]$Id, [string[]]$Ingredients, [string]$Result, [int]$Count = 1, [string]$Tag = 'crafting_table') {
    Write-JsonFile (Join-Path $bp "recipes/$Id.json") ([ordered]@{ format_version = '1.20.10'; 'minecraft:recipe_shapeless' = [ordered]@{
                description = [ordered]@{ identifier = "${ns}:$Id" }; tags = @($Tag); ingredients = @($Ingredients | ForEach-Object { Item-Ref $_ }); unlock = @((Item-Ref $Ingredients[0]))
                result = [ordered]@{ item = $Result; count = $Count }
            }
        })
}
function Stonecut([string]$Id, [string]$From, [string]$Result, [int]$Count = 1) { Shapeless $Id @($From) $Result $Count 'stonecutter' }

# ---- the blocks ----------------------------------------------------------------------------------
$stone = @{ tool = 'pickaxe'; stone = $true; needsTool = $true; sound = 'stone'; blast = 6.0 }
$sets = @(
    @{ path = 'ash_bricks'; part = 'ash_bricks'; tex = 'cm_ash_bricks'; hard = 2.0; map = '#5a5a5e'; en = 'Ash Bricks'; zh = '灰烬砖块'; enPart = 'Ash Brick'; zhPart = '灰烬砖'; wall = $true; stairs = $true
        recipe = @{ pattern = @('##', '##'); key = @{ '#' = "${ns}:ash_brick" }; count = 1 }; cut = @()
    },
    @{ path = 'stone_tile'; part = 'stone_tile'; tex = 'cm_stone_tile'; hard = 1.5; map = '#8a8a8a'; en = 'Stone Tile'; zh = '石瓦'; enPart = 'Stone Tile'; zhPart = '石瓦'; wall = $true; stairs = $true
        recipe = @{ pattern = @('##', '##'); key = @{ '#' = 'minecraft:stone_bricks' }; count = 4 }; cut = @('minecraft:stone', 'minecraft:stone_bricks')
    },
    @{ path = 'blackstone_tile'; part = 'blackstone_tile'; tex = 'cm_blackstone_tile'; hard = 1.5; map = '#2a2328'; en = 'Blackstone Tile'; zh = '黑石瓦'; enPart = 'Blackstone Tile'; zhPart = '黑石瓦'; wall = $true; stairs = $true
        recipe = @{ pattern = @('##', '##'); key = @{ '#' = 'minecraft:polished_blackstone_bricks' }; count = 4 }; cut = @('minecraft:blackstone', 'minecraft:polished_blackstone', 'minecraft:polished_blackstone_bricks')
    },
    @{ path = 'lapis_bricks'; part = 'lapis_bricks'; tex = 'cm_lapis_bricks'; hard = 2.0; map = '#2a4fa6'; en = 'Lapis Bricks'; zh = '青金石砖'; enPart = 'Lapis Brick'; zhPart = '青金石砖'; wall = $true; stairs = $true
        recipe = @{ pattern = @('##', '##'); key = @{ '#' = 'minecraft:lapis_block' }; count = 4 }; cut = @('minecraft:lapis_block')
    },
    @{ path = 'checker_block'; part = 'checker'; tex = 'cm_checker_block'; hard = 1.5; map = '#9a9a9a'; en = 'Checker Block'; zh = '棋盘格方块'; enPart = 'Checker'; zhPart = '棋盘格'; wall = $false; stairs = $false
        recipe = @{ pattern = @('QB', 'BQ'); key = @{ Q = 'minecraft:quartz_block'; B = 'minecraft:polished_blackstone' }; count = 4 }; cut = @()
    }
)
foreach ($s in $sets) {
    $P = @{} + $stone + $s
    Add-Terrain $s.tex
    Write-FullBlock $P
    Add-Name "tile.${ns}:$($s.path).name" $s.en $s.zh
    Shaped $s.path $s.recipe.pattern $s.recipe.key "${ns}:$($s.path)" $s.recipe.count
    foreach ($from in $s.cut) { Stonecut "$($s.path)_from_$($from -replace '^minecraft:', '')_stonecutting" $from "${ns}:$($s.path)" 1 }
    $block = "${ns}:$($s.path)"
    $slab = "$($s.part)_slab"
    Write-Slab $P $slab
    Add-Name "tile.${ns}:$slab.name" "$($s.enPart) Slab" "$($s.zhPart)台阶"
    Shaped $slab @('###') @{ '#' = $block } "${ns}:$slab" 6
    Stonecut "${slab}_stonecutting" $block "${ns}:$slab" 2
    if ($s.stairs) {
        $st = "$($s.part)_stairs"
        Write-Stairs $P $st
        Add-Name "tile.${ns}:$st.name" "$($s.enPart) Stairs" "$($s.zhPart)楼梯"
        Shaped $st @('#  ', '## ', '###') @{ '#' = $block } "${ns}:$st" 4
        Stonecut "${st}_stonecutting" $block "${ns}:$st" 1
    }
    if ($s.wall) {
        $w = "$($s.part)_wall"
        Write-Wall $P $w
        Add-Name "tile.${ns}:$w.name" "$($s.enPart) Wall" "$($s.zhPart)墙"
        Shaped $w @('###', '###') @{ '#' = $block } "${ns}:$w" 6
        Stonecut "${w}_stonecutting" $block "${ns}:$w" 1
    }
}

# fine wood (+ stairs, slab)
$fine = @{ path = 'fine_wood'; tex = 'cm_fine_wood'; hard = 2.0; blast = 3.0; map = '#b08a5a'; tool = 'axe'; wood = $true; flammable = $true; sound = 'wood' }
Add-Terrain 'cm_fine_wood'
Write-FullBlock $fine
Add-Name "tile.${ns}:fine_wood.name" 'Fine Wood' '精制木材'
Shaped 'fine_wood' @('#', '#') @{ '#' = '#minecraft:wooden_slabs' } "${ns}:fine_wood" 1
Write-Stairs $fine 'fine_wood_stairs'
Add-Name "tile.${ns}:fine_wood_stairs.name" 'Fine Wood Stairs' '精制木材楼梯'
Shaped 'fine_wood_stairs' @('#  ', '## ', '###') @{ '#' = "${ns}:fine_wood" } "${ns}:fine_wood_stairs" 4
Write-Slab $fine 'fine_wood_slab'
Add-Name "tile.${ns}:fine_wood_slab.name" 'Fine Wood Slab' '精制木材台阶'
Shaped 'fine_wood_slab' @('###') @{ '#' = "${ns}:fine_wood" } "${ns}:fine_wood_slab" 6

# daub and timber frames
$daub = @{ hard = 1.5; blast = 3.0; map = '#d8cfb8'; tool = 'pickaxe'; sound = 'stone' }
$timber = @{ hard = 0.1; blast = 0.1; map = '#8f6a3c'; tool = 'axe'; wood = $true; flammable = $true; sound = 'wood'; render = 'alpha_test' }
$flipState = @{ "${ns}:flipped" = @($false, $true) }
foreach ($v in @(
        @{ p = 'daub'; base = $daub; en = 'Daub'; zh = '抹灰墙'; tex = 'cm_daub' },
        @{ p = 'daub_frame'; base = $daub; en = 'Daub Frame'; zh = '抹灰木框架'; tex = 'cm_daub_frame' },
        @{ p = 'daub_brace'; base = $daub; en = 'Daub Brace'; zh = '抹灰木斜撑'; tex = 'cm_daub_brace'; flip = $true },
        @{ p = 'daub_cross_brace'; base = $daub; en = 'Daub Cross Brace'; zh = '抹灰木交叉撑'; tex = 'cm_daub_cross_brace' },
        @{ p = 'timber_frame'; base = $timber; en = 'Timber Frame'; zh = '木框架'; tex = 'cm_timber_frame' },
        @{ p = 'timber_brace'; base = $timber; en = 'Timber Brace'; zh = '木斜撑'; tex = 'cm_timber_brace'; flip = $true },
        @{ p = 'timber_cross_brace'; base = $timber; en = 'Timber Cross Brace'; zh = '木交叉撑'; tex = 'cm_timber_cross_brace' }
    )) {
    $P = @{} + $v.base + @{ path = $v.p; tex = $v.tex }
    Add-Terrain $v.tex
    if ($v.flip) {
        Add-Terrain "$($v.tex)_flipped"
        $P.states = $flipState
        $P.perms = @([ordered]@{ condition = "q.block_state('${ns}:flipped')"; components = [ordered]@{
                    'minecraft:material_instances' = [ordered]@{ '*' = (Material "$($v.tex)_flipped" ($P.render ?? 'opaque')) }
                }
            })
    }
    $P.category = 'construction'
    Write-FullBlock $P
    Add-Name "tile.${ns}:$($v.p).name" $v.en $v.zh
}
Shaped 'daub' @('CW', 'WC') @{ C = 'minecraft:clay_ball'; W = 'minecraft:wheat' } "${ns}:daub" 2
Shaped 'timber_frame' @('###', '# #', '###') @{ '#' = 'minecraft:stick' } "${ns}:timber_frame" 4
Shapeless 'timber_brace' @("${ns}:timber_frame", 'minecraft:stick') "${ns}:timber_brace" 1
Shapeless 'timber_cross_brace' @("${ns}:timber_frame", 'minecraft:stick', 'minecraft:stick') "${ns}:timber_cross_brace" 1
Shapeless 'daub_frame' @("${ns}:timber_frame", "${ns}:daub") "${ns}:daub_frame" 1
Shapeless 'daub_brace' @("${ns}:timber_brace", "${ns}:daub") "${ns}:daub_brace" 1
Shapeless 'daub_cross_brace' @("${ns}:timber_cross_brace", "${ns}:daub") "${ns}:daub_cross_brace" 1

# simple blocks
foreach ($v in @(
        @{ path = 'soap_block'; tex = 'cm_soap_block'; hard = 1.25; blast = 4.2; map = '#f0b8d8'; tool = 'pickaxe'; sound = 'coral'; en = 'Soap Block'; zh = '肥皂块'
            extra = @{ 'minecraft:friction' = 0.06 }
        },
        @{ path = 'flint_block'; tex = 'cm_flint_block'; hard = 2.0; blast = 7.5; map = '#3c3c40'; tool = 'pickaxe'; stone = $true; needsTool = $true; sound = 'stone'; en = 'Flint Block'; zh = '燧石块' },
        @{ path = 'feather_block'; tex = 'cm_feather_block'; hard = 0.5; blast = 0.5; map = '#f2f2ee'; tool = 'hoe'; flammable = $true; sound = 'cloth'; en = 'Feather Block'; zh = '羽毛块'
            extra = @{ 'minecraft:collision_box' = (Box @(-8, 0, -8, 8, 14, 8)) }
        },
        @{ path = 'sugar_cube'; tex = 'cm_sugar_cube'; hard = 0.5; blast = 0.5; map = '#fbfbfb'; tool = 'shovel'; sound = 'sand'; en = 'Sugar Cube'; zh = '方糖'; category = 'items'
        }
    )) {
    Add-Terrain $v.tex
    Write-FullBlock $v
    Add-Name "tile.${ns}:$($v.path).name" $v.en $v.zh
}
Shaped 'flint_block' @('###', '###', '###') @{ '#' = 'minecraft:flint' } "${ns}:flint_block" 1
Shapeless 'flint_from_flint_block' @("${ns}:flint_block") 'minecraft:flint' 9
Shaped 'feather_block' @('###', '###', '###') @{ '#' = 'minecraft:feather' } "${ns}:feather_block" 1
Shaped 'sugar_cube' @('###', '###', '###') @{ '#' = 'minecraft:sugar' } "${ns}:sugar_cube" 1
Shapeless 'sugar_from_sugar_cube' @("${ns}:sugar_cube") 'minecraft:sugar' 9

# raked gravel
foreach ($i in 0..5) { Add-Terrain "cm_raked_gravel_$i" }
$rc = Base-Components @{ hard = 0.6; blast = 0.6; map = '#857f7c'; tool = 'shovel' }
$rc['minecraft:geometry'] = 'geometry.complementaries.raked_gravel'
$rc['minecraft:material_instances'] = [ordered]@{ '*' = (Material 'gravel'); up = (Material 'cm_raked_gravel_0') }
$rc['minecraft:collision_box'] = Box @(-8, 0, -8, 8, 15, 8)
$rc['minecraft:selection_box'] = Box @(-8, 0, -8, 8, 15, 8)
$rc['minecraft:light_dampening'] = 0
$rc['minecraft:loot'] = 'loot_tables/blocks/raked_gravel.json'
Loot-Table 'raked_gravel' @(
    [ordered]@{ rolls = 1; entries = @([ordered]@{ type = 'item'; name = 'minecraft:gravel'; weight = 9 }, [ordered]@{ type = 'item'; name = 'minecraft:flint'; weight = 1 }) }
)
$rd = Describe 'raked_gravel' 'nature'
$rd.states = [ordered]@{ "${ns}:shape" = [ordered]@{ values = [ordered]@{ min = 0; max = 5 } } }
$rperms = @(for ($i = 1; $i -le 5; $i++) {
        [ordered]@{ condition = "q.block_state('${ns}:shape') == $i"; components = [ordered]@{
                'minecraft:material_instances' = [ordered]@{ '*' = (Material 'gravel'); up = (Material "cm_raked_gravel_$i") }
            }
        }
    })
Write-Block 'raked_gravel' $rd $rc $rperms
$soundOf["${ns}:raked_gravel"] = 'gravel'
Add-Name "tile.${ns}:raked_gravel.name" 'Raked Gravel' '耙过的沙砾'

# ash layers
Add-Terrain 'cm_ash'
$ac = Base-Components @{ hard = 0.1; blast = 0.1; map = '#9a9a9a'; tool = 'shovel' }
$ac['minecraft:geometry'] = 'geometry.complementaries.layers'
$ac['minecraft:material_instances'] = [ordered]@{ '*' = (Material 'cm_ash') }
$ac['minecraft:light_dampening'] = 0
$ad = Describe 'ash' 'nature'
$ad.states = [ordered]@{ "${ns}:layers" = [ordered]@{ values = [ordered]@{ min = 1; max = 8 } } }
$aperms = @(for ($i = 1; $i -le 8; $i++) {
        $vis = [ordered]@{}; for ($j = 1; $j -le 8; $j++) { $vis["layers_$j"] = Vis ($j -eq $i) }
        $comp = [ordered]@{
            'minecraft:geometry'      = [ordered]@{ identifier = 'geometry.complementaries.layers'; bone_visibility = $vis }
            'minecraft:collision_box' = if ($i -eq 1) { $false } else { Box @(-8, 0, -8, 8, (2 * ($i - 1)), 8) }
            'minecraft:selection_box' = Box @(-8, 0, -8, 8, (2 * $i), 8)
            'minecraft:loot'          = "loot_tables/blocks/ash_$i.json"
        }
        if ($i -eq 8) { $comp['minecraft:light_dampening'] = 15 }
        Loot-Table "ash_$i" @((Simple-Pool "${ns}:ash" $i))
        [ordered]@{ condition = "q.block_state('${ns}:layers') == $i"; components = $comp }
    })
Write-Block 'ash' $ad $ac $aperms
$soundOf["${ns}:ash"] = 'sand'
Add-Name "tile.${ns}:ash.name" 'Ash' '灰烬'

# items
function Write-Item([string]$Path, [string]$Icon, $Extra, [string]$NameKey, [string]$Category = 'items') {
    $c = [ordered]@{ 'minecraft:icon' = $Icon; 'minecraft:display_name' = [ordered]@{ value = $NameKey }; 'minecraft:max_stack_size' = 64 }
    foreach ($k in $Extra.Keys) { $c[$k] = $Extra[$k] }
    Write-JsonFile (Join-Path $bp "items/$Path.json") ([ordered]@{ format_version = '1.21.60'; 'minecraft:item' = [ordered]@{
                description = [ordered]@{ identifier = "${ns}:$Path"; menu_category = [ordered]@{ category = $Category } }; components = $c
            }
        })
    $script:itemTex[$Icon] = [ordered]@{ textures = "textures/items/building/$($Icon -replace '^cm_', '')" }
}
Write-Item 'ash' 'cm_ash' @{ 'minecraft:block_placer' = [ordered]@{ block = "${ns}:ash"; replace_block_item = $true } } "tile.${ns}:ash.name" 'nature'
Write-Item 'ash_brick' 'cm_ash_brick' @{} "item.${ns}:ash_brick.name"
Add-Name "item.${ns}:ash_brick.name" 'Ash Brick' '灰烬砖'
Write-JsonFile (Join-Path $bp 'recipes/ash_brick.json') ([ordered]@{ format_version = '1.20.10'; 'minecraft:recipe_furnace' = [ordered]@{
            description = [ordered]@{ identifier = "${ns}:ash_brick" }; tags = @('furnace'); input = "${ns}:ash"; output = "${ns}:ash_brick"
        }
    })

# basalt deltas ash
Write-JsonFile (Join-Path $bp 'features/basalt_ash_feature.json') ([ordered]@{ format_version = '1.13.0'; 'minecraft:single_block_feature' = [ordered]@{
            description             = [ordered]@{ identifier = "${ns}:basalt_ash_feature" }
            places_block            = [ordered]@{ name = "${ns}:ash"; states = [ordered]@{ "${ns}:layers" = 1 } }
            enforce_placement_rules = $false; enforce_survivability_rules = $false
            may_attach_to           = [ordered]@{ bottom = @('minecraft:basalt', 'minecraft:blackstone', 'minecraft:magma') }
            may_replace             = @('minecraft:air')
        }
    })
Write-JsonFile (Join-Path $bp 'feature_rules/basalt_ash_feature_rule.json') ([ordered]@{ format_version = '1.13.0'; 'minecraft:feature_rules' = [ordered]@{
            description  = [ordered]@{ identifier = "${ns}:basalt_ash_feature_rule"; places_feature = "${ns}:basalt_ash_feature" }
            conditions   = [ordered]@{ placement_pass = 'after_surface_pass'; 'minecraft:biome_filter' = @([ordered]@{ test = 'has_biome_tag'; value = 'basalt_deltas' }) }
            distribution = [ordered]@{
                iterations = 96
                x          = [ordered]@{ distribution = 'uniform'; extent = @(0, 15) }
                y          = [ordered]@{ distribution = 'uniform'; extent = @(32, 110) }
                z          = [ordered]@{ distribution = 'uniform'; extent = @(0, 15) }
            }
        }
    })

# ---- fragments -----------------------------------------------------------------------------------
Write-JsonFile (Join-Path $rp 'terrain_texture.d/building.json') $terrain
Write-JsonFile (Join-Path $rp 'item_texture.d/building.json') $itemTex
$bj = [ordered]@{}; foreach ($k in $soundOf.Keys) { $bj[$k] = [ordered]@{ sound = $soundOf[$k] } }
Write-JsonFile (Join-Path $rp 'blocks.d/building.json') $bj
Write-TextFile (Join-Path $rp 'texts.d/en_US/building.lang') @($names | ForEach-Object { "$($_[0])=$($_[1])" })
Write-TextFile (Join-Path $rp 'texts.d/zh_CN/building.lang') @($names | ForEach-Object { "$($_[0])=$($_[2])" })
Write-Host "building: wrote $($soundOf.Count) blocks, $($terrain.Count) block textures"
