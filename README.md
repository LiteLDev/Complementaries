# Complementaries

[![Build](https://github.com/LiteLDev/Complementaries/actions/workflows/build.yml/badge.svg)](https://github.com/LiteLDev/Complementaries/actions/workflows/build.yml)
[![License](https://img.shields.io/github/license/LiteLDev/Complementaries)](COPYING.LESSER)

Vanilla+ blocks and items: jars, signposts, faucets, weather vanes, spring launchers and more. Complementaries brings MehVahdJukaar's Java mod [Supplementaries](https://modrinth.com/mod/supplementaries) to Bedrock, for dedicated servers and single-player worlds.

## Install

You need Minecraft Bedrock Edition **1.26.51** for Windows (x64) with LeviLamina **26.51.x** (on a client, installed through [LeviLauncher](https://github.com/LiteLDev/LeviLauncher)).

With [lip](https://github.com/LiteLDev/lip):

```sh
lip install github.com/LiteLDev/Complementaries           # dedicated server
lip install github.com/LiteLDev/Complementaries#client    # client
```

Or download the zip from the [latest release](https://github.com/LiteLDev/Complementaries/releases/latest) and extract it next to `LeviLamina/`:

| Where | Zip | Folder |
| --- | --- | --- |
| Dedicated server | `complementaries-server-windows-x64.zip` | `plugins/Complementaries/` |
| Client (LeviLauncher) | `complementaries-client-windows-x64.zip` | `mods/Complementaries/` |

Players who join a dedicated server don't need to install anything: the server sends its resource
pack. Installing the client mod as well adds a few client-side extras, such as climbing ropes without
the server correcting your movement.

## Features

Every block, item and mechanic is described in detail, including what could not be ported to Bedrock,
in [docs/SPEC.md](docs/SPEC.md) and the unit specs it links to.

| Group | What you get |
| --- | --- |
| [Jars and fluids](docs/spec/jar.md) | Jars that hold fluids, cookies or small mobs; soft fluids (water, lava, honey, milk, potions, soups, experience, ...); the faucet |
| [Storage](docs/spec/storage.md) | Sack, present and trapped present, quiver, lunch basket |
| [Locks](docs/spec/lock.md) | Key, lock block, safe, iron and gold gates, gold and netherite doors and trapdoors |
| [Signs](docs/spec/sign.md) | Sign posts for every wood, way signs near villages, notice board, doormat |
| [Blackboard](docs/spec/blackboard.md) | Blackboard with a drawing screen, chalk and dye drawing, glow and wax |
| [Instruments](docs/spec/instrument.md) | Globe and sepia globe, wind vane, clock block, hourglass, altimeter |
| [Mechanisms](docs/spec/mechanism.md) | Spring launcher, bellows, turn table, cog block, crank |
| [Redstone](docs/spec/circuit.md) | Relayer, speaker block, crystal display, redstone illuminator, minecart with dispenser |
| [Lighting](docs/spec/lighting.md) | Sconces, candle holders, stone lamps, wall lanterns |
| [Building blocks](docs/spec/building.md) | Ash, ash bricks, stone and blackstone tiles, lapis bricks, checker blocks, daub and timber frames, raked gravel, fine wood, soap, flint, feather and sugar blocks |
| [Rope and flax](docs/spec/rope.md) | Rope, rope arrow, pulley, flax crop, wild flax, flax block |
| [Displays](docs/spec/display.md) | Pedestal, item shelf, hat stand, statue |
| [Tableware](docs/spec/tableware.md) | Goblet, urn, planter, flower box, book piles, pancake, candy, jar boat |
| [Flags](docs/spec/flag.md) | Flags with banner patterns, awnings, buntings, dragon banner pattern |
| [Cannon](docs/spec/cannon.md) | Cannon, cannonball, bombs, red merchant |
| [Tools](docs/spec/tool.md) | Slingshot, bubble blower, soap, wrench, flute, confetti popper, antique ink, slice map |
| [Traps](docs/spec/trap.md) | Bamboo spikes, tipped spikes, cage, fodder |
| [Vanilla tweaks](docs/spec/tweak.md) | Placeable sticks, rods and gunpowder; throwable bricks and slimeballs; hanging flower pots; clock and compass info; experience bottling; bell chains; cauldron dyeing; zombie horses |

## Commands

| Command | Who | What it does |
| --- | --- | --- |
| `/supplementaries reload` | Operators, console | Reloads the configuration and translations. |
| `/supplementaries selftest ["glob"]` | Operators, console | Runs the in-game selftests. |
| `/supplementaries cage <entity>` | Operators | Gives a cage holding a new mob of that type. |
| `/supplementaries globe newseed` / `resetseed` | Operators, console | Picks a new globe map, or goes back to the one from the world seed. |
| `/supplementaries tool enchant <stasis\|loyalty\|clear> [level]` | Operators | Applies Stasis or Loyalty to the held slingshot, or clears them (Bedrock has no custom enchantments). |
| `/supplementaries waysign` | Operators | Builds a way sign at your position, pointing to the nearest structures. |
| `/supplementaries blackboard set <x> <y> <z> "<pixels>"` | Operators, console | Sets a blackboard's drawing (256 hex digits, one colour per pixel). |
| `/supplementaries jardemo` | Operators | Builds a row of filled jars, for screenshots. |

`/complementaries` is an alias of `/supplementaries`.

## Configuration

Settings live in `config/*.json` inside the mod folder (`plugins/Complementaries/` or `mods/Complementaries/`). Each file is created with defaults on first start; `/supplementaries reload` applies changes.

| File | Setting | Default | Meaning |
| --- | --- | --- | --- |
| `mod.json` | `language` | `en_US` | Language of server messages when a player's own language has no translation. |
| | `usePlayerLocale` | `true` | Use each player's game language when a translation exists. |
| | `requireResourcePack` | `true` | Dedicated server: players must accept the resource pack to join. |

Each feature group has its own file. Every key, its default and its meaning are listed in the
"Configuration" section of the group's spec:

| File | Spec | File | Spec |
| --- | --- | --- | --- |
| `jar.json` | [jar](docs/spec/jar.md) | `building.json` | [building](docs/spec/building.md) |
| `storage.json` | [storage](docs/spec/storage.md) | `rope.json` | [rope](docs/spec/rope.md) |
| `lock.json` | [lock](docs/spec/lock.md) | `display.json` | [display](docs/spec/display.md) |
| `sign.json` | [sign](docs/spec/sign.md) | `tableware.json` | [tableware](docs/spec/tableware.md) |
| `blackboard.json` | [blackboard](docs/spec/blackboard.md) | `flag.json` | [flag](docs/spec/flag.md) |
| `instrument.json` | [instrument](docs/spec/instrument.md) | `cannon.json` | [cannon](docs/spec/cannon.md) |
| `mechanism.json` | [mechanism](docs/spec/mechanism.md) | `tool.json` | [tool](docs/spec/tool.md) |
| `circuit.json` | [circuit](docs/spec/circuit.md) | `trap.json` | [trap](docs/spec/trap.md) |
| `lighting.json` | [lighting](docs/spec/lighting.md) | `tweak.json` | [tweak](docs/spec/tweak.md) |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). [AGENTS.md](AGENTS.md) is the developer guide.

## Credits

Complementaries reproduces the behaviour of [Supplementaries](https://github.com/MehVahdJukaar/Supplementaries) by MehVahdJukaar. It copies no code or assets from it.

## License

[LGPL-3.0-or-later](COPYING.LESSER)
