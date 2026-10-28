# U8 `circuit`: redstone gadgets

Behaviour reference: Supplementaries 4.0.2 (MehVahdJukaar). Behaviour only; no code or assets were
copied. Where the exact Java value could not be confirmed it is marked *(reconstructed)*.

Unit owner files: `src/feature/circuit/`, `assets/pack/{bp,rp}/**/circuit*` and the per-id files listed
below. Selftest area: `ctx.testPos(800..899)`.

## 1. Blocks and items

| Id (`complementaries:`) | Kind | Block states | Drops | Notes |
| --- | --- | --- | --- | --- |
| `relayer` | block | `minecraft:facing_direction` (6, placement trait), `complementaries:powered` (bool) | itself | stone-like, hardness 3.0 (pickaxe), not a redstone conductor |
| `speaker_block` | block | `minecraft:cardinal_direction` (4, placement trait), `complementaries:powered` (bool) | itself | wood, hardness 1.0 (axe), flammable |
| `crystal_display` | block | `minecraft:cardinal_direction`, `complementaries:power` (0-15), `complementaries:chain` (0 alone, 1 tens, 2 units) | itself | glass-like frame, hardness 0.5 |
| `redstone_illuminator` | block | `complementaries:power` (0-15) | itself | light level = `power`; hardness 0.3, glass sounds |
| `dispenser_minecart` | item + entity | entity is a chest-type minecart with a 9-slot inventory | item: itself; breaking the cart drops a minecart, a dispenser and the contents | stacks to 1 |

Recipes *(reconstructed: the pattern is ours, the ingredients follow Supplementaries)*:

| Output | Recipe |
| --- | --- |
| `relayer` | `CCC`, `RQR`, `CCC` (C cobblestone, R redstone dust, Q quartz) |
| `speaker_block` | `PPP`, `PNP`, `PRP` (P planks, N note block, R redstone dust) |
| `crystal_display` | `SGS`, `ARA`, `SSS` (S smooth stone, G glass pane, A amethyst shard, R redstone dust) |
| `redstone_illuminator` (x2) | `GQG`, `QRQ`, `GQG` (G glowstone dust, Q quartz, R redstone dust) |
| `dispenser_minecart` | shapeless: dispenser + minecart |

## 2. Mechanics

### Relayer
- A directional block (all six directions, placed like a piston: the front points away from the
  player that places it).
- Input: the redstone signal coming **from the block behind it** (the back face only): the output of
  a redstone component standing there (wire, torch, repeater, comparator, lever, button, plate,
  observer, redstone block, another relayer facing into it) or the power of a powered solid block.
- When the input is above 0 the relayer is `powered` and emits a **strong** signal of 15 out of its
  front face (it powers the block in front, which in turn powers wires and components next to it,
  like a repeater would). There is **no delay**: the output changes in the same redstone update as
  the input.
- Side and front inputs are ignored. Relayers chain without delay.

### Speaker Block
- Right-click (use) opens the **speaker screen**. Sneaking with an item in hand places the item instead.
- The screen holds: the **message** (up to 256 characters), the **mode** (`Chat`, `Status message`,
  `Title`, `Narrator`) and the **volume** (1 .. `speakerRange`, default 16): players within
  `volume` blocks hear the message.
- On a rising redstone edge (unpowered -> powered) the speaker broadcasts its message:
  - `Chat`: a chat line `"<name>: <message>"` (the speaker's name in italics; `Speaker Block` unless
    the block item was renamed in an anvil before placing it);
  - `Status message`: shown above the hotbar (action bar);
  - `Title`: shown as a title in the middle of the screen;
  - `Narrator`: read aloud by the client narrator. If `speakerNarrator` is off, it falls back to chat.
- An empty message broadcasts nothing. Note particles appear above the block when it speaks.
- The message, mode, volume and name are kept per block (world data `cm:circuit:speaker:<dim>:<x>:<y>:<z>`)
  and removed when the block is broken.

### Crystal Display
- Shows the redstone power it receives (best signal of all six sides, 0-15) as a number on its
  front face, as a two-digit seven-segment read-out (a leading zero is blank).
- **Chaining:** two displays side by side facing the same way form a two-digit number from 00 to 99:
  the left one (as seen from the front) shows the tens and the right one the units; each shows its
  own power clamped to 9 as one big digit. Longer rows pair up from the left.
- Updates immediately when the power or a neighbour display changes.

### Redstone Illuminator
- Emits light equal to the redstone power it receives (0-15); its texture brightens with the power
  (off / dim / bright).

### Minecart with Dispenser
- Placed on a rail like any minecart. It holds a dispenser's 9 slots; use it (right-click) to open the
  inventory. Hoppers can fill / empty it.
- Every time it rolls onto a **powered activator rail** it fires once, like a dispenser: one random
  non-empty slot, using that item's dispense behaviour (arrows, fire charges, eggs, potions, buckets,
  bone meal, armour, ...; other items are dropped). It fires in its direction of travel, or along its
  facing when still. Firing an empty cart clicks.
- Broken by a player: drops a dispenser minecart item (creative: nothing) and its contents.

## 3. Screens

| Screen | Modded client | Vanilla client (BDS) |
| --- | --- | --- |
| Speaker screen | server form (custom form): title `Speaker Block`, text input "Message", dropdown "Mode", slider "Volume" | the same form (identical on both clients) |
| Dispenser minecart inventory | vanilla container screen (9 slots) | same |

## 4. Configuration (`config/circuit.json`)

| Key | Default | Meaning |
| --- | --- | --- |
| `version` | 1 | config version |
| `speakerRange` | 64 | maximum speaker volume (radius in blocks) |
| `speakerDefaultVolume` | 16 | volume of a newly placed speaker |
| `speakerNarrator` | true | allow the narrator mode (off: narrator messages go to chat) |
| `speakerMaxLength` | 256 | maximum message length |
| `dispenserMinecartEnabled` | true | the cart fires on activator rails |

## 5. Commands

None.

## 6. Strings

Mod text (`assets/lang/<locale>/circuit.json`):

| Key | English |
| --- | --- |
| `gui.supplementaries.speaker_block.edit` | Speaker Block |
| `gui.supplementaries.speaker_block.message` | Message |
| `gui.supplementaries.speaker_block.mode` | Mode |
| `gui.supplementaries.speaker_block.chat_message` | Chat |
| `gui.supplementaries.speaker_block.status_message` | Status message |
| `gui.supplementaries.speaker_block.title_message` | Title |
| `gui.supplementaries.speaker_block.narrator_message` | Narrator |
| `gui.supplementaries.speaker_block.volume` | Volume |
| `message.supplementaries.speaker` | {0}: {1} |
| `block.supplementaries.speaker_block` | Speaker Block |

Block / item / entity names (RP `texts.d/<locale>/circuit.lang`): Relayer, Speaker Block, Crystal Display,
Redstone Illuminator, Minecart with Dispenser.

## 7. Not ported / deviations

- **Narrator text-to-speech**: a server cannot drive the client's speech engine. Narrator messages are
  sent as chat lines, which Bedrock reads aloud when the player enables *Chat text-to-speech*.
- **Speaker screen**: drawn as a server form on every client (Bedrock has no data-driven way to draw a
  block screen; the form has the same fields as the Java screen).
- **Weak vs strong power**: Bedrock's circuit system does not separate weak and strong input power of a
  solid block, so a relayer behind a weakly powered block also turns on.
