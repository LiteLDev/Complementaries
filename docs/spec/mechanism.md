# U7 `mechanism`: spring launcher, bellows, turn table, cog block, crank

Behaviour reference: Supplementaries 4.0.2 (MehVahdJukaar). This file describes behaviour only; no code
or assets come from Supplementaries. Everything here runs on the server (BDS and the integrated server
of local worlds); every visual is data driven (block states, geometry, textures, particles, sounds), so a
vanilla client and a modded client see the same thing. The unit has no screens.

## 1. Blocks and items

| Id (`complementaries:`) | English name | Item | Creative | Notes |
|---|---|---|---|---|
| `piston_launcher` | Spring Launcher | yes | Redstone | base block |
| `piston_launcher_head` | Spring Launcher Head | no | hidden | placed by the base while extended |
| `piston_launcher_arm` | Spring Launcher Arm | no | hidden | the moving head (2 ticks while extending / retracting) |
| `bellows` | Bellows | yes | Redstone | |
| `turn_table` | Turn Table | yes | Redstone | |
| `cog_block` | Cog Block | yes | Redstone | |
| `crank` | Crank | yes | Redstone | |

### Block states

| Block | State | Values | Meaning |
|---|---|---|---|
| piston_launcher | `minecraft:facing_direction` (placement trait) | down, up, north, south, west, east | the direction the player looked when placing. The launcher **faces the opposite way** (towards the player, like a piston). |
| piston_launcher | `complementaries:extended` | bool | the head is out |
| piston_launcher_head / _arm | `complementaries:facing` | 0..5 (down, up, north, south, west, east) | direction of the launcher it belongs to |
| bellows | `minecraft:facing_direction` (trait) | 6 directions | as for the launcher, the bellows blows towards the player who placed it |
| bellows | `complementaries:stage` | 0..7 | compression of the bellows (0 open, 7 closed), drives the model |
| turn_table | `minecraft:facing_direction` (trait) | 6 directions | the turn table's top faces the opposite way (towards the player) |
| turn_table | `complementaries:inverted` | bool | false = clockwise, true = counter-clockwise (seen from the top face) |
| turn_table | `complementaries:rotating` | bool | powered (lit arrow on the top texture) |
| cog_block | `complementaries:power` | 0..15 | the redstone power it carries and emits |
| cog_block | `complementaries:twisting` | bool | powered: shows the turning texture |
| crank | `minecraft:block_face` (placement trait) | 6 directions | the face it was placed on = the direction it points; it hangs on the block behind |
| crank | `complementaries:power` | 0..15 | output power, also the handle angle (power mod 4 quarter turns) |

### Recipes (shaped crafting table)

| Result | Pattern | Keys |
|---|---|---|
| Spring Launcher ×1 | `III` / `ISI` / `IPI` | I iron ingot, S slime ball, P piston |
| Bellows ×1 | `PPP` / `LLL` / `PPP` | P any planks (oak, spruce, ... tag `minecraft:planks`), L leather |
| Turn Table ×1 | `PPP` / `CRC` / `CCC` | P any planks, C cobblestone, R redstone |
| Cog Block ×4 | ` C ` / `CRC` / ` C ` | C copper ingot, R redstone |
| Crank ×1 | ` S` / `SC` | S stick, C cobblestone |

(Supplementaries uses its own data-pack recipes; these keep the same ingredients and yields where
known.)

### Drops
Every block with an item drops itself (`loot_tables/blocks/<id>.json`, any tool). The head and the arm
drop nothing; breaking either in survival breaks the base, which drops the Spring Launcher.
Mining: launcher, turn table, cog block: pickaxe, 1.5 s base (stone-like, `destroy_time` 1.5 / 0.75 for
the crank); bellows: axe, wood-like (0.8). The launcher head/arm are not movable by pistons, nor is an
extended launcher.

## 2. Mechanics

All periods are in game ticks (20 per second). "Powered" means the block receives a redstone signal
(any strength > 0) from any side, exactly as a piston would (data-driven `minecraft:redstone_consumer`).

### 2.1 Spring launcher (`piston_launcher`)
- **Extend**: when the base becomes powered while retracted and the space in front is air or a
  replaceable block (grass, water, snow layer...), it extends: the arm appears in front (2 ticks, half
  way), then the head (full length). The base switches to `extended=true` at once. Sound: piston out.
- **Launch**: when it starts extending, every entity (mobs, players, items, minecarts, boats...) whose
  box touches the space in front of the base (for an upward launcher: the block above, plus the top of
  the base, so things standing on it are included) gets its velocity along the facing set to
  `launcherVelocity` (default 1.5 blocks/tick); the other components are kept (an upward launch keeps
  the horizontal motion). Players are sent the motion directly. The launched entity's fall distance is
  reset.
- **Retract**: when the base is extended and no longer powered, the head turns back into the arm
  (2 ticks), then into air; `extended=false`. Sound: piston in. It never pulls entities or blocks.
- **Blocked**: a non-replaceable block in front stops the extension until it is removed (the base
  stays retracted, checked every tick while powered).
- **Trampoline / fall-damage negation**: an entity (not sneaking) that falls onto a retracted
  upward-facing launcher from at least `fallHeightRequired` blocks (default 5) takes no fall damage and
  triggers the launcher, which bounces it up as above. After the bounce the launcher retracts again
  unless it is powered. Shorter falls behave like a normal block (vanilla damage).
- **Consistency**: breaking the head or the arm in survival breaks the base too (drops the launcher);
  in creative it just removes it. Removing the base removes its head/arm. If the head disappears for
  another reason (explosion, `/setblock`), the base retracts (and re-extends if still powered).

### 2.2 Bellows (`bellows`)
- Powered with power `p`, the bellows pumps continuously. A full cycle (open → closed → open) takes
  `T = max(10, (period - 10) * (15 - p) / 14 + 10)` ticks (`period` default 78: 78 ticks at power 1,
  10 ticks at power 15). The model's `stage` follows `7 * (1 - cos(2π t / T)) / 2`.
- **Blowing** (stage rising, first half of the cycle): every entity within `range` blocks (default 5)
  in front of the nozzle, in the 1×1 column along the facing, is pushed away along the facing by
  `power * 0.05 * (1 - d / range)` blocks/tick per tick (`power` default 2.0, `d` the distance from the
  nozzle), up to 1 block/tick along the axis. Sneaking players are not pushed. Air particles leave the
  nozzle every other tick (they drift along the facing), and the bellows plays a soft blow sound at
  the start of each blow.
- **Sucking** (second half): entities in the same column are pulled towards the bellows at half that
  strength.
- **Heat**: while blowing, a lit furnace, blast furnace, smoker, campfire or soul campfire directly in
  front is ticked one extra time per tick (cooks twice as fast).
- **Manual use**: an entity landing on top of an unpowered bellows (or a player using it with an empty
  hand) presses it once: one full cycle at power 15 speed with the same blow / suck effects.
- When power is removed, the current cycle finishes and the bellows stays open (stage 0).

### 2.3 Turn table (`turn_table`)
- Powered with power `p`, it rotates the block in front of its top face by 90° every
  `P = (period - 1) * (15 - p) / 14 + 1` ticks (`period` default 20: 20 ticks at power 1, 1 tick at
  power 15). The first rotation happens one period after it becomes powered.
- Direction: clockwise when looking at the top face; `inverted` turns it counter-clockwise. A
  downward-facing table therefore turns the other way seen from above.
- **Which blocks rotate**: any block whose states carry a direction or an axis. For a vertical turn
  table (facing up/down): all horizontal directions (`minecraft:cardinal_direction`, `direction`,
  `weirdo_direction` (stairs), `facing_direction` / `minecraft:facing_direction` /
  `minecraft:block_face` horizontals, `torch_facing_direction`, `lever_direction`, rail shapes,
  `ground_sign_direction` (signs, banners: 4 steps of 22.5°), `pillar_axis` x↔z, `portal_axis`,
  vine bits). For a horizontal turn table: 6-way facings and `pillar_axis`, rotated around the
  table's axis. Containers keep their contents. Not rotated: beds, doors, double plants, chests that
  are part of a double chest, extended pistons and piston heads, extended spring launchers and their
  heads/arms, our own turn tables' neighbours that would break. A successful rotation plays the item
  frame rotate sound.
- **Entities**: an upward turn table spins entities standing on it (`rotateEntities`, default true):
  their yaw turns by `90 / P` degrees per tick in the table's direction, and they are carried around the
  centre of the block.
- **Toggling**: using the turn table (right-click) on any face other than its top, or on its top with an
  empty hand, toggles `inverted` (comparator click sound, pitch 0.55 / 0.5). Placing blocks on the top
  works as usual.

### 2.4 Cog block (`cog_block`)
- A redstone conductor in block form: its power is the strongest signal it receives from a
  non-cog neighbour (lever, button, plate, torch, repeater/comparator pointing into it, dust on top or
  pointing into it, a powered block, a crank...), or the strongest neighbouring cog's power minus 1.
  A chain of cogs therefore loses one level per block, like redstone dust, and never keeps itself on.
- It emits its power (weakly) to all six neighbours, so it powers lamps, pistons, dust, other mod
  blocks, and is read by comparators like any producer. `twisting` is true while powered (the
  turning texture).
- Updated every tick (redstone runs every 2 ticks, so there is at most one tick of extra delay per
  cluster, not per block: the whole connected cluster (up to 4096 cogs) is resolved at once).

### 2.5 Crank (`crank`)
- Placed on any face of a block (like a lever). It pops off (drops) when the block behind it is
  removed or becomes non-solid.
- **Use** (right-click): turns it one step clockwise: power + 1, wrapping 15 → 0. **Sneak + use**:
  counter-clockwise, power − 1, wrapping 0 → 15. Wooden click sound, pitch rising with the power;
  redstone dust particles.
- Output: power `power` to all neighbours, and strong power into the block it hangs on (like a lever),
  so the signal passes through that block.
- **Crankable blocks**: if the block behind the crank is registered as crankable (see
  `src/feature/mechanism/Crankable.h`; e.g. the pulley block of the rope unit), each turn also calls
  it with the direction. The crank still changes its own power.

## 3. Screens
None. (Supplementaries has none for these blocks.)

## 4. Configuration (`config/mechanism.json`)

| Key | Default | Range | Meaning |
|---|---|---|---|
| `springLauncher.enabled` | true | | the launcher reacts to redstone / falls (the block always exists) |
| `springLauncher.launcherVelocity` | 1.5 | 0..16 | launch speed (blocks/tick) |
| `springLauncher.fallHeightRequired` | 5 | 0..512 | fall distance that triggers the trampoline |
| `bellows.enabled` | true | | |
| `bellows.period` | 78 | 10..512 | cycle length at power 1 |
| `bellows.power` | 2.0 | 0..16 | push strength |
| `bellows.range` | 5 | 1..16 | reach in blocks |
| `bellows.heatBlocks` | true | | furnaces / campfires in front cook faster |
| `turnTable.enabled` | true | | |
| `turnTable.period` | 20 | 1..256 | rotation period at power 1 |
| `turnTable.rotateEntities` | true | | spin entities standing on an upward table |
| `cogBlock.enabled` | true | | |
| `crank.enabled` | true | | |

Values outside the range are clamped when read.

## 5. Commands
None of its own. Selftests: `/supplementaries selftest "mechanism.*"`.

## 6. Strings

Block names (RP `texts.d/<locale>/mechanism.lang`):

| Key | en_US | zh_CN |
|---|---|---|
| `tile.complementaries:piston_launcher.name` | Spring Launcher | 弹簧发射器 |
| `tile.complementaries:piston_launcher_head.name` | Spring Launcher Head | 弹簧发射器头 |
| `tile.complementaries:piston_launcher_arm.name` | Spring Launcher Arm | 弹簧发射器臂 |
| `tile.complementaries:bellows.name` | Bellows | 风箱 |
| `tile.complementaries:turn_table.name` | Turn Table | 转盘 |
| `tile.complementaries:cog_block.name` | Cog Block | 齿轮方块 |
| `tile.complementaries:crank.name` | Crank | 曲柄 |

Mod strings (`assets/lang/<locale>/mechanism.json`), shown on the action bar when a player toggles or
turns a block (Complementaries addition so that vanilla clients see the state):

| Key | en_US |
|---|---|
| `cm.mechanism.turn_table.clockwise` | Turn Table: clockwise |
| `cm.mechanism.turn_table.counterclockwise` | Turn Table: counter-clockwise |
| `cm.mechanism.crank.power` | Power: {} |

## 7. Log lines (e2e contract)
`mechanism: launcher at <x> <y> <z> launched <n> entities`, `mechanism: turn table at ... rotated <id>`,
`mechanism: crank at ... power <n>`, `mechanism: turn table at ... inverted=<b>` (one per user action,
never per tick).

## 8. Not ported / differences

| Supplementaries | Here | Reason |
|---|---|---|
| Smooth (per-frame) head extension, bellows and crank animation rendered by block entity renderers | stepped animation through block states (2-tick arm, 8 bellows stages, crank quarter turns) | custom Bedrock blocks cannot have block entities or renderers; block states keep vanilla and modded clients identical |
| Crank handle in 16 positions | 4 positions (power mod 4) | block transformations and geometry are limited to 90° steps |
| Bellows pushing *client* particles (smoke, flames) in front | not pushed | particles are client-only emitters without a server-side handle |
| Bellows feeding fire spread | not ported | fire spread is a hard-coded random tick |
| Exact Supplementaries cycle formulas for bellows | formulas above | tuned to the same defaults (period 78 at power 1) |
