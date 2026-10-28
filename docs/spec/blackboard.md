# U5 Blackboard

Behaviour of the Supplementaries 4.0.2 blackboard and how Complementaries reproduces it on Bedrock.
Behaviour reference only: no code or assets are copied.

## Blocks and items

| Id | What |
| -- | ---- |
| `complementaries:blackboard` | The blackboard block and its block item (creative tab: building / decoration). |
| `complementaries:blackboard_display` | Complementaries-only helper entity that renders a drawing (two per drawn board, see *Rendering*). Not spawnable, not summonable by players, invisible to interaction. |

**Recipe** (shaped, gives 2):

```
S B
S B W
S B
```

`S` any wooden slab, `B` blackstone, `W` an item of the `blackboard_white` group (white dye).
Bedrock has no item-tag ingredient for "wooden slabs" across all wood types in a single
recipe slot, so the recipe is registered once per vanilla wooden slab (oak, spruce, birch, jungle,
acacia, dark oak, mangrove, cherry, pale oak, bamboo, crimson, warped).

**Soap recipe** (shapeless): blackboard + `complementaries:soap` -> blank blackboard. Owned by
the soap unit (it defines the soap item); this unit only washes placed boards with soap.

**Duplicate recipe** (special): one drawn blackboard + one blank blackboard -> two copies of the
drawing (the drawn one stays in the grid). See *Not ported*.

**Block states**

| State | Values | Meaning |
| ----- | ------ | ------- |
| `minecraft:cardinal_direction` | north, south, east, west | The side the drawing faces (Java `facing`), towards the player who placed it. |
| waterlogged | via `minecraft:liquid_detection` | The block can hold water like Java's `waterlogged`. |

Shape: a 16x16x5 px board on the back side of the block (Java `box(0,0,11,16,16,16)` for north);
collision and selection use the same box. Not pathfindable. Cannot be pushed by pistons (Java: has
a block entity). Strength 1.5 (wood/stone-like), any tool.

**Per-board data** (Java block entity `BlackboardData`): 16x16 pixel bytes, `glow` (bool),
`waxed` (bool). Empty = all pixels 0, not glowing, not waxed.

Pixel values (Java convention, kept for compatibility): `0` blank (also what black draws),
`1` white, `15` orange, and every other dye its dye id (2 magenta, 3 light blue, 4 yellow, 5 lime,
6 pink, 7 gray, 8 light gray, 9 cyan, 10 purple, 11 blue, 12 brown, 13 green, 14 red). Colours
are the dye map colours; 0 and 1 render white chalk / nothing.

**Drops**: the block drops itself; the item keeps the board's data (pixels, glow, waxed) when the
board is not empty (Java loot `copy_components`). Pick block (Ctrl + pick in creative) also keeps
the data.

**Item**: a blackboard item with data places a board with that data. Tooltip: "Waxed" and
"Glowing" lines (gray) when set, plus a picture of the drawing (Java tooltip image).

## Mechanics

Using an item on a placed blackboard (not sneaking with an item in hand; Java "secondary use"):

1. **Honeycomb** (any face): if already waxed -> sign "waxed interact fail" sound, nothing else.
   Otherwise consume one honeycomb (not in creative), honeycomb wax-on sound, wax particles,
   `waxed = true`. A waxed board cannot be drawn on or edited.
2. **Glow ink sac** (any face): if already glowing -> fail sound. Otherwise consume one, ink sac
   sound, glow particles, `glow = true`: the drawing renders at full brightness.
3. **Brush** (any face): passes to the brushing logic: a non-waxed, non-empty board is cleared,
   with the brush sound.
4. **Soap** (`complementaries:soap`, any face): washes one layer: removes wax, else removes glow,
   else clears the drawing (Java `IWashable`).
5. **Front face, `interaction_mode` allows manual drawing, not waxed, chalk item in hand**: the
   pixel under the cursor is set to the chalk colour; when it changes, the "Blackboard drawn on"
   sound plays. Chalk items (Java item tags `supplementaries:blackboard_<colour>`):
   - always: white dye (`blackboard_white`) draws white; black dye, coal and charcoal
     (`blackboard_black`) erase (pixel 0);
   - with `colored_blackboard`: every other dye draws its colour.
6. Otherwise, if `interaction_mode` allows the GUI and the board is not waxed, the **edit screen**
   opens (any item, any face). One player at a time can edit a board.

Holding use on the board keeps drawing (Bedrock repeats the use action while the button is held).

Pixel under the cursor: the hit point on the front face, 16x16 grid, x = column from the viewer's
left, y = row from the top.

## Edit screen ("Edit Blackboard")

Java layout: title "Edit Blackboard"; the board as a grid of clickable pixels (up to 6 screen px
each); buttons **Clear**, **Done**, **Undo** below; with `colored_blackboard` a 2x8 palette of the
16 dye colours (black = blank) on the left. Left click on a pixel sets it to the selected colour,
or back to blank if it already has it; dragging paints the value set by the first click. Undo
reverts the last stroke (20 steps). Changes are sent to the server when the screen closes (Done,
Esc); the server checks the editor may still edit the board (in reach, not waxed, colours allowed),
then plays the "draw complete" sound if the drawing changed.

**Connected boards** (`connected_gui`): the screen shows up to 3x3 neighbouring blackboards that
face the same way, are not waxed and are connected (4-neighbourhood, same plane) to the clicked one,
as one big canvas; the 3x3 window with the most boards is chosen.

### Complementaries

- **Modded client** (Complementaries client mod present, local world or remote server): a native
  screen drawn by the client mod with the Java layout and controls (pixel grid, Clear / Done /
  Undo, palette). Left click / drag draws, right click / drag erases. The result is sent with the
  `/supplementaries blackboard set` sub-command on close.
- **Vanilla client** (BDS, no client mod): a server form (data-driven custom form) titled "Edit
  Blackboard": a live preview of the canvas (coloured block characters) with a cursor, sliders for
  the cursor column / row, a colour dropdown (with `colored_blackboard`), and buttons **Draw**
  (toggle the pixel like a Java click), **Erase**, **Clear**, **Undo**, **Done**. Changes apply when
  the form closes, as in Java.

## Rendering

Java renders the drawing as a dynamic texture on the block. Bedrock custom blocks have no block
entities or dynamic textures, so each drawn board gets two `complementaries:blackboard_display`
helper entities (top and bottom 8 rows). Their entity properties hold the pixels (6 pixels of
4 bits per property), the facing, the glow flag and a mask of the colours in use; resource-pack
geometry has one quad per pixel and one render controller per colour, whose part visibility shows
the pixels of that colour. This renders identically on vanilla and modded clients. Glowing boards
use unlit render controllers. Blank boards have no helper entities.

The server keeps the helpers in sync: spawned when a board gets a drawing, updated on every change,
removed when the board is cleared or broken, and re-created if they go missing (e.g. `/kill`).

## Configuration (`config/blackboard.json`)

| Key | Default | Java |
| --- | ------- | ---- |
| `enabled` | `true` | `blackboard.enabled`: when false, using the board does nothing special (it stays a decorative block). |
| `colored_blackboard` | `false` | "Enable to draw directly on a blackboard using any dye. Gui still only works in black and white" |
| `interaction_mode` | `"both"` | "Interaction mode for blackboards": `both`, `gui` (screen only), `manual` (direct drawing only) |
| `connected_gui` | `true` | "Opening a blackboard gui also shows up to a 3x3 of connected blackboards so you can draw on all of them at once" |

## Commands

`/supplementaries blackboard set <x> <y> <z> <pixels>` (any player): the channel the client
screen uses to submit a drawing (256 hex digits, one per pixel, row-major from the top-left).
Accepted only from the player currently editing that board. Not meant to be typed.

## Strings

| Key | English |
| --- | ------- |
| `gui.supplementaries.blackboard.edit` | Edit Blackboard |
| `gui.supplementaries.blackboard.clear` | Clear |
| `gui.supplementaries.blackboard.undo` | Undo |
| `gui.done` (fallback) | Done |
| `message.supplementaries.blackboard` | Waxed |
| `message.supplementaries.glowing` | Glowing |
| `subtitles.supplementaries.blackboard.draw` | Blackboard drawn on |
| `cm.blackboard.draw` | Draw |
| `cm.blackboard.erase` | Erase |
| `cm.blackboard.column` | Column |
| `cm.blackboard.row` | Row |
| `cm.blackboard.color` | Colour |
| `cm.blackboard.busy` | Someone else is editing this blackboard |
| `color.minecraft.<dye>` | the 16 dye colour names |

Block name: `tile.complementaries:blackboard.name` = Blackboard (RP `texts`).

## Not ported

- **Duplicate recipe** (drawn + blank -> two drawn): Bedrock crafting recipes cannot match on or
  copy item data; the crafting result is computed by the game from data-driven recipes. Copy a
  drawing by placing both boards and using the connected edit screen instead.
- **Create display target / Inspirations cauldron recipe**: integrations with Java-only mods.
- **Tooltip image size option** (`tweaks.tooltip_image_size`): Bedrock tooltips are text; the
  preview is drawn with coloured block characters at the font size.
