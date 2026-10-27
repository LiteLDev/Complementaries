# Complementaries

[![Build](https://github.com/LiteLDev/Complementaries/actions/workflows/build.yml/badge.svg)](https://github.com/LiteLDev/Complementaries/actions/workflows/build.yml)
[![License](https://img.shields.io/github/license/LiteLDev/Complementaries)](COPYING.LESSER)

Vanilla+ blocks and items: jars, signposts, faucets, weather vanes, spring launchers and more. Complementaries brings MehVahdJukaar's Java mod [Supplementaries](https://modrinth.com/mod/supplementaries) to Bedrock, for dedicated servers and single-player worlds.

> [!NOTE]
> Complementaries is in early development. There is no release yet.

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

## Commands

| Command | Who | What it does |
| --- | --- | --- |
| `/supplementaries reload` | Operators, console | Reloads the configuration and translations. |
| `/supplementaries selftest ["glob"]` | Operators, console | Runs the in-game selftests. |

`/complementaries` is an alias of `/supplementaries`.

## Configuration

Settings live in `config/*.json` inside the mod folder (`plugins/Complementaries/` or `mods/Complementaries/`). Each file is created with defaults on first start.

| File | Setting | Default | Meaning |
| --- | --- | --- | --- |
| `mod.json` | `language` | `en_US` | Language of server messages when a player's own language has no translation. |
| | `usePlayerLocale` | `true` | Use each player's game language when a translation exists. |
| | `requireResourcePack` | `true` | Dedicated server: players must accept the resource pack to join. |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). [AGENTS.md](AGENTS.md) is the developer guide.

## Credits

Complementaries reproduces the behaviour of [Supplementaries](https://github.com/MehVahdJukaar/Supplementaries) by MehVahdJukaar. It copies no code or assets from it.

## License

[LGPL-3.0-or-later](COPYING.LESSER)
