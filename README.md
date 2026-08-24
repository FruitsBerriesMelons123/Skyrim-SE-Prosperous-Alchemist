# Prosperous Alchemist

Prosperous Alchemist is an SKSE plugin that calculates the most valuable potion or poison currently craftable from the player's ingredients. It accounts for the alchemy skill, relevant perks, worn Fortify Alchemy equipment, ingredient effects, and optional ingredient protection rules.

![Prosperous Alchemist preview](Prosperous%20Alchemist%20AE%20GOG%20SE%20VR.png)

> This repository contains the native plugin source and build configuration. It is not a packaged installer or a complete mod archive.

For installation and gameplay instructions, see the [user-facing guide](docs/USER_README.md).

## Current build state

- The native plugin is an **Address Library** plugin built with CommonLibSSE-NG.
- The current build was tested with Skyrim runtime **1.7.99** and SKSE **2.3.0**. These are validation versions, not fixed plugin requirements.
- The Release DLL is self-contained with respect to the third-party C++ libraries used by the project. It does not require `fmt.dll`, `spdlog.dll`, or the dynamic MSVC runtime beside the plugin.
- SKSE, Address Library for SKSE Plugins, and a matching Skyrim runtime are still required. Address Library is not part of this repository or embedded in `alchemist.dll`.
- The current plugin package consists of the native DLL; the plugin creates an optional `alchemist.ini` with defaults when it starts. It does not require an ESP/ESL.

## Repository checkout

CommonLibSSE-NG is included as the `alandtse-CommonLibSSE-NG` Git submodule. CommonLibSSE-NG itself uses OpenVR as a nested submodule, so both levels must be initialized before configuring the build.

For a fresh checkout, initialize all submodules during cloning:

```powershell
git clone --recurse-submodules <repository-url>
cd prosperous-alchemist
```

If the repository was cloned without `--recurse-submodules`, run this from the repository root:

```powershell
git submodule update --init --recursive
```

The expected submodule layout is:

```text
<repository root>/
├── ProsperousAlchemist.slnx  # tracked Visual Studio entry point
├── ProsperousAlchemistBootstrap.vcxproj  # tracked Visual Studio/CMake build bridge
├── alchemist/
├── alandtse-CommonLibSSE-NG/
│   └── extern/openvr/
└── build-alchemist/       # generated and ignored
```

## Contents

- [Features](#features)
- [Repository checkout](#repository-checkout)
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

The plugin reads this file when it loads if it exists:

```text
Data/SKSE/Plugins/alchemist.ini
```

If the file is missing, the plugin creates it with the default settings during startup. All settings are in the `[General]` section. Restart the game after editing the file so the plugin loads the new values.

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
- A supported Skyrim SE/AE/VR game runtime.
- SKSE matching the installed game runtime. The plugin does not require exactly SKSE **2.3.0**; that is the version used to build and test this project.
- Address Library for SKSE Plugins, including the version library matching the installed runtime. For the tested runtime, the file is `versionlib-1-7-99-0.bin`.
- The game must be launched through SKSE.

The generated plugin metadata declares Address Library runtime independence. This allows the same DLL to use the Address Library database for supported Skyrim runtimes instead of requiring a separate fixed-address build. It does not make the DLL independent of Address Library or SKSE. Use the Address Library version file matching the installed runtime; `1.7.99` and `2.3.0` are the versions validated for this project, not mandatory versions.

### Installing a compiled plugin

1. Obtain the Release `alchemist.dll` built with Address Library support.
2. If using Mod Organizer 2, create or enable a mod with this archive structure:

```text
Prosperous Alchemist AE/
└── SKSE/
    └── Plugins/
        └── alchemist.dll
```

A typical MO2 mod source directory is:

```text
<MO2 instance>/mods/Prosperous Alchemist AE/SKSE/Plugins/alchemist.dll
```

3. For a direct installation, copy the DLL to:

   ```text
   <Skyrim installation>/Data/SKSE/Plugins/alchemist.dll
   ```

4. Enable the mod in MO2 and launch Skyrim through SKSE.
5. Open the alchemy interface. The plugin uses built-in defaults when `alchemist.ini` is absent.
6. Edit the INI file as needed and restart the game after making changes.

This repository does not currently include a release archive, installer, ESP/ESL, or tracked compiled DLL. The native plugin integrates with the existing alchemy interface; no separate UI plugin is included here.

## Building from source

The current plugin build is the standalone CMake project at [`alchemist`](alchemist). CommonLibSSE-NG is included as the `alandtse-CommonLibSSE-NG` Git submodule and must be configured against a static vcpkg triplet so the resulting Release DLL has no `fmt.dll` or `spdlog.dll` dependency.

The root solution exposes the `Release|x64` build. The CMake project also restricts generated multi-configuration Visual Studio projects to `Release`; the generated projects under `build-alchemist` should not be edited directly.

### Build prerequisites

- Visual Studio with the **Desktop development with C++** workload, an x64 toolchain, and a Windows SDK.
- CMake 3.21 or later. Visual Studio's bundled CMake is supported by the root bootstrap project.
- The CommonLibSSE-NG submodule and its nested OpenVR submodule, initialized recursively.
- vcpkg available on `PATH` and the dependencies from the CommonLibSSE-NG manifest.
- The `x64-windows-static` vcpkg triplet.

### CMake build

If the repository was not cloned recursively, initialize the CommonLibSSE-NG and its nested dependencies:

```powershell
git submodule update --init --recursive
```

Install the CommonLibSSE-NG manifest dependencies into a static triplet. Replace the paths and generator name with the values for the local machine:

```powershell
vcpkg install `
  --x-manifest-root=alandtse-CommonLibSSE-NG `
  --x-install-root=<vcpkg install root> `
  --triplet=x64-windows-static `
  --feature-flags=manifests
```

Configure and build the plugin:

```powershell
cmake -S alchemist -B build-alchemist `
  -G "Visual Studio 18 2026" -A x64 `
  -DVCPKG_STATIC_DIR=<vcpkg install root>/x64-windows-static

cmake --build build-alchemist --config Release --target alchemist
```

The resulting DLL is:

```text
build-alchemist/Release/alchemist.dll
```

The native build supports only the `Release` configuration. The `x64` platform is required by the Visual Studio generator and the plugin toolchain.

### CMake path settings

The project intentionally keeps machine-specific paths out of the source tree:

| Variable | Required | Behavior |
| --- | --- | --- |
| `COMMONLIBSSE_DIR` | No | Defaults to `../alandtse-CommonLibSSE-NG` relative to `alchemist/CMakeLists.txt`. Override it only when the submodule is stored elsewhere. |
| `VCPKG_STATIC_DIR` | Yes | Must point to a static vcpkg installation containing `share/spdlog/spdlogConfig.cmake`. |
| `CMAKE_PREFIX_PATH` | Usually no | Automatically set to `VCPKG_STATIC_DIR`; specify it explicitly only when packages are located elsewhere. |
| `CMAKE_CONFIGURATION_TYPES` | No | Forced to `Release` for Visual Studio multi-configuration generators. |
| `ALCHEMIST_DEPLOY_DIR` | No | When set, adds a post-build step that creates the directory and copies the built `alchemist.dll` there. The root Visual Studio bootstrap also performs this deployment when its local `AlchemistDeployDir` property is set. When omitted, the DLL remains only in the build output directory. |

`COMMONLIBSSE_DIR` is resolved from the repository-relative default automatically. A clone with initialized submodules therefore does not need to provide that path. `VCPKG_STATIC_DIR` is different: it is a local dependency installation and has no portable repository default, so configuration deliberately fails until it is supplied.

To enable automatic deployment to a Mod Organizer 2 profile, add the optional variable during configuration. Set it to the `Plugins` directory, not to the DLL filename:

```powershell
cmake -S alchemist -B build-alchemist `
  -G "Visual Studio 18 2026" -A x64 `
  -DVCPKG_STATIC_DIR=<vcpkg install root>/x64-windows-static `
  -DALCHEMIST_DEPLOY_DIR=<MO2 instance>/mods/Prosperous Alchemist AE/SKSE/Plugins
```

The post-build step overwrites the deployed DLL after a successful `alchemist` build. The deployment setting is stored in the local CMake cache and is not part of the portable source configuration. If switching from the old dynamic build, delete `CMakeCache.txt` and `CMakeFiles` from the build directory before configuring again. Otherwise CMake may retain the old dynamic `spdlog_DIR` and `fmt_DIR` values.

### Visual Studio

Before the first build, configure the static vcpkg installation used by the CMake build. You can set the `VCPKG_STATIC_DIR` environment variable, pass the MSBuild property `VcpkgStaticDir`, or add the property to the local `ProsperousAlchemistBootstrap.vcxproj.user` file.

Deployment is configured per checkout. The tracked post-build event is defined in `alchemist/CMakeLists.txt`; it copies the built DLL when `ALCHEMIST_DEPLOY_DIR` is set. For Visual Studio, the ignored `ProsperousAlchemistBootstrap.vcxproj.user` file can set both the local `VcpkgStaticDir` property and the `AlchemistDeployDir` property for the desired `SKSE/Plugins` directory. The bootstrap project passes both values to CMake. For command-line CMake builds, set `VCPKG_STATIC_DIR` and `ALCHEMIST_DEPLOY_DIR` during configuration. Because the `.user` file and these paths contain machine-specific settings and are not committed, each clone must configure its own dependency and deployment paths.

A local Visual Studio user file can contain:

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="Current" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup>
	<VcpkgStaticDir>&lt;vcpkg install root&gt;\x64-windows-static</VcpkgStaticDir>
	<AlchemistDeployDir>&lt;MO2 instance&gt;\mods\Prosperous Alchemist AE\SKSE\Plugins</AlchemistDeployDir>
  </PropertyGroup>
</Project>
```

Save it beside `ProsperousAlchemistBootstrap.vcxproj` as `ProsperousAlchemistBootstrap.vcxproj.user`. The `.user` extension is ignored intentionally and should not be committed.

Open ProsperousAlchemist.slnx from the repository root, select Release and x64, and build the ProsperousAlchemistBootstrap project. The bootstrap project is the tracked root solution entry point: it configures the CMake project in build-alchemist and invokes the alchemist target. It is designed to work before any generated CMake project files exist.

The project currently uses the v145 platform toolset and C++23. The CommonLibSSE-NG, static vcpkg, and optional deployment paths are CMake cache settings; do not edit generated vcxproj files to change them. Reconfigure CMake instead.

### Generated build files

build-alchemist contains generated CMake and Visual Studio files and is ignored by Git. ProsperousAlchemist.slnx is the canonical tracked solution entry point and remains the stable entry point for a fresh clone; it does not require generated project files to be committed. Do not edit generated vcxproj files or commit CMakeCache.txt; CMake resolves repository-relative paths into absolute paths inside the build directory as part of normal generation. If the cache points to an old checkout, reconfigure from the active repository root or remove the build directory first. Close Visual Studio before deleting a build directory if its .vs database files are locked.

The root `x64` directory is Visual Studio intermediate/output state for the bootstrap project. `Prospero.4C7C8D61` contains MSBuild recipes, logs, and tracking files for that project. Both directories are generated and can be deleted while Visual Studio is closed; they will be recreated by the next build. They do not contain source code or the CMake build cache.

To reset the native build completely, close Visual Studio and delete `build-alchemist`. This removes the CMake cache, generated Visual Studio projects, dependency build state, and compiled outputs. The next build from `ProsperousAlchemist.slnx` will configure the project again, but it requires the recursive submodules and the `VCPKG_STATIC_DIR` setting described above. A complete rebuild can take significantly longer than an incremental build.

After the root bootstrap project has configured the build directory, the generated build-alchemist/alchemist.slnx can optionally be opened for direct work with the generated alchemist target. It is not required for normal builds, and the root solution remains the stable entry point for a fresh clone.

For a direct CMake build, you can prevent Visual Studio from automatically checking for CMake regeneration by configuring with the CMake option CMAKE_SUPPRESS_REGENERATION set to ON. With this option, rerun CMake manually after changing CMakeLists.txt, source lists, dependencies, paths, or the generator/toolchain. The root bootstrap project normally performs CMake configuration automatically.

## Troubleshooting

The following reference covers the expected runtime files, log locations, diagnostic procedure, and the load/crash problems resolved in the current source.

### Runtime log locations

The logs are normally written under the Windows user's Documents folder, not the Skyrim installation folder:

```text
%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\
```

The files of interest are:

```text
skse64_loader.log  # SKSE loader and Skyrim executable version
skse64.log         # SKSE plugin discovery and load status
alchemist.log      # Prosperous Alchemist initialization messages
crash-*.log        # Crash Logger SSE AE VR reports, when installed
```

Use the equivalent paths for the local installation and mod manager:

| Purpose | Path |
| --- | --- |
| Repository root | `<repository root>/` |
| Release build | `<repository root>/build-alchemist/Release/alchemist.dll` |
| MO2 mod source | `<MO2 instance>/mods/Prosperous Alchemist AE/SKSE/Plugins/alchemist.dll` |
| Skyrim installation | `<Skyrim installation>/` |
| Address Library mod source | `<MO2 instance>/mods/Address Library for SKSE Plugins/SKSE/Plugins/` |
| Runtime logs | `%USERPROFILE%/Documents/My Games/Skyrim Special Edition/SKSE/` |

Mod Organizer 2 may virtualize the game's `Data` directory and redirect saves, but the SKSE logs still identify the effective plugin directory and should be checked after each test run.

### Basic diagnostic procedure

1. Exit Skyrim completely.
2. Delete or rename the old `alchemist.log` so the next test produces an unambiguous log.
3. Launch the game through SKSE from the active MO2 profile.
4. Confirm `skse64.log` contains `plugin alchemist.dll ... loaded correctly`.
5. Confirm `alchemist.log` reaches `prosperous alchemist initialized!`.
6. Reproduce the problem once, then close the game and inspect the newest `alchemist.log` and `crash-*.log`.
7. Include the Skyrim runtime, SKSE version, plugin version, and the complete crash log when reporting a problem.

When using MO2, compare the built and deployed DLLs if there is any doubt that the active mod contains the latest build:

```powershell
(Get-FileHash <build path> -Algorithm SHA256).Hash
(Get-FileHash <MO2 mod path> -Algorithm SHA256).Hash
```

The hashes must match.

The current build was validated with Skyrim runtime `1.7.99` and SKSE `2.3.0`. Other supported runtimes should show their own runtime version in `skse64_loader.log`. The expected successful plugin messages are:

```text
alchemist v1-0-0-0
[MESSAGE] Initializing prosperous alchemist...
[MESSAGE] prosperous alchemist Scaleform handlers registered
...prosperous alchemist initialized!
```

### CMake cannot find CommonLibSSE-NG

The repository-relative default expects the CommonLibSSE-NG submodule at `alandtse-CommonLibSSE-NG/`. From the repository root, initialize the complete submodule tree and configure again:

```powershell
git submodule update --init --recursive
```

If the build reports that `openvr.h` is missing from `BSVRInterface.h`, the nested OpenVR submodule has not been initialized. The recursive command above supplies the required headers and library.

### CMake rejects the vcpkg path

`VCPKG_STATIC_DIR` must point to a static vcpkg installation containing `share/spdlog/spdlogConfig.cmake`. A CommonLibSSE-NG checkout alone is not sufficient. Install the CommonLibSSE-NG manifest using the `x64-windows-static` triplet, then configure with that installation path. Do not use the old dynamic triplet or copy development DLLs beside the plugin.

### The post-build DLL copy does not occur

The CMake-side copy is disabled unless `ALCHEMIST_DEPLOY_DIR` is set in the CMake cache. For a Visual Studio build from the root solution, set `AlchemistDeployDir` in the ignored `ProsperousAlchemistBootstrap.vcxproj.user` file. For a direct CMake build, configure `ALCHEMIST_DEPLOY_DIR` as the destination `Plugins` directory, then rebuild the `alchemist` target:

```powershell
cmake -S alchemist -B build-alchemist `
	-DVCPKG_STATIC_DIR=<vcpkg install root>/x64-windows-static `
  -DALCHEMIST_DEPLOY_DIR=<MO2 instance>/mods/Prosperous Alchemist AE/SKSE/Plugins
cmake --build build-alchemist --config Release --target alchemist
```

If the destination contains an older DLL, compare it with the build output:

```powershell
(Get-FileHash build-alchemist/Release/alchemist.dll -Algorithm SHA256).Hash
(Get-FileHash '<MO2 instance>/mods/Prosperous Alchemist AE/SKSE/Plugins/alchemist.dll' -Algorithm SHA256).Hash
```

The hashes should match. Mod Organizer 2 must have the target profile enabled when Skyrim is launched.

### DLL load error `0000007E`

Windows error `0000007E` is `ERROR_MOD_NOT_FOUND`. It means Windows could not load the plugin or one of its imported DLLs. Inspect the Release artifact with:

```powershell
dumpbin /DEPENDENTS build-alchemist/Release/alchemist.dll
```

The current Release build statically links `fmt`, `spdlog`, and the MSVC runtime, so `fmt.dll`, `spdlog.dll`, `MSVCP140.dll`, and `VCRUNTIME140*.dll` should not be required in `Data/SKSE/Plugins`. If any of those third-party libraries appear in the dependency list, the build used the old dynamic vcpkg configuration. Reconfigure from a clean CMake cache using the `x64-windows-static` triplet; do not copy development DLLs into the mod.

### `disabled, fatal error occurred while loading plugin`

This message means the DLL loaded but an exception occurred inside `SKSEPluginLoad`. Check `alchemist.log` to determine how far initialization got. The current source passes an empty string, rather than `nullptr`, as the optional REX INI user path. Passing `nullptr` to the `std::string_view`-based setting store caused the earlier load-time crash.

### Crash when opening the alchemy interface

Install Crash Logger SSE AE VR and reproduce the crash. The report should be taken from:

```text
%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\crash-*.log
```

The Scaleform callback receives its arguments through `RE::GFxFunctionHandler::Params`. `Params::args` points directly to the first `GFxValue`; it is not an ActionScript array. The current source correctly reads `a_params.args[0]`. Calling `a_params.args->GetElement(0, ...)` caused the previous alchemy-lab crash inside Skyrim's Scaleform code.

### No recommendation appears

- Confirm that `alchemist.dll` is in `Data/SKSE/Plugins`.
- Confirm that the game was launched through SKSE.
- Confirm that the installed game runtime is supported by the current Address Library installation. This build was validated with runtime 1.7.99.
- Confirm that Address Library provides the version file matching the installed runtime through the active MO2 profile. The tested runtime uses `versionlib-1-7-99-0.bin`.
- Open the log at:

  ```text
  Documents/My Games/Skyrim Special Edition/SKSE/alchemist.log
  ```

- Confirm that `StringTranslations` begins with the text used by the installed alchemy interface.
- Make sure at least two available, unprotected ingredients share an effect.

### The INI file is not created

The plugin creates `Data/SKSE/Plugins/alchemist.ini` with its default settings when the file is missing. If an existing INI is ignored, confirm its section and setting names, check `alchemist.log`, and restart the game after editing it.

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

The generated SKSE metadata declares `SKSE::VersionIndependence::AddressLibrary`. Address Library supplies the runtime-specific addresses required by CommonLibSSE-NG, allowing the plugin to use the supported runtime database rather than embedding a separate fixed-address build. This declaration does not remove the requirement to install Address Library.

The native plugin registers these Scaleform functions under the `alchemist` movie name:

| Function | Behavior |
| --- | --- |
| `GetBestRecipeName` | Accepts the alchemy craft-description string, performs the recommendation calculation when the alchemy-menu text matches, and returns the formatted result string. |
| `GetBestRecipeDescription` | Registered for the interface, but currently returns an empty string. |

The plugin also registers `SKSEPlugin_Query`, `SKSEPlugin_Load`, and `SKSEPlugin_Version`, writes diagnostic output through the SKSE log system, and listens for the SKSE input-loaded message before reporting initialization.

## Current limitations

- The branch was built and tested against Skyrim runtime 1.7.99. Address Library provides the runtime compatibility layer for supported runtimes, but the matching version library must be installed and the underlying CommonLibSSE-NG/runtime combination must be supported.
- The recommendation cache compares ingredient names and tracked player state. Changing only an inventory quantity may not immediately force a new search if the ingredient set remains the same.
- Localization values support exactly two comma-separated fields per setting and do not support commas inside a translated value.
- Protection and menu detection use exact English/source strings unless overridden through the INI settings.
- `NumberOfIngredientsToStressTest` and the `-1` ingredient logger are diagnostic features, not normal gameplay settings.
- The plugin depends on SKSE, Address Library, Skyrim's Scaleform integration, and the installed runtime; the native DLL alone does not provide a standalone user interface.
- The repository currently has no automated plugin or calculation tests; a successful Release build validates compilation and linking, not in-game behavior.

## License and credits

Prosperous Alchemist is released under the [GNU General Public License version 3 or later](COPYING), with the [Modding Exception and GPL-3.0 Linking Exception](EXCEPTIONS.md).

This licensing change reflects the current build: `alchemist.dll` statically links CommonLibSSE-NG. CommonLibSSE-NG is licensed under GPL-3.0-or-later with the same exceptions, and its upstream README states that a plugin which statically links it must itself use GPL-3.0-or-later or a GPL-compatible license. The exceptions cover interoperation with Skyrim and permitted modding libraries; they do not make the plugin's own code MIT-licensed or remove the corresponding-source obligations for distributed combined works.

For this project, the exception applies to the intended interoperation with:

- **Modded Code:** Skyrim Special Edition, Anniversary Edition, and Virtual Reality.
- **Modding Libraries:** SKSE, Windows, and other libraries permitted under the applicable upstream terms.

The previous Prosperous Alchemist MIT license is preserved in [`licenses/PROSPEROUS-ALCHEMIST-LICENSE-MIT`](licenses/PROSPEROUS-ALCHEMIST-LICENSE-MIT). The historical MIT notice for code on which CommonLibSSE-NG was originally based is preserved in [`licenses/CommonLibSSE-NG-LICENSE-MIT`](licenses/CommonLibSSE-NG-LICENSE-MIT).

The repository also contains historical SKSE source trees and external/development files with their own upstream notices. Those notices remain applicable to the files they cover; the project license does not relicense third-party code.

Special thanks to:

- [SKSE Team](https://skse.silverlock.org)
- [axxonite](https://www.nexusmods.com/skyrim/mods/38634)
- [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG)
