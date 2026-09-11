# Prosperous Alchemist

This project is a from-scratch rewrite of the original Skyrim Legendary Edition SKSE plugin. The source code is available on [GitHub](https://github.com/FruitsBerriesMelons123/Prosperous-Alchemist-NG).

Prosperous Alchemist recommends the most valuable potion or poison that can be made from the ingredients currently available to the player. It considers ingredient effects and, when enabled, the player's Alchemy skill, perks, and Fortify Alchemy equipment.

*Note: Prosperous Alchemist only recommends recipes. It does not automatically craft potions, consume ingredients, alter game records, or change perks.*

## Features

- **Real-Time Calculations:** Evaluates available ingredients using a long-lived in-memory cache and incremental updates to find the highest-value recipe.
- **Smart Filtering and Protection:** Exclude rare ingredients such as Jarrin Root or Daedra Hearts, reserve specific quantities, or filter recipes by ingredients currently selected in Skyrim's native menu.
- **Modern UI:** Renders a Dear ImGui overlay that works alongside Skyrim's native crafting interface.
- **Broad Compatibility:** Supports Special Edition, Anniversary Edition, GOG, and Skyrim VR through the matching SKSE and Address Library runtime files.

## Requirements

- Skyrim Special Edition, Anniversary Edition, GOG, or VR on Windows (see the VR note below).
- [**SKSE64**](https://skse.silverlock.org/) matching the installed Skyrim runtime.
- [**Address Library for SKSE Plugins**](https://www.nexusmods.com/skyrimspecialedition/mods/32444) for SE, AE, and GOG, or [**VR Address Library for SKSE Plugins**](https://www.nexusmods.com/skyrimspecialedition/mods/58101) for Skyrim VR.
- Skyrim's native alchemy/crafting menu; SkyUI is optional and fully supported.
- The game must be launched through SKSE.

### Skyrim VR support

In Skyrim VR, the Dear ImGui overlay renders to Skyrim's desktop companion/mirror window. To view the overlay inside the VR headset, use SteamVR Desktop View or overlay utilities like Desktop+.

### UI compatibility

Prosperous Alchemist NG hooks Skyrim's native crafting engine (`RE::CraftingMenu` / `RE::CraftingSubMenus::AlchemyMenu`). It does not require or modify Scaleform SWF assets and works seamlessly with either vanilla UI or SkyUI.

The same plugin can support compatible runtimes through Address Library. Install the Address Library version file that matches your game runtime.

## Installation

### Mod Organizer 2 or Vortex

Install the archive through Mod Organizer 2 or Vortex as normal. Ensure the deployed plugin is located at `SKSE/Plugins/alchemist.dll` in the active profile or deployment.

### Mod Organizer 2 details

Install the mod with this structure:

```text
Prosperous Alchemist NG/
└── SKSE/
	└── Plugins/
		├── alchemist.dll
		├── alchemist.ini
		├── locales/
		│	└── alchemist.en.json
		└── fonts/
```

If you received only `alchemist.dll`, create or select a mod in MO2 and place it at:

```text
SKSE/Plugins/alchemist.dll
```

The optional `locales` and `fonts` directories go beside the DLL. The release archive includes example locale files and font instructions; font binaries are intentionally optional because system and third-party font licenses vary.

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
4. Search the recipe name, ingredients, or effects, choose a sort mode, and optionally enable the **Effects** column.
5. Select a recipe by clicking its row. Use the pagination controls when the filtered list spans multiple pages.
6. Close the ImGui window or the alchemy crafting menu when finished.

A typical result looks like this:

```text
Potion of Fortify Something: effect description(s)
 Value: 123
Ingredient A, Ingredient B, Ingredient C
```

Ingredient names are displayed alphabetically. Each recipe also retains the actual Skyrim ingredient forms behind its display names, so ingredients that share a name are not treated as interchangeable. The result is calculated from available, unprotected ingredient forms.

By default, the browser filters the displayed recipes to those containing every ingredient currently selected in Skyrim's native alchemy menu. With no ingredients selected, all calculated recipes are shown. Disable **Filter potions by selected ingredients** in Settings to show the complete calculated list regardless of the native menu selection. The filter only changes the browser view; it does not change calculations or consume ingredients.

Recipe calculations use a long-lived in-memory cache and update incrementally when new ingredient forms become available. Player-state changes can reevaluate cached recipes, and repeated changes are coalesced before background processing begins. Superseded work is cancelled. The cache is not saved to disk and is retained after closing the menu only for the configured cache duration.

During a calculation, the overlay shows the current phase and progress. A completion message is shown briefly when the list is ready. If a previous calculation was slow, the existing list may be marked as outdated while the plugin waits for a stable request; select **Recalculate** to request an immediate update.

Potion and poison names use the configured `PotionPoison` prefixes. When CACO renaming is active, its quality and secondary-effect text is retained while the configured type prefix is applied.

Developer Test Hub inventory, equipment, and modifier operations remain available while the alchemy menu is open. Each operation runs on Skyrim's task interface, updates the player's real state, recalculates the overlay, and refreshes the native alchemy submenu after the change. Inventory events raised during a hub task are coalesced so the operation's final refresh is used instead of an intermediate calculation. The hub does not capture or restore player state; use Skyrim's built-in save/load system to preserve or revert changes. Provisioned test fixtures remain available until explicitly removed. Comparison records and algorithm-matrix results are displayed in memory only; the plugin does not write Developer Test Hub or runtime diagnostic files.

The plugin identifies alchemy from the runtime-matched native `CraftingMenu` submenu during the menu lifecycle event. It fails closed when the native menu or active submenu is unavailable and does not patch SkyUI files.

The fake cursor and ImGui mouse input share one render-space coordinate. The plugin converts the cursor from the live game window and maps it to the current D3D11 render dimensions, so it does not require a particular monitor or game resolution. The cursor tip is anchored at the native SkyUI origin; no manual pixel offset is required. This overlay input path is separate from Skyrim's camera sensitivity and does not change native mouse behavior.

## Configuration

The plugin reads:

```text
Data/SKSE/Plugins/alchemist.ini
```

If the file does not exist, it is created with default settings when the plugin starts. Calculation settings are in `[General]`; language selection is in `[Localization]`. Restart the game after editing the file.

**Key settings:**

- **`developer` (`0` / `1`):** Set to `1` to show the Developer Test Hub button and enable the in-game test controls. Leave it at `0` for normal gameplay.
- **`IgnorePlayer` (`0` / `1`):** Uses the player's Alchemy skill, perks, and worn Fortify Alchemy equipment. Set to `1` to ignore player alchemy state.
- **`ProtectIngredients` (`0` / `1`):** Ingredient protection is disabled by default. Set to `1` to exclude the configured protected ingredients from recommendations.
- **`Singlethreaded` (`0` / `1`):** At `0`, recipe evaluation uses worker threads; at `1`, calculation runs single-threaded on the main thread.
- **`FilterPotionsBySelectedIngredients` (`0` / `1`):** Set to `1` to show only recipes containing every ingredient currently selected in Skyrim's native alchemy menu. With no ingredients selected, all calculated recipes are shown.
- **`CacheDurationSeconds`:** Keep the in-memory master recipe cache after closing the alchemy menu for this many seconds (default: `180`). Set to `0` to expire it immediately; no cache file is written.
- **`StaleRecalculateThresholdMs`:** Keep the current list visible as potentially outdated after a calculation longer than this threshold and show a manual **Recalculate** button (default: `500`). Set to `0` to disable this guard.
- **`CraftDebounceMs`:** Delay non-forced background recalculation after rapid crafting or inventory changes so repeated requests are coalesced (default: `400`).
- **`ProtectedIngredients`:** Comma-separated ingredient names, editor IDs, or hexadecimal FormIDs. Use `Name|Count` to reserve a specific quantity, such as `Daedra Heart|3`. An entry without a count protects all copies.
- **`PotionPoison`:** Two comma-separated prefixes applied to beneficial potion and harmful poison names (default: `Potion of,Poison of`). CACO quality and secondary-effect text remains when CACO renaming is active.
- **`Language`:** In `[Localization]`, select a BCP 47 locale such as `fr`, `de`, `zh-CN`, `ja`, or `ko`. Empty or `auto` follows the Windows user interface locale.

Example:

```ini
[General]
developer=0
IgnorePlayer=0
ProtectIngredients=1
Singlethreaded=0
FilterPotionsBySelectedIngredients=1
CacheDurationSeconds=180
StaleRecalculateThresholdMs=500
CraftDebounceMs=400
ProtectedIngredients=Jarrin Root,Daedra Heart|3,Blue Butterfly Wing|10
PotionPoison=Potion of,Poison of

[Localization]
Language=
```

### In-game settings

Open the alchemy overlay and select **Settings** to change these options without editing the INI manually. Settings are grouped into calculation, ingredient protection, and naming sections. The calculation section includes the selected-ingredient filter, cache duration, stale recalculation threshold, and craft debounce delay. **Use single-threaded calculation** uses multithreaded worker-thread evaluation by default; selecting it runs recipe evaluation on the main thread. Protection remains off until **Protect ingredients** is checked. Use **Clear all protected ingredients** to remove every configured entry, or **Reset all settings** to restore the default list. Changes are saved automatically.

### Localization and fonts

The overlay uses UTF-8 resources and supports the major Latin, Cyrillic, Greek, Hebrew, Arabic, Indic, Southeast Asian, Chinese, Japanese, and Korean writing systems when a matching font is available. The plugin first uses the Windows user interface locale, unless `[Localization] Language` in `alchemist.ini` specifies a BCP 47 tag such as `fr`, `de`, `zh-CN`, `zh-TW`, `ja`, or `ko`.

Custom translations are loaded at startup from `SKSE/Plugins/locales/alchemist.<tag>.json`. The exact tag is tried after its base language, with English and built-in English strings as the final fallback. Files must be UTF-8 JSON and may contain only the strings being changed. See `locales/README.md` in the release archive for the schema and named formatting placeholders.

Dear ImGui merges locale-declared font files from `SKSE/Plugins/fonts` with available Windows fonts. If a language's glyphs display as boxes, install a legally redistributable font such as the appropriate Noto family, copy it below `fonts`, and list its filename in the selected locale JSON. Missing resources are skipped gracefully and do not disable the overlay or its mouse cursor.

`ProtectedIngredients` and `PotionPoison` use commas as separators. Do not place commas inside an individual ingredient entry or prefix. Ingredient names and editor IDs are matched case-insensitively; protected entries may also use hexadecimal FormIDs. Entries are managed through `ProtectedIngredients` or the in-game Settings page.

### Optional compatibility mods

- **[Complete Alchemy & Cooking Overhaul (CACO)](https://www.nexusmods.com/skyrimspecialedition/mods/19924):** The plugin automatically detects CACO's loaded records and duration settings at `kDataLoaded`. When a calculation source and the live Skyrim alchemy settings are available, it uses CACO effect variants, mixed-effect handling, optional impure processing, Crucible exemplars, custom naming, and reweighting in its prediction. Missing optional lists, settings, or duration variants disable only the dependent behavior; a missing duration variant falls back to the source ingredient effect, while a missing active calculation source leaves the non-CACO path unchanged. CACO Potion Handling remains controlled by CACO's own options.
- **[Alchemy Plus](https://www.nexusmods.com/skyrimspecialedition/mods/80882):** When `AlchemyPlus.dll` and a readable `SKSE/Plugins/AlchemyPlus.json` are present, the plugin automatically applies enabled potency-rounding and impure-cost settings to its prediction. The adapter does not install Alchemy Plus hooks or change game records.
- **SkyUI / vanilla UI:** The plugin is fully compatible with both without requiring an ESP/ESL plugin or modified SWF files.
- The two adapters are independent. When both are active, Automatic mode composes CACO's live calculation with Alchemy Plus's supported rounding and impure-cost behavior.

## Troubleshooting

### The overlay or recommendation does not appear

- Confirm `alchemist.dll` is in the active profile's `SKSE/Plugins` directory.
- Confirm Skyrim's native alchemy crafting submenu is available. If SkyUI is installed, confirm it is enabled in the active profile and that no other UI mod overrides its crafting-menu assets.
- Confirm Skyrim was launched through SKSE.
- Confirm Address Library is installed and contains the version file for the installed runtime.
- Confirm at least two available, unprotected ingredients share an effect.
- Restart the game after changing `alchemist.ini`.

If the window remains hidden, reopen the native alchemy menu and verify that its active alchemy submenu is available. The plugin intentionally fails closed when the native crafting menu or active alchemy submenu cannot be identified; it does not depend on localization resources or patch SkyUI files.

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
- Protection names and display prefixes use configured strings; protected ingredients also accept editor IDs and hexadecimal FormIDs. Menu detection uses the runtime-matched native crafting submenu and is not controlled by display text.
- Inventory quantity changes alone may not invalidate the recommendation cache when the native ingredient forms and protected-ingredient result remain unchanged; player-state changes and CACO option revisions do invalidate it. The cache is in memory only and expires according to `CacheDurationSeconds`.
- The recipe browser's selected-ingredient filter compares native ingredient FormIDs, not display names, so duplicate-name ingredients remain distinct. If the native menu cannot expose its selected entries, the filter has no selected forms to apply and the complete calculated list remains visible.
- Values are estimates based on the available record data. CACO and Alchemy Plus compatibility improves prediction inputs and post-processing, but Skyrim's private potion-construction path is not invoked to preview every hypothetical recipe.
- The Developer Test Hub is hidden unless `[General] developer=1`; it is a diagnostic tool that deliberately changes player state and inventory without automatically restoring them, and should not be enabled in a normal gameplay profile. Its comparison records remain in memory only.
- There is no automatic crafting or ingredient consumption.

## Credits and license

Prosperous Alchemist is licensed under the [GNU General Public License version 3 or later](https://github.com/FruitsBerriesMelons123/Prosperous-Alchemist-NG/blob/main/COPYING), with the [Modding Exception and GPL-3.0 Linking Exception](https://github.com/FruitsBerriesMelons123/Prosperous-Alchemist-NG/blob/main/EXCEPTIONS.md). Third-party notices remain applicable to the files and libraries they cover.

Credits:

- [SKSE Team](https://skse.silverlock.org)
- [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG)
- [axxonite](https://www.nexusmods.com/skyrim/mods/38634)
