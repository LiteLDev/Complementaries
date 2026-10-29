# U18 `tweak`: vanilla tweaks

Behaviour reference: the vanilla tweaks of Supplementaries 4.0.2 (and the ones it shares with its companion
mod Amendments) that no other unit owns. Behaviour only: nothing here is copied from the Java code or assets.

Every tweak is server side (BDS and the integrated server of local worlds) and data driven on the client, so
a vanilla client sees and uses everything (blocks, models, projectiles); the client mod adds nothing extra.
Each tweak can be turned off in `config/tweak.json` (section `tweak`).

## 1. Blocks

All blocks are hidden from the creative menu and from `/give` (they are placed by using the vanilla item);
their own block items are never handed out. Drops are produced by the mod (the loot tables are empty), so they
match the block state; a creative-mode player breaking them gets nothing.

| Id | Placed by | States | Drops |
| -- | --------- | ------ | ----- |
| `complementaries:stick` | using a stick on a block face | `complementaries:axis_x`, `axis_y`, `axis_z` (0/1) | one stick per set axis |
| `complementaries:blaze_rod` | using a blaze rod | same | one blaze rod per axis |
| `complementaries:breeze_rod` | using a breeze rod | same | one breeze rod per axis |
| `complementaries:gunpowder` | using gunpowder on the top face of a block | `complementaries:north`/`south`/`east`/`west` (0 none, 1 side, 2 up), `complementaries:burning` (0..8) | gunpowder (not when burnt out) |
| `complementaries:hanging_flower_pot` | using a flower pot on the bottom face of a block (or a rope / chain) | `complementaries:plant_group` (0..2) and `complementaries:plant` (0..15): plant index = group * 16 + plant (0 empty, 1..32; a block state holds at most 16 values) | flower pot + the plant |

### 1.1 Placeable sticks and rods

- Using a stick (blaze rod, breeze rod) on a block face places one stick lying along the axis of that face in
  the block next to the clicked face (`up`/`down` = Y axis, `north`/`south` = Z, `east`/`west` = X).
- Using one on an existing stick block of the same kind adds a stick along the clicked face's axis if that
  axis is still free (up to three sticks per block, one per axis: a "tripod"/"cross" in any direction).
  Otherwise the normal placement in the neighbouring block happens.
- Clicking a block that has its own interaction (chest, door, ...) without sneaking keeps that interaction.
- The block has no collision (sticks are walked through like in Supplementaries), a thin selection box
  around the stick (one axis) or the whole cell (two or three axes), breaks instantly, and the sound of wood
  (sticks) / stone (rods). Blaze rods glow (light 6); breeze rods don't.
- Breaking drops one item per stick. Survival placement consumes one item.

### 1.2 Placeable gunpowder

- Using gunpowder on the top face of a block places a gunpowder trail on top of it (needs a block below;
  it pops off when the block below goes away, like redstone dust).
- Connections look like redstone dust: a trail connects to neighbouring gunpowder on the same level
  (`side`), one block up the side of a wall (`up`, when the block above the trail is free) and one block down,
  and to TNT. A lone trail is a dot.
- Ignition: flint and steel (uses 1 durability) or a fire charge (consumed) used on the trail, fire / soul
  fire / lava placed next to it, or a burning neighbour trail.
- Burning: `burning` goes 1..8, one stage every `gunpowderBurnTicks` ticks (default 2). When a trail reaches
  stage `gunpowderSpreadAge` (default 2) it ignites every connected neighbour: other trails, TNT (primed, as
  if lit), unlit campfires / soul campfires and unlit candles / candle cakes. After stage 8 the trail is gone
  (no drop). A burning trail glows and plays the fuse sound when lit.
- Burning trails are remembered in the world (`cm:tweak:burning`) and resume when the world is loaded again.

### 1.3 Hanging flower pots

- Using a flower pot on the bottom face of a block, or on any face of a rope (`complementaries:rope`) or a
  vertical chain, places a hanging flower pot below the block. It hangs on three strings and pops off when
  the block above disappears.
- Using a plant on an empty hanging pot puts it in (one item, no consumption in creative); using the pot
  with an empty hand takes the plant out. Supported plants: poppy, dandelion, blue orchid, allium, azure
  bluet, the four tulips, oxeye daisy, cornflower, lily of the valley, wither rose, torchflower, the oak,
  spruce, birch, jungle, acacia, dark oak and cherry saplings, mangrove propagule, red and brown mushrooms,
  dead bush, crimson / warped fungus and roots, bamboo, open / closed eyeblossom.
- Breaking drops the flower pot and the plant.

## 2. Items and projectiles

### 2.1 Throwable bricks

- Using a brick or a nether brick throws it like a snowball (entity `complementaries:throwable_brick`, item
  sprite; variant 1 = nether brick). One item is consumed in survival.
- Hitting an entity deals 1 damage with knockback. Hitting glass, stained glass, glass panes, stained glass
  panes, flower pots, decorated pots or hanging flower pots breaks the block (pots drop their contents).
  The brick itself is used up (break particles of the brick).

### 2.2 Throwable slimeballs

- Using a slimeball throws it (entity `complementaries:throwable_slimeball`). A hit mob gets "slimed":
  Slowness II for `slimedDurationTicks` (default 200). The slimeball bounces off blocks up to two times
  (losing half its speed each time, with the squish sound), then splats.

### 2.3 Clock and compass information

- Using a clock shows the time of day in the action bar: `message.supplementaries.clock` "{0}:{1}" in 24 h
  (day ticks 0 = 06:00).
- Using a compass shows the position: `message.supplementaries.compass` "X: {0} Y: {1} Z: {2}". In the
  Nether / End the compass shows `message.supplementaries.compass.spinning` "The compass is spinning wildly!".

### 2.4 Bottling experience

- Using a glass bottle on an enchanting table turns it into a bottle o' enchanting, costing
  `bottleXpCost` experience points (default 7). Not possible with fewer points (message
  `message.supplementaries.bottle_xp.not_enough` "Not enough experience"). Creative players pay nothing.

## 3. Mechanics on vanilla blocks and mobs

### 3.1 Bell chains

- Using a vertical chain (or a `complementaries:rope`) with an empty hand rings the bell connected to it: the
  mod walks up and down the chain/rope column (up to `bellChainLength` blocks, default 16) and rings the
  first bell found at its end (sound, swing animation and the raid alarm like a normal ring).

### 3.2 Cauldron dyeing

Bedrock already has dyed water, potion cauldrons, leather / horse armour dyeing and tipped arrows. The mod
adds the block dyeing of Supplementaries:

- Using wool, carpet, (stained) terracotta, (stained) glass, (stained) glass panes, candles, concrete, concrete
  powder or a bed on a cauldron of dyed water recolours the whole held stack to the dye colour closest to the
  water colour, and lowers the water by one bottle. A cauldron that runs dry loses its colour.

### 3.3 Zombie horses

- Feeding rotten flesh to a skeleton horse counts up; after `zombieHorseFleshCount` (default 64) pieces the
  skeleton horse turns into a zombie horse (keeping its name, tameness and position; its saddle drops), with
  the zombify sound. Progress is kept in `cm:tweak:horse:<id>`.
- Rotten flesh also heals a zombie horse by 2. (Taming and riding zombie horses is vanilla on Bedrock.)

## 4. Screens

None. All feedback is in the action bar / chat.

## 5. Configuration (`config/tweak.json`)

| Key | Default | Meaning |
| --- | ------- | ------- |
| `placeableSticks` | true | sticks become blocks |
| `placeableBlazeRods` | true | blaze rods become blocks |
| `placeableBreezeRods` | true | breeze rods become blocks |
| `placeableGunpowder` | true | gunpowder trails |
| `gunpowderBurnTicks` | 2 | ticks per burning stage (1..20) |
| `gunpowderSpreadAge` | 2 | stage at which the fire spreads (1..8) |
| `hangingFlowerPots` | true | hanging flower pots |
| `throwableBricks` | true | bricks / nether bricks can be thrown |
| `throwableSlimeballs` | true | slimeballs can be thrown |
| `slimedDurationTicks` | 200 | slowness length of a slimeball hit |
| `clockRightClick` | true | clock shows the time |
| `compassRightClick` | true | compass shows the position |
| `bottleXp` | true | bottling experience at an enchanting table |
| `bottleXpCost` | 7 | experience points per bottle |
| `bellChainRinging` | true | ringing bells through chains / ropes |
| `bellChainLength` | 16 | max chain length |
| `cauldronDyeing` | true | dyeing blocks in dyed water |
| `zombieHorseConversion` | true | skeleton horse to zombie horse with rotten flesh |
| `zombieHorseFleshCount` | 64 | rotten flesh needed |

## 6. Commands

None.

## 7. Strings

| Key | English |
| --- | ------- |
| `message.supplementaries.clock` | `{0}:{1}` |
| `message.supplementaries.compass` | `X: {0} Y: {1} Z: {2}` |
| `message.supplementaries.compass.spinning` | `The compass is spinning wildly!` |
| `message.supplementaries.bottle_xp.not_enough` | `Not enough experience` |
| `message.supplementaries.zombie_horse.progress` | `This skeleton horse craves more flesh ({0}/{1})` |

Block names (RP `texts.d/*/tweak.lang`): Stick, Blaze Rod, Breeze Rod, Gunpowder, Hanging Flower Pot; entity
names: Thrown Brick, Thrown Slimeball.

## 8. Not ported

| Tweak | Why |
| ----- | --- |
| Potion mixing in cauldrons (mixed potions with combined effects) | Bedrock potions are fixed potion ids; an item cannot carry an arbitrary effect list that the game applies when drunk. |
| Random adventurer maps sold by cartographers / wandering traders | Bedrock trade tables are data files that can only be replaced, not extended; replacing Mojang's tables would break vanilla trades on every update. |
| Dragon banner pattern, ceiling banners | Banner patterns and banner rendering live in block entities, which custom blocks cannot have. |
| Shulker shell helmet, item lore on anvils, golden apple disenchanting | The vanilla items / anvil and grindstone screens are client-side and not data driven. |
| Wandering traders opening doors | Needs changing the vanilla entity definition (navigation); replacing it would override Mojang's entity file. |
| Wall lanterns, cake tweaks, skull piles / candles, book piles, raked gravel, soap, wrench | Owned by other units (lighting, tableware, display, building, tool). |
| Fern / grass in hanging pots | Their textures are tinted by the biome colour, which custom block materials cannot apply. |
