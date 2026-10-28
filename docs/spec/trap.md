# U17 `trap`: bamboo spikes, tipped spikes, cage, fodder

Behaviour reference: Supplementaries 4.0.2 (MehVahdJukaar). This file describes behaviour only;
no code or assets come from Supplementaries.

Unit files: `src/feature/trap/`, `assets/lang/*/trap.json`, BP `blocks|items|recipes|loot_tables/blocks`
files named after the ids below, RP fragments `*.d/trap.json`, `texts.d/*/trap.lang`,
`tools/textures/trap.ps1`, `tools/e2e/{server,client}/scenarios/trap.ps1`. Selftests `trap.*`
(dx 1700..1799). World data `cm:trap:*`. Config section `trap`.

## 1. Blocks and items

| Id | Kind | Stack | Recipe |
| -- | ---- | ----- | ------ |
| `complementaries:bamboo_spikes` | block + item | 64 | shaped `1 1` / `1 1` / `000`, `1` = bamboo, `0` = any wooden slab -> 2 |
| `complementaries:bamboo_spikes_tipped` | item (places `bamboo_spikes`, tipped) | 64 | shapeless bamboo spikes (or an empty tipped spike) + lingering potion -> 1 tipped spike carrying that potion |
| `complementaries:cage` | block + item | 64 | shaped `111` / `2 2` / `000`, `1` = iron ingot, `2` = iron bars, `0` = any wooden slab -> 1 |
| `complementaries:fodder` | block + item | 64 | shapeless hay bale + flax block (`complementaries:flax_block`, owned by the flax unit) -> 2 |

### Bamboo spikes (`bamboo_spikes`)

- States: `facing` (6 directions, Bedrock `minecraft:block_face`): the clicked face; the spikes
  point that way, the 1-pixel base plate lies against the clicked block. `tipped` (Bedrock
  `complementaries:tip`, 0 = plain, 1..8 = tipped, the value picks the tip colour from the potion
  colour). `waterlogged` (Bedrock: not ported, see 6).
- Shape: the base plate (16x16x1 px) is the collision and outline; the interaction shape is the
  full block, so entities walk *into* the spikes.
- Hardness of bamboo-ish wood (2.0), axe tool. Drops itself; a tipped block drops a tipped spike
  carrying the potion and the remaining charges (block entity data in Java, world DB here).
  Sound: wood.
- Pathfinding: mobs avoid it (`DAMAGE_OTHER`).

**Entity inside** (every tick, every block position the living entity's bounding box overlaps):
- Creative players are ignored.
- The entity is slowed: horizontal motion x0.95, vertical x0.45 when the spikes point up, x0.95
  otherwise. The fall distance is kept (spikes do not cancel fall damage).
- Server side: a player sneaking on upward spikes takes no damage. Otherwise the entity is hurt
  with the `bamboo_spikes` damage type: 3 when its feet are above `y + 1/16` (it fell or jumped
  in), else 1.5. Invulnerability frames (10 ticks) limit the rate.
- Death message `death.attack.supplementaries.bamboo_spikes`: "%1$s was impaled on a spike";
  `.player` variant "%1$s impaled on a spike whilst fighting %2$s" when a killer gets credit.
- Config `player_loot` (off): the damage is credited to a fake spectator player so mobs drop
  player-only loot and experience.
- Tipped: after the damage the potion is applied (see below). When the charges run out the block
  turns back to plain spikes.

### Tipped spikes

- A block holds one potion (all effects of a lingering potion) and `charges` (max 16).
- Tipping in the world: use a lingering potion on untipped spikes -> tipped with 16 charges,
  honey-block-fall sound (volume 0.5, pitch 1.5), the potion becomes a glass bottle (not in
  creative). A dispenser facing spikes and holding a lingering potion does the same and gets the
  bottle back. Already tipped spikes refuse a new potion.
- Valid potion: has effects; with `only_allow_harmful_effects` (default on) every effect must be
  harmful and the potion must not be in the blacklist (oozing, infested); otherwise only the
  finite blacklist applies (empty in vanilla).
- Applying (server, per entity, 20-tick cooldown per block since the last charge was used): for
  every effect of the potion that the entity can receive and does not have yet: instant effects
  are applied at once (scale 0.5), others are added with 10 % of the potion's duration and the
  same amplifier. If at least one effect was used: a coloured potion particle and one charge is
  consumed, unless `only_allow_harmful_effects` is on and the potion has a harmful effect: then
  the charges never run out ("infinite durability").
- Random display tick: 1 % chance per tick of a potion-coloured particle on tipped spikes.
- Item: "Bamboo Spikes of <potion>" (`item.supplementaries.bamboo_spikes_tipped.effect`), tooltip
  lists the effects with durations x0.1; a durability bar coloured like the potion shows the
  charges when `only_allow_harmful_effects` is off. The item colour (tips) follows the potion.
- Soap (soap unit) washes tipped spikes back to plain spikes (block and crafting).
- Jungle temple chests: 38 % chance of 1 item: plain spikes (weight 4) or tipped poison spikes with
  20..200 charges (weight 3).

### Cage (`cage`)

- States: `facing` (horizontal, opposite of the player's look), `light_level` 0..15 (from the
  captured mob's data, e.g. glowing mobs), `waterlogged`.
- Shape 14x16x14 px, not pathfindable. Iron bars look. Drops itself with the captured mob.
- Comparator: 15 when a mob is inside, else 0.

**Item use**
- Catching: hit (left click) or use (right click) a mob with an empty cage. Rules, in order:
  - never players or dead entities;
  - `allow_all_mobs` or the `/supplementaries cage <entity>` temporary list -> always;
  - entity type blacklist (bosses, multipart) -> never;
  - `cage_auto_detect` and the hitbox fits (width - 1/8 < 0.875 and height - 1/8 < 1) -> yes;
  - `cage_allow_all_babies` and baby -> yes;
  - in `cage_catchable` (slime, magma cube, vex, bee, frog, allay, endermite, parrot, rabbit, cat,
    chicken, armadillo, bat, fox, ocelot, silverfish, tadpole, axolotl, ...) -> yes;
  - in `cage_baby_catchable` (cow, sheep, pig, goat, ...) and baby -> yes.
  - Then `require_taming` (default on): a tameable mob must be tamed and owned by the player
    ("You must tame this animal first"); `health_threshold` < 100: the mob's health must be at most
    that percentage ("You can only catch animals with less than %s health"). Other failures show
    "You can't catch this animal" (action bar).
- On success: the mob is saved (position relative to the cage, rotation and animations reset),
  removed, its leash dropped, nearby neutral mobs of the same type get angry at the player, chain
  fall sound (pitch 0.7). The held cage becomes a full cage (one item).
- Full cage tooltip: the mob's name; hint "[Sneak] to place" (`message.supplementaries.cage.tooltip`).
- Releasing: use a full cage on a block *without sneaking*: the mob is spawned at the clicked
  point, facing the player's yaw, made persistent with `persistent_mobs`, neutral mobs get angry at
  a survival player; chicken-egg sound (pitch 0.05). The item becomes an empty cage, except in
  creative (the mob stays in the item, with a new UUID).
- Placing: only while sneaking (empty or full). A placed full cage shows the mob inside, scaled to
  fit (14/16 wide, 1 high, 1 px margin), standing on the floor or hovering in the middle for flying
  mobs; some mobs keep animating (a few, like chickens, even lay eggs).

### Fodder (`fodder`)

- States: `layers` 1..8 (default 8 when placed), `waterlogged`. Shape: `2 * layers` px high.
  Hay-block sounds.
- Layers merge downwards: when a fodder block sits on top of a fodder block that is not full, the
  lower one is filled from the upper one (the upper loses those layers or disappears).
- Using a hoe on fodder removes one layer (or the block), hoe-till sound, block break particles,
  1 durability.
- Comparator: `layers * 2 - 1`. Mobs path over it only with at most 4 layers.
- Drops 1 fodder only when full (8 layers).
- Eating (goal priority 3 for `eats_fodder` mobs: pig, cow, sheep, chicken, goat, mooshroom):
  an animal that is not in love and has no breeding cooldown (babies too) searches every
  800..1200 ticks for fodder with air above within 8 blocks horizontally and 2 vertically (600
  ticks between attempts once found), walks there (speed 1) and, once within 1.5 blocks of the
  block above it, eats for 30 ticks (crack animation, block particles every 2 ticks; sheep play
  their grass eating animation), then: one layer less (or the block is removed), happy villager
  particles; an adult falls in love (breeds without being fed); `ate()` runs (sheep regrow wool,
  a baby sheep grows up faster).

## 2. Screens

None. All feedback is chat/action bar text and item tooltips, which vanilla clients show as is
(item lore).

## 3. Configuration (`config/trap.json`)

| Key | Default | Meaning |
| --- | ------- | ------- |
| `bambooSpikes.enabled` | true | spikes damage entities |
| `bambooSpikes.tippedSpikes` | true | tipping with potions and tipped behaviour |
| `bambooSpikes.playerLoot` | false | spike kills drop player-only loot and XP |
| `bambooSpikes.onlyAllowHarmfulEffects` | true | only harmful potions, infinite charges |
| `cage.enabled` | true | catching and releasing |
| `cage.allowAllMobs` | false | any entity can be caught (not for survival) |
| `cage.allowAllBabies` | false | any baby can be caught |
| `cage.autoDetect` | false | any mob whose hitbox fits can be caught |
| `cage.persistentMobs` | false | released mobs never despawn |
| `cage.healthThreshold` | 100 | max health percentage to catch (100 = any) |
| `cage.requireTaming` | true | tameable mobs must be tamed by the player |
| `cage.catchable` / `cage.babyCatchable` | tag lists | the `cage_catchable` / `cage_baby_catchable` tags |
| `fodder.enabled` | true | animals eat fodder |
| `fodder.eaters` | pig, cow, sheep, chicken, goat, mooshroom | the `eats_fodder` tag |

## 4. Commands

`/supplementaries cage <entity>` (op): adds the entity type to a temporary (until the world
closes) catchable list. Reply: `commands.supplementaries.cage` "Added %s to temporary Cage and
Jars catchable list".

## 5. Strings

| Key | English |
| --- | ------- |
| `block.supplementaries.bamboo_spikes` | Bamboo Spikes |
| `item.supplementaries.bamboo_spikes_tipped` | Tipped Bamboo Spikes |
| `item.supplementaries.bamboo_spikes_tipped.effect` | Bamboo Spikes of %s |
| `block.supplementaries.cage` | Cage |
| `block.supplementaries.fodder` | Fodder |
| `death.attack.supplementaries.bamboo_spikes` | %1$s was impaled on a spike |
| `death.attack.supplementaries.bamboo_spikes.player` | %1$s impaled on a spike whilst fighting %2$s |
| `message.supplementaries.cage.fail` | You can't catch this animal |
| `message.supplementaries.cage.fail_health` | You can only catch animals with less than %s health |
| `message.supplementaries.cage.fail_tamed` | You must tame this animal first |
| `message.supplementaries.cage.tooltip` | [%s] to place |
| `commands.supplementaries.cage` | Added %s to temporary Cage and Jars catchable list |

Complementaries additions: `cm.trap.charges` "Charges: {}", `cm.trap.infinite` "Infinite",
`cm.trap.sneak` "Sneak".

## 6. Bedrock implementation

- Blocks are data driven (BP): geometry, per-state transformation (facing), collision/selection
  boxes, and per-permutation loot (tipped spikes, full cages and partial fodder drop nothing from
  the loot table; the mod drops the item with its data instead).
- Per-block data (custom blocks have no block entities): `cm:trap:spike:<dim>:<x>:<y>:<z>`
  `{potion, charges, last}` and `cm:trap:cage:<dim>:<x>:<y>:<z>` `{mob (base64 NBT), name,
  display}`. A block change hook drops the item with its data when such a block disappears
  (mining, explosion, `/setblock`), except when a creative player broke it.
- Items carry data in their user NBT: tipped spikes `{CmPotion, CmCharges}` (lore: effects,
  charges), full cages `{CmMob, CmMobName}` (lore: mob name, sneak hint). Crafted tipped spikes
  carry the potion id as aux value (one BP recipe per harmful lingering potion) and get their NBT
  when placed or picked.
- Spikes: a server tick scans living entities against spike blocks (Java `entityInside`).
  Damage cause `contact`; the death message is replaced with
  `death.attack.complementaries.bamboo_spikes` (RP text).
- Cage display: the captured mob is shown by a real copy of the mob inside the cage, frozen
  (no AI), persistent, invulnerable, untargetable by cages and spikes, tagged `cm_cage_display`.
  It is removed with the cage and re-created if it goes missing. Works for vanilla clients.
- Fodder eating: a server-side goal per eligible animal (state kept by unique id), using the
  mob's own navigation.

## 7. Not ported

| Feature | Reason |
| ------- | ------ |
| Waterlogging of spikes / cage / fodder | data-driven blocks have no waterlogged state; liquid detection is not stable for custom block geometry in 1.26 |
| Player slow-down inside spikes | player movement is client authoritative on Bedrock; mobs are slowed |
| Pathfinding avoidance of spikes / fodder height rule | Bedrock path costs come from engine block properties that data-driven blocks cannot set |
| Comparator output of cage and fodder | data-driven blocks cannot produce an analog signal |
| Cage light level from the captured mob | needs per-mob light data (Supplementaries data packs) |
| Captured mob scaling to fit / egg laying in cages | the display is the real mob model; allowed mobs already fit |
| Per-potion item tint of tipped spikes and the charges bar | item icons and bars cannot vary by NBT on Bedrock; the tooltip shows the potion and charges, the block tips are tinted (8 colours) |
| Creative tab with every tipped variant | the creative inventory cannot list NBT variants |
| Dispenser tipping, jungle temple loot injection | dispensing and vanilla loot tables are not hooked by this unit |
| Crafting tipped spikes from beneficial potions when `only_allow_harmful_effects` is off | recipes are static BP files; beneficial potions can still be applied in the world |
| Angering nearby neutral mobs on capture / release | Bedrock neutral mobs have no shared persistent-anger API |
