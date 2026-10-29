# U14 `flag`: flags, buntings, awnings, dragon banner pattern

Behaviour reference: Supplementaries 4.0.2 (MC 1.21.1). Behaviour only; nothing is copied from the
Java code or assets. Colours below are the 16 dye colours in Java order:
`white orange magenta light_blue yellow lime pink gray light_gray cyan purple blue brown green red black`
(Java dye index 0..15; Bedrock `ItemColor` index = 15 - Java index).

## 1. Blocks and items

| Id | Kind | Recipe (Supplementaries) | Drops |
|----|------|--------------------------|-------|
| `complementaries:flag_<color>` (16) | block + item | shaped `WWW / WWW / S  ` (W = `<color>` wool, S = stick) → 1 | itself, keeping its banner patterns and custom name |
| `complementaries:awning` | block + item | shaped `FFF / S S` (F = `complementaries:flax`, S = stick) → 2 | itself (survives explosion) |
| `complementaries:awning_<color>` (16) | block + item | shaped `CCC / S S` (C = `<color>` carpet) → 2; shapeless `awning` + `<color>` dye → 1 | itself |
| `complementaries:bunting_<color>` (16) | ceiling block + item | shaped `TTT / WWW /  W ` (T = string, W = `<color>` wool) → 6; shapeless any bunting + `<color>` dye → 1 | itself |
| `complementaries:bunting_wall_<color>` (16) | wall block (no item) | – | `bunting_<color>` |
| `complementaries:dragon_banner_pattern` | item (stack 1) | shapeless paper + dragon head → 1 | – |

Supplementaries makes both the "natural" carpet recipe and the flax one available depending on
whether flax is enabled; Complementaries ships both (flax is enabled by default in Supplementaries).

### Flag (`FlagBlock`)
- States: horizontal `facing` (towards the player who placed it), waterlogged (not ported, §8).
- Shape: `4,0,4 → 12,16,12` (a pole 8 px wide, full height); no special collision.
- The cloth is 24 px long and 16 px tall, starts 1 px off the pole centre and extends away from the
  player who placed it (the opposite of `facing`). Both sides show the base colour plus up to 6
  banner pattern layers (flag-specific pattern textures, wider than tall; the banner's top edge
  lies along the pole).
- Flags are not pathfindable, are a valid respawn block, hardness 1 (wood sound).
- Animation (client config): the cloth is split into segments; segment at pixel `d` (0..23) turns
  `ang(d) = (intensity + intensity_increment * d) * sin(d / wavyness - 2π t)` degrees about the
  vertical axis, with `t = ((x*7 + z*13 + gameTime) mod slowness) / slowness`.
  Defaults: `slowness` 100, `wavyness` 4, `intensity` 1, `intensity_increment` 0.3.
- Empty main hand on a flag with `stick_pole` on: move the flag one block **up** along a vertical pole
  of placeable sticks (sneaking: **down**). The flag swaps places with the stick it moves into. Pole
  length is limited by `pole_length` (16).
- Using a map on a flag toggles a flag marker on that map (map-marker feature).
- Pick block keeps the patterns.

### Flag item (`FlagItem`)
- Tooltip lists the pattern layers like a banner ("Red Bordure", ...).
- Works in the **loom** like a banner (flag icon in the banner slot, live preview).
- Special recipe `flag_from_banner`: a patterned banner/flag + a blank flag/banner of the **same colour**
  → the blank one with the patterns; the patterned one stays in the grid. Banner → banner is excluded
  (vanilla duplication handles it). At most 6 layers.
- Held: rendered as a two-block-tall pole with the flag on top; both arms raise the pole.

### Awning (`AwningBlock`)
- States: horizontal `facing`, `bottom` (lower or upper half), `slanted`, waterlogged.
- Shapes: bottom `0,0,0 → 16,8,16` (collision `y 1..8`); top `0,8,0 → 16,16,16` (collision `y 9..16`).
- Placement: tries the player's nearest looking directions (horizontal only); `bottom` = not
  clicked on a ceiling and (clicked on top of a block, or clicked in the lower half of a side). If the
  block in front (or front-above when top) is a slanted awning facing back at us, the new one becomes
  slanted and snaps to that half. Facing = opposite of the tried direction; the first state that can
  survive wins, otherwise nothing is placed. Clicking an awning while looking in its outward
  direction continues the row (and the slant).
- Survives if: the block behind is solid; or the block behind is a flat awning, or a slanted awning
  when this one is bottom; or the block behind-above is a slanted awning; or a block to the left or
  right is solid. Otherwise it breaks (drops itself).
- Empty hand click (with `slant` on): toggles `slanted` (place sound, game event open/close).
- Entities falling on an awning faster than 0.32 b/t downward bounce: their velocity is reflected
  on the awning normal (straight up for flat ones; tilted by `90 - angle` degrees towards the
  front for slanted ones, `angle` = atan(16/6) ≈ 69.44°) and the bounce sound plays. No fall damage.
  Slower entities just stop. Tag `cant_bounce_off_awnings` and bounce-suppressing entities (sneaking)
  do not bounce.
- With `shift_through` on, a sneaking entity falls through awnings (collision only for entities above
  the shape that are not descending); awnings are then pathfindable.
- Adjacent flat awnings of the same half hide their shared faces.

### Buntings (`BuntingCeilingBlock`, `BuntingWallBlock`, `RopeBuntingBlock`)
- Item: standing-and-wall item; on a rope it attaches to the rope block (`rope_buntings`, up to one
  bunting per horizontal side of the rope, swinging in the wind).
- Ceiling bunting: placed under a block with a sturdy bottom face; state horizontal `axis` (player's
  horizontal look axis). Shape `0,0,7 → 16,16,9` (rotated for X). No collision. Breaks when the
  ceiling goes.
- Wall bunting: placed on a sturdy side face; state `facing` (away from the wall). Shape a 1 px plate
  against the wall. No collision. Breaks when the wall goes.
- Item hint tooltip: "Can be placed on ropes".

### Dragon banner pattern
- Item `dragon_banner_pattern` ("Banner Pattern", description "Dragon Charge") adds the pattern
  `supplementaries:dragon` ("<Colour> Dragon Charge") to looms.

## 2. Mechanics on Bedrock (Complementaries)

- **Flag cloth** is drawn by a helper entity `complementaries:flag_cloth` (no collision, no physics,
  invulnerable, saved with the chunk) standing in the flag block. Its synced entity properties carry
  the base colour, the facing and the 6 pattern layers (`pattern * 16 + colour`); the resource pack
  renders 7 layers with tinted pattern atlases and animates 8 cloth segments with the formula above
  (default client values; Molang). Works on vanilla clients (data driven).
- Flag data (patterns, custom name) lives in the world DB at `cm:flag:flag:<dim>:<x>:<y>:<z>`.
  The block drops a plain flag through its loot table; the mod puts the patterns/name back on the
  dropped item in the same tick. A flag placed by commands/structures gets a cloth with no patterns.
  A cloth whose block is gone despawns.
- Flag item patterns are stored like Bedrock banners (`Patterns: [{Pattern, Color}]` user data,
  colours as Bedrock `ItemColor`), with item lore listing the layers.
- **Loom** (vanilla loom cannot take a flag on Bedrock): using a loom while holding a flag opens the
  *Flag loom* form (all clients): pick a pattern (patterns that need a pattern item list it and need it
  in the inventory; the dragon pattern needs `dragon_banner_pattern`), then a dye colour (one dye of
  that colour is consumed, unless creative); at most 6 layers. "Copy from banner" lists patterned
  banners/flags of the same colour in the inventory and copies their patterns (the source is kept),
  which is the `flag_from_banner` recipe. "Remove last layer" mirrors the banner cauldron wash.
- **Held flag**: an attachable draws a two-block pole with the base-coloured flag.
- **Flag pole**: `complementaries:stick` blocks (sticks unit) stacked vertically form a pole.
- **Awnings**: placement, survival, slant toggle and bounce run in the mod. The bounce works on players
  and mobs; fall damage from a bounce is cancelled. Sneaking players on an awning drop through it
  (moved below its collision box when there is room).
- **Buntings**: the item places the ceiling block under a block and the wall block on a side face; both
  break when their support is removed.

## 3. Screens

| Screen | Java | Bedrock |
|--------|------|---------|
| Loom with a flag | vanilla loom, flag slot icon + flag preview | *Flag loom* form (SimpleForm pattern list → SimpleForm colour list), same on modded and vanilla clients |

Form texts: title "Flag Loom"; buttons "Copy patterns from a banner", "Remove last pattern",
pattern buttons by name, colour buttons "<Colour> (<n> dye)". Messages: "This flag already has 6 patterns",
"You need %s dye", "You need the %s banner pattern", "No banner with the same colour and patterns found".

## 4. Configuration (`config/flag.json`)

| Key | Default | Meaning |
|-----|---------|---------|
| `flagEnabled` | true | flags react (placement data, loom, pole) |
| `stickPole` | true | empty hand on a flag moves it along a stick pole |
| `poleLength` | 16 | maximum pole length |
| `awningEnabled` | true | awning placement/bounce logic |
| `awningSlant` | true | empty hand toggles slanted awnings |
| `awningShiftThrough` | true | sneaking drops through awnings |
| `awningBounceAngle` | 69.44 | bounce angle (degrees) of slanted awnings |
| `buntingsEnabled` | true | bunting placement logic |
| `dragonPattern` | true | dragon pattern available in the flag loom |

Client config keys `slowness`, `wavyness`, `intensity`, `intensity_increment`, `fanciness`,
`render_as_banner`, `fast_buntings` are baked into the resource pack (defaults).

## 5. Commands

None (Supplementaries has none for these features).

## 6. Strings

Block/item names (RP `texts.d/*/flag.lang`): "<Colour> Flag", "Awning", "<Colour> Awning",
"<Colour> Bunting", "Banner Pattern" (dragon). Mod strings (`assets/lang/*/flag.json`):
`message.supplementaries.bunting` "Can be placed on ropes",
`item.supplementaries.dragon_banner_pattern.desc` "Dragon Charge",
`cm.flag.color.<color>` colour names, `cm.flag.pattern.<code>` pattern names (Supplementaries/vanilla
wording, e.g. "Bordure", "Field Masoned", "Dragon Charge"), `cm.flag.layer` "{0} {1}",
`cm.flag.loom.*` form texts.

## 7. Selftests / e2e

`flag.*` selftests (dx 1400..1499): pattern codec round trip, flag place → cloth entity + DB,
break → drop carries patterns, loom apply/copy, pole move, awning placement/survival/slant/bounce
normal, bunting ceiling/wall placement and support loss. E2E: server scenario runs them and
gives/places blocks by command; client scenario screenshots flags, awnings and buntings.

## 8. Not ported

| Feature | Reason |
|---------|--------|
| Ceiling banners | Not in Supplementaries 4.0.2 (moved to the separate mod *Amendments*). |
| Rope buntings (`rope_buntings`) | Needs the rope unit's block and its connection states; custom blocks cannot hold a block entity to merge both. |
| Waterlogging of flags/awnings | Bedrock custom blocks cannot be waterlogged in 1.26 without experimental liquid detection. |
| Flag in the vanilla loom UI / `flag_from_banner` in the crafting grid | The loom and crafting screens are client side and only accept vanilla banners; replaced by the Flag loom form. |
| Dragon pattern on vanilla banners and shields | Bedrock banner patterns are a fixed engine list rendered by the client. Flags support it. |
| Patterns on the held flag | Attachables cannot read item data; the held flag shows its base colour. |
| Raised-arm pose while holding a flag | Needs a player animation override shared by every mod; vanilla arm pose is kept. |
| Flag map markers | Belongs to the map-marker feature; Bedrock maps have no custom decorations. |
| Configurable client wave values / fanciness / render as banner / fast buntings | Values are baked into the resource pack. |
| Awning faces culling between neighbours | Bedrock custom block geometry has no neighbour-aware culling. |
