# U12 `display`: pedestal, item shelf, hat stand, statue

Behaviour of the display blocks and the hat stand of Supplementaries **4.0.2** (MC 1.21.1) and how
Complementaries reproduces it on Bedrock. Behaviour reference only: nothing here is copied from the
Supplementaries code or assets.

## 1. Content

| Id | Kind | Recipe (shaped) | Drops |
|----|------|-----------------|-------|
| `complementaries:pedestal` | block | `SSS` / ` B ` / `SSS`, S = stone brick slab, B = stone bricks or chiseled stone bricks; **2** pedestals | itself (survives explosions) + the displayed item |
| `complementaries:item_shelf` | block | `WWW`, W = any wooden slab; **2** shelves | itself + the displayed item |
| `complementaries:statue` | block | ` C ` / ` S `, C = clay block, S = smooth stone slab; 1 statue | itself + the held item |
| `complementaries:hat_stand` | item + entity | `P` / `S`, P = any planks, S = smooth stone slab; 1 hat stand | itself + the hat (when broken by a non-creative player, arrow, explosion, fire) |
| `complementaries:item_display` | helper entity (not obtainable) | – | – |

All three blocks hold **one** item (stack size 1). Comparators next to them read **15** when an
item is displayed and **0** otherwise.

## 2. Mechanics

### 2.1 Common item interaction (pedestal, item shelf, statue)

- Use with an item while the block is empty: one item is taken from the hand (not in creative) and
  displayed. Sound: item frame "add item" (item shelf: chiseled bookshelf insert).
- Use while the block holds an item (main hand): the item is given back (to the empty hand, else
  the inventory, else dropped at the block). Sound: item frame "remove item".
- The interaction consumes the click: no block is placed against the display block.
- Breaking the block drops the displayed item.

### 2.2 Pedestal

- States (Java): `up`, `down` (connected to pedestals above/below), `axis` (x/z, horizontal
  direction of the placer), `waterlogged`, `item_status` = `none` (cannot hold an item: a pedestal or
  a solid block is on top), `empty`, `full`.
- Stacking: a pedestal connects up to an empty pedestal above it (`up`), and down to a pedestal
  below whose status is `none` (`down`). Models: single, up, down, middle. A pedestal with an item
  does not connect up. Only a pedestal whose status is not `none` accepts items.
- Displayed item rendering (above the top, half scale, rotated to the `axis`):
  - sword (any sword or `#pedestal_downright`): stuck in the pedestal, point down, 1.5x;
  - trident (or `#pedestal_upright`): upright, 1.5x;
  - end crystal: a small end crystal entity (rotating, bobbing);
  - globe / sepia globe (U-globe unit): the globe model, spinning;
  - anything else: floating and spinning around the vertical axis (client options
    `pedestal_spin`, `pedestal_spin_speed`, `pedestal_special`).
  - a named item shows its name above the pedestal when the player is within 16 blocks.
- **Enchanting power**: an end crystal on a pedestal placed where a bookshelf would count for an
  enchanting table adds `crystal_enchanting` (default 3) bookshelves of power (NeoForge only in 4.0.2).
- Indiana Jones swap: using a sack on a pedestal swaps the sack and the displayed item (sack unit).

### 2.3 Item shelf

- Wall shelf: `facing` = the clicked face (clicking the top or bottom of a block gives `north`).
  Needs a non-air block behind it, otherwise it pops off. Waterloggable. Thin 3-pixel shape.
- The item is rendered half scale, standing on the shelf in front of the wall; block items are
  lowered a quarter block so they sit on the shelf (client option `shelf_translate`).
- Named items show their name when the player is within 4 blocks.
- Config `climbable_shelves` (default false) makes shelves ladders.

### 2.4 Statue

- A half-scale player statue (1 block tall) on a stone base, facing the player who placed it.
  States: `facing`, `waterlogged`, `powered`, `lit` (light level 7).
- It holds one item; the pose follows the item: empty hand = standing (one arm and leg forward,
  mirrored for north/west facing), sword = both arms raised holding the sword, tool = arms raised a
  bit less, candle = arms forward holding a lit candle (block `lit`, candle flame particles and
  ambient candle sound), globe, anything else = arms forward holding the item.
- Powered by redstone: the statue waves its right arm.
- Renaming: a statue named after a Supplementaries contributor (online credits list) shows that
  contributor's skin and name.
- Festivities: Halloween replaces the head with a carved pumpkin; Christmas/birthday/April Fools add
  a party hat.

### 2.5 Hat stand

- Placed like an armour stand by using the item on a block top or side (not the bottom), if the
  0.625 x 0.75 box is free. Faces away from the player, snapped to 22.5 degrees. Dispensers place
  hat stands too. Sound: armour stand place.
- Body: base plate, short neck, wooden dummy head. Holds one helmet-slot item (helmets, carved
  pumpkins, skulls, ... ; any item with config `unrestricted`).
- Use with a head item: equips it (swaps with the current hat). Empty hand: takes the hat back.
  Creative: places a copy. Sneak-use: makes the head swing like a pendulum.
- Entities walking through it and projectiles make it swing too.
- Punch: wobbles (armour stand hit sound); a second punch within 5 ticks breaks it, dropping the
  hat stand and the hat. Creative players break it at once without drops. Arrows and explosions
  break it; fire burns it down slowly; lava quickly.
- "Skibidi": a hat stand standing in a cauldron/composter (tall) or a hopper/decorated pot pops its
  head up and dances for 8 s (turning towards a player: see Not ported); it starts randomly (20 % every 5 s).

## 3. Screens

None. (No GUI in Supplementaries for these blocks.)

## 4. Configuration (`config/display.json`)

| Key | Default | Meaning |
|-----|---------|---------|
| `crystalEnchanting` | `3` | Bookshelves of enchanting power an end crystal on a pedestal adds (0 = off). |
| `pedestalSpin` | `true` | Items on pedestals spin (Supplementaries client option, server-wide here). |
| `pedestalSpecial` | `true` | Swords, tridents and end crystals get their special pose on pedestals. |
| `shelfTranslate` | `true` | Block items on shelves are lowered onto the shelf. |
| `hatStandUnrestricted` | `false` | Any item can go on a hat stand. |
| `hatStandSkibidi` | `true` | Hat stands in cauldrons/composters/hoppers/decorated pots dance. |

## 5. Commands

None.

## 6. Strings

Block / item / entity names (RP `texts.d/<locale>/display.lang`): Pedestal, Item Shelf, Statue,
Hat Stand (item and entity). Mod strings (`assets/lang/<locale>/display.json`):
`cm.display.placed` / `cm.display.taken` (log only).

## 7. Bedrock implementation

- **No block entities**: a display block keeps its item in the world DB (`cm:display:<dim>:<x>:<y>:<z>`,
  JSON with the item as SNBT); the block states carry what the client needs.
- **Rendering**: a helper entity `complementaries:item_display` per display block (no collision,
  no damage, not saved-critical: it is re-created from the DB). It holds the item in its main hand;
  data-driven client animations (RP) place, scale, spin and pose it per mode. The statue's figure
  (with poses, waving arm and candle flame particles) is the same entity with a player-shaped
  geometry. Works identically on modded and vanilla clients (all from the resource pack).
- **Hat stand**: data-driven entity; the hat is the head armour slot (armour attachables render it);
  wobble, pendulum swing and the skibidi dance are client animations driven by entity properties.
- **Comparator**: the custom block types get a vtable copy overriding `hasComparatorSignal` /
  `getComparatorSignal`.
- **Enchanting**: `EnchantUtils::getBookCasePositions` is extended with the crystal pedestals.
- **Redstone (statue `powered`)**: polled every 4 ticks from the six neighbours.

## 8. Not ported

| Feature | Reason |
|---------|--------|
| Contributor skins on renamed statues | Needs Supplementaries' online credits list and Java skin downloads; Bedrock entity textures are fixed by the resource pack. Renamed statues behave like unnamed ones. |
| Festivity decorations on statues (pumpkin head, party hat) | Low value; can be added later as animation variants. |
| `climbable_shelves` | Bedrock has no climbable custom blocks (the climb check is not data-driven and is not a block virtual). |
| Pedestal globe rendering | Belongs to the globe unit; globes display as normal spinning items. |
| Sack "Indiana Jones" swap | Belongs to the sack unit (not in this unit). |
| Item names floating above pedestals/shelves | Bedrock shows entity name tags only when looking at the entity; the helper entity cannot be looked at. |
| Waterlogging | Bedrock data-driven blocks cannot be waterlogged in this engine version. |
| Feature on/off flags | Blocks and recipes come from the behavior pack and cannot be unregistered at runtime. |
| Dispensers placing hat stands / inserting into display blocks | Bedrock dispensers drop data-driven items; the dispense behaviour is not a hookable per-item registry. Hoppers do not feed display blocks either (no container block entity). |
| Skibidi dance turning towards a player | The dance animation plays; the head does not track a target. |
| Redstone power through solid blocks (statue) | Bedrock's circuit graph is not queryable for custom blocks; only adjacent power sources count. |
