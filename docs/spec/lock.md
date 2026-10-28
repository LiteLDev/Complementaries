# U3 `lock`: keys, lock block, safe, metal gates and doors

Behaviour reference: Supplementaries **4.0.2** (MC 1.21.1). This file describes behaviour only.
Unit `lock`, namespace `cm::feature::lock`, selftests `lock.*` (dx 300..399), world data `cm:lock:*`,
config section `config/lock.json`.

## 1. Content

| Id (`complementaries:`) | Kind | Supplementaries id | Recipe (Java, ported as is) |
|---|---|---|---|
| `key` | item, stack 64 | `supplementaries:key` | shaped `.G.`/`.n.`/`.n.`: gold ingot over two gold nuggets → 1 |
| `lock_block` | block + item | `supplementaries:lock_block` | shaped `IPI`/`PRP`/`IPI`: iron ingots, planks, redstone → 1 |
| `safe` | block + item, stack 1 | `supplementaries:safe` | shapeless: any shulker box + netherite ingot → 1 |
| `iron_gate` | block + item | `supplementaries:iron_gate` | shaped `...`/`nIn`/`nIn`: iron nuggets + iron ingots → 2 |
| `gold_gate` | block + item | `supplementaries:gold_gate` | shaped `...`/`nGn`/`nGn`: gold nuggets + gold ingots → 2 (Java: needs gold bars or Quark; ported unconditionally) |
| `gold_door` | block (2 high) + item | `supplementaries:gold_door` | shaped `GG`/`GG`/`GG` → 3 (group `iron_door`) |
| `gold_trapdoor` | block + item | `supplementaries:gold_trapdoor` | shaped `GG`/`GG` → 1 (group `iron_trapdoor`) |
| `netherite_door` | block (2 high) + item | `supplementaries:netherite_door` | shapeless gold door + netherite scrap → 1 |
| `netherite_trapdoor` | block + item | `supplementaries:netherite_trapdoor` | shapeless gold trapdoor + netherite scrap → 1 |

Block properties (copied from the vanilla block named in Java):

| Block | Like | Hardness / blast resistance | Tool | Other |
|---|---|---|---|---|
| lock_block | iron block, strength 5 | 5 / 6 | pickaxe | not a redstone conductor, metal sound |
| safe | netherite block | 50 / 1200 | pickaxe | immovable by pistons, 14×16×14 shape, waterloggable, no light |
| iron_gate, gold_gate | iron bars | 5 / 6 | pickaxe | metal sound, waterloggable |
| gold_door, gold_trapdoor | gold block | 3 / 6 | pickaxe | metal sound, no occlusion |
| netherite_door, netherite_trapdoor | netherite block | 50 / 1200 | pickaxe | netherite sound, items are fire resistant |

Drops: every block drops itself (doors only from the lower half; explosions follow vanilla
"survives explosion"). The safe drops itself **with its contents, owner, password and custom name**.

## 2. Keys and key-lockable blocks

A *key* is the `key` item (or any item in the `keys` tag). Its **password** is its custom name
(anvil rename) or the literal `Key` when unnamed. Key-lockable blocks: lock block, netherite door,
netherite trapdoor, safe. They store a password (none by default).

Using a key-lockable block (lock block, netherite door/trapdoor) with the item in hand:

1. Spectators: nothing.
2. **Sneaking with a key** in hand: if the player is creative or the held key matches the block's
   password, the password is cleared → action bar `Cleared ownership`, sound iron trapdoor open
   (vol 0.5, pitch 1.5); nothing else happens. A wrong key falls through to 4.
3. **No password yet**: holding a key assigns its password → action bar `Assigned to '<pw>' key`
   (or `Assigned to key` when the password is longer than 50 chars), same sound; the block is not
   used. Without a key the block is used normally (an unlocked netherite door opens like any door).
4. **Password set**: the player's inventory (hotbar, main, armor, offhand) is searched. Creative
   players always pass. A key with the right password → the block is used. Otherwise action bar
   `You do not have the correct key` when any (wrong) key is carried, or `This block is locked` /
   `This door is locked` / `This trapdoor is locked` / `This safe is locked` when no key is carried.

A dispenser facing a key-lockable block and dispensing a key uses the block as a fake player
holding that key (the key is not consumed).

## 3. Lock block

States: `powered` (bool). Using it (not while powered) runs §2; when it opens, the block becomes
powered for **20 ticks** (strength 15 on every side, weak power only), plays the iron trapdoor open
sound (vol 1, pitch 0.9–1.0) and emits `block_activate`; after 20 ticks it unpowers silently.
Item hint tooltip: `Key Lockable` (gray italic).

## 4. Safe

States: `facing` (horizontal, toward the placer), `open`, `waterlogged`, `lavalogged`.
27-slot container (shulker box menu, title `Safe`). Data: owner UUID + name, password, items.

- **Placing** sets the owner to the placer when the item has none. Item data restores owner,
  password, custom name and contents.
- **Using** (any item): spectators → nothing.
  - Sneaking with the right key (or creative) clears password and owner → `Cleared ownership`.
  - If the block in front of the safe's door is a redstone conductor (solid) nothing happens.
  - Key mode (default): no password + key in hand → assign (§2.3), not opened. Password set and
    the right key not carried (non-creative) → message (§2.4), not opened. Otherwise opens.
  - Simple mode (`simple_safes`): the first opener becomes the owner; others get `This safe is
    owned by <name>` and cannot open (creative still opens). Sneaking with a tripwire hook or a key
    as owner (or creative non-owner) clears ownership.
  - Opening angers nearby piglins (as a chest), plays iron trapdoor open (vol 0.5, pitch
    0.65–0.75) at the door and sets `open`; closing plays the close sound and clears `open`.
- **Title**: key mode with a password ≤ 50 chars → `Safe key: <pw>` (`%2$s key: %1$s`); simple
  mode with an owner → `<owner>'s Safe`; else `Safe`.
- **Contents rules**: like a shulker box (no shulker boxes, no safes) and the key that opens the
  safe cannot be put inside. Hoppers can neither insert nor extract.
- **Comparator**: 15 when the safe is locked (owner/password), 0 when public.
- **Breaking**: keeps contents in the dropped item (also in creative when not empty). With
  `prevent_breaking`, only players who could open it (or creative) can break it.
- **Item tooltip**: key mode `Locked` / `Unbound`; simple mode `This safe is owned by <name>` for
  non-owners; then up to 5 `<item> x<count>` lines and `and <n> more...`; hint `Key Lockable`.
- **Recipe**: shulker box + netherite ingot; the shulker's contents and name carry over.

## 5. Iron gate and gold gate

Fence-gate shaped metal gates. States: `facing`, `open`, `powered`, `in_wall`, `waterlogged`.

- Placement: facing = player's horizontal facing; `open = powered =` has neighbour signal;
  `in_wall` = solid face / gate / iron bars above, or iron bars on either side.
- Hand use: allowed when `door-like_gates` is false (default, both gates), or for a gold gate that
  is not powered. Opening turns the gate to face away from the player (closest of facing /
  opposite to the player's yaw); closing keeps the facing. Sound iron trapdoor open/close
  (vol 1, pitch 0.9–1.0), game events block_open/close.
- `double_opening` (default true): a gate directly above/below with the same state toggles too;
  a gate whose vertical neighbour gate (same facing) changes `open` follows it unless powered.
- Redstone: `powered` tracks the neighbour signal; on a change iron gates (and gold gates unless
  `door-like_gates`) set `open = powered` with the sound.
- Shapes: closed collision 16×24×4 along the gate (like a fence gate), open: no collision;
  outline always 16×16×4. `in_wall` lowers the model by 3 px.
- Iron bars next to a gate connect to it (Java: bars' state). 

## 6. Gold door and gold trapdoor

Vanilla door / trapdoor behaviour with the gold block-set (iron door sounds, metal):
opened **by hand only while not powered**; redstone never opens or closes them, it only sets
`powered`, which **locks** them in their current state. Placement starts closed (`powered` from the
current signal for the trapdoor). The trapdoor is waterloggable and never a mob spawn.
Doors: two halves, hinge from neighbours (vanilla rules), both halves share open/powered.
(Double doors open together only with Quark / Double Doors installed.)

## 7. Netherite door and netherite trapdoor

Iron block-set door/trapdoor with netherite sounds; **redstone is ignored**; opened only through
the key logic of §2 (`door` / `trapdoor` messages; no password = opens by hand). Sound iron door /
iron trapdoor open/close, game events block_open/close. The door's lock data lives in its lower
half. Config `unbreakable`: only players carrying the right key (or creative) can break them.
The trapdoor is lava- and waterloggable (light 15 when lavalogged).

## 8. Screens

| Screen | Java | Complementaries |
|---|---|---|
| Safe | Shulker box menu (3×9 + player inventory), title per §4 | The vanilla 27-slot container screen of a hidden storage entity bound to the safe (same layout); works on modded and vanilla clients alike. |

## 9. Configuration (`config/lock.json`)

| Key | Default | Java key | Meaning |
|---|---|---|---|
| `enableKey`, `enableLockBlock`, `enableSafe`, `enableIronGate`, `enableGoldDoor`, `enableGoldTrapdoor`, `enableNetheriteDoor`, `enableNetheriteTrapdoor` | true | feature flags | disable the mechanics (blocks still load) |
| `safePreventBreaking` | false | `safe.prevent_breaking` | safes only breakable by who can open them |
| `simpleSafes` | false | `safe.simple_safes` | owner-bound safes without keys |
| `gateDoubleOpening` | true | `iron_gate.double_opening` | stacked gates open together |
| `doorLikeGates` | false | `iron_gate.door-like_gates` | iron gates redstone-only, gold gates locked by redstone |
| `netheriteUnbreakable` | false | `netherite_doors.unbreakable` | netherite doors/trapdoors need the key to break |

## 10. Commands

None.

## 11. Strings

| Key | English | 简体中文 |
|---|---|---|
| `message.supplementaries.key.lockable` | Key Lockable | 钥匙可锁 |
| `message.supplementaries.lock_block.locked` | This block is locked | 方块已上锁 |
| `message.supplementaries.door.locked` | This door is locked | 门已上锁 |
| `message.supplementaries.trapdoor.locked` | This trapdoor is locked | 活板门已上锁 |
| `message.supplementaries.safe.locked` | This safe is locked | 保险箱已上锁 |
| `message.supplementaries.safe.assigned_key` | Assigned to '{}' key | 已配对“{}”钥匙 |
| `message.supplementaries.safe.assigned_key_generic` | Assigned to key | 已配对钥匙 |
| `message.supplementaries.safe.bound` | Locked | 已上锁 |
| `message.supplementaries.safe.unbound` | Unbound | 未绑定 |
| `message.supplementaries.safe.cleared` | Cleared ownership | 已清除所有权 |
| `message.supplementaries.safe.incorrect_key` | You do not have the correct key | 未持有正确的钥匙 |
| `message.supplementaries.safe.owner` | This safe is owned by {} | 该保险箱为{}所有 |
| `gui.supplementaries.safe` | Safe | 保险箱 |
| `gui.supplementaries.safe.name` | {0}'s {1} | {0}的{1} |
| `gui.supplementaries.safe.password` | {1} key: {0} | {1}的钥匙：{0} |
| `cm.lock.safe.item_count` | {} x{} | {} x{} |
| `cm.lock.safe.more` | and {} more... | 还有{}项…… |

Block / item names (RP `texts.d`): Key, Lock Block, Safe, Iron Gate, Gold Gate, Gold Door,
Gold Trapdoor, Netherite Door, Netherite Trapdoor (钥匙, 锁方块, 保险箱, 铁栏杆门, 金栏杆门, 金门,
金活板门, 下界合金门, 下界合金活板门).

## 12. Bedrock implementation notes

- Custom blocks cannot have block entities (container block entities need experiments), so:
  passwords and safe owners live in the world DB (`cm:lock:lock:<dim>:<x>:<y>:<z>`,
  `cm:lock:safe:<dim>:<x>:<y>:<z>`), safe contents in a hidden `complementaries:safe_storage`
  entity (27-slot inventory, not siphonable by hoppers) inside the block, opened with the
  vanilla container screen.
- Redstone output of the lock block: `minecraft:redstone_producer` (power 15 in the powered
  permutation). Redstone input of gates and gold doors: `minecraft:redstone_consumer`, whose
  strength the mod reads from the dimension's circuit graph every tick.
- Item data of a safe (owner, password, contents) is kept in the item's user NBT, and its tooltip
  lines are written as item lore so vanilla clients see them.

## 13. Not ported

| Feature | Reason |
|---|---|
| Lavalogged safe / netherite trapdoor | Bedrock blocks cannot hold lava. |
| Fire-resistant netherite door / trapdoor items | No data-driven item component for it. |
| Double doors opening together | Java needs Quark / Double Doors; not part of standalone Supplementaries. |
| Iron bars visually connecting to gates | Bedrock decides bar connections in the renderer from vanilla rules. |
| Keys in Curios / Trinkets / key-ring slots | No such slots on Bedrock. |
| Key in a dispenser | Bedrock dispensers have no per-item behaviour hook for data-driven items. |
| Piglins angered by opening a safe | Bedrock has no "container opened" anger hook for non-vanilla containers. |
| Safe recipe keeping the shulker box's contents | Bedrock recipes cannot copy item data; the shulker's contents are dropped back to the crafter instead (see §4). |
