# Prosperous Alchemist

Prosperous Alchemist recommends the most valuable potion or poison that can be made from the ingredients currently available to the player. It considers ingredient effects and, when enabled, the player's Alchemy skill, perks, and Fortify Alchemy equipment.

The plugin recommends a recipe. It does not automatically craft potions, consume ingredients, alter game records, or change perks.

## Requirements

- Skyrim Special Edition, Anniversary Edition, or VR on Windows (see VR note below).
- Skyrim's native alchemy/crafting menu; SkyUI is optional and supported.
- SKSE matching the installed Skyrim runtime.
- Address Library for SKSE Plugins, including the version library matching the installed runtime.
- The game must be launched through SKSE.

> [!NOTE]
> **Skyrim VR Support**: In Skyrim VR, the Dear ImGui overlay renders to Skyrim's desktop companion/mirror window. To view the overlay inside the VR headset, use SteamVR Desktop View or overlay utilities like Desktop+.
>
> **UI Compatibility**: Prosperous Alchemist NG hooks Skyrim's native crafting engine (`RE::CraftingMenu` / `RE::CraftingSubMenus::AlchemyMenu`). It does not require or modify Scaleform SWF assets and works seamlessly with either vanilla UI or SkyUI.

The same plugin can support compatible runtimes through Address Library. Install the Address Library version file that matches your game runtime.

## Installation

### Mod Organizer 2

Install the mod with this structure:

```text
Prosperous Alchemist NG/
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

Prosperous Alchemist does not require an ESP or ESL or ship a replacement Scaleform SWF. It functions with Skyrim's native alchemy menu and SkyUI, rendering its separate overlay window with Dear ImGui.

## Using the plugin

1. Launch Skyrim through SKSE.
2. Open an Alchemy Table.
3. Review the automatically opened Prosperous Alchemist overlay.
4. Search or sort the recipe table and review each potion or poison's effects, value, and ingredients.
5. Close the ImGui window or the alchemy crafting menu when finished.

A typical result looks like this:

```text
Potion of Fortify Something: effect description(s)
 Value: 123
Ingredient A, Ingredient B, Ingredient C
```

Ingredient names are displayed alphabetically. The result is calculated from available, unprotected ingredients and is cached until the ingredient set, protected-ingredient result, tracked player state, or active CACO setting revision changes.

Developer Test Hub inventory, equipment, and modifier operations remain available while the alchemy menu is open. Each operation runs on Skyrim's task interface, updates the player's real state, recalculates the overlay, and refreshes the native alchemy submenu after the change. Inventory events raised during a hub task are coalesced so the operation's final refresh is used instead of an intermediate calculation. The hub does not capture or restore player state; use Skyrim's built-in save/load system to preserve or revert changes. Provisioned test fixtures remain available until explicitly removed. Comparison records and algorithm-matrix results are displayed in memory only; the plugin does not write Developer Test Hub or runtime diagnostic files.

The plugin identifies alchemy from the runtime-matched native `CraftingMenu` submenu during the menu lifecycle event. It fails closed when the native menu or active submenu is unavailable and does not patch SkyUI files.

The fake cursor and ImGui mouse input share one render-space coordinate. The plugin converts the cursor from the live game window and maps it to the current D3D11 render dimensions, so it does not require a particular monitor or game resolution. The cursor tip is anchored at the native SkyUI origin; no manual pixel offset is required. This overlay input path is separate from Skyrim's camera sensitivity and does not change native mouse behavior.

## Configuration

The plugin reads:

```text
Data/SKSE/Plugins/alchemist.ini
```

If the file does not exist, it is created with default settings when the plugin starts. All settings are in the `[General]` section. Restart the game after editing the file.

| Setting | Default | Description |
| --- | ---: | --- |
| `developer` | `0` | Set to `1` to show the Developer Test Hub button and enable the in-game test controls. Leave it at `0` for normal gameplay. |
| `IgnorePlayer` | `0` | Uses the player's Alchemy skill, perks, and worn Fortify Alchemy equipment. Set to `1` to ignore player alchemy state. |
| `ProtectIngredients` | `0` | Ingredient protection is disabled by default. Set to `1` to exclude the configured protected ingredients from recommendations. |
| `Singlethreaded` | `0` | Uses the multithreaded recipe evaluation path when set to `0`. Set to `1` to force the single-threaded path. |
| `NumberOfIngredientsToStressTest` | `0` | Normal operation when `0`. A positive value evaluates the first N loaded ingredient forms for diagnostics. Negative values behave like `0`; results are shown in the Developer Test Hub and are not written to disk. |
| `ProtectedIngredients` | default list | Comma-separated ingredient names, editor IDs, or hexadecimal FormIDs. Use `entry\|count` to keep that many copies protected. An entry without a count protects all copies. |
| `StringTranslations` | `Alchemy,No potion recipes are currently available.` | Two comma-separated values retained for configuration compatibility. The first value is not used for menu detection; only the second value is used as the no-recipe message. |
| `PotionPoison` | `Potion of,Poison of` | Two comma-separated prefixes: the beneficial potion prefix followed by the harmful poison prefix. |

Example:

```ini
[General]
developer=0
IgnorePlayer=0
ProtectIngredients=1
Singlethreaded=0
NumberOfIngredientsToStressTest=0
ProtectedIngredients=Jarrin Root,Daedra Heart|3,Blue Butterfly Wing|10
StringTranslations=Alchemy,No potion recipes are currently available.
PotionPoison=Potion of,Poison of
```

### In-game settings

Open the alchemy overlay and select **Settings** to change these options without editing the INI manually. Settings are grouped into calculation, ingredient protection, display text, and advanced diagnostics sections. **Use single-threaded calculation** is disabled by default (multithreaded evaluation is used); enable it to force single-threaded evaluation. Protection is disabled until **Protect ingredients** is checked. Use **Clear all protected ingredients** to remove every configured entry, or **Reset all settings** to restore the default list. Changes are saved automatically.

### Advanced diagnostics

`NumberOfIngredientsToStressTest` is normally `0`. A positive value replaces the inventory ingredient set for a diagnostic calculation using that many forms from the game's global ingredient list. Zero and negative values use the normal inventory calculation. Stress-test results, the exhaustive algorithm matrix, and comparison records are shown in the Developer Test Hub; they are not written to disk.

String settings use commas as separators. Do not place commas inside an individual translated value or ingredient entry. Leave the first `StringTranslations` value as `Alchemy`; it is a legacy compatibility field, not a localized detection setting. Ingredient names and editor IDs are matched case-insensitively; protected entries may also use hexadecimal FormIDs. Entries are managed through `ProtectedIngredients` or the in-game Settings page.

### Optional compatibility mods

- **Alchemy Plus:** When `AlchemyPlus.dll` and a readable `SKSE/Plugins/AlchemyPlus.json` are present, the plugin applies supported enabled potency-rounding and impure-cost settings to its prediction. The adapter does not install Alchemy Plus hooks or change game records.
- **Complete Alchemy & Cooking Overhaul (CACO):** The plugin resolves CACO's loaded records and duration settings at `kDataLoaded`. When the required live records are available, it uses CACO effect variants, settings, optional impure processing, Crucible exemplars, naming, and reweighting in its prediction. CACO Potion Handling remains controlled by CACO's own options; missing or incomplete records leave the adapter inactive rather than changing the non-CACO path.
- The two adapters are independent. When both are active, Automatic mode composes CACO's live calculation with Alchemy Plus's supported rounding and impure-cost behavior.

## Troubleshooting

### The recommendation does not appear

- Confirm `alchemist.dll` is in the active profile's `SKSE/Plugins` directory.
- Confirm Skyrim's native alchemy crafting submenu is available. If SkyUI is installed, confirm it is enabled in the active profile and that no other UI mod overrides its crafting-menu assets.
- Confirm Skyrim was launched through SKSE.
- Confirm Address Library is installed and contains the version file for the installed runtime.
- Confirm at least two available, unprotected ingredients share an effect.
- If the no-recipe label should be translated, update the second `StringTranslations` value. The first value is retained for compatibility and does not control menu detection.
- Restart the game after changing `alchemist.ini`.

If the window remains hidden, reopen the native alchemy menu and verify that its active alchemy submenu is available. The plugin intentionally fails closed when the native crafting menu or active alchemy submenu cannot be identified; it does not fall back to localized text or patch SkyUI files.

### The INI file is missing or changes do not apply

The file is created under the game's effective `Data/SKSE/Plugins` directory when the plugin starts. Exit Skyrim completely, verify the active MO2 profile, edit the file, and launch through SKSE again.

### The deployed DLL may be outdated

When using a mod manager, verify that the active profile contains the intended DLL. The plugin build output and the deployed file should have matching file hashes if you have access to both files.

### External diagnostics

SKSE and an installed Crash Logger are normally under:

```text
%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\
```

Relevant files include:

```text
skse64_loader.log
skse64.log
crash-*.log
```

Prosperous Alchemist does not create a runtime or Developer Test Hub log. For a load problem, confirm that `skse64.log` reports `alchemist.dll` loaded correctly. If the game crashes, reproduce the problem once and include the newest SKSE entries and the Crash Logger SSE AE VR report when requesting help.

## Compatibility and limitations

- The plugin depends on SKSE, Address Library, Skyrim's native crafting menu and D3D11 renderer, Dear ImGui linked into the plugin, and a supported game runtime. SkyUI is optional.
- The calculation is an estimate based on Skyrim effect data and the configured player bonuses; it is not a guarantee for every modded effect or game setup.
- Protection names and display prefixes use configured strings; protected ingredients also accept editor IDs and hexadecimal FormIDs. Menu detection uses the runtime-matched native crafting submenu and is not localized through `StringTranslations`.
- Inventory quantity changes alone may not invalidate the recommendation cache when the ingredient set and protected-ingredient result remain unchanged; player-state changes and CACO option revisions do invalidate it.
- `NumberOfIngredientsToStressTest` is a diagnostic feature. Positive values use the first N loaded ingredient forms; zero and negative values use the normal inventory calculation. Results are shown in the Developer Test Hub and are not written to disk.
- The Developer Test Hub is hidden unless `[General] developer=1`; it is a diagnostic tool that deliberately changes player state and inventory without automatically restoring them, and should not be enabled in a normal gameplay profile. Its comparison records remain in memory only.
- There is no automatic crafting or ingredient consumption.

## Credits and license

Prosperous Alchemist is licensed under the [GNU General Public License version 3 or later](../COPYING), with the [Modding Exception and GPL-3.0 Linking Exception](../EXCEPTIONS.md). Third-party notices remain applicable to the files and libraries they cover.

Credits:

- [SKSE Team](https://skse.silverlock.org)
- [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG)
- [axxonite](https://www.nexusmods.com/skyrim/mods/38634)
