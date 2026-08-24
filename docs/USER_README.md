# Prosperous Alchemist

Prosperous Alchemist recommends the most valuable potion or poison that can be made from the ingredients currently available to the player. It considers ingredient effects and, when enabled, the player's Alchemy skill, perks, and Fortify Alchemy equipment.

The plugin recommends a recipe. It does not automatically craft potions, consume ingredients, alter game records, or change perks.

## Requirements

- Skyrim Special Edition, Anniversary Edition, or VR on Windows.
- SKSE matching the installed Skyrim runtime.
- Address Library for SKSE Plugins, including the version library matching the installed runtime.
- The game must be launched through SKSE.

The same plugin can support compatible runtimes through Address Library. Runtime `1.7.99` with SKSE `2.3.0` is the version combination validated for this project; it is not a fixed requirement. Install the Address Library version file that matches your game runtime.

## Installation

### Mod Organizer 2

Install the mod with this structure:

```text
Prosperous Alchemist AE/
└── SKSE/
	└── Plugins/
		└── alchemist.dll
```

If you received only `alchemist.dll`, create or select a mod in MO2 and place it at:

```text
SKSE/Plugins/alchemist.dll
```

Enable the mod and launch Skyrim through SKSE. Make sure the intended MO2 profile is active.

### Direct installation

Copy `alchemist.dll` to:

```text
<Skyrim installation>/Data/SKSE/Plugins/alchemist.dll
```

Launch the game through SKSE after copying the file.

Prosperous Alchemist does not require an ESP or ESL, and it does not include a separate user interface plugin. It integrates with the existing alchemy interface.

## Using the plugin

1. Launch Skyrim through SKSE.
2. Open the alchemy interface.
3. Request the best-recipe result in the alchemy menu.
4. Review the recommended potion or poison, effects, value, and ingredients.

A typical result looks like this:

```text
Potion of Fortify Something: effect description(s)
 Value: 123
Ingredient A, Ingredient B, Ingredient C
```

Ingredient names are displayed alphabetically. The result is calculated from available, unprotected ingredients and is cached until the ingredient set or tracked player state changes.

The default menu text is `Alchemy`. For a translated or modified interface, change `StringTranslations` as described below.

## Configuration

The plugin reads:

```text
Data/SKSE/Plugins/alchemist.ini
```

If the file does not exist, it is created with default settings when the plugin starts. All settings are in the `[General]` section. Restart the game after editing the file.

| Setting | Default | Description |
| --- | ---: | --- |
| `IgnorePlayer` | `1` | Ignores Alchemy skill, perks, and worn Fortify Alchemy equipment. Set to `0` to use the current player state. |
| `ProtectIngredients` | `0` | Set to `1` to enable the built-in protection list and protect ingredients with Fortify Enchanting or Fortify Smithing effects. |
| `Singlethreaded` | `0` | Uses the normal multithreaded search by default. Set to `1` for the single-threaded search path. |
| `MoreIngredientsToProtect` | empty | Comma-separated custom ingredient names. Use `Name\|count` to protect an ingredient while its inventory count is at or below the specified threshold. A name without a count is effectively always protected. |
| `IngredientsToUnprotect` | empty | Comma-separated names to exempt from built-in and custom protection rules. |
| `StringTranslations` | `Alchemy,No potion recipes are currently available.` | Two comma-separated values: the alchemy-menu text and the no-recipe message. The menu text is matched as part of the incoming menu description. |
| `PotionPoison` | `Potion of,Poison of` | Two comma-separated prefixes: the beneficial potion prefix followed by the harmful poison prefix. |

Example:

```ini
[General]
IgnorePlayer=0
ProtectIngredients=1
Singlethreaded=0
MoreIngredientsToProtect=Jarrin Root,Daedra Heart|3,Blue Butterfly Wing|10
IngredientsToUnprotect=Deathbell
StringTranslations=Alchemy,No potion recipes are currently available.
PotionPoison=Potion of,Poison of
```

### Advanced diagnostics

`NumberOfIngredientsToStressTest` is normally `0`. A positive value runs a diagnostic calculation using that many ingredients from the game's global ingredient list. A value of `-1` logs known ingredient names. These modes are intended for troubleshooting, not normal gameplay.

String settings use commas as separators. Do not place commas inside an individual translated value or ingredient entry. Ingredient names are matched exactly, including spelling and punctuation. `IngredientsToUnprotect` takes precedence over protection rules.

## Troubleshooting

### The recommendation does not appear

- Confirm `alchemist.dll` is in the active profile's `SKSE/Plugins` directory.
- Confirm Skyrim was launched through SKSE.
- Confirm Address Library is installed and contains the version file for the installed runtime.
- Confirm at least two available, unprotected ingredients share an effect.
- If the game uses translated or modified menu text, update `StringTranslations`.
- Restart the game after changing `alchemist.ini`.

### The INI file is missing or changes do not apply

The file is created under the game's effective `Data/SKSE/Plugins` directory when the plugin starts. Exit Skyrim completely, verify the active MO2 profile, edit the file, and launch through SKSE again.

### The deployed DLL may be outdated

When using a mod manager, verify that the active profile contains the intended DLL. The plugin build output and the deployed file should have matching file hashes if you have access to both files.

### Logs

SKSE and plugin logs are normally under:

```text
%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\
```

Relevant files include:

```text
skse64_loader.log
skse64.log
alchemist.log
crash-*.log
```

For a load problem, confirm that `skse64.log` reports `alchemist.dll` loaded correctly and inspect `alchemist.log` for initialization messages. If the game crashes, install Crash Logger SSE AE VR and include the newest crash report when requesting help.

## Compatibility and limitations

- The plugin depends on SKSE, Address Library, Skyrim's Scaleform integration, and a supported game runtime.
- The calculation is an estimate based on Skyrim effect data and the configured player bonuses; it is not a guarantee for every modded effect or game setup.
- Protection and menu detection depend on configured names and text. Names and strings are case- and punctuation-sensitive where noted.
- Inventory quantity changes alone may not immediately invalidate the recommendation cache when the ingredient set remains unchanged.
- The plugin has no separate in-game configuration menu.
- There is no automatic crafting or ingredient consumption.

## Credits and license

Prosperous Alchemist is licensed under the [GNU General Public License version 3 or later](../COPYING), with the [Modding Exception and GPL-3.0 Linking Exception](../EXCEPTIONS.md). Third-party notices remain applicable to the files and libraries they cover.

Credits:

- [SKSE Team](https://skse.silverlock.org)
- [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG)
- [axxonite](https://www.nexusmods.com/skyrim/mods/38634)
