# U15 `cannon`: cannon, cannonballs, bombs and the red merchant

Behaviour reference: Supplementaries 4.0.2 (MehVahdJukaar). This file describes behaviour only;
no code or assets come from Supplementaries. English strings that exist in Supplementaries'
`en_us.json` keep its wording. Unit number 15: selftests use `dx` 1500..1599.

## 1. Content

| Supplementaries id | Complementaries id | Kind |
|---|---|---|
| `supplementaries:cannon` | `complementaries:cannon` | block + item ("Can shoot anything") |
| `supplementaries:cannonball` | `complementaries:cannonball` | placeable block + item, and the flying projectile entity |
| `supplementaries:bomb` | `complementaries:bomb` | throwable item + projectile entity |
| `supplementaries:bomb_blue` | `complementaries:bomb_blue` | throwable item + projectile entity |
| (baby bombs of the blue bomb) | `complementaries:bomb_blue_baby` | projectile entity |
| `supplementaries:bomb_spiky` (Supplementaries 1.x) | `complementaries:bomb_spiky` | throwable item + projectile entity, kept from the work plan |
| `supplementaries:red_merchant` | `complementaries:red_merchant` | trader entity + spawn egg |
| — | `complementaries:cannon_barrel` | helper entity drawing a cannon's barrel (Bedrock blocks have no block-entity renderers) |

Supplementaries uses one bomb entity with a type field; Bedrock has one entity per bomb type so
every client draws the right sprite without a custom renderer.

### Recipes (crafting table)

| Result | Recipe |
|---|---|
| Cannon | `"  I"`, `"IBI"`, `"PPP"`: I iron ingot, B iron block, P oak planks |
| Cannonball ×4 | `" N "`, `"NIN"`, `" N "`: N iron nugget, I iron ingot |
| Bomb ×2 | `" S "`, `"NGN"`, `" N "`: S string, N iron nugget, G gunpowder |
| Blue bomb | shapeless: bomb + lapis lazuli + blaze powder |
| Spiky bomb | shapeless: bomb + 4 iron nuggets |

## 2. Cannon block

- **Placement** on any face, like a dispenser: state `minecraft:block_face` (`up` = standing on the
  floor, `down` = hanging, sides = on a wall). The base plate lies on the supporting face.
- **Drops** itself, plus its contents when broken by any means. Hardness 3.5, blast resistance 6,
  metal sounds. Redstone consumer.
- **Contents** (world DB `cm:cannon:c:<dim>:<x>:<y>:<z>`, since custom blocks have no block entities):
  fuel slot (gunpowder only, up to 64) and projectile slot (any item, one stack).
- **Aim**: `yaw` (−180..180°) and `pitch` (−20..90°, elevation above the base plane) in the
  base's frame; a floor cannon uses Minecraft yaw (0 = south, 90 = west). **Power level** 1..4 =
  gunpowder used per shot; muzzle speed = power × 0.75 × `firePower` blocks/tick.
- **Barrel**: a helper entity at the block centre turns to the aim (synced entity properties drive
  the model, so vanilla clients see it too) and recoils when firing.

### Interaction

| Action | Result |
|---|---|
| Use with gunpowder (fuel not full) | Loads it into the fuel slot. |
| Use with any other item when the projectile slot is empty or holds the same item | Loads it as the projectile. |
| Use with an empty hand, or an item that does not fit | Opens the cannon screen (§5). |
| Sneak-use with an item | Vanilla (place a block against the cannon). |
| Rising edge of a redstone signal | Lights the fuse. |
| Break (any cause) | Drops the cannon and its contents; the barrel disappears. |

### Firing

1. Needs `fuel >= power`, a projectile and no cooldown; otherwise the player sees
   "The cannon is not loaded".
2. Lighting: fuse sound and smoke; after `fuseTime` ticks (default 40) the shot: `power`
   gunpowder and one projectile are used, a deep explosion sound plays (`complementaries.cannon.fire`),
   smoke and an explosion puff come out of the muzzle, the barrel recoils. Cooldown `cooldown`
   ticks (default 60).
3. What is shot:
   - cannonball → cannonball entity (§3); bombs → bomb entities (§4);
   - arrow, snowball, egg, ender pearl, fire charge (small fireball), wind charge, splash/lingering
     potion (same potion), bottle o' enchanting, trident, firework rocket → their vanilla entities;
   - TNT → primed TNT, which explodes on impact when `explodeTnt` is on;
   - anything else → the item flies out as an item entity.
4. A wall right in front of the muzzle is hit at once (Bedrock projectiles skip block hits on their
   first tick).

### Maneuver (manual aiming)

The screen's "Maneuver" action puts the player in control:
- The barrel follows the player's view every tick (clamped to the barrel limits).
- A dotted trajectory preview (particles shown only to that player) follows the predicted path to
  the first block hit, which is marked in red.
- Attack / swing fires; scrolling the hotbar changes the power (+1/−1, wraps); sneaking, walking
  more than 6 blocks away or the cannon disappearing ends it.
- Action bar: `Press Sneak to Dismount, Attack to Fire  |  Power: N (scroll to change)`.

## 3. Cannonball

- Fired only (players cannot throw it); gravity 0.05, drag 0.99. Placeable as a small block.
- Energy = `cannonballPowerScaling` × speed². Hitting an entity deals damage = energy (projectile
  damage owned by the shooter) and halves the energy; each entity is hit once per ball.
- Hitting a block: the blocks inside `cannonballBreakRadius` of the impact (the block hit first,
  then the nearest ones) are destroyed (with drops) while the energy can pay their destroy time;
  the ball goes on with the energy left. When nothing breaks, or the ball is too slow, it breaks
  apart ("Cannonball breaks": break sound and smoke).

## 4. Bombs

Thrown like snowballs (power 1.25, gravity 0.05), item cooldown 0.5 s, shot by dispensers (Bedrock
dispenses `minecraft:projectile` items) and by cannons. They explode on contact (`bombFuse` > 0:
that many ticks after the impact). Bombs bounce off slime and honey blocks (Supplementaries'
`bouncy_blocks`).

| Bomb | Explosion | Blocks | Extra |
|---|---|---|---|
| Bomb | `bombExplosionRadius` 2 | `bombBreakBlocks`: `weak` (default: blast resistance ≤ `bombMaxResistance` 4, e.g. dirt, glass, wood), `all`, `none` | — |
| Blue bomb | `blueBombExplosionRadius` 3 ("same blast as a normal bomb, just a bit bigger") | `blueBombBreakBlocks` (default `none`) | scatters `blueBombSplitCount` (4) baby bombs: each explodes on contact with radius `blueBombBabyExplosionRadius` and sets entities within twice that radius on fire for `blueBombBabyFireDuration` (5) s; blue flame particles |
| Spiky bomb | `bombExplosionRadius` | none | 24 shrapnel spikes in every direction; each hits the first mob within 7 blocks (blocked by solid blocks) for `spikyDamage` 4 |

## 5. Screens

**Cannon screen** — one LeviLamina custom form for every client (modded clients show the same form):

```
Cannon
Gunpowder: 12 / 64
Projectile: Cannonball × 3
Yaw    -180 ───●─── 180
Pitch   -20 ──●──── 90
Power   1  2 [3] 4
Action  [Apply | Fire | Maneuver | Take out gunpowder | Take out projectile]
                                                          [Submit]
```

Supplementaries' screen holds the two slots; Bedrock forms have no item slots, so the slots are
filled by using items on the cannon and emptied with the two "Take out" actions.

The red merchant uses the vanilla trading screen (data-driven trade table) on every client.

## 6. Red merchant

- A travelling trader in red robes (villager-shaped). Sells: 2 bombs (1 emerald), blue bomb (3),
  spiky bomb (2), 4 cannonballs (1), 3 firework rockets (1), TNT (3), cannon (12); buys 8 gunpowder
  for 1 emerald.
- Spawns at night near players in the Overworld: every 1200 ticks, if no red merchant is alive,
  with chance 5 % × `redMerchantSpawnMultiplier` ("won't spawn at 0 and will spawn twice as often
  on 2"), 16–32 blocks from a random player on a dry solid surface. Despawns after 20–30 minutes
  once no player is within 24 blocks.
- When a player hurts it, it gets angry for 30 s and throws bombs at its attacker (every 1.5–2.5 s,
  range 12); it does not trade while angry and is immune to explosions. 20 HP; avoids zombies and
  illagers like the wandering trader.

## 7. Configuration (`config/cannon.json`)

| Key | Default | Supplementaries option |
|---|---|---|
| `fuseTime` | 40 | `cannon.fuse_time` |
| `cooldown` | 60 | `cannon.cooldown` |
| `firePower` | 1.0 | `cannon.fire_power` |
| `explodeTnt` | true | `cannon.explode_tnt` |
| `maxPower` | 4 | — |
| `cannonballPowerScaling` | 1.0 | `cannonball.power_scaling` |
| `cannonballBreakRadius` | 1.1 | `cannonball.break_radius` |
| `bombExplosionRadius` | 2.0 | `bomb.explosion_radius` |
| `bombBreakBlocks` | "weak" | `bomb.break_blocks` |
| `bombMaxResistance` | 4.0 | (the `bomb_breakable` tag) |
| `bombFuse` | 0 | `bomb.bomb_fuse` |
| `bombCooldown` | true | `bomb.cooldown` (the cooldown itself is in the item definition) |
| `blueBombExplosionRadius` | 3.0 | `blue_bomb.explosion_radius` |
| `blueBombBreakBlocks` | "none" | `blue_bomb.break_blocks` |
| `blueBombSplitCount` | 4 | `blue_bomb.split_count` |
| `blueBombBabyExplosionRadius` | 1.2 | `blue_bomb.baby_explosion_radius` |
| `blueBombBabyFireDuration` | 5 | `blue_bomb.baby_fire_duration` |
| `spikyShrapnel` / `spikyDamage` | 24 / 4.0 | — |
| `redMerchantSpawnMultiplier` | 1.0 | `general.red_merchant_spawn_multiplier` |

## 8. Commands

None (Supplementaries has no cannon commands). `/summon complementaries:red_merchant` works.

## 9. Strings

| Key | English |
|---|---|
| `gui.supplementaries.cannon` | Cannon |
| `gui.supplementaries.cannon.maneuver` | Maneuver |
| `message.supplementaries.cannon` | Can shoot anything |
| `message.supplementaries.cannon.maneuver` | Press {0} to Dismount, {1} to Fire |
| `cm.cannon.fuel` | Gunpowder: {0} / {1} |
| `cm.cannon.projectile` / `cm.cannon.projectile_empty` | Projectile: {0} × {1} / Projectile: none |
| `cm.cannon.yaw` / `cm.cannon.pitch` / `cm.cannon.power_level` | Yaw / Pitch / Power |
| `cm.cannon.action` / `cm.cannon.apply` / `cm.cannon.fire` | Action / Apply / Fire |
| `cm.cannon.take_fuel` / `cm.cannon.take_projectile` | Take out gunpowder / Take out projectile |
| `cm.cannon.loaded` / `cm.cannon.not_loaded` | Cannon loaded / The cannon is not loaded |
| `cm.cannon.power` | Power: {0} (scroll to change) |
| `cm.cannon.key.sneak` / `cm.cannon.key.attack` | Sneak / Attack |
| `cm.cannon.maneuver_end` | Stopped maneuvering the cannon |

Block, item and entity names live in `texts.d/<locale>/cannon.lang` (`tile.complementaries:cannon.name`
"Cannon", `tile.complementaries:cannonball.name` "Cannonball", `item.complementaries:bomb.name` "Bomb",
`item.complementaries:bomb_blue.name` "Blue Bomb", `entity.complementaries:red_merchant.name` "Red Merchant",
`item.spawn_egg.entity.complementaries:red_merchant.name` "Red Merchant Spawn Egg", ...).

## 10. Not ported

| Feature | Reason |
|---|---|
| Camera moving into the cannon while maneuvering | No camera API a server-side mod can use for vanilla clients; the player aims with their own view and sees the particle trajectory. |
| Item slots in the cannon screen | Bedrock forms have no slots; loading uses the item on the cannon, unloading the screen's "Take out" actions. |
| Cannon boat / cannon raft (`cannon_boat`, `cannon_raft`) | Bedrock boats are native entities: a data-driven entity cannot reuse the boat's water physics and seats with an extra cannon, so a boat that steers and floats like the vanilla one is not possible yet. |
| Custom death messages (`death.attack.supplementaries.cannonball`) | Bedrock death messages are fixed per damage cause; cannonball kills use the projectile message. |
| "Heave Ho" music disc (`cannon.music_disc_heave_ho`) | Needs an original recording; no audio is shipped. |
| `cannon_blacklist` / `cannon_tnts` tags | Bedrock has no item/block tags a mod can extend at runtime; the TNT rule applies to `minecraft:tnt`. |
| Pistons moving cannons | The DB record stays at the old position; a moved cannon is treated as broken (contents dropped). |
