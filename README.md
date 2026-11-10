# Rack Randomizer

Rack Randomizer is a D2RLoader plugin by **MadMike** that gives offline weapon racks and armor stands fresh randomness when they choose an item base. It keeps the game's native eligibility rules, rarity checks, and weapon rack rerolls. A rack can still roll the same base by chance.

## Installation

1. Close Diablo II: Resurrected and D2RLoader.
2. Download `d2rl-rack-randomizer.dll` from the [latest release](../../releases/latest).
3. Place it in `d2rloader/plugins/` for global use or in `mods/<mod>/d2rloader/plugins/` for a single mod.
4. Launch D2RLoader and enter an offline game. The plugin needs no configuration.

## Compatibility

- Tested on Windows x64 with **D2R 3.4.95032** and **D2RLoader 1.3.1-beta**. The plugin requires loader support for Plugin ABI 4 and the Mutation Service.
- The plugin checks the native rack routines and declines to load when they differ from the audited code.
- CrossOver, Wine, and Proton remain untested with this plugin.

## Build from source

Use CMake 3.29 or newer, the Visual Studio x64 Developer Command Prompt, and [D2RLoader PluginSDK 0.4.0](https://github.com/D2RLoader/PluginSDK).

```cmd
cmake -S . -B ../build-rack-randomizer -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DD2RL_SDK_ROOT="<path-to-PluginSDK>"
cmake --build ../build-rack-randomizer
```

See the [hook inventory](HOOK_INVENTORY.md) for the native integration and [LICENSE](LICENSE) for licensing.
