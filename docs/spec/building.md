# U10 `building`: building blocks

Behaviour reference for the building blocks of Supplementaries 4.0.2 that Complementaries ports in
unit U10 (`src/feature/building/`). Behaviour only: no code or assets come from Supplementaries.
Where this document gives a number that Supplementaries keeps in its config, the same number is the
default of our config (section 5).

## 1. Overview

Decorative and utility building blocks:

- **Ash**: a layered block (1 to 8 layers, like snow) that appears where fire burns blocks and is
  dropped by mobs that die burning. Rain slowly washes it away. It falls like sand and piles up.
  Smelted into **ash bricks** (with stairs, slab and wall).
- **Stone tile**, **blackstone tile**, **lapis bricks** (each with stairs, slab and wall),
  **checker block** (with slab), **fine wood** (with stairs and slab).
- **Daub** and **timber frames**: timber frame, brace and cross brace are see-through wooden frames;
  using daub on one fills it (daub frame / brace / cross brace). A frame used on a daub block is put
  around it. An axe takes the frame off again.
- **Raked gravel**: a hoe used on gravel rakes it; the raked lines join their neighbours.
- **Soap block** (slippery), **flint block**, **feather block** (cancels fall damage),
  **sugar cube** (falls, dissolves in water and rain, feeds horses).

Everything works on a vanilla Bedrock client: blocks, models and textures are data-driven (behavior
and resource pack) and every mechanic runs on the server. There are no screens.

## 2. Blocks and items

All ids are `complementaries:<path>`. "Hardness / blast" are the Java values; Bedrock uses them as
`seconds_to_destroy` / `explosion_resistance`. "Tool" is the tool that mines it fast; blocks marked
*needs tool* drop nothing without it.

| Path | Kind | Hardness / blast | Tool | Sound | Notes |
| ---- | ---- | ---------------- | ---- | ----- | ----- |
| `ash` | layer block 1-8 | 0.1 / 0.1 | shovel | sand | falls; drops 1 `ash` per layer; the item is a 2D icon |
| `ash_brick` | item | | | | smelted from `ash` |
| `ash_bricks` (+`_stairs`, `_slab`, `_wall`) | stone | 2.0 / 6.0 | pickaxe, needs tool | stone | |
| `stone_tile` (+`_stairs`, `_slab`, `_wall`) | stone | 1.5 / 6.0 | pickaxe, needs tool | stone | |
| `blackstone_tile` (+`_stairs`, `_slab`, `_wall`) | stone | 1.5 / 6.0 | pickaxe, needs tool | stone | |
| `lapis_bricks` (+`_stairs`, `_slab`, `_wall`) | stone | 2.0 / 6.0 | pickaxe, needs tool | stone | |
| `checker_block`, `checker_slab` | stone | 1.5 / 6.0 | pickaxe, needs tool | stone | |
| `daub` | full block | 1.5 / 3.0 | pickaxe | stone | |
| `daub_frame`, `daub_brace`, `daub_cross_brace` | full block | 1.5 / 3.0 | pickaxe | stone | brace: `flipped` |
| `timber_frame`, `timber_brace`, `timber_cross_brace` | see-through frame | 0.1 / 0.1 | axe | wood | brace: `flipped`; flammable |
| `raked_gravel` | 15/16 tall | 0.6 / 0.6 | shovel | gravel | falls; drops `minecraft:gravel` (flint 10 %) |
| `fine_wood` (+`_stairs`, `_slab`) | wood | 2.0 / 3.0 | axe | wood | flammable |
| `soap_block` | full block | 1.25 / 4.2 | pickaxe | coral | slipperiness 0.94 |
| `flint_block` | full block | 2.0 / 7.5 | pickaxe, needs tool | stone | |
| `feather_block` | full block | 0.5 / 0.5 | hoe / shears | wool | no fall damage; flammable |
| `sugar_cube` | full block | 0.5 / 0.5 | shovel | sand | falls; dissolves |

Block states (Bedrock names):

| Block | States |
| ----- | ------ |
| `ash` | `complementaries:layers` 1-8 |
| slabs | `minecraft:vertical_half` bottom/top (placement trait), `complementaries:double` bool |
| stairs | `minecraft:cardinal_direction` (placement trait), `minecraft:vertical_half` (placement trait), `complementaries:shape` 0 straight, 1 inner_left, 2 inner_right, 3 outer_left, 4 outer_right |
| walls | `complementaries:wall_north/east/south/west` 0 none, 1 low, 2 tall; `complementaries:wall_post` bool |
| `timber_brace`, `daub_brace` | `complementaries:flipped` bool |
| `raked_gravel` | `complementaries:shape` 0 north-south, 1 east-west, 2 north-east, 3 north-west, 4 south-east, 5 south-west |

### Recipes

| Result | Recipe |
| ------ | ------ |
| `ash_brick` | furnace: `ash` |
| `ash_bricks` | 2x2 `ash_brick` |
| `stone_tile` x4 | 2x2 `minecraft:stone_bricks`; stonecutter: stone, stone bricks |
| `blackstone_tile` x4 | 2x2 `minecraft:polished_blackstone_bricks`; stonecutter: blackstone, polished blackstone, polished blackstone bricks |
| `lapis_bricks` x4 | 2x2 `minecraft:lapis_block`; stonecutter: lapis block |
| `checker_block` x4 | `QB` / `BQ`: quartz block, polished blackstone |
| `<set>_stairs` x4 | 6 blocks in a stair pattern; stonecutter 1 |
| `<set>_slab` x6 | 3 blocks in a row; stonecutter 2 |
| `<set>_wall` x6 | 6 blocks in two rows; stonecutter 1 |
| `daub` x2 | `CW` / `WC`: clay ball, wheat |
| `timber_frame` x4 | ring of 8 sticks |
| `timber_brace` | `timber_frame` + stick |
| `timber_cross_brace` | `timber_frame` + 2 sticks |
| `daub_frame` / `daub_brace` / `daub_cross_brace` | the timber variant + `daub` |
| `fine_wood` | 2 wooden slabs stacked |
| `flint_block` | 3x3 flint; back to 9 flint |
| `feather_block` | 3x3 feather |
| `sugar_cube` | 3x3 sugar; back to 9 sugar |

The soap block is crafted from 4 soap (`complementaries:soap`, from the `tool` unit) in a 2x2 square and
turns back into 4 soap.

## 3. Mechanics

### Ash

- **Layers**: like snow. Using `ash` on the top face of an ash layer (or on a block whose placement
  position holds ash) adds one layer, up to 8. Collision is `(layers - 1) * 2` pixels; the outline is
  `layers * 2` pixels. A full block of ash (8 layers) is solid.
- **Gravity**: ash falls when the block below is air, fire, liquid or another replaceable block
  (2 ticks after it is placed or its neighbour changes). Falling ash landing on ash merges into it;
  layers past 8 go into a new ash block above.
- **From fire** (`ashFromFireChance`, default 1.0): when fire burns a flammable block away, the block
  becomes ash with that chance. Values above 1 add `floor(chance)` layers plus one more with the
  fractional chance.
- **From burning mobs** (`ashFromBurningMobs`, default true): a mob (not a player) that dies while on
  fire or from fire/lava damage drops 1 to 2 ash.
- **Rain** (`rainWashAsh`, default true): ash exposed to rain loses one layer now and then (on its
  random tick, 1 in 2); the last layer disappears.
- **Basalt deltas**: ash layers generate on the floor of basalt deltas (`basaltAsh`; the feature is
  data-driven and always on, see section 8).

### Stairs, slabs, walls

- Slabs place on the bottom or top half (click position, as vanilla). Using a slab on a single slab
  of the same kind from its empty side (top face or upper half of a side of a bottom slab, bottom
  face or lower half of a side of a top slab, or a slab in the placement position) makes a double
  slab. Double slabs drop 2.
- Stairs face the player and are upside down when placed on the top half of a face or on a ceiling.
  They join neighbouring stairs into inner and outer corners like vanilla stairs (this unit's stairs
  and every vanilla stairs block).
- Walls join walls (ours and vanilla), fences, fence gates, iron bars, glass panes and solid block
  faces. The post shows unless exactly two opposite sides connect and there is nothing above; a side
  is tall when the block above is a wall connection on that side or a solid block.

### Daub and timber frames

- Timber frames are see-through and have full collision.
- Braces: placed `flipped` when you click the right half of the face (as you look at it; on the
  top or bottom face, the half to the player's right), so the diagonal runs the way you pointed.
- Using `daub` on a timber frame / brace / cross brace turns it into the daub variant (the brace keeps
  its direction) and uses one daub (`replaceDaub`, default true).
- Using a timber frame / brace / cross brace item on a `daub` block puts the frame around it (the
  block becomes the daub variant) and uses the item. Sneaking places the frame normally; with
  `swapOnShift` it is the other way round.
- Using an axe on a daub frame / brace / cross brace (`axesStrip`, default true) takes the frame off:
  the block becomes `daub`, the timber item drops, the axe loses 1 durability.

### Raked gravel

- Using a hoe on the top or a side of gravel with air above rakes it (hoe till sound, 1 durability).
- Its lines run along the player's view and then join raked gravel next to it like rails: two
  neighbours on perpendicular sides make a corner.
- It turns back into gravel when a solid block is placed on it, and falls like gravel.

### Soap block, flint block

- The soap block is slippery (slipperiness 0.94, Bedrock friction 0.06).
- The flint block is a storage block.

### Feather block

- Entities that fall onto a feather block take no fall damage. Its collision is 14 pixels tall, so
  you sink in a little.

### Sugar cube

- Falls like sand. It dissolves (disappears with a fizz) when it touches water on any side or lands
  in water, and on a random tick while it is rained on.
- Using a sugar cube on a horse, donkey or mule feeds it: it heals 2 health and gets Speed II for
  `horseSpeedDuration` seconds (default 10). One cube is used (not in creative).

## 4. Screens

None. On a vanilla client everything above works unchanged (data-driven blocks, server logic).

## 5. Configuration

`config/building.json`:

| Key | Default | Meaning |
| --- | ------- | ------- |
| `version` | 1 | |
| `ashFromFireChance` | 1.0 | chance that a burned block leaves ash (> 1: more layers) |
| `ashFromBurningMobs` | true | burning mobs drop ash |
| `rainWashAsh` | true | rain washes ash away |
| `swapOnShift` | false | timber frames: frame a daub block only while sneaking |
| `axesStrip` | true | axes take the frame off daub frames |
| `replaceDaub` | true | daub fills timber frames |
| `horseSpeedDuration` | 10 | seconds of Speed from a sugar cube |
| `sugarCubeDissolves` | true | sugar cubes dissolve in water and rain |

## 6. Commands

None.

## 7. Strings

Block and item names (RP `texts.d/<locale>/building.lang`): `tile.complementaries:<path>.name` and
`item.complementaries:<path>.name`, English as in Supplementaries ("Ash", "Ash Bricks",
"Ash Brick Stairs", "Ash Brick Slab", "Ash Brick Wall", "Stone Tile", "Stone Tile Stairs",
"Blackstone Tile", "Lapis Bricks", "Checker Block", "Checker Slab", "Daub", "Daub Frame",
"Daub Brace", "Daub Cross Brace", "Timber Frame", "Timber Brace", "Timber Cross Brace",
"Raked Gravel", "Fine Wood", "Soap Block", "Flint Block", "Feather Block", "Sugar Cube").

Mod strings (`assets/lang/<locale>/building.json`) are only the log/self-test descriptions
`cm.building.*`.

## 8. Not ported / differences

- **Framed blocks** (a timber frame holding any block): needs a block entity that renders another
  block's model inside the frame. Bedrock custom blocks have no block entities and their model is
  fixed per state, so only daub can be framed.
- **Checker vertical slab**: Supplementaries registers it only together with Quark, which has no
  Bedrock version.
- **Falling animation**: falling ash, raked gravel and sugar cubes move down one block per tick as
  blocks instead of as a falling-block entity (Bedrock's falling-block entity only lands vanilla
  falling blocks correctly).
- **Wall height**: Bedrock limits custom collision boxes to one block, so our walls are 1 block tall
  for collision (Java walls are 1.5).
- **`basaltAsh` toggle**: the basalt deltas ash is a behavior pack feature rule; it can't be switched
  off at run time.
- **Random ticks**: custom blocks get no vanilla random ticks, so ash and sugar cubes keep a queued
  block tick in their chunk's tick queue (every 20 to 120 s, saved with the chunk); rain washing and
  dissolving run on it at about the vanilla average rate.
