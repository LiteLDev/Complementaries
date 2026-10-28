# U1 `jar`: jar, soft fluids, faucet

Behaviour reference: Supplementaries 4.0.2 (MC 1.21.1) and the soft-fluid system of its library
(Moonlight). This file describes behaviour only; no code or asset is taken from either project.

## 1. Blocks and items

| Id | Kind | Notes |
| -- | ---- | ----- |
| `complementaries:jar` | block + block item | Glass, hardness 0.5, blast resistance 1, glass sounds, light from its contents. |
| `complementaries:faucet` | block + block item | Iron, hardness 3, blast resistance 4.8, iron-bars sounds. |
| `complementaries:jar_boat` | block + block item | "Boat in a Jar", decorative, glass, faces the player. |
| `complementaries:jar_mob` | helper entity | Invisible, no hitbox, not saved as a mob: it only draws the mob captured in a jar (§3.6). |

### Recipes

- **Jar** (shaped): `G S G` / `G _ G` / `G G G`, `G` = glass, `S` = any wooden slab. 1 jar.
- **Faucet** (shaped): `_ R _` / `I I I` / `_ _ I`, `R` = stick, `I` = iron ingot. 1 faucet.
- **Boat in a Jar** (shapeless): jar + any boat.

### Block states

Jar (`complementaries:jar`):
- `complementaries:content` 0..63: what the jar shows (0 empty, 1..47 a fluid / fluid colour, 48..50
  a cookie kind, 62/63 "light only" for luminous mobs). Drives the fluid texture, the cookie texture
  and the light emission (`minecraft:light_emission` by permutation).
- `complementaries:level` 0..12: fluid height in pixels or number of cookies.

Faucet (`complementaries:faucet`):
- `minecraft:cardinal_direction` (placement direction = the face clicked, or north when placing on
  a top/bottom face, as in Supplementaries: the spout points away from the block behind it),
- `complementaries:enabled` (toggled by hand), `complementaries:powered` (redstone),
  open = enabled XOR powered,
- `complementaries:connected` (a jar / beehive below: the faucet model sits lower, on the lid),
- `complementaries:content` 0..63: the fluid behind the faucet (0 = none = Java `has_water` false);
  drives the colour of the stream and the light (lum/2 clamped to 1..7).

Boat in a Jar: `minecraft:cardinal_direction`, `complementaries:wood` 0..9 (oak, spruce, birch,
jungle, acacia, dark oak, mangrove, cherry, bamboo, pale oak).

### Drops

- Jar: always drops itself **with its contents** (fluid, cookies or mob are kept in the item, like
  a shulker box). Breaking in creative drops nothing. Pistons destroy the jar (it drops).
- Faucet, Boat in a Jar: themselves.

## 2. Soft fluids

A soft fluid is a virtual fluid with an amount counted in **bottles**: bottle = 1, bowl = 2,
bucket = 4, honey block = 4, slime block = 9, powder-snow bucket = 3. A tank holds one fluid kind
(same fluid and same extra data, e.g. the same potion) up to its capacity.

| Fluid | Containers (filled -> empty, amount) | Light | Food |
| ----- | ------------------------------------ | ----- | ---- |
| water | water bucket -> bucket (4); water bottle -> glass bottle (1) | 0 | - |
| lava | lava bucket -> bucket (4) | 15 | - |
| honey | honey bottle -> glass bottle (1); honey block -> nothing (4) | 0 | honey bottle |
| milk | milk bucket -> bucket (4) | 0 | removes one effect per bottle |
| mushroom_stew / beetroot_soup / rabbit_stew | stew -> bowl (2) | 0 | stew / 2 |
| suspicious_stew | stew -> bowl (2), keeps its effect | 0 | stew / 2 |
| experience | bottle o' enchanting -> glass bottle (1) | 5 | gives the xp of a bottle |
| dragon_breath | dragon's breath -> glass bottle (1) | 3 | - |
| ghast_tear | ghast tear -> nothing (1) | 0 | - |
| magma_cream | magma cream -> nothing (1) | 0 | - |
| slime | slimeball -> nothing (1); slime block -> nothing (9) | 0 | - |
| powder_snow | powder snow bucket -> bucket (3) | 0 | - |
| ominous | ominous bottle -> glass bottle (1), keeps the level | 0 | the bottle |
| potion | potion / splash / lingering -> glass bottle (1), keeps type and bottle kind | 0 | the potion |
| lumisene | lumisene bottle -> glass bottle (1), lumisene bucket -> bucket (4) (only if the lumisene unit is installed) | 15 | the bottle |

Rules (tank = jar):
- **Filling a held container** has priority: clicking with an empty container (glass bottle, bowl,
  bucket, or an empty hand for "-> nothing" fluids) takes that container's amount out of the tank if
  the tank holds at least that much of a fluid that container can carry, and gives the filled item.
- **Emptying a held container**: the filled item's fluid is poured in only if all of it fits.
- Water bottles become water; a potion keeps its potion id and bottle kind (normal / splash /
  lingering), and only identical potions mix.
- Comparator-style fill level: `floor(count / capacity * 14) + 1` (see Not ported).
- Sounds: the container's fill / empty sound (buckets, bottles, honey).

## 3. Jar

### 3.1 Interaction order (right click on a placed jar)

1. Fluids (if `jarLiquids` and the jar holds no cookies and no mob): fill or empty the held
   container as in §2.
2. Cookies (if `jarCookies` and the jar holds no fluid and no mob): a held item from the cookie
   list (`minecraft:cookie`, `complementaries:candy` and the config list) is put in, one at a time,
   up to 12 (`jar_cookie` sound). Creative keeps the item.
3. Fish buckets: an empty jar takes a fish bucket (cod, salmon, pufferfish, tropical fish,
   axolotl, tadpole) and gives back an empty bucket; an empty bucket on a jar with such a fish gives
   the fish bucket back.
4. Not sneaking and `drinkFromJar` on: drink one bottle of a food fluid (honey, milk, stews,
   potions, experience, ominous) or eat one cookie (survival only).
5. Empty hand: take the last cookie out.

### 3.2 Jar item

- Placing needs **sneaking** while capturing is enabled (tooltip `[Sneak] to place`); a plain right
  click with the item is used to release mobs or capture them.
- **Capturing**: left click (attack) or right click a catchable mob with an empty jar item: the mob
  is removed and stored in the item (catch sound). Catchable with jars: slime, magma cube, bee,
  allay, frog, vex, silverfish, endermite (in the Supplementaries data), baby rabbits, every fish
  (cod, salmon, pufferfish, tropical fish, axolotl, tadpole; stored as the fish with water), plus
  `extraCatchable` from the config, plus every mob smaller than 0.625 x 0.875 blocks that is not a
  monster when `autoDetect` is on, plus every mob when `allowAllMobs` is on. Never players.
  Failures show `You can't catch this animal`, `You must tame this animal first` (tameable mobs
  that are not tamed by you when `requireTaming`), `You can only catch animals with less than X%
  health` (`healthThreshold`).
- A boat captured with an empty jar gives a **Boat in a Jar**.
- **Releasing**: right click a block (not sneaking) with a jar holding a mob: the mob is spawned at
  the clicked point (it keeps its data: name, health, variant, ...); bees get angry at the player
  in survival; the jar empties (creative keeps it). Release sound: chicken egg at low pitch.
- **Tooltip**: fluid `Water: 4 mBtl` style (`message.supplementaries.fluid_tooltip`), cookies
  `Cookie x5`, mob name; empty jars show the hint `Can store bowls, bottles, buckets, cookies and
  small mobs`. On Bedrock this is item lore (visible to every client).
- With `drinkFromJarItem` on, using a jar item that holds a food fluid drinks one bottle from it.

### 3.3 Visuals

- Fluid rises one pixel per bottle at capacity 12 (rounded proportionally otherwise), tinted per
  fluid; potions use their effect colour; lava, experience, dragon's breath and lumisene glow.
- Cookies are stacked flat inside the jar (up to 12).
- Fish (bucket contents) swim as flat sprites in water.
- Other mobs are drawn inside the jar, scaled to fit (1 px margin), floating mobs (bee, allay, vex)
  centred, others standing on the bottom.

### 3.4 Light

Jar light = the fluid's luminosity (lava 15, lumisene 15, experience 5, dragon's breath 3), or the
mob's (endermite 5).

## 4. Faucet

- Placed against a block: the spout points away from it; the block **behind** is the source.
- Right click toggles `enabled` (faucet sound, pitch 1 when closing, 1.2 when opening).
- Redstone power toggles `powered`; it is open when exactly one of them is set.
- While open, every time its cooldown is over it moves fluid from behind into the block below,
  cooldown 20 ticks per bottle moved:

Sources (behind, first match): water cauldron (its level, 1 at a time), lava cauldron (4),
powder-snow cauldron (level), full beehive / bee nest (honey level 5 -> 1 honey, empties it), jar
(1 bottle at a time), brewing stand (one potion bottle -> glass bottle), water source block
(infinite, 1 at a time), lava source block (4, picked up), and if not connected below and
`spillItems` / `fillEntitiesBelow`: one item from a container behind (dropped below, or given to a
non-player entity below).

Targets (below, first match): empty cauldron / water cauldron (water), empty cauldron (4 lava ->
lava cauldron), powder snow cauldron, empty beehive (1 honey -> full), jar (1 bottle), brewing
stand (fills glass bottles), sponge (eats the fluid), air with experience (drops xp orbs), concrete
powder + water (hardens), dirt + water (mud). Nothing else accepts fluid (the faucet keeps the
fluid in the source).

- `connected`: a jar (or beehive) directly below; the faucet model is lowered onto it.
- `content` shows the fluid available behind (stream colour); the faucet drips when it has fluid
  and is not connected.

## 5. Screens

None (Supplementaries has none for these blocks). Vanilla clients get the full behaviour: all
visuals are data driven (block states + the helper entity) and tooltips are item lore.

## 6. Configuration (`config/jar.json`)

| Key | Default | Meaning |
| --- | ------- | ------- |
| `enabled` | true | Jar and Boat in a Jar behaviour. |
| `capacity` | 12 | Jar fluid capacity in bottles (0..1024). |
| `drinkFromJar` | false | Right click drinks / eats from a placed jar. |
| `drinkFromJarItem` | false | Using a jar item drinks from it. |
| `autoDetect` | false | Any non-monster mob smaller than the jar is catchable. |
| `jarCapture` | true | Jars capture mobs (and need sneaking to be placed). |
| `jarCookies` | true | Jars hold cookies. |
| `jarLiquids` | true | Jars hold fluids. |
| `cookieItems` | `["minecraft:cookie","complementaries:candy"]` | Items that go into jars. |
| `extraCatchable` | `[]` | Extra catchable entity ids. |
| `allowAllMobs` | false | Shared with cages in Supplementaries (`cage.allow_all_mobs`). |
| `persistentMobs` | false | Released mobs never despawn. |
| `healthThreshold` | 100 | Only mobs under this health % are catchable (100 = any). |
| `requireTaming` | true | Tameable mobs must be tamed by you. |
| `faucetEnabled` | true | Faucet behaviour. |
| `spillItems` | true | Faucets drop items pulled from containers. |
| `fillEntitiesBelow` | false | Faucets give pulled items to entities below. |

## 7. Commands

None of its own. `/supplementaries selftest "jar.*"` runs this unit's tests.

## 8. Strings

| Key | English |
| --- | ------- |
| `message.supplementaries.jar` | Can store bowls, bottles, buckets, cookies and small mobs |
| `message.supplementaries.fluid_tooltip` | {}: {} mBtl |
| `message.supplementaries.cage.fail` (owned by the cage unit) | You can't catch this animal |
| `message.supplementaries.cage.fail_tamed` (cage) | You must tame this animal first |
| `message.supplementaries.cage.fail_health` (cage) | You can only catch animals with less than {} health |
| `message.supplementaries.cage.tooltip` (cage) | [{}] to place |
| `fluid.supplementaries.experience` / `honey` / `milk` / `slime` / `lumisene` | Experience / Honey / Milk / Slime / Lumisene |
| `cm.jar.fluid.<id>` | names of the other fluids (Water, Lava, Mushroom Stew, ...) |
| `cm.jar.cookies` | {} x{} |
| `cm.jar.mob` | Mob: {} |

Block names (RP `texts.d`): Jar, Faucet, Boat in a Jar.

## 9. Not ported

- **Comparator output** of jars: Bedrock custom blocks cannot provide a comparator signal (the
  hook point is a folded function shared by many blocks).
- **Jar item contents rendered on the item** (Java draws the fluid / mob on the item): Bedrock
  item rendering cannot depend on item data; the contents are in the tooltip.
- **Jar item stack size 16**: a custom block's item stacks to 64 on Bedrock (filled jars do not
  stack anyway, they carry data).
- **Pick block with contents**: Bedrock copies only block entity data, which custom blocks lack.
- **Mod fluids / mod fish / mod mobs** and the datapack-driven `faucet_interactions` /
  `catchable_mobs_properties`: there is no mod ecosystem on Bedrock; the vanilla entries are built in.
- **Mob animations in jars** are the vanilla idle animations of the drawn model, not the
  per-mob container animations of Supplementaries (bee wing flap still plays).
- **Mobs without a built-in jar model** (e.g. with `allowAllMobs`) are kept and released correctly
  but not drawn inside the jar.
- **"Pony jar"** easter egg (fluid + mob when the jar is renamed): Bedrock jars cannot be renamed
  as blocks.
- **Faucet extraction from mod fluid tanks, finite fluids, kettles, sap logs**: no Bedrock source.
