# U4 `sign`: sign posts, notice board, doormat

Behaviour reference: Supplementaries 4.0.2 (MehVahdJukaar). This file describes behaviour only; no
code or assets come from Supplementaries. Unit number 4: selftests use `dx` 400..499.

## 1. Content

| Supplementaries | Complementaries id | Kind |
| --- | --- | --- |
| `sign_post_<wood>` items (one per wood type) | `complementaries:sign_post_<wood>` | item |
| `sign_post` block (block entity, mimics the fence it was put on) | the original post block stays; `complementaries:sign_post` display entity | entity |
| `notice_board` | `complementaries:notice_board` | block + item |
| `doormat` | `complementaries:doormat` | block + item |
| (text of notice boards / doormats, rendered by the block entity renderer) | `complementaries:text_display` display entity | entity |

Woods (`<wood>`): `oak`, `spruce`, `birch`, `jungle`, `acacia`, `dark_oak`, `mangrove`, `cherry`,
`bamboo`, `crimson`, `warped`, `pale_oak` (every vanilla wood family with planks).

### Recipes

| Result | Recipe |
| --- | --- |
| 2 `sign_post_<wood>` | shaped `WW` / `WW` / ` S` (W = `<wood>` planks, S = stick) |
| 1 `notice_board` | shaped `WWW` / `WPW` / `WWW` (W = any planks, P = paper) |
| 1 `doormat` | shaped `HH` (H = wheat) |

Recipes unlock in the recipe book when the player picks up the ingredients (Bedrock `unlock`).

### Drops

- Sign post arms drop their `sign_post_<wood>` item when the post block is removed in any way
  (mined, exploded, replaced, pushed by a piston). The post block drops its own loot as usual.
- Notice board: drops itself and the held item.
- Doormat: drops itself.

## 2. Sign post

### Data per arm (Java `SignPostBlockTile.Sign`)

| Field | Values |
| --- | --- |
| active | the arm exists |
| wood | wood type of the arm (its texture and drop) |
| yaw | direction the arm points to, degrees (0 = south, 90 = west, 180 = north, 270 = east; any value) |
| left | arrow points to the left of the board (mirrored arm) |
| text | one line, at most 90 font pixels wide |
| color | dye colour of the text (default black) |
| glowing | glow ink applied |

Plus `waxed` for the whole post. A post holds up to two arms: **up** (upper half) and **down**
(lower half). Stored in the world DB under `cm:sign:post:<dim>:<x>:<y>:<z>`.

### Posts

Valid posts: every vanilla fence and wall (`*_fence`, `*_wall`, `nether_brick_fence`). Unlike
Java, the post block itself is not replaced by a mimic block: it stays the real block (neighbour
connections, waterlogging, sounds and drops behave exactly as the original block), and the arms are
drawn by a display entity standing on the post.

### Interactions (right click on the post)

The arm is chosen by the clicked height: upper half = up, lower half = down; if that arm is empty the
other one is used.

| Held item | Effect |
| --- | --- |
| `sign_post_<wood>` on a post without that arm | adds an arm of that wood on the clicked half (the other half if taken), pointing along the player's view rotated to the nearest 22.5°; `left` when the player clicked the left part of the post. Consumes 1 item (not in creative), wood place sound. Opens the text editor right away (Java opens the screen after placing). |
| compass | the arm points to the world spawn (overworld); in other dimensions nothing happens (the needle spins). |
| lodestone compass | the arm points to the linked lodestone (same dimension only). |
| recovery compass | the arm points to the player's last death position (same dimension only). |
| dye | text colour (dye consumed, `sign.dye.use` sound), only when the arm has text and the colour differs. |
| glow ink sac | text glows (consumed, `sign.glow_ink_sac.use`). |
| ink sac | removes the glow (consumed, `sign.ink_sac.use`). |
| honeycomb | waxes the post (consumed, `sign.waxed`), then it cannot be edited, dyed or flipped. |
| axe on a waxed post | removes the wax (axe damaged by 1). |
| empty hand, sneaking | flips the arm: mirrors it (`left` toggled) and turns it by 180° so it points the other way. |
| empty hand | opens the text editor. |

A waxed post ignores the interactions above except the axe and plays the "waxed sign" sound.

### Text editor (Java `SignPostScreen`)

Java: a screen that renders the two arms large and edits one line per active arm (Up/Down
arrows or Tab switch lines; Enter / Done closes). Text wider than 90 font pixels is not accepted.

Bedrock (every client): a form titled "Edit Sign Message" with one input per active arm, prefilled
with the current text. Text is cut to the 90 px width limit (vanilla font metrics) and formatting
codes (`§`) are removed. Closing the form without submitting keeps the old text.

### Compass pointing

`yaw = atan2(-dx, dz)` towards the target block centre (Java `Sign.pointToward`), stored with full
precision; the arm renders at that exact angle.

### Way signs (world generation)

Java generates road signs near villages: a post with two arms pointing to nearby villages /
structures, with the structure name and (config) the distance as text.

Complementaries: while players explore the overworld, every 128×128 block cell is considered once
(cells whose centre is 48..96 blocks from a player and loaded). With probability `waySignChance`
the nearest village is looked up; if it is between `waySignMinDistance` and `waySignMaxDistance`
blocks away, a spruce fence is placed on the surface (grass / dirt / sand / gravel / path, solid
ground, open air above) near the cell centre and a sign post is put on it:

- up arm (spruce): points to the village, text `Village` (+ ` 123m` when `waySignDistanceText`),
- down arm (oak): points to the nearest other structure in range (pillager outpost, trail ruins,
  trial chambers; woodland mansions are not searched because that search can stall the server),
  text with its name, if any.

Processed cells are stored under `cm:sign:wg:<cx>:<cz>` so each cell is tried only once.

## 3. Notice board

Block `complementaries:notice_board`, states `complementaries:facing` (0 S, 1 W, 2 N, 3 E: the
direction the front faces, towards the player who placed it), `complementaries:has_book` (front
shows paper) and `complementaries:powered`. Item data in the world DB under
`cm:sign:board:<dim>:<x>:<y>:<z>` (`item` NBT, `page`, `color`, `glowing`).

| Action (on the front face) | Effect |
| --- | --- |
| valid item, board empty | puts one item on the board (`item_frame.add_item` sound). |
| empty hand, sneaking, item on board | takes the item back (`item_frame.remove_item`). |
| empty hand, multi-page book on board | turns to the next page (wraps around; `item.book.page_turn`). |
| empty hand, other item | takes the item back. |
| dye / glow ink sac / ink sac | text colour / glow / unglow, as on sign posts. |

Valid items: writable book, written book, (filled / empty) map, banners; any item when
`noticeBoardAcceptsAnyItem` is on.

Display (Java renderer): books show the current page, wrapped to the board width and scaled down to
fit; other items show their name. Redstone: a rising edge (unpowered → powered) turns the page.
Power is read from the six neighbours (redstone block / torch, powered repeater or comparator,
dust / plates / detectors with a signal, lever, button, observer). Comparator output (Java): 0
empty; book: current page number (1-based, max 15); other items: 15 — see "Not ported".

## 4. Doormat

Block `complementaries:doormat` (1 px high, full width, `complementaries:facing` set on placement so
the text reads from the placing player). 3 lines of text, each at most 75 font pixels; data under
`cm:sign:mat:<dim>:<x>:<y>:<z>` (`lines`, `color`, `glowing`, `waxed`).

| Action | Effect |
| --- | --- |
| empty hand | opens the editor ("Edit Doormat Message", 3 inputs). |
| dye / glow ink sac / ink sac / honeycomb | as on sign posts. |

## 5. Rendering

- **Sign post arms**: the `complementaries:sign_post` entity (geometry + animation + render
  controllers, data in the entity's synched variant fields) draws each arm with its wood texture,
  exact yaw and mirroring; works on every client.
- **Text** (arms, notice board page, doormat) is carried in the display entity's name tag:
  - modded client (`src/client/sign/`): the name tag is replaced by flat text on both faces of the
    arm / on the board face / on the mat, in the dye colour, depth tested, with a dark outline when
    glowing;
  - vanilla client (BDS fallback): the text shows as a coloured floating name tag above the post,
    board or mat (closest `§` colour of the dye).

## 6. Configuration (`config/sign.json`)

| Key | Default | Meaning |
| --- | --- | --- |
| `signPostEnabled` | true | sign posts can be placed |
| `noticeBoardEnabled` | true | notice boards work |
| `noticeBoardAcceptsAnyItem` | false | any item can be put on a notice board |
| `doormatEnabled` | true | doormats work |
| `waySigns` | true | generate way signs |
| `waySignChance` | 0.25 | chance per 128×128 cell |
| `waySignMinDistance` | 32 | min distance to the village |
| `waySignMaxDistance` | 256 | max distance to the village |
| `waySignDistanceText` | true | add the distance (`123m`) to way sign text |

## 7. Commands

- `/supplementaries waysign` (operator, player): builds a way sign at the player's position pointing
  to the nearest village (for testing and map makers). Prints `No village nearby` when none.

## 8. Strings

Mod text (`assets/lang/<locale>/sign.json`):

| Key | English |
| --- | --- |
| `cm.sign.edit` | Edit Sign Message |
| `cm.sign.edit.up` | Upper sign |
| `cm.sign.edit.down` | Lower sign |
| `cm.sign.doormat.edit` | Edit Doormat Message |
| `cm.sign.line` | Line {} |
| `cm.sign.waxed` | This sign is waxed |
| `cm.sign.way_sign.village` | Village |
| `cm.sign.way_sign.pillager_outpost` | Pillager Outpost |
| `cm.sign.way_sign.trail_ruins` | Trail Ruins |
| `cm.sign.way_sign.trial_chambers` | Trial Chambers |
| `cm.sign.way_sign.distance` | {} {}m |
| `cm.sign.way_sign.none` | No village nearby |
| `cm.sign.way_sign.placed` | Way sign placed at {} |

Block / item names (`texts.d`): `Oak Sign Post` … `Pale Oak Sign Post`, `Notice Board`, `Doormat`.

## 9. Not ported / differences

| Java | Bedrock | Reason |
| --- | --- | --- |
| Sign post mimics the fence (a separate block with a block entity) | the real post block stays, arms are an entity | custom blocks have no block entities and cannot copy another block's model; keeping the real block is the faithful result |
| Sign post / doormat screens render the sign in 3D while typing | form with one input per line | no native text-entry screen for custom blocks; forms work on every client |
| Notice board 1-slot container screen | direct insert / take / page turn on the front face | no custom container for custom blocks without a block entity |
| Maps and banners drawn on the notice board | their name is shown | drawing map / banner textures on a block face needs a block entity renderer |
| Text lighting follows the world light (dark at night unless glowing) | text is drawn full bright | Bedrock name tag text has no light input |
| Way signs as a jigsaw structure placed during chunk generation at village road ends | placed on the surface near villages once a player explores the area | Bedrock has no data-driven way to add jigsaw pieces to vanilla villages |
| Modded wood types | only vanilla woods | Bedrock has no runtime wood type registry |
| Comparators read the notice board page | the value is computed (`boardSignal`, selftested) but comparators do not read it | data-driven blocks cannot be comparator sources, and the engine's `hasComparatorSignal` body is shared (folded) with unrelated functions, so it cannot be hooked safely |
