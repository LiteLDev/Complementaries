# U2 `storage`: sack, presents, lunch basket, quiver

Behaviour reference: Supplementaries 4.0.2 (MehVahdJukaar). This file describes behaviour only; no code
or assets come from Supplementaries. Everything lives in `src/feature/storage/` (namespace
`cm::feature::storage`). Selftests: `storage.*`, test area dx 200..299.

## 1. Content model (all four features)

Supplementaries keeps container contents in block entities and item NBT. Bedrock custom blocks have no
block entities, so Complementaries keeps one **content record** per container in the world DB and
points to it from the block or the item:

| Key | Value |
| --- | ----- |
| `cm:storage:inv:<uid>` | `{"items": [{"slot": n, "nbt": "<SNBT of the item stack>"}]}` — the contents of one sack / present / quiver / lunch basket |
| `cm:storage:block:<dim>:<x>:<y>:<z>` | `{"kind": "sack"\|"present", "uid": "...", "packed": bool, "recipient": "...", "sender": "...", "description": "..."}` |

Items carry `{"cm_storage": {"uid": "...", "sel": n, ...}}` in their user data (NBT). `uid` is a random
128-bit hex id, assigned on first use. Breaking a block hands its uid to the dropped item; placing the
item hands it back to the block. Contents are never copied, so no duplication can happen through the
item/block round trip. Items show their contents as lore lines (Supplementaries shows them in the
tooltip): up to 5 lines `<name> x<count>`, then `and <n> more...`.

A creative-mode copy of an item (pick block / creative inventory duplication) shares the uid and
therefore the contents (like Bedrock's own bundles). This only affects creative mode.

## 2. Sack

| | |
| --- | --- |
| Id | `complementaries:sack` (block + block item) |
| Recipe | shaped `" S "`, `"L L"`, `"LLL"`: S = string, L = leather → 1 sack |
| States | `complementaries:open` (bool; true while someone has the sack screen open) |
| Shape | 10×10×10 body with a tied neck (open state: neck untied) |
| Hardness | 0.8, sound: wool; mined by hand |
| Drops | itself with its contents (any way it is removed: player, explosion, command). In creative an empty sack drops nothing, a non-empty one drops itself (shulker-box behaviour). Loot table is empty; the mod spawns the item. |
| Pistons | pushed and pulled like other blocks; the contents move with it (Bedrock turns a pushed block into air and places it again next to it a few ticks later: the record waits up to 6 ticks for the block to reappear next to its old place, else the block drops) |

Mechanics:
- **Container**: `sack_slots` slots (default 9). Right-click opens the sack screen and plays
  `block.sack.open` (Bedrock: `armor.equip_leather` sound); the block switches to `open=true` while
  viewed, back to `false` when the last viewer closes it (sound again).
- **Allowed items**: everything that may go into a container item: not sacks, not shulker boxes, not
  bundles. Disallowed items put into the screen bounce back to the player's inventory (or drop at
  their feet when it is full; with several viewers they drop at the sack, since the server cannot
  tell who put them in).
- **Gravity**: the sack is a falling block. When the block below becomes air / replaceable it drops
  straight down to the first supporting block (Bedrock: it moves there instantly; no falling entity
  animation, see *Not ported*).
- **Overencumbered** (`sack_penalty`, default true): every 27 game ticks each survival/adventure player
  counts the sacks in their inventory (stack counts, off-hand included). With `n` sacks and increment
  `i = sack_increment` (default 2), if `n > i` they get the Overencumbered effect for 10 s with
  amplifier `(n - 1) / i - 1`. Bedrock has no custom mob effects: the effect is vanilla **Slowness**
  at that amplifier, without particles (Overencumbered is a movement-speed penalty too).
- **Tooltip**: contents as lore (see §1).

Screen: Supplementaries draws the slots centred in rows of up to 9. Bedrock: a vanilla container
screen is opened on an invisible helper entity (`complementaries:storage_view`) that holds a copy of
the contents while the screen is open; every change is written through to the content record, the
helper disappears when the screen closes. The layout is the closest vanilla grid: 1–5 slots hopper
row, 6–9 slots dispenser 3×3, 10–27 slots chest 3×9 (slots beyond the configured count stay empty and
reject items). This screen works the same on vanilla and modded clients.

## 3. Presents

| | |
| --- | --- |
| Ids | `complementaries:present`, `complementaries:present_<color>` (16 dye colours) |
| | `complementaries:trapped_present`, `complementaries:trapped_present_<color>` |
| Recipes | present: shaped `"PSP"`, `"P P"`, `"PPP"` (P = paper, S = string). Trapped present: shapeless present + tripwire hook. Dyed: see *dyeing* below |
| States | `complementaries:packed` (bool) |
| Shape | 10×10×10 box (12×3×12 lid with a bow when packed; open box when unpacked) |
| Hardness | 0.5 (instant-ish), wool sound |
| Drops | itself, keeping contents, packed state, recipient, sender and description (same rule as the sack) |

Present (normal):
- One slot. Right-click on an unpacked present opens the **present screen**: the slot, a *recipient*
  text field (`To:`; empty = public), a *description* field and a **Pack** button. Packing stores the
  sender (the packing player's name), sets `packed=true`, plays the pack sound
  (`block.present.pack`; Bedrock: the vanilla `item.book.page_turn` paper sound) and closes the screen.
- Right-click on a packed present: players allowed to open it (public present, the recipient, or a
  creative player) unpack it: `packed=false`, sound `block.present.open` and the screen opens with the
  gift inside; the message `From <sender>` is shown. Anyone else gets the action-bar message
  `This present is for <recipient>` and nothing happens.
- The packed item's tooltip shows `To: <recipient>` / `From: <sender>` / the description; a public
  present shows `Public`.

Trapped present:
- Same packing screen without the recipient field. When packed, any player right-clicking it opens it:
  one item of the content is **activated** with the dispenser behaviour of the item, shooting upwards
  from the top of the present (TNT ignites, arrows/potions/fire charges/snowballs/eggs are shot, spawn
  eggs spawn their mob, fireworks launch); the rest of the stack (or an item without a dispenser
  behaviour) pops out. The present becomes unpacked and empty.

Screen on Bedrock: a LeviLamina form (custom form) with the current content, an *item* drop-down
(*keep*, *put the item you hold*, *take the item out*), the recipient field (normal presents), the
description field and a *Pack* toggle. Forms work on every client; the modded client uses the same form.

Dyeing: right-click a placed present with a dye: it becomes the present of that colour, keeping its
contents and state, and the dye is consumed (not in creative). Supplementaries also has a crafting
recipe (present + dye); on Bedrock a crafting result cannot keep the input's data, which would destroy
packed gifts, so dyed presents are made by using the dye on the placed block (and by crafting an
**empty** present: shapeless `present + dye` recipes are provided and only make sense for empty
presents, as on Bedrock crafting clears contents — the empty-present case is identical).

## 4. Quiver

| | |
| --- | --- |
| Id | `complementaries:quiver` (item, stack size 1) |
| Recipe | shaped `" SL"`, `"L L"`, `"LL "` (S = string, L = leather) |
| Loot | skeletons (incl. strays and bogged) spawn with a quiver with chance `quiver_skeleton_spawn_chance` (default 0.03); such a skeleton drops a quiver filled with a few random (tipped) arrows when killed by a player |

Mechanics:
- Holds `quiver_slots` stacks of arrows (default 6): arrows, tipped arrows, spectral arrows.
- **Selected arrow**: one slot is selected (`sel`). Cycling: sneak + use the quiver, or, while holding a
  bow or crossbow, sneak + swing (left-click air). The action bar shows the selected arrow.
  Supplementaries cycles with the mouse wheel while aiming / a keybind; Bedrock clients send neither
  to the server.
- **Shooting**: while a player holds a bow or crossbow in the main hand and carries a quiver (anywhere
  in the inventory) with a selected arrow stack, and the off-hand is empty, the selected stack is lent
  to the off-hand (Bedrock shoots off-hand arrows first; it also lets an unmodified client draw the bow).
  The lent stack is marked; switching away from the bow, changing the selection or losing the quiver
  returns what is left of it to the quiver. Arrows shot are taken from the quiver's stack. A marked
  stack found anywhere else in a player's inventory (moved, dropped and picked up again) is returned to
  its quiver, or loses the mark when that quiver is not in the inventory.
- Use (not sneaking): opens the quiver screen (container screen as for the sack, arrows only).
- Tooltip: contents as lore, the selected stack first and marked `>`.

## 5. Lunch basket

| | |
| --- | --- |
| Id | `complementaries:lunch_basket` (item, stack size 1) |
| Recipe | shaped `" T "`, `"P P"`, `"PPP"` (T = stick, P = any planks) |

Mechanics:
- Holds `lunch_basket_slots` stacks of food (default 6). Only edible items are accepted.
- **Selected food** as for the quiver: sneak + use cycles it (action bar shows the selection).
- Use (not sneaking): eats the selected food with that food's eating time, nutrition and effects (and
  its container remainder, e.g. a bowl, goes to the inventory). If the basket is empty, or the player
  cannot eat the selected food now (full hunger and not an always-edible food), the basket screen opens
  instead. Sneak + swing (left-click air) while holding the basket always opens the screen.
- Tooltip: contents as lore, the selected stack marked `>`.

## 6. Configuration (`config/storage.json`)

| Key | Default | Meaning |
| --- | --- | --- |
| `sackPenalty` | `true` | apply Overencumbered (Slowness) for too many sacks |
| `sackIncrement` | `2` (0–50) | sacks allowed before the penalty; each further multiple adds a level (0 = never) |
| `sackSlots` | `9` (1–27) | slots of a sack |
| `quiverSlots` | `6` (1–9) | stacks a quiver holds |
| `quiverSkeletonSpawnChance` | `0.03` (0–1) | chance that a skeleton spawns with a quiver |
| `lunchBasketSlots` | `6` (1–9) | stacks a lunch basket holds |
| `presentsAllowDyeing` | `true` | right-click a present with a dye to recolour it |

## 7. Commands

None (Supplementaries has none for these features).

## 8. Strings (`assets/lang/<locale>/storage.json`)

| Key | English |
| --- | --- |
| `block.supplementaries.sack` | Sack |
| `item.supplementaries.quiver` | Quiver |
| `item.supplementaries.lunch_basket` | Lunch Basket |
| `block.supplementaries.present` | Present |
| `block.supplementaries.trapped_present` | Trapped Present |
| `cm.storage.more` | and {} more... |
| `message.supplementaries.present.info` | This present is for {} |
| `message.supplementaries.present.from` | From {} |
| `message.supplementaries.present.to` | To: {} |
| `message.supplementaries.present.public` | Public |
| `gui.supplementaries.present.pack` | Pack |
| `gui.supplementaries.present.send` | To: |
| `gui.supplementaries.present.description` | Description |
| `message.supplementaries.quiver.selected` | Selected: {} |
| `message.supplementaries.lunch_basket.selected` | Selected: {} |
| `message.supplementaries.quiver.empty` | The quiver is empty |
| `message.supplementaries.lunch_basket.empty` | The basket is empty |
| `effect.supplementaries.overencumbered` | Overencumbered |
| `cm.storage.present.item` | Item |
| `cm.storage.present.keep` | Keep the content |
| `cm.storage.present.put` | Put the item you hold |
| `cm.storage.present.take` | Take the item out |
| `cm.storage.present.empty` | Empty |
| `cm.storage.present.contents` | Content: {} |
| `cm.storage.bounced` | {} can't go in there |

Block and item names for the game come from `texts.d/<locale>/storage.lang`.

## 9. Not ported / differences (Bedrock reasons)

- **Falling sack animation**: custom blocks cannot become falling-block entities; the sack moves down
  instantly.
- **Waterlogging** of sacks and presents: custom blocks have no waterlogged state.
- **Overencumbered as its own effect**: Bedrock has no custom mob effects; vanilla Slowness is used.
- **Bundle-style item interaction** (clicking arrows/food onto the quiver or basket inside the
  inventory screen): Bedrock's inventory screen only supports this for its own bundle storage, which
  cannot hold the quiver's 6 stacks or feed bows; the quiver and basket open their own screen instead.
- **Mouse-wheel / keybind cycling** of the selected arrow or food and the quiver HUD overlay: replaced
  by sneak + use / sneak + swing and the action bar; the lent off-hand stack shows the selected arrow.
- **Quiver worn on the back (rendered on the player model)** and skeletons visibly wearing quivers:
  needs per-player render layers that Bedrock does not expose to data-driven packs.
- **Skeletons shooting their quiver's tipped arrows**: vanilla Bedrock skeleton AI picks its own
  arrows; the quiver only matters for its drop.
- **Sacks/presents stacking**: Bedrock block items always stack to 64; empty sacks therefore stack
  (filled ones never stack because their uids differ).
- **Present / sack slot layout**: vanilla container grids (hopper / dispenser / chest) instead of the
  centred custom rows; presents use a form.
