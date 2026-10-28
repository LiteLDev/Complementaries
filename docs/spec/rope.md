# U11 `rope`: rope, rope arrow, pulley, flax

Behaviour reference: Supplementaries 4.0.2 (MehVahdJukaar). This file describes behaviour only;
no code or assets of Supplementaries are used. Unit number 11: selftest `dx` 1100..1199, world
data under `cm:rope:*`, config section `rope`, code in `src/feature/rope/` and `src/client/rope/`.

## 1. Content

| Supplementaries id | Complementaries id | Kind | Notes |
| ------------------ | ------------------ | ---- | ----- |
| `supplementaries:rope` | `complementaries:rope` | block + item | climbable, connects, hangs |
| `supplementaries:rope_arrow` | `complementaries:rope_arrow` | item + projectile entity | 32 charges (durability) |
| `supplementaries:pulley_block` | `complementaries:pulley_block` | block | 1-slot inventory (world DB) |
| `supplementaries:flax` (block) | `complementaries:flax_crop` | crop block, 2 tall | Bedrock blocks and items share one id space, so the crop block cannot be called `flax` like the item |
| `supplementaries:flax` (item) | `complementaries:flax` | item | compostable 65 % |
| `supplementaries:flax_seeds` | `complementaries:flax_seeds` | item | plants `flax_crop` on farmland, compostable 30 % |
| `supplementaries:wild_flax` | `complementaries:wild_flax` | plant block | world generation patches |
| `supplementaries:flax_block` | `complementaries:flax_block` | pillar block | fall damage x0.2 (like hay), compostable 85 % |

### Recipes

| Result | Recipe |
| ------ | ------ |
| 3 rope | shaped, flax in a vertical column of 3 |
| flax block | 9 flax (3x3) |
| 9 flax | 1 flax block |
| pulley block | shaped `PPP / PIP / PPP`, P = any planks, I = iron ingot |
| rope arrow (k charges) | shapeless: 1 arrow + k rope (k = 1..8). The arrow holds k ropes. |
| flax seeds | 1 flax -> 1 flax seeds |

### Block states

- `rope`: `complementaries:up`, `down`, `north`, `south`, `west`, `east`, `knot` (booleans) and
  `complementaries:distance` (0..7). They are recomputed by the mod after every change around the
  rope (like fences and scaffolding); a placed or `/setblock` rope gets its real shape on the next tick.
- `pulley_block`: `minecraft:block_face` (placement trait: the clicked face gives the wheel axis), `complementaries:type`
  (0 none, 1 rope, 2 chain: what is wound on it), `complementaries:flipped` (toggles on every turn,
  animation of the wheel).
- `flax_crop`: `complementaries:age` (0..7), `complementaries:upper` (bool, upper half).
- `flax_block`: `minecraft:block_face` (placement trait), giving the pillar axis like logs.

### Drops

- rope: 1 rope. pulley: itself + its content. flax block: itself. wild flax: 1 flax seeds and
  50 % one flax.
- flax crop (only the lower half drops; breaking either half removes both): age 7: 1 flax plus
  binomial(3, 0.57) extra flax seeds plus 1 seed; younger: 1 flax seeds.

## 2. Mechanics

### Rope

- **Support (distance)**, like scaffolding: a rope under a rope has the same distance as the rope
  above; a rope directly under a block whose bottom face is sturdy (or under a chain, iron bars,
  fence, wall, lantern...) has distance 0; with `blockSideAttachment` a rope next to a sturdy
  side face has distance 0; otherwise the distance is 1 + the smallest distance of a horizontal
  rope neighbour. Distance 7 = unsupported: the rope breaks (drops) one tick later, so a cut rope
  falls apart from the cut down/outwards one block per tick.
- **Connections**: `up` when the block above is a rope or the rope hangs from it; `down` when the
  block below is a rope or something that hangs from ropes (bells, lanterns, chains, pulleys and any
  block with a sturdy top face). Sides connect to ropes, fences, walls, iron bars / panes, and (with
  `blockSideAttachment`, only for a rope that does not hang from above) to sturdy side faces.
  `knot` is shown where the rope is not a straight line (junctions, corners, side attachments).
- **Placing**: a rope can only be placed where it is supported. Using a rope on a rope extends the
  rope column downwards: the rope is added under the lowest rope of the column (also into water).
  If that is blocked the rope is placed normally against the clicked face.
- **Climbing**: ropes are climbable (`BlockProperty::Climbable`, set on the server and, by the
  client mod, on the client). Climbing negates fall damage like ladders.
- **Pull up**: using a rope with an empty hand (not sneaking) removes the lowest rope of the column
  below and gives it to the player (survival).
- **Bells**: using a rope (empty hand) whose column goes up to a bell (through ropes and chains,
  at most `bellChainLength` blocks) rings the bell instead (`bellRinging`).
- Ropes burn, are broken instantly by hand (hardness 0.25-ish) and sound like wool.

### Rope arrow

- Rope arrows are shot from bows: the offhand is checked first, then the inventory in slot order,
  so whichever of rope arrow / arrow comes first is shot, as in Java. With the client mod a bow
  draws with rope arrows alone; a vanilla client only starts drawing a bow when it also has a
  normal arrow. The arrow carries its charges (item durability: charges = 32 - damage).
- On hitting a block: if the block is a rope, ropes are added under that rope column; otherwise a
  rope is placed on the hit face (needs the support rules above), then one more rope is placed below
  every tick until the charges run out or the way is blocked.
- The arrow stays stuck and can be picked up by walking over it (survival players), giving back a
  rope arrow with the remaining charges, or a normal arrow when no charge is left. It despawns after
  60 s like arrows.
- Using a rope arrow on a rope winds the rope column (from the bottom) back into the arrow until
  it is full.

### Pulley block

- Placed like a log (axis from the clicked face). Using it (not sneaking) opens the pulley screen
  (one slot that accepts ropes or chains). The `type` state shows what is wound on it.
- **Turning**: turning the pulley clockwise winds: the lowest rope/chain of the column hanging under
  the pulley is taken back into the pulley (if the slot has room), and a block hanging under the
  column (anything movable: not liquid, not unbreakable, no block entity) is pulled up into the
  freed space (`pulleyMovesBlocks`).
  Counter-clockwise unwinds: one rope/chain from the slot is added at the bottom of the column; a
  block hanging there is first pushed down one block (if the space below is free).
- Turned by a **crank** attached to it (using the crank: clockwise; sneaking: counter-clockwise) and
  by a **turn table** facing it (every rotation of the turn table turns the pulley). Other units call
  `cm::feature::rope::turnPulley(region, pos, clockwise)` (`src/feature/rope/Rope.h`); as a
  fallback the pulley also reacts to `complementaries:crank` blocks being used next to it, and to a
  `complementaries:turn_table` next to it changing its facing (the facing is restored and the wheel
  turns clockwise instead).
- Breaking the pulley drops its content.

### Flax

- `flax_seeds` plant on farmland. The crop grows on random ticks (simulated by the mod at the
  Java default rate of 3 random ticks per 16x16x16 section per tick) when the light above is >= 9, with the vanilla crop growth chance
  (better on hydrated farmland with crops around). From age 4 the crop needs the block above to be
  free: it then grows a second block (`upper`), which ages together with the lower one.
- Bone meal adds 2..5 ages (and the upper half). Breaking one half breaks the other. The crop
  breaks when the farmland under it is gone.
- **Wild flax** generates in patches (about 1 patch per 8 chunks, 8..12 plants) on grass, dirt
  and sand in plains, savannas, deserts' rivers, beaches and swamps
  (`complementaries:wild_flax_feature`, feature rule `complementaries:wild_flax_patch`). It can be
  placed on grass, dirt, farmland and sand.
- The flax block reduces fall damage to 20 % (like hay).

## 3. Screens

| Screen | Modded client | Vanilla client (BDS) |
| ------ | ------------- | -------------------- |
| Pulley | LeviLamina form: title "Pulley", content "Wound: <item> x<count>" (or "Empty"), buttons "Put in held ropes/chains", "Take out", "Turn clockwise", "Turn counter-clockwise" | the same form |

The pulley slot is a form on both client kinds (see *Not ported*).

## 4. Configuration (`config/rope.json`)

| Key | Default | Meaning |
| --- | ------- | ------- |
| `blockSideAttachment` | `true` | ropes are supported by and attach to sturdy side faces |
| `bellRinging` | `true` | using a rope or chain hanging from a bell rings it |
| `bellChainLength` | `16` | longest rope/chain column that still rings a bell |
| `ropeArrowEnabled` | `true` | bows can shoot rope arrows |
| `pulleyMovesBlocks` | `true` | pulleys pull up / lower the block hanging from the rope |
| `flaxGrowthMultiplier` | `1.0` | scales the flax growth chance |

## 5. Commands

None. (Use `/supplementaries selftest "rope.*"` for the tests.)

## 6. Strings

Block / item names (RP `texts.d`): Rope, Rope Arrow, Pulley Block, Flax, Flax Seeds, Wild Flax,
Flax Block (`tile.complementaries:<id>.name` / `item.complementaries:<id>.name`).

Mod strings (`assets/lang/<locale>/rope.json`):

| Key | English |
| --- | ------- |
| `gui.supplementaries.pulley_block` | Pulley |
| `cm.rope.pulley.empty` | Empty |
| `cm.rope.pulley.content` | Wound: {} x{} |
| `cm.rope.pulley.put` | Put in held {} |
| `cm.rope.pulley.take` | Take out |
| `cm.rope.pulley.turn_cw` | Turn clockwise |
| `cm.rope.pulley.turn_ccw` | Turn counter-clockwise |
| `cm.rope.pulley.wrong_item` | Only ropes or chains can be wound on a pulley |
| `cm.rope.pulley.full` | The pulley is full |
| `cm.rope.item.rope` | Rope |
| `cm.rope.item.chain` | Chain |

Rope arrow charges are shown by the item's durability bar.

## 7. Not ported

- **Rope knots on fences** (Java replaces a fence with a "rope knot" block that mimics it): custom
  Bedrock blocks cannot mimic an arbitrary block's model (no block entity renderers). Ropes still
  connect to fences, walls and bars.
- **Fast sliding (`slide_on_fall`)**: Bedrock's climbing physics come from a per-block-type flag;
  descending a rope uses ladder speed. Fall damage is still negated.
- **Waterlogged ropes**: custom blocks have no waterlogged state; ropes placed into water replace
  the water source.
- **Crossbows and dispensers** do not shoot rope arrows: the crossbow stores its charged projectile
  in hard-coded vanilla data and the dispenser would lose the charges; dispensers drop the item.
- **Adding ropes to a rope arrow in the crafting grid** (dynamic recipe): Bedrock recipes have fixed
  results. Rope arrows are crafted with 1..8 ropes, and a rope arrow used on a rope collects it.
- **Pulley container slot screen**: a one-slot native container needs a block entity; the pulley
  uses a form instead (same function: insert, take, turn).
- **Ropes in mineshafts** (`rope_override` world generation): vanilla Bedrock structures cannot be
  edited by behavior packs.
- **Vanilla clients climbing**: a client without the mod does not know the rope is climbable; the
  server's authoritative movement corrects it, which feels less smooth.
