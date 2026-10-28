# Complementaries behaviour spec

What Complementaries reproduces from the Java mod [Supplementaries](https://modrinth.com/mod/supplementaries) **4.0.2** (MehVahdJukaar).
This is the behaviour reference for every feature unit (see [AGENTS.md](../AGENTS.md)). It describes
behaviour only: never copy code or assets from Supplementaries.

## 1. Overview

Complementaries reproduces every block, item, mechanic, screen and vanilla tweak of Supplementaries
4.0.2, as faithfully as Bedrock allows, on both LeviLamina platforms:

- **Dedicated server (BDS).** All gameplay runs on the server. Players with the client mod get the
  full experience (native screens and renderers); vanilla clients get a functional fallback
  (LeviLamina forms, data-driven visuals) and never crash.
- **Client (local worlds).** The integrated server runs the same gameplay code; client-only parts
  (screens, renderers, tooltips) live in `src/client/<unit>/`.

Each feature unit documents its behaviour in its own file, `docs/spec/<unit>.md`, so units can be
written in parallel without touching this file. Sections 2–8 below are the union of those files.

## 2. Blocks and items

See section "Blocks and items" of each unit spec in [§9](#9-work-plan). Ids are `complementaries:<path>`,
using the paths of Supplementaries.

## 3. Mechanics

See section "Mechanics" of each unit spec.

## 4. Screens

See section "Screens" of each unit spec: layout on the modded client and the form fallback for vanilla clients.

## 5. Configuration

One section per unit, `config/<unit>.json` (`CM_REGISTER_CONFIG("<unit>", ...)`). Keys and defaults
are listed in each unit spec.

## 6. Commands

`/supplementaries` (alias `/complementaries`): `reload`, `selftest ["glob"]`, plus the subcommands
listed in each unit spec.

## 7. Strings

Translation keys follow Supplementaries where the string exists there, `cm.<unit>.*` otherwise.
Each unit lists its keys and English text; translations live in `assets/lang/{en_US,zh_CN}/<unit>.json`.

## 8. Not ported

Each unit spec lists what it cannot reproduce on Bedrock and why.

## 9. Work plan

Units are independent: none depends on another unit's PR. Unit U<n> owns selftest `dx` 100n..100n+99.

| Unit | Name | Spec | Content |
| --- | --- | --- | --- |
| U1 | `jar` | [jar.md](spec/jar.md) | Jar (items, cookies, mobs), soft fluids (water, lava, honey, milk, potions, soups, XP, lumisene), faucet |
| U2 | `storage` | [storage.md](spec/storage.md) | Sack, present and trapped present, lunch basket, quiver |
| U3 | `lock` | [lock.md](spec/lock.md) | Key, lock block, safe, iron gate, gold and netherite doors and trapdoors |
| U4 | `sign` | [sign.md](spec/sign.md) | Sign posts (way signs), notice board, doormat |
| U5 | `blackboard` | [blackboard.md](spec/blackboard.md) | Blackboard and its drawing screen |
| U6 | `instrument` | [instrument.md](spec/instrument.md) | Globe and sepia globe, wind vane, clock block, hourglass, altimeter |
| U7 | `mechanism` | [mechanism.md](spec/mechanism.md) | Spring launcher, bellows, turn table, cog block, crank |
| U8 | `circuit` | [circuit.md](spec/circuit.md) | Relayer, speaker block, crystal display, redstone illuminator, dispenser minecart |
| U9 | `lighting` | [lighting.md](spec/lighting.md) | Sconces, candle holders, stone lamps, wall lanterns |
| U10 | `building` | [building.md](spec/building.md) | Ash, bricks and tiles, checker blocks, daub and timber frames, raked gravel, fine wood, soap/flint/feather/sugar blocks |
| U11 | `rope` | [rope.md](spec/rope.md) | Rope, rope arrow, pulley block, flax and wild flax, flax block |
| U12 | `display` | [display.md](spec/display.md) | Pedestal, item shelf, hat stand, statue |
| U13 | `tableware` | [tableware.md](spec/tableware.md) | Goblet, urn, planter, flower box, book pile, pancake, candy, jar boat |
| U14 | `flag` | [flag.md](spec/flag.md) | Flags, bunting, awnings, ceiling banners, banner patterns |
| U15 | `cannon` | [cannon.md](spec/cannon.md) | Cannon, cannonball, bombs, red merchant |
| U16 | `tool` | [tool.md](spec/tool.md) | Slingshot, bubble blower, soap, wrench, flute, confetti popper, antique ink, slice map |
| U17 | `trap` | [trap.md](spec/trap.md) | Bamboo and tipped spikes, cage, fodder |
| U18 | `tweak` | [tweak.md](spec/tweak.md) | Vanilla tweaks: placeable sticks/rods/gunpowder, throwables, hanging pots, cauldron dyeing, bell chains, trader maps, ... |
