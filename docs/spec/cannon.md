# U15 `cannon`: cannon, cannonballs, bombs and the red merchant

Behaviour reference: Supplementaries 4.0.2 (MehVahdJukaar). This file describes behaviour only;
no code or assets come from Supplementaries. Unit number 15: selftests use `dx` 1500..1599.

## 1. Content

| Supplementaries id | Complementaries id | Kind |
|---|---|---|
| `supplementaries:cannon` | `complementaries:cannon` | block + block item |
| `supplementaries:cannonball` | `complementaries:cannonball` | item + projectile entity |
| `supplementaries:bomb` | `complementaries:bomb` | throwable item + projectile entity |
| `supplementaries:bomb_blue` | `complementaries:bomb_blue` | throwable item + projectile entity |
| `supplementaries:bomb_spiky` | `complementaries:bomb_spiky` | throwable item + projectile entity |
| `supplementaries:red_merchant` | `complementaries:red_merchant` | trader entity (+ spawn egg) |
| — | `complementaries:cannon_barrel` | helper entity that draws the barrel of a cannon block (Bedrock blocks cannot have block-entity renderers) |

Supplementaries uses one bomb entity with a type field; Bedrock has one entity per bomb type, so
the client picks the right sprite without a custom renderer.

### Recipes

The shapes follow the Supplementaries recipes as closely as they are known; ingredients use vanilla
items only.

| Result | Recipe |
|---|---|
| Cannon | shaped `" I "`, `"IBI"`, `"P P"`... see `assets/pack/bp/recipes/cannon.json`: iron ingots around an iron block over a polished blackstone base (one cannon) |
| Cannonball ×4 | shaped `" N "`, `"NIN"`, `" N "` (iron nuggets around an iron ingot) — 4 cannonballs |
| Bomb ×2 | shaped `" N "`, `"NGN"`, `" N "`? — iron nuggets around gunpowder with a string fuse (`" S "`, `"NGN"`, `" N "`) |
| Blue bomb | shapeless: bomb + soul sand/soil (blue flames) + gunpowder |
| Spiky bomb | shapeless: bomb + 2 iron nuggets + pointed dripstone... simplified to bomb + 4 iron nuggets |

(The exact grids live in the recipe JSON files and are listed in the PR body.)

## 2. Cannon block

- **Placement**: placed on any face, like a dispenser. The base plate sits on the clicked face
  (block state `minecraft:block_face`, Bedrock placement trait). The barrel aims anywhere in the
  hemisphere away from the base.
- **States**: `minecraft:block_face` (`down`/`up`/`north`/`south`/`east`/`west`; the base lies on
  the opposite face: `up` = standing on the floor).
- **Drops**: itself (loot table), plus its contents (gunpowder and the loaded projectile) when it
  is broken by any means.
- **Hardness** 3.5 (pickaxe), blast resistance 6, metal sounds.
- **Inventory** (2 slots, stored in the world DB under `cm:cannon:c:<dim>:<x>:<y>:<z>`, since
  Bedrock custom blocks have no block entities):
  - *fuel*: gunpowder only, up to 64;
  - *projectile*: any one item stack (max stack size of that item).
- **Aim**: `yaw` (−180..180°) and `pitch` (−20..90°, elevation above the base plane) relative to the
  base. A floor cannon: yaw 0 = south, 90 = west (Minecraft yaw), pitch 90 = straight up.
- **Power level** 1..4: gunpowder used per shot, and the projectile speed scales with it
  (`speed = power × config.speedPerPower`).

### Interaction

| Action | Result |
|---|---|
| Use with gunpowder (fuel not full) | Inserts the stack into the fuel slot ("Cannon loaded"). |
| Use with any other item while the projectile slot is empty or holds the same item | Inserts it as the projectile. |
| Use with an empty hand / an item that does not fit | Opens the cannon screen (see §5). |
| Sneak-use with an item | Vanilla behaviour (place the block against the cannon). |
| Redstone: rising edge of a signal | Fires (if loaded and not on cooldown). |
| Break | Drops the cannon and its contents; the barrel entity is removed. |

### Firing

1. The shot needs `fuel >= power` and a projectile, and the cannon must not be cooling down.
   Otherwise the player gets "The cannon is not loaded" / redstone does nothing.
2. Ignition: fuse sound (`random.fuse`), smoke at the touch hole, then after
   `fuseTicks` (default 10) the cannon fires: `power` gunpowder and one projectile are used, the
   fire sound plays (`complementaries.cannon.fire`: a deep explosion), a burst of smoke and flame
   particles comes out of the muzzle, and the barrel recoils (entity animation).
3. Cooldown `cooldownTicks` (default 30) before the next shot.
4. Projectile behaviour by item:
   - cannonball → cannonball entity (§3);
   - bomb / blue bomb / spiky bomb → the matching bomb entity;
   - arrow, spectral arrow, tipped arrow → arrow entity; snowball, egg, ender pearl, fire charge
     (small fireball), wind charge, splash/lingering potion, experience bottle, trident → their vanilla
     projectile entities;
   - TNT → primed TNT (fuse 80 ticks) launched with the shot velocity;
   - firework rocket → a launched firework;
   - any other item → the item is shot as an item entity.
5. Recoil: entities standing on/next to the cannon are not moved (Supplementaries only moves the
   cannon boat); the barrel plays its recoil animation.

### Manoeuvre (manual aiming)

From the cannon screen the player can "Manoeuvre" the cannon:
- The barrel follows the player's view direction every tick (clamped to the barrel limits).
- A dotted trajectory preview (particles visible only to that player) shows the predicted path up
  to the first block hit, and the hit point is marked.
- Attack/swing (left click) fires; scrolling the hotbar changes the power level (+1/−1, wraps);
  sneaking, walking more than 6 blocks away, or the cannon disappearing exits.
- The action bar shows `Power: N  |  Left click: fire  |  Scroll: power  |  Sneak: exit`.

Supplementaries moves the camera into the cannon for this; Bedrock gives no camera control to a
mod for vanilla clients, so the player keeps their own camera and aims by looking (the preview is
drawn from the cannon's muzzle).

## 3. Cannonball

- Fired from a cannon (players cannot throw it). Gravity 0.05 b/t², drag 0.99.
- Hitting a block: if the block's destroy time is at most
  `cannonballBreakHardness × (speed / 2)` (and not unbreakable, and `cannonballBreaksBlocks` is on),
  the block is destroyed (with drops) and the ball continues with its speed reduced by the block's
  hardness share; otherwise the ball stops and drops as an item.
- Hitting an entity: damage `cannonballDamage × speed` (projectile damage owned by the shooter);
  the ball keeps flying at 60 % speed (each entity is hit once).
- A ball slower than 0.3 b/t drops as an item.

## 4. Bombs

Thrown like snowballs (power 1.25, gravity 0.05) with a 10-tick cooldown (`minecraft:cooldown`),
also shot by dispensers (Bedrock dispenses `minecraft:projectile` items) and by cannons. They
explode on impact:

| Bomb | Radius | Blocks | Extra |
|---|---|---|---|
| Bomb | `bombRadius` 2.0 | breaks "weak" blocks (blast resistance ≤ `bombMaxResistance` 4) | — |
| Blue bomb | `blueBombRadius` 5.15 | none | sets entities in the radius on fire (blue flames, `blueBombFireSeconds` 8 s), blue flame particles |
| Spiky bomb | `bombRadius` 2.0 | none | shrapnel: `spikyShrapnel` 24 spikes in every direction; each spike hits the first entity within 7 blocks for `spikyDamage` 4 |

`bombBreakBlocks` (`all` / `weak` / `none`, default `weak`) applies to the normal bomb.

## 5. Screens

**Cannon screen** (one screen for every client, a LeviLamina custom form, which modded clients
show as well):

```
Cannon
Gunpowder: 12 / 64      Projectile: Cannonball × 3
[ Yaw   ]  -180 ───●─── 180
[ Pitch ]   -20 ──●──── 90
[ Power ]  1  2 [3] 4
( Action ▼ )  Apply | Fire | Manoeuvre | Take out gunpowder | Take out projectile
                                    [Done]
```

Supplementaries' screen holds two inventory slots; Bedrock forms have no slots, so the slots are
filled by using items on the cannon and emptied with the two "Take out" actions.

The red merchant uses the vanilla trading screen (data-driven trade table) on every client.

## 6. Red merchant

- An illager-style travelling trader in red robes. Sells explosives for emeralds:
  bomb (1 emerald → 2), blue bomb (3 → 1), spiky bomb (2 → 1), cannonball (1 → 4),
  firework rocket (1 → 3), TNT (3 → 1), cannon (12 → 1); buys gunpowder (8 → 1 emerald).
- Spawns at night near players in the Overworld: every `redMerchantSpawnInterval` (1200) ticks, if no
  red merchant is alive, with probability `redMerchantSpawnChance` (0.05), 16–32 blocks from a random
  player on a dry solid surface. Despawns after 20–30 minutes when no player is within 24 blocks.
- When attacked it retaliates by throwing bombs at the attacker (ranged attack, every 2 s, 12-block
  range) for a while, then calms down. 20 HP. Avoids zombies like the wandering trader.
- Drops nothing; does not open trades while angry.

## 7. Configuration (`config/cannon.json`)

| Key | Default | Meaning |
|---|---|---|
| `fuseTicks` | 10 | Ticks between ignition and the shot |
| `cooldownTicks` | 30 | Ticks after a shot before the next |
| `speedPerPower` | 0.75 | Projectile speed (blocks/tick) per power level |
| `maxPower` | 4 | Highest power level |
| `cannonballBreaksBlocks` | true | Cannonballs destroy blocks |
| `cannonballBreakHardness` | 4.0 | Destroy-time budget at speed 2 |
| `cannonballDamage` | 5.0 | Damage per block/tick of speed |
| `bombRadius` | 2.0 | Bomb / spiky bomb explosion radius |
| `bombBreakBlocks` | "weak" | `all` / `weak` / `none` |
| `bombMaxResistance` | 4.0 | Weak-block threshold (blast resistance) |
| `blueBombRadius` | 5.15 | Blue bomb radius |
| `blueBombFireSeconds` | 8 | Burn time of the blue bomb |
| `spikyShrapnel` | 24 | Spikes per spiky bomb |
| `spikyDamage` | 4.0 | Damage per spike |
| `redMerchantSpawn` | true | Red merchants spawn naturally |
| `redMerchantSpawnInterval` | 1200 | Ticks between spawn attempts |
| `redMerchantSpawnChance` | 0.05 | Chance per attempt (at night) |

## 8. Commands

None (Supplementaries has no cannon commands). Admins can `/summon complementaries:red_merchant`.

## 9. Strings

| Key | English |
|---|---|
| `gui.supplementaries.cannon` | Cannon |
| `gui.supplementaries.cannon.fuel` | Gunpowder: {0} / {1} |
| `gui.supplementaries.cannon.projectile` | Projectile: {0} × {1} |
| `gui.supplementaries.cannon.projectile_empty` | Projectile: none |
| `gui.supplementaries.cannon.yaw` | Yaw |
| `gui.supplementaries.cannon.pitch` | Pitch |
| `gui.supplementaries.cannon.power` | Power |
| `gui.supplementaries.cannon.action` | Action |
| `gui.supplementaries.cannon.apply` | Apply |
| `gui.supplementaries.cannon.fire` | Fire |
| `gui.supplementaries.cannon.maneuver` | Manoeuvre |
| `gui.supplementaries.cannon.take_fuel` | Take out gunpowder |
| `gui.supplementaries.cannon.take_projectile` | Take out projectile |
| `message.supplementaries.cannon.not_loaded` | The cannon is not loaded |
| `message.supplementaries.cannon.loaded` | Cannon loaded |
| `message.supplementaries.cannon.maneuver` | Power: {0}  \|  Left click: fire  \|  Scroll: power  \|  Sneak: exit |
| `message.supplementaries.cannon.maneuver_end` | Stopped manoeuvring the cannon |

Block, item and entity names come from `texts.d/<locale>/cannon.lang`
(`tile.complementaries:cannon.name`, `item.complementaries:cannonball`, `entity.complementaries:red_merchant.name`, ...).

## 10. Not ported

| Feature | Reason |
|---|---|
| Camera moving into the cannon while manoeuvring | Bedrock offers no camera API usable for this on vanilla clients; the player aims with their own view instead and sees a particle trajectory. |
| Cannon screen inventory slots | Bedrock forms have no item slots; loading is done by using items on the cannon and the screen's "Take out" actions. |
| Cannon boat (cannon mounted on a boat) | Bedrock boats are hard-coded native entities that cannot carry an extra seat/block renderer; not ported. |
| Pistons moving cannons | Custom blocks keep their state but the cannon's DB record stays at the old position; a moved cannon is reset (contents dropped). |
