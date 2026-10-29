# U13 `tableware` — behaviour spec

Goblet, urn, planter, flower box, book piles, pancake, candy and jar boat of Supplementaries 4.0.2,
as Complementaries reproduces them. Behaviour reference only (written from the behaviour of the
Java mod, no code or assets copied). Unit dx range for selftests: 1300..1399.

All visuals are data driven (behavior pack block definitions + resource pack geometry with bone
visibility driven by block states), so a **vanilla client on BDS sees and uses everything** exactly
like a modded client: there are no screens in this unit and nothing client-only. The client build
runs the same server logic on the integrated server of local worlds.

## 1. Blocks and items

| Id | Kind | Block states (Bedrock) | Drops |
|----|------|------------------------|-------|
| `complementaries:goblet` | block + block item | `cm:fluid` 0..15 (see §2.1) | itself (the drink is lost) |
| `complementaries:urn` | block + block item | `cm:treasure` 0..1 | itself; treasure urns drop `loot_tables/blocks/urn_treasure.json` |
| `complementaries:planter` | block + block item | — | itself |
| `complementaries:flower_box` | block + block item | `minecraft:cardinal_direction` (front, towards the player), `cm:wall` 0..1, `cm:flower0..2` 0..15 | itself + the planted flowers |
| `complementaries:book_pile` | block, no item | `minecraft:cardinal_direction`, `cm:book0..3` 0..7 (0 empty, 1-6 plain book colours, 7 enchanted) | the exact books (NBT kept) |
| `complementaries:book_pile_horizontal` | block, no item | `minecraft:cardinal_direction`, `cm:book0..3` 0..2 (0 empty, 1 written book, 2 book and quill) | the exact books (NBT kept) |
| `complementaries:pancake` | block placed by the pancake item | `cm:pancakes` 0..7 (= 1..8 pancakes), `cm:topping` 0..3 (none, honey, syrup, chocolate) | one pancake per layer |
| `complementaries:jar_boat` | block + block item | `minecraft:cardinal_direction` | itself |
| `complementaries:pancake` | item (food, places the pancake block) | | |
| `complementaries:candy` | item (food) | | |

Recipes (crafting table):

| Result | Recipe |
|--------|--------|
| Goblet | shaped `I I` / ` I ` / ` I ` (iron ingots) |
| Urn | shaped ` C ` / `C C` / `CCC` (terracotta) |
| Planter | shaped `T T` / `TDT` / `TTT` (terracotta, dirt) |
| Flower Box | shaped `P P` / `PPP` (any planks) |
| Pancake x2 | shapeless egg + milk bucket + 2 wheat (+ sugar) |
| Candy x2 | shapeless sugar + sugar |
| Jar Boat | not craftable: shipwreck treasure loot (see §2.8), creative inventory |

Book piles have no item; they are made by placing books (§2.5).

## 2. Mechanics

### 2.1 Goblet
- Holds **one bottle** of a fluid (small self-contained fluid model, independent from the jar unit).
- Fill: use a filled bottle on an empty goblet. Accepted: water bottle, potion, splash potion,
  lingering potion, honey bottle, dragon's breath, bottle o' enchanting. The bottle becomes a glass
  bottle (in creative the held item is kept). Sound: bottle empty.
- Empty: use a glass bottle on a filled goblet: you get the bottle back (same potion, same type).
- Drink: use an empty hand on a goblet with a drinkable fluid: water (puts out fire), potions of any
  type (their effects apply), honey (6 food, 1.2 saturation, cures poison). Sound: drink.
  Dragon's breath and experience are not drinkable.
- `cm:fluid` values: 0 empty, 1 water, 2 honey, 3 milk (reserved), 4 dragon's breath,
  5 experience, 6..15 potion colour buckets (red, dark red, orange, yellow, green, light blue, blue,
  pink, grey, black). The exact potion (aux id and bottle type) is kept in the world DB under
  `cm:tableware:goblet:<dim>:<x>:<y>:<z>`; the colour bucket is chosen from the potion's effect.
- Not ported: comparator output (see §8).

### 2.2 Urn
- Decorative clay pot, breaks instantly-ish (hardness 0.5, like a decorated pot).
- Generated in caves (`underground_pass`, overworld, y -50..60) in small clusters (1-4 urns on
  stone / deepslate / tuff floors) with `cm:treasure=1`. Player-placed urns have `cm:treasure=0`.
- Breaking a treasure urn (any way: player, explosion, projectile) drops loot from
  `loot_tables/blocks/urn_treasure.json` (common: string, bones, rotten flesh, arrows, coal, gold
  nuggets, seeds, emerald / rare: gold ingot, diamond, emerald, enchanted book, music disc) and, with
  `urnEntitySpawnChance` (default 0.1), releases a mob from `urnSpawnMobs` (default
  `minecraft:silverfish`).
- An arrow / trident / any projectile that hits an urn breaks it.

### 2.3 Planter
- A terracotta box full of soil. Anything planted on dirt or farmland can be planted on top: all
  crops (wheat, carrots, potatoes, beetroot, melon / pumpkin stems, torchflower), saplings,
  flowers, ferns, dead bushes, mushrooms are placed and survive on it.
- Crops on a planter grow as on **moist farmland** (the planter counts as hydrated farmland for the
  growth speed); it never dries and can't be trampled.

### 2.4 Flower Box
- Placed on the floor (`cm:wall=0`, front towards the player) or against a wall
  (`cm:wall=1`, back against the wall, front away from it).
- Three slots, left to right as seen from the front. Use a flower on a slot (the third of the box
  you click) to plant it; use an empty hand to take it out. Accepted (vanilla small plants):
  dandelion, poppy, blue orchid, allium, azure bluet, red / orange / white / pink tulip, oxeye
  daisy, cornflower, lily of the valley, wither rose, oak sapling, birch sapling (state values 1..15
  in that order).
- Breaking the box drops it and its flowers.

### 2.5 Book piles
- Sneak + use a book on the **top face** of a block (the space above must be free) to put it down:
  - books and enchanted books → standing `book_pile` (up to 4 side by side; plain books get a
    random cover colour, enchanted books are purple);
  - written books and books and quills → lying `book_pile_horizontal` (up to 4 stacked).
  - `mixedBooks` (default false): every book kind can go into both piles.
  - `placeableBooks` (default true) / `writtenBooks` (default true) enable the two kinds.
- Sneak + use another book of a matching kind on a pile adds it (max 4).
- Use an empty hand on a pile to take the last book back.
- The exact books (enchantments, titles, pages) are stored in the world DB
  (`cm:tableware:books:<dim>:<x>:<y>:<z>`) and dropped when the pile is broken.
- **Enchanting power**: every book in a pile within bookshelf range of an enchanting table (same
  positions as bookshelves, with the air gap) adds `bookPower` (0.333) — enchanted books add
  `enchantedBookPower` (0.6) — to the table's bookshelf count (rounded down, max 15 in total).

### 2.6 Pancake
- Food item: 3 food, 0.6 saturation modifier. Using it on a block places a pancake.
- Use a pancake on a pancake block to stack it (max 8).
- Use a honey bottle on a pancake without topping to add a honey topping (bottle returned).
  Syrup and chocolate toppings exist as states (Supplementaries takes them from other mods).
- Use anything else (or an empty hand) while hungry (or in creative) to eat the top pancake
  (3 food, 0.6 saturation; +1 food with a topping); the last one removes the block.
- Breaking a pancake block drops one pancake per layer.

### 2.7 Candy
- Food item: 1 food, 0.1 saturation modifier, always edible, eaten fast (0.8 s).

### 2.8 Jar Boat
- Decorative boat in a glass jar, faces the player when placed.
- Generated in shipwreck treasure chests with chance `jarBoatShipwreckChance` (default 0.25).

### 2.9 Common rules
- Like Java block use, a click while sneaking with an item in hand is left to the item (goblet,
  flower box and pancake ignore it); book piles are the exception: placing and adding books needs
  sneaking.
- In creative, held items are not used up, and a bottle exchange only hands out the result when the
  player has none yet.

## 3. Screens

None. (Every interaction is a right click on the block.)

## 4. Configuration — `config/tableware.json`

| Key | Default | Meaning |
|-----|---------|---------|
| `version` | 1 | config schema version |
| `placeableBooks` | true | books / enchanted books can be placed as piles |
| `writtenBooks` | true | written books / books and quills can be placed as piles |
| `mixedBooks` | false | any book kind in any pile |
| `bookPower` | 0.333 | enchanting power per plain book in a pile |
| `enchantedBookPower` | 0.6 | enchanting power per enchanted book in a pile |
| `urnEntitySpawnChance` | 0.1 | chance a treasure urn releases a mob |
| `urnSpawnMobs` | `["minecraft:silverfish"]` | mobs a treasure urn can release |
| `planterGrowthBoost` | true | crops on planters grow as on moist farmland |
| `jarBoatShipwreckChance` | 0.25 | chance of a jar boat in a shipwreck treasure chest |

## 5. Commands

None.

## 6. Strings

Block / item names (RP `texts.d/<locale>/tableware.lang`, `minecraft:display_name` keys):

| Key | en_US | zh_CN |
|-----|-------|-------|
| `tile.complementaries:goblet.name` | Goblet | 高脚杯 |
| `tile.complementaries:urn.name` | Urn | 瓮 |
| `tile.complementaries:planter.name` | Planter | 花盆 |
| `tile.complementaries:flower_box.name` | Flower Box | 花箱 |
| `tile.complementaries:book_pile.name` | Book Pile | 书堆 |
| `tile.complementaries:book_pile_horizontal.name` | Book Pile | 书堆 |
| `tile.complementaries:pancake.name` | Pancake | 煎饼 |
| `tile.complementaries:jar_boat.name` | Jar Boat | 瓶中船 |
| `item.complementaries:pancake.name` | Pancake | 煎饼 |
| `item.complementaries:candy.name` | Candy | 糖果 |

Mod text (`assets/lang/<locale>/tableware.json`): `cm.tableware.name` (unit name only; the unit
sends no chat messages).

Log lines (for e2e): `tableware: goblet filled with <fluid>`, `tableware: goblet emptied`,
`tableware: drank <fluid> from a goblet`, `tableware: planted <flower> in a flower box`,
`tableware: placed a book pile`, `tableware: stacked a pancake (<n>)`, `tableware: ate a pancake`,
`tableware: urn released <mob>`.

## 7. Implementation notes (Bedrock)

- Custom blocks have no block entities: per-block data is in block states plus the world DB
  (goblet potion data, book NBT).
- Interactions use `PlayerInteractBlockEvent`; the event is cancelled and the world edit runs on the
  next server tick (a cancelled interaction rolls back block changes made during it).
- Contents are dropped from `BlockChangedEvent` (any removal: player, explosion, piston, command).
- Planter: hooks on `CropBlock::mayPlaceOn/canSurvive`, `FoliageBlock::mayPlaceOn/canSurvive`,
  `FlowerBlock::mayPlaceOn` and `CropUtils::getGrowthSpeed`.
- Book pile enchanting power: hook on `EnchantingContainerManagerModel::recalculateOptions` (sets the
  table position) and `EnchantUtils::getEnchantCosts` (adds the pile power to the bookcase count).
- Urn projectile break: hook on `ProjectileComponent::onHit`.
- Jar boat loot: hook on `LootTable::fill` for `chests/shipwrecktreasure.json`.

## 8. Not ported

| Feature | Reason |
|---------|--------|
| Pancake as a music disc (jukebox song) | the song is a Supplementaries audio asset (cannot be copied) and Bedrock records only accept the vanilla sound events |
| Syrup / chocolate toppings from items | the items come from other mods (Autumnity, Neapolitan); the states and textures exist |
| Exact potion colour in the goblet | Bedrock blocks are drawn from states only: potion colours are quantized to 10 buckets |
| Goblet comparator output | data-driven Bedrock blocks cannot provide a comparator signal |
| Any modded plant in the flower box / tall flowers | a block state holds at most 16 values: 15 vanilla small plants are supported |
| Enchanted-book glint on book piles | blocks have no glint material on Bedrock |
| Waterlogging | custom blocks cannot be waterlogged reliably on 1.26.51 without the experimental liquid component |
