# Instructions

## Base Paths
See **user-paths.md** for paths for locations referenced in these instructions.

## How I Launch Skyrim
- I use Mod Organizer 2. It uses the profile listed in **user-paths.md**. Do not attempt to launch Skyrim yourself.

## Project Guidelines
- Before planning or coding read **problems.md** which should have implementation details on what has been attempted to be coded for specific issues in the past. This file should always be kept up-to-date with the latest changes and kept organized in the most recommended way for its intended use. It should be updated after planning but before coding, and then once again after finishing coding. New notes should be added at the top of the file and should include full timestamps. When performing analysis **problems.md** must be updated immediately after finding any valuable information.
- Check **updates.md** and if it has been a full day since git submodules were updated, perform an update, including nested submodules, to their latest remote commits and record the update check.
- Do not put source files in the root directory. Prefer to put them in the alchemist folder or make a new appropriately named folder if that is more appropriate.
- Do not remove any mod functionality. If this you ever think this is a good idea, write your reason in **requests.md** and then move on to something else.
- Do not defer or disable Developer Test Hub operations while the alchemy or crafting menu is open as a crash workaround; this workflow must remain functional, and the underlying crash must be fixed instead.
- Do not remove the mouse cursor functionality for the imgui window.
- If multiple options are considered for a task, list all of them in **problems.md** even the ones that weren't ultimately chosen.
- The mod should accurately predict potion costs in vanilla Skyrim, and also with either Alchemy Plus or CACO or both enabled.
- The mod should be localization friendly where possible.
- Do not hardcode things unless absolutely necessary. Hardcoded things should be noted in the readme and why it was necessary to be hardcoded.

## Mod Version Changes (Explicit User Authorization Required)
- Do not update any mod-version value, version-bearing filename, generated version metadata, built artifact, deployed artifact, or release archive listed in this section unless the user specifically tells you to perform that mod-version update. An audit or documentation request is not authorization to change the version.
- For any explicitly authorized mod-version update, apply the user-supplied target consistently to these direct project-owned locations:
  - **`alchemist/include/version.h`**: update the `MYFP_VERSION_MAJOR`, `MYFP_VERSION_MINOR`, `MYFP_VERSION_PATCH`, and `MYFP_VERSION_BETA` components as required by the target release. `MYFP_VERSION_VERSTRING` is derived from those macros and should not be replaced with a separate literal. The public release version is three-part; the Windows/plugin metadata uses the corresponding four-part version, normally with a zero beta component.
  - **`alchemist/CMakeLists.txt`**: update both the `project(... VERSION ...)` declaration and the `add_commonlibsse_plugin(... VERSION ...)` argument to the same four-part target version.
- The following locations are propagated or regenerated consequences of an authorized source update; do not hand-edit them:
  - **`alchemist/version.rc`**: no numeric version is stored here; `FILEVERSION`, `PRODUCTVERSION`, `FileVersion`, and `ProductVersion` consume `version.h` macros automatically and should only be verified.
  - **`build-alchemist/CMakeCache.txt`**: CMake regenerates `CMAKE_PROJECT_VERSION` and its major/minor/patch/tweak entries from `alchemist/CMakeLists.txt`.
  - **`build-alchemist/__alchemistPlugin.cpp`**: the CommonLibSSE-NG CMake helper regenerates the `SKSEPluginInfo` `REL::Version` from the plugin `VERSION` argument.
  - **`build-alchemist/alchemist.dll`**: rebuild to embed the new plugin and Windows file/product metadata; never patch the binary directly.
  - **The deployed `alchemist.dll` named by `DLL_DEPLOY` in the local `user-paths.md`/`config.py` settings**: redeploy the freshly built DLL after an authorized build; the path files contain machine-specific destinations, not version values.
  - **`dist/Prosperous-Alchemist-NG-v<target-public-version>.zip`**: generate a new archive with `python build.py --package`; do not rename or edit an existing archive in place.
- No direct version update is required in `alchemist/version.rc`, `docs/USER_README.md`, either `alchemist.ini`, or the local path/configuration files; they were audited and contain no independent mod-version value.
- Do not change version-looking values in the `alandtse-CommonLibSSE-NG` dependency or its nested OpenVR checkout, `.git` metadata, license text, generated dependency tree under `build-alchemist/_deps`, CMake's minimum version, ImGui's tag, or unrelated numeric literals. Those values are not the Prosperous Alchemist mod version.
- After an explicitly authorized bump, keep the direct locations consistent, reconfigure/build to regenerate the dependent locations, package and deploy as required, and verify the resulting version metadata. Do not perform any of those version changes for an audit-only request.

## SKSE
- Source code location listed in **user-paths.md**.

## SkyUI
- Source code location listed in **user-paths.md**.

## SSEEdit
- Executuble location listed in **user-paths.md**.

## Alchemy Plus
- Source code location listed in **user-paths.md**.

## Complete Alchemy and Cooking Overhaul (CACO)
- Source code location listed in **user-paths.md**.
- See also **caco.md**.

## Ingredients
### ingredient_name,form_id,editor_id,effect_name,effect_form_id,base_cost,magnitude,duration,power_affects_magnitude,power_affects_duration,no_magnitude,no_duration,beneficial,harmful,hostile
- See **ingredients-vanilla.csv** for the full ingredient list used in vanilla skyrim.
- See **ingredients-caco.csv** for the full ingredient list used in CACO.

## Potions
These need to be regenerated manually by the user if the skse plugin algorithm is modified.
### Predicted with no perks and alchemy level at 15
- See **potions-predicted-vanilla.csv** for the full potion list as predicted in vanilla skyrim.
- See **potions-predicted-caco.csv** for the full potion list as predicted with CACO enabled and Alchemy Plus disabled.
- See **potions-predicted-ap.csv** for the full potion list as predicted with CACO disabled and Alchemy Plus enabled.
- See **potions-predicted-caco-ap.csv** for the full potion list as predicted with CACO enabled and Alchemy Plus enabled.

## Temp Python Scripts
- Create temporary python scripts in **temp** to perform these specific tasks instead of doing them yourself. They should all have verbose terminal output describing their current action and progress.
### When inspecting live record payloads
### When reading installed plugin sizes
### When performing bounded inspections

## Debugging
- When errors are found after running Skyrim, check logs in location listed in **user-paths.md**.
- Inspect that exact SKSE directory directly; do not probe base drive paths or another broad parent path first.

## Logs
- The only logs that are allowed to be read are the log paths listed in **user-paths.md**.. If additional log directories or log files are wanted, put your request for those files to be allowed in **requests.md** and then move on to something else.

## Building
- Appropriately create or update **build.py** to perform a clean build of the project (but do not clean or rebuild CommonLibSSE unnecessarily). The python script should use flushed writes to write its output to **build.log**. The AI can then monitor this file for checking build progress. The **build.log** file should be blanked before each new build.
- Never invoke the top-level CMake `clean` target from **build.py**; it removes CommonLibSSE-NG outputs. Use a plugin-only cleanup that preserves the CommonLibSSE-NG build artifacts.
- User expects the built plugin DLL to be deployed to the DLL Deploy location listed in **user-paths.md** and considers the task incomplete unless that file is updated.
- Record the build start time immediately before invoking the intended build, run that build once, and verify that the built DLL is newer than the recorded time. If the command succeeds but the DLL is older, treat it as a stale/incremental-build failure and perform at most one clean rebuild; do not start another rebuild solely to capture or display the timestamp after a valid artifact has been produced.
- Verify that the deployed DLL has the same timestamp and contents as the built DLL, and provide both timestamps in the response. If deployment is blocked by Skyrim, close or terminate SkyrimSE.exe before retrying.
- If deployment is blocked because Skyrim is running, terminating SkyrimSE.exe is authorized so the built plugin DLL can be deployed and verified.

## Example files
Should be kept up-to-date with their base file.
**config.example.py** is the example file for **config.py**.
**user-paths.example.md** is the example file for **user-paths.md**.

## README
- All project copilot instructions should be reflected generically in the project readme.
