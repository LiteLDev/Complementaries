# U9 `lighting`: sconces, candle holders, stone lamps, wall lanterns

Behaviour reference: Supplementaries 4.0.2 (MC 1.21.1). Behaviour only; every model, texture and
line of code here is our own. Selftest area: `dx` 900..999.

## 1. Blocks and items

All ids are `complementaries:<path>`. Java has a separate wall block for every sconce
(`sconce_wall`, `sconce_wall_soul`, ...) that shares the floor block's item, like torches. Bedrock
has one block per item instead: the attachment is the vanilla `minecraft:block_face` state
(`up` = standing on the floor, `north|south|east|west` = on a wall, facing away from it). The Java
wall ids are therefore not separate blocks here.

| Id | Kind | Light (lit) | Notes |
|----|------|-------------|-------|
| `sconce` | sconce | 14 | iron sconce holding a torch flame |
| `sconce_soul` | sconce | 10 | soul fire flame |
| `sconce_ender` | sconce | 13 | purple (ender) flame |
| `sconce_glow` | sconce | 15 | cyan (glow) flame |
| `sconce_green` | sconce | 14 | green flame (nether brass / copper torch flame) |
| `sconce_lever` | sconce, wall only | 14 | a wall sconce that is also a lever |
| `candle_holder` | candle holder | 3 per candle | plain (undyed) candles |
| `candle_holder_<color>` | candle holder | 3 per candle | 16 colours: white, orange, magenta, light_blue, yellow, lime, pink, gray, light_gray, cyan, purple, blue, brown, green, red, black |
| `stone_lamp` | lamp | 15 | full block |
| `blackstone_lamp` | lamp | 15 | full block |
| `deepslate_lamp` | lamp | 15 | full block |
| `end_stone_lamp` | lamp | 15 | full block |
| `wall_lantern` | wall lantern | lantern's | no item: placed by using a lantern on a wall |

The compat sconces of Java (`sconce_ender` needs the Endergetic Expansion's ender torch,
`sconce_glow` Infernal Expansion's glow torch, `sconce_green` a green-flame torch) are always
enabled here, with recipes made from vanilla Bedrock ingredients (end rod, glowstone dust, copper
torch).

### Block states

| Block | States |
|-------|--------|
| sconces | `minecraft:block_face` (`up`, `north`, `south`, `east`, `west`), `complementaries:lit` (bool, default true) |
| `sconce_lever` | `minecraft:block_face` (walls only), `complementaries:lit` (default true), `complementaries:powered` (bool) |
| candle holders | `minecraft:block_face` (`up` floor, `down` ceiling, sides = wall), `minecraft:cardinal_direction` (rotation on the floor / ceiling), `complementaries:candles` (1..4), `complementaries:lit` (bool, default false) |
| `wall_lantern` | `minecraft:block_face` (sides), `complementaries:lantern` (0..9, see below), `complementaries:swing` (0..8, animation frame) |

Every sconce and candle holder is waterloggable (`minecraft:liquid_detection`, water can be
contained). Lamps have no states.

`complementaries:lantern`: 0 `lantern`, 1 `soul_lantern`, 2 `copper_lantern`, 3
`exposed_copper_lantern`, 4 `weathered_copper_lantern`, 5 `oxidized_copper_lantern`, 6..9 the
`waxed_` variants of 2..5 (all `minecraft:`).

### Recipes (shaped unless noted)

| Result | Pattern | Keys |
|--------|---------|------|
| 2 `sconce` | `NTN` / ` N ` | N iron nugget, T torch |
| 2 `sconce_soul` | `NTN` / ` N ` | T soul torch |
| 2 `sconce_ender` | `NTN` / ` N ` | T end rod |
| 2 `sconce_glow` | `NGN` / ` N ` | G glowstone dust |
| 2 `sconce_green` | `NTN` / ` N ` | T copper torch |
| `sconce_lever` | shapeless | sconce + lever |
| `candle_holder` | `C` / `N` | C candle, N iron nugget |
| `candle_holder_<color>` | `C` / `N` | C `<color>_candle` |
| 8 `stone_lamp` | `SSS` / `SBS` / `SSS` | S stone, B blaze powder |
| 8 `blackstone_lamp` | same | S polished blackstone |
| 8 `deepslate_lamp` | same | S polished deepslate |
| 8 `end_stone_lamp` | same | S end stone bricks |

### Drops

Sconces, the sconce lever and lamps drop themselves. A candle holder drops one candle holder item
per candle. A wall lantern drops the lantern it holds. Explosions drop the same.

## 2. Mechanics

### Placement

- Sconces: on the top of a block (standing) or on its side (on the wall), like torches. Not on
  ceilings. They pop off (drop) when the supporting block goes.
- `sconce_lever`: walls only.
- Candle holders: on the floor, the ceiling or a wall; floor and ceiling holders turn to face the
  player. Using a candle holder item on a candle holder of the **same** id with fewer than 4
  candles adds a candle instead of placing a new block (the item is used up outside creative).
- Lamps: plain full blocks.
- Wall lanterns: using a `minecraft:lantern`, `soul_lantern` or a copper lantern on the **side** of
  a block whose face is sturdy places a `wall_lantern` hanging from a bracket on that wall (Java
  "wall lanterns" tweak; vanilla Bedrock would only stand it on the floor). Lanterns used on the top
  or bottom of a block keep their vanilla behaviour. Blocked by the config blacklist. A wall
  lantern drops its lantern when its wall goes.

### Lighting and extinguishing ("lightable" blocks: sconces and candle holders)

| Action | Effect |
|--------|--------|
| Flint and steel on an unlit block | lights it, damages the flint and steel by 1, sound `fire.ignite` |
| Fire charge on an unlit block | lights it, uses the charge (not in creative), sound `mob.ghast.fireball` |
| A burning projectile (flaming arrow, fireball) hits it | lights it |
| Empty hand on a lit block | puts it out, sound `random.fizz` (candle holders: `extinguish.candle`), smoke |
| A splash water potion lands on it or next to it (1 block) | puts it out |
| Water fills the block (waterlogged) | puts it out; a waterlogged block cannot be lit |
| Placed in water | placed unlit |

Placed state: sconces are placed lit, candle holders unlit (like candles).

### Sconce lever

Using the sconce lever with an empty hand (or any item that does not light it) toggles
`powered`: a redstone signal of 15 on every side and strongly through the wall it hangs on, like a
lever, with the lever click sound (pitch 0.6 on, 0.5 off). The flame is independent of the power
(it can be lit/unlit as any sconce, with flint and steel, projectiles and water).

### Particles

Lit sconces show smoke plus a flame of their fire type (torch flame, soul flame, ender, glow,
green) above the flame, at the rate of vanilla torches. Lit candle holders show the flame itself
in the model (always visible) and an occasional smoke puff above each wick. Particles are spawned
by the server around each player (random sampling of the blocks within 16 blocks, like the
client's animate tick), so vanilla clients see them too.

### Wall lanterns

- Light level of the held lantern (lantern 15, soul lantern 10, copper lanterns 15).
- Swinging: a mob, player or other entity moving through the wall lantern sets it swinging
  side to side, in the direction of the entity's motion, with an amplitude growing with its speed;
  the swing decays over about 3 seconds. Bedrock blocks cannot be animated, so the swing is played
  as `complementaries:swing` frames (0 = at rest, 1..4 = 5..20 degrees one way, 5..8 = the other).
- Breaking or picking (pick block) gives the lantern.

### Lamps

Decorative full blocks with light level 15; mineable with a pickaxe.

## 3. Screens

None. (No GUI in this unit, so no vanilla-client form fallback is needed.)

## 4. Configuration: `config/lighting.json`

| Key | Default | Meaning |
|-----|---------|---------|
| `version` | 1 | config version |
| `wallLanterns` | true | place lanterns on walls (Java `tweaks.wall_lantern.placement`) |
| `wallLanternBlacklist` | [] | lantern item ids that never become wall lanterns |
| `wallLanternSwing` | true | entities make wall lanterns swing |
| `fireProjectilesLight` | true | burning projectiles light sconces and candle holders |
| `waterPotionsExtinguish` | true | splash water potions put them out |
| `particles` | true | server-side flame/smoke particles |
| `particleSamplesPerTick` | 667 | random block samples per player per tick for particles |

## 5. Commands

None.

## 6. Strings

Block / item names (RP `texts.d/<locale>/lighting.lang`): `tile.complementaries:<id>.name` and
`item.complementaries:<id>` with the Supplementaries names: Sconce, Soul Sconce, Ender Sconce,
Glow Sconce, Green Sconce, Sconce Lever, Candle Holder, `<Colour>` Candle Holder, Stone Lamp,
Blackstone Lamp, Deepslate Lamp, End Stone Lamp, Wall Lantern.

Mod strings (`assets/lang/<locale>/lighting.json`): `cm.lighting.lit` "Lit", `cm.lighting.unlit`
"Unlit", `cm.lighting.candles` "Candles: {}" (debug / log text).

## 7. Not ported

| Feature | Reason |
|---------|--------|
| Separate `sconce_wall*` block ids | Bedrock blocks carry the face as a state; one block per item. |
| Smooth (per-frame) wall lantern swing | Bedrock blocks cannot be animated; the swing is a sequence of 9 pre-rotated frames switched every tick (block state). |
| Compat-mod torches (Endergetic, Infernal Expansion) | Those mods don't exist on Bedrock; the sconces are crafted from vanilla items instead. |
| Wall lanterns for modded lanterns | Only vanilla lanterns exist on Bedrock (lantern, soul lantern, 8 copper lanterns). |
