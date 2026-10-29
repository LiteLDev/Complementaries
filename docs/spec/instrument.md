# U6 `instrument`: globes, wind vane, clock block, hourglass, altimeter

Behaviour of Supplementaries 4.0.2 (MC 1.21.1) that this unit reproduces, and how it maps to Bedrock.
Behaviour reference only: no code or assets are copied from Supplementaries.

Source module: `src/feature/instrument/` (namespace `cm::feature::instrument`). World data keys:
`cm:instrument:*`. Config section: `config/instrument.json`. Selftests: `instrument.*` (dx 600..699).

## 1. Blocks and items

| Id | Kind | Obtained | Drops |
|----|------|----------|-------|
| `complementaries:globe` | block | Wandering Trader (10 emeralds, max 3 trades), shipwreck treasure chests (25 %), soap on a sepia globe | itself, keeping the custom name (unless destroyed by an explosion that does not survive) |
| `complementaries:globe_sepia` | block | globe + antique ink (shapeless), Wandering Trader (12 emeralds) | itself, keeping the custom name |
| `complementaries:wind_vane` | block | ` I ` / `IBI` / ` R ` (I iron ingot, B iron bars, R redstone) | itself |
| `complementaries:clock_block` | block | `PPP` / `PCP` / `PPP` (P any planks, C clock) | itself, 2 when two-faced |
| `complementaries:hourglass` | block | `gGg` / ` g ` / `gGg` (G gold ingot, g glass) | itself + its contents |
| `complementaries:altimeter` | item | ` c ` / `cRc` / ` c ` (c copper ingot, R redstone) | - |

Recipes: `globe_sepia` = globe + `complementaries:antique_ink`; `globe` = sepia globe + `complementaries:soap`
(both ingredients belong to other units; the recipes resolve once those units are present).

Tooltips (hint lines, gray italic): globe `Sold by the Wandering Trader` (`message.supplementaries.globe`), hourglass
`Accepts any dust, sand or powder` (`message.supplementaries.hourglass`), wind vane `Reacts to weather`
(`message.supplementaries.wind_vane`).

### Block states

Supplementaries (Java) → Complementaries (Bedrock; every state has at most 16 values):

| Block | Java states | Bedrock states |
|-------|-------------|----------------|
| globe, globe_sepia | `facing` (4), `powered`, `rotation` 0..3, `waterlogged` | `minecraft:cardinal_direction` (placement trait, faces the player), `complementaries:angle` 0..15 (sphere yaw in 22.5° steps; at rest `= 4 * (3 - rotation)`), `complementaries:skin` 0..12 (model + palette, see 3.1), `complementaries:map` 0..15 (world map variant). `powered` and `rotation` live in the world DB. |
| clock_block | `facing` (4), `hour` 0..24, `two_faced`, `waterlogged` | `minecraft:cardinal_direction`, `complementaries:hour` 0..11 (hour-hand position on the 12 h dial), `complementaries:minute` 0..11 (minute hand, 5 min steps), `complementaries:two_faced` 0..1 |
| hourglass | `facing` (6), `light_level` 0..15, `waterlogged` | `minecraft:block_face` (placement trait: the clicked face), `complementaries:level` 0..15 (sand progress), `complementaries:dust` 0..15 + `complementaries:dust_page` 0..1 (sand texture and light) |
| wind_vane | `wind_strength` 0..3, `waterlogged` | `complementaries:wind` 0..3, `complementaries:yaw` 0..15 + `complementaries:yaw_fine` 0..3 (vane yaw in 64 steps of 5.625°) |

Waterlogging uses `minecraft:liquid_detection`; pistons cannot move these blocks (`minecraft:movable` immovable;
in Java they are block entities, which pistons do not push).

## 2. Mechanics

### 2.1 Globe

- Placement needs a sturdy block below (`canSurvive`: center support); breaks when the support goes away.
  Faces the player. Shape 12×15×12 px.
- **Use (right click, any hand, not sneaking with an item):** spins the globe: yaw += 450°, the resting face turns
  by 90° (`rotation` ← next), sound `block.globe.spin` (volume 0.65, pitch 0.9 + weighted 0..0.2). Spin decays
  every tick: `yaw = yaw * 0.94 - 0.7` until ≤ 0 (≈ 3 s). Repeated clicks accumulate.
- With `show_coordinates` (default true) the user sees `X: <x>, Z: <z>` (`message.supplementaries.compass`) on
  the action bar; in unnatural dimensions (nether, end) the numbers are obfuscated unless
  `works_in_unnatural_dimensions`.
- **Redstone:** a rising edge of neighbour power spins the globe (same as a click, no coordinates).
- **Comparator:** 15 while spinning, otherwise `rotation_face/90 + 1` (1..4).
- **Shears:** toggle the *sheared* model (a quarter cut out), sheep shear sound, break particles.
- **Custom name** (from a named item): special globes, case-insensitive:
  - `flat`, `flat earth` → flat (paper-thin) globe with the world map;
  - `moon`, `luna`, `selene`, `cynthia` → moon;
  - `earth`, `terra`, `gaia`, `gaea`, `tierra`, `tellus`, `terre` → real Earth map (sepia variant on sepia globes);
  - `sun`, `sol`, `helios` → sun;
  - `round`, `sphere`, `spherical` → round model with the world map;
  - also the translated names `Flat`, `Moon`, `Earth`, `Sun`, `Round` (`globe.supplementaries.*`) in the server
    language.
- **World map:** each world has a globe seed (default: the world seed). The map is a procedurally generated
  32×16 cube-map texture (continents, polar ice caps, hot biomes near the equator, coasts, mushroom islands,
  icebergs), coloured with a 13-colour palette per dimension (overworld, nether, end) or the sepia palette.
  `random_world` (default true) off → every globe shows the Earth map.
- Sepia globe: same block, sepia palettes; soap washes it back to a globe (soap unit).
- Commands: `/supplementaries globe newseed` (random new globe seed, `Globe seed changed`),
  `/supplementaries globe resetseed` (back to the world seed, `Globe seed reset`). Admin (level 2).

### 2.2 Wind vane

- Shape 12×16×12 px. Every 20 ticks the server sets `wind_strength`: 2 thundering, 1 raining, 0 clear
  (dimensions without weather: 0); 3 while *wind charged*.
- Redstone: emits `wind_strength` as a weak signal on every side and a strong signal into the block below;
  comparators read `wind_strength`.
- An explosion next to it (wind charge, TNT, ...) *wind charges* it for 40 ticks: the vane spins 5 full turns,
  `wind_strength` 3, sound `block.wind_vane.spin`.
- Animation (client side in Java): yaw = `max_angle_1·sin(2π·t/period_1) + max_angle_2·sin(2π·t/period_2)` where
  `t` advances by `max(1, wind_strength · power_scaling)` per tick (defaults 30°, 10°, 450, 150, 3.0), with a
  per-position phase. Occasional creaking sound (1 in 1200 / 380 / 220 ticks for strength 0 / 1 / 2+).

### 2.3 Clock block

- Faces the player; front shape 16×16×15 px. Placing a clock block against the back of a single-faced clock makes
  it two-faced (dial and hands on both sides, drops 2).
- Hands show the time of day: hour hand jumps per hour, minute hand moves in 7.2° (1.2 min) steps; in dimensions
  where clocks don't work (`works_in_unnatural_dimensions` false) the hands spin randomly.
- Every 20 ticks (while `doDaylightCycle`): comparator power `clamp(daytime / 1500, 0, 15)` updates and a tick
  sound plays (`block.clock.tick_1`/`_2` alternating by minute, volume 0.08). Comparators read 0 in unnatural
  dimensions. (Complementaries also updates the power with the cycle off, so `/time set` moves it; only the
  tick sound needs the cycle.)
- Use (empty hand or any item that is not a clock block placed on the back): shows the time on the action bar,
  `H:MM` 24 h (default) or `H:MM AM/PM` (`clock_24h` false); obfuscated random time in unnatural dimensions.
- `clock_right_click` (default true): using a vanilla clock item anywhere shows the same message.

### 2.4 Hourglass

- Placed facing the clicked face (6 directions). Shape 8×16×8 (vertical) or rotated.
- Holds one item of a *dust*: right click with a dust inserts one (sand place sound); right click with an empty
  hand takes it out. Sneak + empty hand on a vertical hourglass flips it (up ↔ down, rotate sound).
- Facing up: progress goes 0 → 1 by `1 / duration` per tick; facing down: 1 → 0. Inserting resets the progress
  (0 up, 1 down). Sideways: frozen.
- Comparator: `1 + floor(progress · 14)` facing up, `1 + floor((1 - progress) · 14)` facing down.
- Light level from the dust; the sand is drawn with the dust's texture.
- Dusts (ticks for a full flow): sand / red sand / suspicious sand 70, sugar & sugar cube 40, ash 95, concrete
  powder 105, gunpowder 150, glowstone dust 190 (light 10), soul sand 211, blaze powder 277 (light 6), redstone
  400 (light 4), slime ball / slime block 1750, honey block 2000, any other dust 150.
- Hoppers insert from the top while empty, and extract only when the flow is finished.

### 2.5 Altimeter (depth meter)

- Use: shows `Y: <y>` (`message.supplementaries.altimeter`) on the action bar (`click_action`, default true).
- The item shows a depth gauge: a 5 px window over a vertical strip (sky → surface → caves → deepslate → lava)
  scrolling with the holder's height between the dimension's build limits; per-dimension strips for the
  overworld, nether and end.

## 3. Bedrock design

### 3.1 Visuals (data-driven, identical on modded and vanilla clients)

Bedrock custom blocks cannot have block entities or renderers, so every animated part is a bone of the block
geometry shown or hidden with `bone_visibility` Molang on block states, and the server drives the states:

- Globe: stand + 16 sphere bones per model (globe cube, flat, sheared, round) at 22.5° yaw steps on an axis tilted
  22.5°; `skin` picks the model and material: 0..2 map (overworld/nether/end palette), 3..5 flat, 6 moon, 7 earth,
  8 sun, 9 sheared, 10..12 round. `map` picks one of 16 pre-generated world maps (`instrument.ps1` runs the same
  generator for 16 seeds per palette); the globe seed is hashed to a variant. During a spin the server updates
  `angle` every tick.
- Wind vane: 64 vane bones; the server evaluates the sway formula per tick and writes `yaw`/`yaw_fine` when the
  bucket changes (only while a player is within 64 blocks).
- Clock: 12 hour-hand and 12 minute-hand bones per face, drawn as pixel lines.
- Hourglass: 16 sand levels per bulb + the falling stream; material per dust via permutations.
- Altimeter: static icon (mid-depth window) in the inventory; Bedrock items have no dynamic icons.

### 3.2 Server logic

- Every placed block is tracked in the world DB (`cm:instrument:b:<dim>:<x>:<y>:<z>`, JSON with the per-block data:
  globe rotation/name/sheared/powered, hourglass item + progress, wind-charge ticks) and in memory, found through
  `BlockChangedEvent` (placement, breaking, explosions, `/setblock`, `/fill`).
- Comparator output: our block types get a private copy of their vtable with `hasComparatorSignal` /
  `getComparatorSignal` replaced (only these block types are affected).
- Redstone input (globe): `minecraft:redstone_consumer` installs a consumer in the circuit graph; the server reads
  its strength. Redstone output (wind vane): `minecraft:redstone_producer` per `wind` permutation.
- Messages use the action bar (`SetTitlePacket`), sounds `PlaySoundPacket` with our sound events.

## 4. Screens

None. (No screen in Supplementaries either; the clock/globe/altimeter messages use the action bar.)

## 5. Configuration (`config/instrument.json`)

| Key | Default | Supplementaries key |
|-----|---------|---------------------|
| `globe` / `globeSepia` / `windVane` / `clockBlock` / `hourglass` / `altimeter` | true | feature toggles (disabled blocks stop ticking and interacting) |
| `globeShowCoordinates` | true | `building.globe.show_coordinates` |
| `globeRandomWorld` | true | client `blocks.globe.random_world` |
| `clockRightClick` | true | `tweaks.clock_and_compass.clock_right_click` |
| `worksInUnnaturalDimensions` | false | `tweaks.clock_and_compass.works_in_unnatural_dimensions` |
| `clock24h` | true | client `blocks.clock_block.24h_format` |
| `windVanePowerScaling` | 3.0 | client `blocks.wind_vane.power_scaling` |
| `windVaneMaxAngle1` / `windVaneMaxAngle2` | 30.0 / 10.0 | client `blocks.wind_vane.max_angle_1/2` |
| `windVanePeriod1` / `windVanePeriod2` | 450.0 / 150.0 | client `blocks.wind_vane.period_1/2` |
| `altimeterClickAction` | true | client `items.altimeter.click_action` |

Client options of Supplementaries are server options here, because the server drives the visuals.

## 6. Commands

`/supplementaries globe newseed`, `/supplementaries globe resetseed` (op).

## 7. Strings

| Key | English |
|-----|---------|
| `message.supplementaries.altimeter` | `Y: {}` |
| `message.supplementaries.compass` | `X: {}, Z: {}` (referenced, owned by the compass tweak; English fallback) |
| `message.supplementaries.globe` | `Sold by the Wandering Trader` |
| `message.supplementaries.hourglass` | `Accepts any dust, sand or powder` |
| `message.supplementaries.wind_vane` | `Reacts to weather` |
| `commands.supplementaries.globe_changed` | `Globe seed changed` |
| `commands.supplementaries.globe_reset` | `Globe seed reset` |
| `globe.supplementaries.flat` / `moon` / `earth` / `sun` / `round` | `Flat` / `Moon` / `Earth` / `Sun` / `Round` |
| `cm.instrument.am` / `cm.instrument.pm` | ` AM` / ` PM` |
| block / item names (RP `texts.d`) | Globe, Sepia Globe, Wind Vane, Clock Block, Hourglass, Altimeter |

## 8. Not ported

| Feature | Reason |
|---------|--------|
| Smooth (per-frame) animation of globe, wind vane and clock hands | no block entity renderers for custom blocks; animated through server-driven block states at 20 Hz in 22.5° (globe), 5.6° (vane), 30° (hands) steps |
| A unique globe texture for every seed | block textures are static pack files; the seed selects one of 16 generated maps per palette |
| Altimeter icon moving with the height | Bedrock item icons are static (no item model overrides) |
| Advancement *You Spin Me Right Round Baby* | Bedrock has no custom advancements |
| Wandering Trader offers and the shipwreck loot injection | vanilla trade/loot tables are shared files (one owner); left to the integration of the trade/loot tables |
| Hopper insertion/extraction for the hourglass | hoppers only talk to block-entity containers |
| Earth-day / Christmas / April-fools variants, supporter globes, Create/Flywheel compat | seasonal or mod-integration extras |
| Soap washing a placed sepia globe | belongs to the soap unit (the soap recipe is provided) |
| Item hint tooltips (`Sold by the Wandering Trader`, `Accepts any dust, sand or powder`, `Reacts to weather`) | Bedrock items show no extra tooltip lines without per-stack lore; the strings are kept for the units that add lore |
