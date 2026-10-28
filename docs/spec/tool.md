# U16 `tool`: tools and gadgets

Behaviour reference for the tool unit: slingshot, bubble blower (+ bubble block), soap, wrench,
flute, confetti popper, antique ink and slice map, as they behave in Supplementaries 4.0.2.
Written from the observed behaviour of the Java mod; nothing is copied from its code or assets.

Source files: `src/feature/tool/` (server logic, selftests `tool.*`, dx 1600..1699),
`assets/lang/*/tool.json`, BP `items/`, `blocks/bubble_block.json`, `recipes/`, RP textures,
`particles/tool_*.json`, `tools/textures/tool.ps1`, `tools/e2e/{server,client}/scenarios/tool.ps1`.

## 1. Items and blocks

| Id (`complementaries:`) | Kind | Stack | Durability | Notes |
| --- | --- | --- | --- | --- |
| `slingshot` | item | 1 | 192 | hold use to charge (bow pose), release to fire |
| `bubble_blower` | item | 1 | 250 charges | hold use to blow bubbles; use on a block face places a bubble block |
| `bubble_block` | block | – | – | translucent, no item form, no drops |
| `soap` | item | 64 | – | consumed when it cleans something |
| `wrench` | item | 1 | 128 | 3 attack damage |
| `flute` | item | 1 | 64 | 1 s cooldown |
| `confetti_popper` | item | 16 | – | consumed on use |
| `antique_ink` | item | 16 | – | consumed on use |
| `empty_slice_map` | item | 64 | – | turns into a vanilla `minecraft:filled_map` showing a slice |

### Recipes

Supplementaries uses some of its own materials (ash, flax ...) owned by other units. To keep this
unit independent the Bedrock recipes use vanilla ingredients with the same shape idea:

| Result | Recipe |
| --- | --- |
| slingshot | shaped `SLS / .S. / .S.`, S = stick, L = string |
| bubble_blower | shaped `.N / S.`, N = iron nugget, S = stick (recharging: see below) |
| bubble_blower (refill) | shapeless bubble_blower + soap (a fresh, fully charged blower) |
| soap ×2 | shapeless honeycomb + bone meal |
| wrench | shaped `C.C / .C. / .C.`, C = copper ingot |
| flute | shaped `..B / .B. / B..`, B = bamboo |
| confetti_popper ×2 | shapeless paper + gunpowder + pink dye |
| antique_ink | shapeless glass bottle + ink sac + brown dye |
| empty_slice_map | shapeless empty map + amethyst shard |

### Bubble block states / drops

`bubble_block` has no states. It has no loot (pops), breaks instantly, is not pushable by pistons
(pops). Light passes through; collision is a full cube.

## 2. Mechanics

### Slingshot

- **Charging**: hold use. Full charge after `slingshot.chargeTime` ticks (default 20), minus 25 %
  per Quick Charge level (at least 5 ticks). Releasing earlier fires weaker; below 10 % of a full
  charge nothing happens.
- **Ammo**: the item in the off hand if it is not empty, otherwise the first stack in the hotbar,
  then the main inventory (never the slingshot itself, never items in `slingshot.ammoBlacklist`).
  One item is taken per projectile (none in creative). Without ammo nothing happens.
- **Projectile**: the fired item flies as a real item entity (so every client renders the actual
  item, as Supplementaries renders the item), cannot be picked up while flying, speed
  `1.5 × power × slingshot.rangeMultiplier` blocks/tick, item gravity.
- **Hitting a block**: a block item is placed on the face it hit if that space is replaceable and the
  block may stand there (`slingshot.placeBlocks`), with the block's place sound; otherwise the item
  drops where it hit and can be picked up. Ender pearls teleport the shooter to the impact point
  (5 fall damage like a thrown pearl), eggs have a 1/8 chance to hatch a chick.
- **Hitting an entity**: 1 damage (projectile damage from the shooter) plus knockback along the
  flight direction; the item then drops.
- **Multishot**: three projectiles at -10°, 0°, +10°, each consuming one ammo item (fewer if less
  ammo is available).
- **Loyalty**: instead of placing / dropping, the projectile flies back to the shooter (faster
  with higher levels) and goes into their inventory.
- **Stasis** (Supplementaries enchantment): no gravity; the projectile slows down by
  `slingshot.stasisDeceleration` per tick and starts falling once it is almost still.
- Each shot costs 1 durability. Sounds: `random.bow` (high pitch) on release.
- Unbreaking / Mending: vanilla durability handling.

### Bubble blower and bubble block

- **Blowing**: while use is held, a stream of bubbles (particle `complementaries:bubble`) leaves the
  player's mouth along the look direction every 2 ticks; every 4th puff costs one charge.
  An empty blower (all charges used) does nothing.
- **Bubble block**: using the blower on a block face places a `bubble_block` in the adjacent
  replaceable space, costs `bubbleBlower.bubbleBlockCost` charges (default 5), sound `random.pop`.
- **Refill**: using the blower while the other hand holds soap refills all charges and consumes
  one soap (keeps enchantments); the shapeless recipe makes a fresh blower.
- **Bubble block lifetime**: pops after `bubbleBlower.bubbleLifetime` ticks (default 1200; ×4 with
  Stasis on the blower that made it). Popping: block removed, `random.pop`, bubble particles.
- **Stepping on it**: an entity standing or landing on a bubble block makes it pop after 5 ticks
  (`bubbleBlower.breakWhenTouched`), unless it wears boots with Feather Falling. Falling onto it
  cancels the fall damage (it is soft). Hitting it pops it immediately.

### Soap

Using soap on something cleans it and uses one soap (not in creative):

- **Blocks** (use on block): coloured wool / carpet → white; stained glass (+ panes) → glass /
  glass pane; coloured terracotta → terracotta; coloured shulker box → undyed shulker box (contents
  kept); coloured candles / candle cakes → plain; sticky piston → piston (same facing); signs: text
  colour back to black, glow ink removed (front and back), antique ink removed. Particles
  `complementaries:suds`, sound `random.splash` (quiet).
- **Entities** (use on entity): a coloured sheep becomes white.
- **Items** (use in the air): cleans the item in the other hand: dye removed from leather armour /
  leather horse armour / wolf armour, banner patterns removed, coloured shulker box / wool / carpet /
  glass / terracotta / candle stacks turned plain, antique flag removed from books and maps.
- **Water**: using soap while standing in water (or looking at water) makes a cloud of bubbles
  without using the soap.

### Wrench

- **Use on a block**: rotates it clockwise around the axis of the clicked face; sneaking rotates
  counter-clockwise. Horizontal-only blocks (stairs, chests, furnaces, signs, banners, skulls,
  repeaters, ...) rotate about the vertical axis whatever face is clicked. Six-way blocks (observers,
  dispensers, pistons, end rods, ...) rotate their facing about the clicked face's axis. Pillars
  (logs, basalt, ...) swap their axis unless the clicked face is along it. 16-way rotations (signs,
  banners) turn by 90°.
- Blocks that cannot be rotated safely are skipped: multi-block structures (beds, doors, tall
  plants, extended pistons), double chests, portals, and anything in `wrench.blacklist`.
- 1 durability per rotation, sound `random.click` (pitch 1.0 / 0.8 counter-clockwise), the
  normal block interaction (opening chests ...) is suppressed while holding the wrench, except
  when `wrench.bypassWhenSneaking` is off and the player sneaks.
- **Use on an entity**: turns it by 45° (armor stands, mobs), sneaking the other way.

### Flute

- **Use (unbound)**: plays a tune (`note.flute`, rising pitches, note particles). Every pet tamed by
  the player within `flute.unboundRadius` (64) blocks stands up and teleports next to the player.
  Parrots within that radius dance for 10 s.
- **Binding**: use the flute on your own tamed pet: the flute stores the pet (lore
  "Bound to: <name>"). Sneak-use in the air unbinds it.
- **Use (bound)**: only the bound pet is called, from up to `flute.boundDistance` (500) blocks in
  the same dimension. If it is gone the binding is cleared.
- 1 durability per tune, 1 s cooldown.

### Confetti popper

- Use: a cone of confetti (`complementaries:confetti`) bursts in the look direction, sound
  `firework.blast` + `random.pop`, the popper is consumed (not in creative).
- A dispenser fires a popper the same way in its facing direction.

### Antique ink

- Use on a sign: both sides become *antique*: dark sepia text, glow removed. Stored per sign
  (`cm:tool:antique:<dim>:<x>:<y>:<z>`). Soap or an ink sac (vanilla, text black) removes it.
- Use in the air with a written book or a filled map in the other hand: the item becomes antique
  (lore line "Antique", flag `cm_antique` in its data). Soap removes it.

### Slice map

- Use an empty slice map: it becomes a locked vanilla filled map (scale 0, centred on the player,
  like an empty map) that shows the world **cut at the player's eye height** `Y`: for every column the
  first non-air block at or below `Y` (looking down at most 64 blocks), shaded by depth; columns with
  nothing below are dark. Lore "Height: Y".
- While a player holds it, the area within `sliceMap.rangeMultiplier × 64` blocks of them is redrawn
  progressively (about 1024 columns per tick), so caves appear as you explore.
- Copying a slice map in a cartography table copies the image (vanilla); the copy is not updated.

## 3. Screens

None. All feedback is in-world (particles, sounds, lore). Vanilla clients see the same thing as
modded clients: the projectile is a real item entity, particles and textures come from the resource
pack.

## 4. Configuration (`config/tool.json`)

| Key | Default | Meaning |
| --- | --- | --- |
| `slingshot.enabled` | true | slingshot behaviour |
| `slingshot.chargeTime` | 20 | ticks to full charge |
| `slingshot.rangeMultiplier` | 1.0 | projectile speed multiplier |
| `slingshot.stasisDeceleration` | 0.9625 | speed factor per tick with Stasis |
| `slingshot.placeBlocks` | true | block items are placed where they land |
| `slingshot.ammoBlacklist` | `[]` | item ids never fired |
| `bubbleBlower.enabled` | true | bubble blower behaviour |
| `bubbleBlower.bubbleLifetime` | 1200 | bubble block lifetime (ticks) |
| `bubbleBlower.bubbleBlockCost` | 5 | charges per bubble block |
| `bubbleBlower.breakWhenTouched` | true | entities standing on a bubble pop it |
| `soap.enabled` | true | soap behaviour |
| `wrench.enabled` | true | wrench behaviour |
| `wrench.bypassWhenSneaking` | false | sneaking with the wrench uses the block normally |
| `wrench.blacklist` | `[]` | block ids never rotated |
| `flute.enabled` | true | flute behaviour |
| `flute.unboundRadius` | 64 | radius of an unbound flute |
| `flute.boundDistance` | 500 | max distance of a bound pet |
| `confettiPopper.enabled` | true | popper behaviour |
| `antiqueInk.enabled` | true | antique ink behaviour |
| `sliceMap.enabled` | true | slice map behaviour |
| `sliceMap.rangeMultiplier` | 1.0 | update radius (×64 blocks) |

## 5. Commands

`/supplementaries tool enchant <stasis|loyalty> [level]` (operator): applies the enchantment to the
held slingshot / bubble blower (Bedrock cannot add Loyalty to it with an anvil, and Stasis does not
exist in Bedrock; see below). `/supplementaries tool enchant clear` removes both.

## 6. Strings (`assets/lang/<locale>/tool.json`)

| Key | English |
| --- | --- |
| `message.supplementaries.flute.bound` | `Bound to: {}` |
| `message.supplementaries.flute.unbound` | `Flute unbound` |
| `message.supplementaries.flute.not_your_pet` | `This is not your pet` |
| `message.supplementaries.slice_map` | `Height: {}` |
| `message.supplementaries.antique` | `Antique` |
| `message.supplementaries.bubble_blower.empty` | `Needs soap` |
| `enchantment.supplementaries.stasis` | `Stasis` |
| `cm.tool.enchant.done` | `Applied {} {}` |
| `cm.tool.enchant.cleared` | `Removed Loyalty and Stasis` |
| `cm.tool.enchant.no_item` | `Hold a slingshot or a bubble blower` |

Item / block names (RP `texts.d/*/tool.lang`): Slingshot, Bubble Blower, Bubble Block, Soap, Wrench,
Flute, Confetti Popper, Antique Ink, Empty Slice Map.

## 7. Not ported

| Feature | Reason |
| --- | --- |
| Stasis as a real enchantment (enchanting table / books) | Bedrock has no custom enchantments. Stored as item data `cm_stasis` with a lore line, applied with the command. |
| Loyalty on the slingshot from anvils / `/enchant` | the item's enchantment slot (crossbow: Quick Charge, Multishot) cannot also accept trident enchantments. The behaviour is implemented; the command applies it. |
| Charge-dependent slingshot icons | data-driven item icons cannot change with the draw progress; the bow pose is used. |
| Antique **font** on signs / books, sepia map rendering | Bedrock cannot change the font of one sign or book, nor tint one map; signs get sepia text colour, books and maps keep the flag and lore. |
| Antique book / slice map crafting recipes that keep NBT | Bedrock data recipes cannot copy item data; the same results are made by using the ink / soap with the item in the other hand. |
| Slingshot spinning projectile model | the projectile is a vanilla item entity (bobbing item render). |
