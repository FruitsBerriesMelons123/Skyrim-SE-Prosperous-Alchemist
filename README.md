# Prosperous Alchemist

Prosperous Alchemist is an SKSE plugin that calculates the most valuable potion or poison currently craftable from the player's ingredients. It accounts for the alchemy skill, relevant perks, worn Fortify Alchemy equipment, ingredient effects, and optional ingredient protection rules.

![Prosperous Alchemist preview](Prosperous%20Alchemist%20AE%20GOG%20SE%20VR.png)

> This repository contains the native plugin source and Visual Studio project. It is not a packaged installer or a complete mod archive.

## Contents

- [Features](#features)
- [How it works](#how-it-works)
- [Calculation model](#calculation-model)
- [Configuration](#configuration)
- [Installation](#installation)
- [Building from source](#building-from-source)
- [Troubleshooting](#troubleshooting)
- [Technical integration](#technical-integration)
- [Current limitations](#current-limitations)
- [License and credits](#license-and-credits)

## Features

- **Best-recipe recommendation** — Searches combinations of two and three ingredients and recommends the recipe with the highest calculated value.
- **Inventory-aware calculations** — Uses the ingredient forms currently in the player's inventory.
- **Player-aware calculations** — Can use the player's Alchemy skill, Alchemist ranks, Physician, Benefactor, Poisoner, Seeker of Shadows, and worn Fortify Alchemy enchantments.
- **Potion and poison support** — Determines whether a result is a potion or poison from its dominant shared effect and displays the appropriate name.
- **Ingredient protection** — Optionally excludes quest, crafting, Atronach Forge, Hearthfire, and other valuable ingredients from recommendations.
- **Custom protection rules** — Adds custom ingredients with an optional inventory threshold and allows individual ingredients to be exempted from protection.
- **Localization support** — Configures the alchemy menu text, no-recipe message, and Potion/Poison name prefixes through the INI file.
- **Multithreaded search** — Uses multiple worker threads by default, with a single-threaded option for troubleshooting or compatibility.
- **Diagnostics** — Includes an optional stress-test mode and ingredient-list logging for development and troubleshooting.

The plugin recommends recipes; it does not automatically craft potions, consume ingredients, modify game records, or change the player's perks.

## How it works

When the alchemy interface requests the plugin's best-recipe result, Prosperous Alchemist:

1. Reads the player's available ingredient forms and inventory counts.
2. Optionally reads the player's current skill, perks, and worn Fortify Alchemy effects.
3. Removes protected ingredients when protection is enabled.
4. Finds every unique pair of remaining ingredients that shares at least one effect.
5. Tests a third ingredient when it can add another effect to the recipe.
6. Applies the configured player bonuses and calculates the expected item value.
7. Returns the highest-value result, including its name, effects, value, and sorted ingredients.

The result is recalculated when the ingredient set or tracked player state changes. Otherwise, the previous result is reused. The plugin only performs the calculation when the alchemy menu request contains the configured alchemy-menu text.

### Result format

A successful recommendation contains:

```text
Potion of Fortify Something: effect description(s)
 Value: 123
Ingredient A, Ingredient B, Ingredient C
```

Poison results use `Poison of` by default. If no valid recipe is found, the default message is:

```text
No potion recipes are currently available.
```

Ingredient names are sorted alphabetically in the displayed recipe. Effect descriptions have their magnitude and duration placeholders replaced with the calculated values.

## Calculation model

The plugin uses the effect data exposed by Skyrim's ingredient and magic-effect records. For effects whose magnitude or duration is affected by alchemy skill, it applies the following base calculation:

```text
calculated value = base value
				  × (4 + (Alchemy skill / 5 × 0.1))
				  × (1 + Alchemist ranks × 0.20)
				  × (1 + Fortify Alchemy / 100)
```

The same player factors are applied to magnitude and duration when the effect supports them. The relevant additional bonuses are:

- **Physician** — Multiplies Restore Health, Restore Magicka, and Restore Stamina magnitude and duration by 1.25.
- **Benefactor** — Multiplies beneficial potion effects by 1.25.
- **Poisoner** — Multiplies harmful poison effects by 1.25.
- **Seeker of Shadows** — Multiplies calculated magnitude and duration by 1.10.
- **Purity** — Excludes effects of the opposite type from the calculated result when a potion or poison contains both beneficial and harmful effects.

For each effect, the estimated value is calculated from its base cost, magnitude, and duration:

```text
base cost × magnitude^1.1 × (duration / 10)^1.1
```

When an effect has only magnitude or only duration, the available component is used. If the same effect exists on multiple ingredients, the implementation keeps the higher-cost version for the recipe calculation.

The recipe's shared effect with the highest calculated cost determines the result type:

- Beneficial dominant effect → **Potion**
- Harmful dominant effect → **Poison**

The displayed value is the calculated value rounded down to a whole number. This is a recommendation based on the plugin's calculation model and should not be treated as a guarantee for every game configuration or modded effect.

## Configuration

The plugin creates this file automatically the first time it loads:

```text
Data/SKSE/Plugins/alchemist.ini
```

All settings are in the `[General]` section. Restart the game after editing the file so the plugin loads the new values.

### General settings

| Setting | Default | Description |
| --- | ---: | --- |
| `IgnorePlayer` | `1` | Ignores the player's skill, perks, and worn Fortify Alchemy equipment when calculating values. Set to `0` to use the current player state. The ingredient list still comes from the player's inventory. |
| `ProtectIngredients` | `0` | Set to `1` to enable the built-in protection list and protection for ingredients with Fortify Enchanting or Fortify Smithing. |
| `Singlethreaded` | `0` | Uses the multithreaded search by default. Set to `1` to use the single-threaded search path. |
| `NumberOfIngredientsToStressTest` | `0` | Normal operation when `0`. A positive number enables a diagnostic calculation using the first that many ingredients in the game's global ingredient list. `-1` logs all known ingredient names. |
| `MoreIngredientsToProtect` | empty | Comma-separated custom ingredient names. Each name can optionally use `Name\|count` to protect it while the inventory count is at or below that threshold. A name without a count uses `999`, effectively always protecting it. |
| `IngredientsToUnprotect` | empty | Comma-separated ingredient names that should not be protected. This takes precedence over the built-in and custom protection rules. |
| `StringTranslations` | `Alchemy,No potion recipes are currently available.` | Two comma-separated strings: the alchemy-menu text used to identify the menu and the no-recipe message. |
| `PotionPoison` | `Potion of,Poison of` | Two comma-separated prefixes: the beneficial potion prefix followed by the harmful poison prefix. |

String settings are intentionally comma-delimited. Do not add additional commas to an individual value. Ingredient names are matched exactly, including spelling and punctuation.

### Example configuration

```ini
[General]
IgnorePlayer=0
ProtectIngredients=1
Singlethreaded=0
NumberOfIngredientsToStressTest=0
MoreIngredientsToProtect=Jarrin Root,Daedra Heart|3,Blue Butterfly Wing|10
IngredientsToUnprotect=Deathbell
StringTranslations=Alchemy,No potion recipes are currently available.
PotionPoison=Potion of,Poison of
```

### Built-in protection list

With `ProtectIngredients=1`, the following ingredients are protected according to the indicated inventory thresholds. An ingredient marked **always** is protected regardless of its count.

<details>
<summary>Show built-in protected ingredients</summary>

| Ingredient | Protected when count is at or below |
| --- | ---: |
| Berit's Ashes | always |
| Bliss Bug Thorax | 11 |
| Bone Hawk Claw | always |
| Briar Heart | 3 |
| Corkbulb Root | always |
| Corrupted Human Heart | always |
| Crimson Nirnroot | 31 |
| Daedra Heart | always |
| Deathbell | 32 |
| Dragon's Tongue | 11 |
| Ectoplasm | 11 |
| Farengar's Frost Salt | always |
| Fine-Cut Void Salts | always |
| Fire Salts | 21 |
| Frost Mirriam | 11 |
| Frost Salts | 11 |
| Giant's Toe | 3 |
| Goldfish | 2 |
| Hagraven Claw | 2 |
| Hagraven Feathers | 2 |
| Human Heart | always |
| Ice Wraith Teeth | 6 |
| Ironwood Fruit | 2 |
| Jarrin Root | always |
| Jazbay Grapes | 21 |
| Juniper Berries | 2 |
| Juvenile Mudcrab | 2 |
| Large Antlers | always |
| Mudcrab Chitin | always |
| Netch Jelly | 6 |
| Nightshade | 21 |
| Nirnroot | 21 |
| Salt Pile | 11 |
| Scathecraw | 11 |
| Simon Rodayne's Heart | always |
| Slaughterfish Scales | always |
| Taproot | 4 |
| Torchbug Abdomen | 11 |
| Torchbug Thorax | 11 |
| Troll Fat | 2 |
| Vampire Dust | 3 |
| Void Salts | 12 |

Ingredients containing the `Fortify Enchanting` or `Fortify Smithing` effect are also protected when the built-in protection mode is enabled.

</details>

## Installation

### Requirements

- Skyrim Special Edition/Anniversary Edition on Windows.
- A game executable with runtime **1.7.99**, which is the runtime targeted by this branch.
- SKSE matching the installed game runtime. The checked-in source identifies itself as SKSE **2.3.0**.
- The game must be launched through SKSE.

The plugin performs an exact runtime check and rejects unsupported runtimes. Do not use the built DLL with a different game runtime unless the source has been updated and rebuilt for that runtime.

### Installing a compiled plugin

1. Obtain a compiled `alchemist.dll` built for the matching runtime.
2. Copy it to:

   ```text
   <Skyrim installation>/Data/SKSE/Plugins/alchemist.dll
   ```

3. Launch Skyrim through SKSE.
4. Open the alchemy interface. The plugin creates `alchemist.ini` automatically if it does not already exist.
5. Edit the INI file as needed and restart the game after making changes.

This repository does not currently include a release archive, installer, ESP/ESL, or tracked compiled DLL. If a distribution package provides additional UI files, install those files according to that package's archive structure.

## Building from source

The repository includes the SKSE and common source trees required by the Visual Studio projects.

### Visual Studio

1. Install Visual Studio with the **Desktop development with C++** workload, an x64 toolchain, and a Windows SDK.
2. Open [`skse64/skse64.sln`](skse64/skse64.sln).
3. Select the `Release` configuration and `x64` platform.
4. Build the solution or build the `alchemist` project after its SKSE/common dependencies.
5. The plugin is produced at:

   ```text
   skse64/x64/Release/alchemist.dll
   ```

The alchemist project currently uses the `v145` platform toolset and C++17. When invoking MSBuild directly on the project rather than through the solution, set `SolutionDir` to the repository's `skse64` directory so the existing relative include and dependency paths resolve correctly.

## Troubleshooting

### No recommendation appears

- Confirm that `alchemist.dll` is in `Data/SKSE/Plugins`.
- Confirm that the game was launched through SKSE.
- Confirm that the installed game runtime is exactly 1.7.99.
- Open the log at:

  ```text
  Documents/My Games/Skyrim Special Edition/SKSE/alchemist.log
  ```

- Confirm that `StringTranslations` begins with the text used by the installed alchemy interface.
- Make sure at least two available, unprotected ingredients share an effect.

### The INI file is not created

The plugin creates the file during load. Check the SKSE log for a plugin load error, verify write permissions for the game's `Data/SKSE/Plugins` directory, and make sure the DLL matches the game runtime.

### Values appear unchanged after editing the INI

The plugin loads settings during startup. Exit the game completely and start it again after changing `alchemist.ini`.

### Custom protection does not work

Ingredient names are exact matches. Use the name displayed by the game and separate entries with commas. For example:

```ini
MoreIngredientsToProtect=Daedra Heart|3,Blue Butterfly Wing
IngredientsToUnprotect=Deathbell
```

`IngredientsToUnprotect` is evaluated first and overrides protection for matching names.

## Technical integration

The native plugin registers these Scaleform functions under the `alchemist` movie name:

| Function | Behavior |
| --- | --- |
| `GetBestRecipeName` | Accepts the alchemy craft-description string, performs the recommendation calculation when the alchemy-menu text matches, and returns the formatted result string. |
| `GetBestRecipeDescription` | Registered for the interface, but currently returns an empty string. |

The plugin also registers `SKSEPlugin_Query`, `SKSEPlugin_Load`, and `SKSEPlugin_Version`, writes diagnostic output through the SKSE log system, and listens for the SKSE input-loaded message before reporting initialization.

## Current limitations

- Only the runtime defined by `CURRENT_RELEASE_RUNTIME` is accepted; this branch currently defines that as Skyrim runtime 1.7.99.
- The recommendation cache compares ingredient names and tracked player state. Changing only an inventory quantity may not immediately force a new search if the ingredient set remains the same.
- Localization values support exactly two comma-separated fields per setting and do not support commas inside a translated value.
- Protection and menu detection use exact English/source strings unless overridden through the INI settings.
- `NumberOfIngredientsToStressTest` and the `-1` ingredient logger are diagnostic features, not normal gameplay settings.
- The plugin depends on the surrounding SKSE/Scaleform integration; the native DLL alone does not provide a standalone user interface.

## License and credits

Prosperous Alchemist is released under the [MIT License](LICENSE).

Special thanks to:

- [SKSE Team](https://skse.silverlock.org)
- [axxonite](https://www.nexusmods.com/skyrim/mods/38634)
- [Ryan-rsm-McKenzie](https://github.com/Ryan-rsm-McKenzie/CommonLibSSE/wiki/Getting-Started/b089fcaa77aeac3f2db019ecb30df94743574b6f)
- [sonycman](https://www.nexusmods.com/skyrimspecialedition/users/23615814)
- [shadeMe](https://github.com/shadeMe/SME-Sundries)
