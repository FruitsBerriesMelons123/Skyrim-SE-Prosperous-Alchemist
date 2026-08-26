# CACO Potion Costs, Values, and Prosperous Alchemist Compatibility

## Combined Alchemy Plus and CACO evaluation

The production evaluator supports CACO and Alchemy Plus independently and together. When both adapters report active settings, Automatic mode uses CACO's live records and game settings first, applies Alchemy Plus's configured magnitude/duration rounding to the constructed effect input, and uses that post-rounding contribution for shared-effect priority, effect ordering, and the pre-adjustment gold total. Alchemy Plus's optional signed impure-cost correction is applied before CACO's optional truncating 20% impure-potion adjustment. This composition preserves the four supported modes: vanilla, Alchemy Plus only, CACO only, and both adapters.

This document details the potion-cost and potion-value mechanics in the Complete Alchemy and Cooking Overhaul (CACO) Papyrus source scripts, analyzes the calculation divergence between Skyrim's engine auto-calc and CACO's post-processing pipelines, and records how the implemented Prosperous Alchemist adapter models those behaviors. `C_pre` denotes the engine-generated gold value of a newly crafted potion prior to script adjustments, while `C_caco` represents the final integer value assigned after CACO's manual post-processing.

## Scope and Baseline Assumptions

* **Historical baseline:** The original discrepancy was evaluated against the vanilla Prosperous Alchemist path; the current production evaluator includes the CACO adapter described below.
* **Third-Party Mods:** The adapter supports CACO and Alchemy Plus independently and in combination; the historical source comparison below isolates CACO behavior where noted.
* **Historical problem statement:** Earlier builds miscalculated CACO potion values—such as predicting 5,463 gold for a Jarrin Root, Nordic Barnacle, and Salmon Roe mixture that actually crafts for 93 gold—because they relied on vanilla cost formulas, ignored CACO's base-record rebalances, and omitted CACO's post-creation Papyrus logic. The current adapter addresses the supported live-record and post-processing portions without mutating the active menu.

## Key Technical Requirements for Compatibility

* **Impure Potion Penalty:** Detect conflicting `MagicAlchBeneficial` and `MagicAlchHarmful` keywords on the resulting potion and apply CACO's 80% value reduction formula: $C_{caco} = \text{int}(C_{pre} \times 0.2)$.
* **Dynamic Record Ingestion:** Read live `MagicEffect` base costs (flattened by CACO from vanilla's 3–285 range down to 35–95) and rebalanced ingredient magnitudes directly from the loaded plugin data rather than using vanilla fallback tables.
* **Duration Variant Resolution:** Interrogate CACO's duration globals (`CACO_RestoreIngH1st`, etc.) to select the active 1-second, 5-second, or 10-second `MagicEffect` form list entry feeding into the auto-calc engine.
* **Crucible Exemplar Mapping:** Intercept single-effect pure potion combinations and substitute the engine calculation with the static gold value of the corresponding CACO exemplar form (`Potion0` through `Potion4`).

## Papyrus Sources

For a normally handled player-created potion, CACO does **not** replace Skyrim's ordinary automatic potion-value calculation with a formula in these Papyrus sources. Skyrim first creates the potion and assigns its current gold value. CACO's direct gold change is limited to a potion that has both `MagicAlchBeneficial` and `MagicAlchHarmful`, when the `ImpureOption` setting is enabled:

```text
C_caco = int(C_pre × 0.2)
```

`C_pre` is the value returned by the created potion's `GetGoldValue()` immediately before CACO's manual adjustment. CACO assigns that reduced integer **before** changing effect fields. It then changes every duration-based effect's duration to `int(duration × 0.2)` and every other effect's magnitude to `float(magnitude × 0.2)`. It does not recalculate gold from those reduced effect fields.

The other ways CACO can affect the eventual value are upstream or orthogonal:

- The six potion/poison duration settings edit ingredient effect durations and select 1-, 5-, or 10-second effect records before Skyrim generates `C_pre`. They can therefore change both the generated effects and the generated gold, but they are not another gold multiplier.
- The ingredient conversion and effect records selected by CACO can change the recipe inputs. Their numeric costs and magnitudes are record/engine data, not formulas in the inspected scripts.
- Reweighting changes item weight, not gold. Renaming and quality classification inspect effect values, but do not change them.
- The crucible can replace a pure one-effect player-made potion with an authored quality exemplar. That is a form replacement, not a price calculation in the script; the replacement form supplies its own effects, gold, and other fields.

### Terminology and scope

This document uses **gold value** for the `Potion.GetGoldValue()`/`SetGoldValue()` field, **effect value** for an effect's magnitude or duration, and **weight** for the potion item weight. The CACO UI calls the impure setting “Impure Potions Are Worthless,” but the source implementation makes the potion worth 20% of its pre-adjustment gold value, not zero (`caco_adjustpotionthread.psc:238-254`; the label is in `complete alchemy & cooking overhaul_english.txt:138-145,233-235`).

The exact direct adjustment is documented first. Duration selectors, ingredient/effect records, quality and exemplar replacement are included because they can change the inputs or the value-bearing form that a potion ends up using. Unrelated food, recipe-book, and general crafting behavior is called out separately rather than being treated as potion pricing.

### Runtime path for a created potion

1. `CACO_CreatePotionPlayerScript.OnSit` enters its `Crafting` state only when `CACO_DisablePotionHandling.GetValue() == 0` and the furniture has the alchemy-crafting keyword (`caco_createpotionplayerscript.psc:47-57`). The same script registers the `CACO_OnPotionCreation` event on load (`:23-45`).
2. While in that state, `OnItemAdded` accepts an item only when there is no item reference or source container, the count is at least one, and the form has `VendorItemPotion` or `VendorItemPoison`. It queues one adjustment for each created item and then calls `wait_all()` (`caco_createpotionplayerscript.psc:89-105`). This is the post-creation path; the source does not contain a pre-craft ordinary potion-price calculation here.
3. The manager has a limit of 30 thread instances, initializes `thread01` through `thread30`, and selects the first unqueued thread (`caco_adjustpotionthreadmanager.psc:1-70,72-169`). If all are queued it raises `CACO_OnPotionCreation` and waits until a thread becomes available (`:171-213`). The numbered scripts `caco_adjustpotionthread01.psc` through `caco_adjustpotionthread30.psc` each only declare a subclass of `CACO_AdjustPotionThread` at line 1, so all adjustment math is inherited from the canonical script.
4. The canonical thread moves the created base item through `ConverterChest`, obtains the dropped potion reference, and classifies that resulting reference by its beneficial/harmful keywords (`caco_adjustpotionthread.psc:55-88`). This is why impurity is a property of the resulting potion record/reference, not a sum or comparison of effect costs in this source.
5. If the potion is impure and `ImpureOption == 1`, `ImpurePotion` runs. Reweighting and renaming are separate options and may run after or alongside the impurity adjustment (`caco_adjustpotionthread.psc:68-88`). The adjusted reference is returned to the player and its base form is added to `CACO_AlchemyCreatedPotionList` (`:83-88`).

The disable setting is therefore an entry/state gate in the creation script. The source shown does not add a second `CACO_DisablePotionHandling` check inside `OnItemAdded`; changing the setting while already in the crafting state should be understood in light of that state-machine behavior. The MCM hides the other potion-handling controls while the setting is enabled (`caco_mcmscript.psc:402-408`), and its English description says disabling handling disables rename, reweight, and impure handling (`complete alchemy & cooking overhaul_english.txt:233-235`).

### Direct impure-potion adjustment

#### Impurity predicate

The source predicate is exactly:

```text
isImpure = MyPotionRef.HasKeyword(MagicAlchBeneficial)
		   && MyPotionRef.HasKeyword(MagicAlchHarmful)
```

It appears in `caco_adjustpotionthread.psc:64-79`. The source does not derive impurity from positive versus negative numeric values, effect-cost totals, the number of effects, or the `ImpureOption` value. `ImpureOption == 1` only decides whether a potion already classified as impure receives the adjustment (`:68-73`). The available Papyrus source does not show where the two potion-level keywords are assigned.

The repository adapter applies the same logical conjunction to the selected effect set in `alchemist/include/main.h`. It derives the booleans from the loaded effect records' CACO/vanilla keyword and hostility data; it does not create a temporary potion or use a native preview. That side-effect-free distinction is important when the active alchemy menu is open.

### Repository adapter state

The adapter initializes at SKSE's `kDataLoaded` message. It first identifies a loaded full or light CACO plugin (`Complete Alchemy & Cooking Overhaul.esp` or `.esm`) and then resolves the core lists, six duration globals, four ingredient-position lists per family, option globals, Cure Disease/Cure Poison exemplars, and the two alchemy game settings. The plugin name and local form IDs are used as fallbacks, including `Update.esm`, because CACO records can be exposed through different loaded-file arrangements.

`detected` means that the CACO plugin or recognizable CACO records were found. `active` means all required live records and game settings were resolved, so an incomplete installation remains fail-closed and preserves the non-CACO path. The adapter exposes its active state and calculation revision to the prediction engine; a calculation revision changes when duration selectors or supported CACO option state changes, which invalidates the recommendation cache. The plugin does not write CACO diagnostics to a log file.

#### Gold-value order and formula

`ImpurePotion` performs these operations in this order (`caco_adjustpotionthread.psc:238-254`):

```text
DP_Extender.SetAlchAutoCalcFlag(BasePotion, false)
C_pre  = BasePotion.GetGoldValue()
C_caco = (C_pre × 0.2) as int
BasePotion.SetGoldValue(C_caco)

for x = 0 while x < BasePotion.GetNumEffects():
	if effect[x].HasKeyword(MagicAlchDurationBased):
		duration[x]  = (duration[x]  × 0.2) as int
	else:
		magnitude[x] = (magnitude[x] × 0.2) as float
```

`DP_Extender.SetAlchAutoCalcFlag(BasePotion, false)` switches the potion to manual calculation so `SetGoldValue` can take effect; `dp_extender.psc:26-28` documents `true` as AutoCalc and `false` as ManualCalc, and `:8-10` documents its separate automatic-gold getter. CACO actually calls the potion method `BasePotion.GetGoldValue()` and not `DP_Extender.GetGoldValue()` (`caco_adjustpotionthread.psc:240-241`).

The `as int` conversion is the source's integer conversion of the positive result, so the effective operation is truncation toward an integer for ordinary positive potion values. There is no explicit minimum, maximum, or rounding-to-nearest step in the Papyrus function. A duration can consequently become zero when its reduced value is below one. Non-duration magnitudes remain floating-point values. The loop is over every effect and does not recompute or reassign base effect cost, ingredient cost, area, effect count, or XP (`:242-254`).

Most importantly, CACO does not call an automatic price calculation after line 249. The gold field remains the integer assigned at line 241 even though the effect fields have subsequently been reduced. The source also contains no ordinary pure-potion price equation; for pure potions, the source leaves the generated `C_pre` alone unless another path replaces or changes the potion form.

#### What the 0.2 operation does not mean

- It is not `0.2` applied to a cost recomputed from the post-adjustment magnitudes and durations.
- It is not a zero-value assignment despite the MCM label “worthless.”
- It is not applied to every potion: both potion keywords, enabled potion handling, and `ImpureOption == 1` are required for this function to run.
- It is not an XP adjustment. The only XP-related text in this function is the unanswered source comment `;should I adjust XP as well?` at line 253.

### Upstream changes that can alter potion values

#### Six duration selectors

The MCM exposes six independent selectors: Restore Health, Restore Magicka, Restore Stamina, Damage Health, Damage Magicka, and Damage Stamina. The UI displays `1 sec`, `5 secs`, and `10 secs` (`caco_mcmscript.psc:322-325`; `complete alchemy & cooking overhaul_english.txt:37-51`). Each selector cycles through indices 0, 1, and 2 via `AdvChoice` (`caco_mcmscript.psc:1817-1824`).

On selection, CACO writes the index to the relevant global, edits four corresponding ingredient lists, and updates the comparison-position field on every adjustment thread:

| Selector | MCM source lines | Ingredient lists/effect index |
| --- | --- | --- |
| Restore Health | `caco_mcmscript.psc:641-648,671-701` | `CACO_RestoreIngH1st`–`H4th`, indices 0–3 |
| Restore Stamina | `caco_mcmscript.psc:703-710,724-754` | `CACO_RestoreIngS1st`–`S4th`, indices 0–3 |
| Restore Magicka | `caco_mcmscript.psc:756-763,777-807` | `CACO_RestoreIngM1st`–`M4th`, indices 0–3 |
| Damage Health | `caco_mcmscript.psc:809-816,839-869` | `CACO_DamageIngH1st`–`H4th`, indices 0–3 |
| Damage Stamina | `caco_mcmscript.psc:871-878,892-922` | `CACO_DamageIngS1st`–`S4th`, indices 0–3 |
| Damage Magicka | `caco_mcmscript.psc:924-931,945-975` | `CACO_DamageIngM1st`–`M4th`, indices 0–3 |

`ChangeIngredientDuration` maps index 0 to one second, index 1 to five seconds, and every other index to ten seconds, then calls `SetNthEffectDuration` on every ingredient in the supplied list (`caco_mcmscript.psc:1835-1849`). The handlers also set or clear flag `0x00008000` on the matching 1/5/10-second `MagicEffect` variants. The source does not name the flag's engine meaning, but the code clearly switches which duration-specific effect forms are active (`caco_mcmscript.psc:649-670,711-723,764-776,817-838,879-891,932-944`).

The English MCM descriptions explicitly say that changing each duration also adjusts potion magnitudes (`complete alchemy & cooking overhaul_english.txt:179-184`). The Papyrus source proves the ingredient-duration writes and variant selection; it does **not** provide the numeric magnitude formula or the magnitude values stored in those effect records. Therefore the exact upstream contribution to `C_pre` depends on the loaded CACO records and Skyrim's automatic alchemy calculation.

The same settings are re-applied when the player-load alias runs. `RunModCheck` reads all six globals, switches the 1/5/10-second CACO effect variants, and updates the known leveled ingredient lists (`caco_playerloadgamealias.psc:447-485`). `UpdateModIngredients` maps the global index to 1/5/10 seconds and writes the duration into each ingredient effect (`:531-556`); `UpdatePotionDuration` applies the corresponding effect flags (`:559-573`). This makes the duration configuration persistent across the maintenance path, not merely a temporary MCM display value.

#### Ingredient and record inputs

On initialization, the player-load alias sets CACO's ingredient-selection globals and installs CACO's duration modifier perk (`caco_playerloadgamealias.psc:201-243`, especially `:215-220`). Its inventory event handler can replace vanilla ingredients/items with CACO equivalents when `CACOIgnoreIngAddition_KRY` is zero (`:245-277`). Those replacements can change which ingredient effect records feed Skyrim's potion calculation, but the source script does not publish their numeric gold, base cost, magnitude, or duration fields. Such values must be read from the loaded records.

The source also changes the archetype of listed restore effects between `ValueMod` and `PeakValueMod` when the “Restore Effects Do Not Stack” setting changes (`caco_mcmscript.psc:1294-1303,1852-1867`). This controls effect stacking semantics; it does not contain a potion-gold multiplier or a direct magnitude/duration write.

### Other potion properties that are value-like but are not gold

#### Reweighting

`CalculateWeight` is called when `ReweightOption == 1` (`caco_adjustpotionthread.psc:68-79`). Its direct writes are:

| Potion state | Explicit weight written | Weight list |
| --- | ---: | --- |
| One effect, pure beneficial with `Purity`, or pure harmful with `ConcentratedPoison` | 0.2 | `CACO_AlchemyPotionWeightList02` |
| One effect, all other states | 0.3 | `CACO_AlchemyPotionWeightList03` |
| Two effects, pure beneficial with `Purity`, or pure harmful with `ConcentratedPoison` | 0.3 | `CACO_AlchemyPotionWeightList03` |
| Two effects, all other states | 0.4 | `CACO_AlchemyPotionWeightList04` |
| Three or more effects with either of the two pure/perk combinations | 0.4 | `CACO_AlchemyPotionWeightList04` |

These are the explicit branches at `caco_adjustpotionthread.psc:257-294`. For three or more effects without a qualifying pure/perk combination, the function has no `else` write and leaves the existing weight unchanged. The English description states the intended normal weights as 0.3, 0.4, and 0.5 for one, two, and three-or-more effects, with the perk reductions described separately (`complete alchemy & cooking overhaul_english.txt:229,233-234`). Thus 0.5 is the documented/default value, not an explicit `SetWeight(0.5)` in `CalculateWeight`.

The MCM troubleshooting action reapplies only 0.4, 0.3, and 0.2 to the corresponding stored lists (`caco_mcmscript.psc:544-596,1993-2012`). The player-load alias calls its own equivalent maintenance function on load (`caco_playerloadgamealias.psc:201-206,334-357`). The translation says the backup action requires Reweight Player Potions to be enabled (`complete alchemy & cooking overhaul_english.txt:127,229`). None of these weight writes changes `GetGoldValue()`.

#### Quality and renaming

Renaming uses values to classify the potion but does not change numeric effect fields or gold. CACO uses the first sorted effect as the primary effect; the source comments that `GetCostliestEffectIndex` is not used because it appears to use base effect cost rather than each effect's contribution to the actual potion value (`caco_adjustpotionthread.psc:93-110`).

Quality is `Impure` first for a mixed-keyword potion. Otherwise, duration-based primary effects compare their duration to exemplar potion entries 1–4, while non-duration effects compare their magnitude at the configured restore/damage position (`caco_adjustpotionthread.psc:113-201`). Cure disease, cure poison, and blood effects receive no quality prefix (`:151-154`). The thresholds are read from `CACO_AlchemyEffectsList` and `CACO_AlchemyAllPotionList`; their numeric values are authored potion records, not formulas in this script.

The display type is based on effect count and whether the primary effect is harmful: one effect is `Potion`/`Poison`, two are `Draught`/`Poison` and include the second effect name, and three or more are `Elixir`/`Poison` (`caco_adjustpotionthread.psc:203-235`). The English translation describes this as applying to newly created player potions and notes that existing names remain (`complete alchemy & cooking overhaul_english.txt:188`). These operations affect the display name only.

#### Crucible exemplar replacement

`CACO_CrucibleScript` is a separate value-bearing path. When a container receives a form with `PlayerCreatedPotion`, it handles only one-effect potions (`caco_cruciblescript.psc:33-58`). It leaves an impure potion alone (`:57-59`). For a pure duration-based potion, it compares the current duration against exemplar potion entries and removes the created form, adding `Potion0` through `Potion4` as the matching quality (`:59-79`). For cure disease and cure poison effects it similarly replaces the form with `CureDisease` or `CurePoison` (`:80-85`). For other one-effect potions it uses the relevant configured restore/damage index and replaces by magnitude threshold (`:87-124`).

Because this path adds a different authored potion form rather than calling `SetGoldValue`, the source does not expose a replacement-price formula. The resulting form can have different effect magnitude, duration, gold, weight, keywords, and other record fields. The exact replacement values are in the referenced potion records/form lists.

### MCM settings and their cost/value impact

The MCM declares the potion globals, duration globals, ingredient lists, comparison lists, and weight lists at `caco_mcmscript.psc:87-197`. Its potion page displays duration controls and the handling controls at `:372-415`.

| Setting or path | Direct gold effect | Other value effect |
| --- | --- | --- |
| **Impure Potions** | For a created potion with both potion-level keywords and `ImpureOption == 1`, assigns `int(C_pre × 0.2)` once. | Independently scales every duration-based duration and every other magnitude by 0.2. |
| **Disable All Potion Handling** | Prevents the normal `OnSit` transition into CACO's crafting adjustment state when its value is 1; therefore the normal queued post-processing does not start through that path. | Also prevents the rename/reweight/impure controls from being shown in the MCM. |
| **Restore/Damage Health, Magicka, and Stamina duration** | No direct multiplier. Can change `C_pre` indirectly by editing input durations and selecting effect variants. | Changes ingredient durations; the MCM text says associated potion magnitudes are adjusted. Also changes quality-comparison positions. |
| **Reweight Player Potions** | None. | Writes the weights listed above for newly adjusted potions and maintenance lists. |
| **Rename Player Potions** | None. | Changes name/quality text only; quality thresholds read magnitude/duration values. |
| **Update Potion Weights** | None. | Runs weight maintenance as a backup (`caco_mcmscript.psc:592-596`). |
| **Alchemy XP Rate** | No current-potion gold change in the inspected source. | Adds/removes the `CACO_AlchXPRate` perk when the value differs from 1 and writes the XP global (`caco_mcmscript.psc:1525-1533`); the slider range is 0–2 (`:1424-1440`). A changed Alchemy skill can affect later potion generation, but this is not an immediate price multiplier. |
| Recipe visibility, learning, mortar requirements, stacking, and availability controls | No direct gold write in the inspected MCM. | They can change which recipes/effects are available or how effects behave, which can indirectly change a later potion result. |

The impure, reweight, disable, and rename options are toggled and propagated to every adjustment-thread instance in `caco_mcmscript.psc:1170-1293`. The source does not show an additional gold multiplier for any of those settings. The translation's “Impure Potions Are Worthless” wording should therefore not be substituted for the executable `0.2` operation.

### What CACO does and does not price

#### Directly demonstrated by the source

1. A newly generated impure potion's existing gold value is changed to 20% of that value, using integer conversion, when handling and impure processing are enabled.
2. Every effect on that impure potion is separately weakened: duration-based effects use integer-converted 20% duration; other effects use floating-point 20% magnitude.
3. No second gold calculation follows the effect changes.
4. Weight can be explicitly changed to 0.2, 0.3, or 0.4 by the reweighting branches; the normal three-or-more-effect 0.5 value is described by the translation/default behavior but is not written in the fallback branch of the canonical script.
5. A crucible can replace a pure one-effect potion with a pre-authored exemplar whose own value-bearing fields apply.

#### Not defined as a CACO Papyrus pricing formula

- The ordinary pure-potion gold equation is absent. The scripts consume the value already assigned by Skyrim through `GetGoldValue()`.
- Ingredient gold values, magic-effect base costs, and the numeric magnitude adjustments behind the six duration-specific effect records are not assigned in these scripts.
- CACO's `DP_Extender` declares custom potion-price setters (`dp_extender.psc:36-41`), but no inspected CACO script calls them. The presence of those native declarations is not evidence that CACO applies their formula.
- The only other `SetGoldValue` calls found in the relevant CACO scripts set alchemy recipe-book gold to zero after reading (`caco_alchemyrecipescript.psc:203-208`) and reapply that book maintenance on load (`caco_playerloadgamealias.psc:359-366`). They do not price potions.
- `caco_cookfoodplayerscript.psc:190-210` reads a food item's gold value to calculate cooking XP; it does not write potion gold or potion effect values.

### Repository adapter versus CACO runtime

The native files under `alchemist/CACO` are a prediction adapter, not CACO's Papyrus implementation. They are documented here because they determine how this repository attempts to reproduce the source behavior before a potion is crafted.

#### Adapter gates and source discovery

`alchemist/CACO/CACO.h` exposes callbacks for CACO initialization, impurity, effect/gold penalties, weight, naming, and state. `alchemist/CACO/CACO.cpp` defines the 0.2 multiplier, CACO plugin names, exemplar-list names, option names, and duration globals. Initialization resolves the CACO plugin through loaded full/light mod views and resolves typed records through editor IDs and documented local-ID/plugin fallbacks. Plugin presence is reported separately from adapter readiness, and discovery runs at the `kDataLoaded` lifecycle point, after CACO records are expected to be available.

The adapter reads handling, impure, reweight, and rename state dynamically. Impure processing requires active CACO, handling not disabled, the impure option enabled, and both booleans true (`alchemist/CACO/CACO.cpp:399-407`). Reweight and rename have corresponding gates (`:409-417`).

#### Adapter effect and gold behavior

The adapter's effect operation matches the source's branch: duration-based effects use integer truncation after multiplying by 0.2, while other magnitudes remain floating point. Its gold helper rejects nonpositive/nonfinite input, floors the supplied floating-point pre-adjustment cost, multiplies the result by 0.2, and converts to a bounded 32-bit integer. The pre-adjustment input is a side-effect-free native-equivalent estimate assembled from live CACO records; no temporary potion or native preview is created.

`alchemist/include/main.h` shows the application order: it saves the native-equivalent pre-adjustment gold, scales effect fields, keeps the adjusted display values separate, and finally assigns the compatibility gold penalty from the saved pre-adjustment gold. The adjusted effect estimates are not used as the penalty input. The CACO path derives selected effects, input rounding, shared-effect order, and cost from live records without mutating Skyrim state; the result remains an estimate where the engine's private automatic construction cannot be called safely.

The non-CACO effect-strength and cost equations remain Prosperous Alchemist's vanilla estimate, not CACO's ordinary price formula. The CACO-specific multiplier is read from live `fAlchemyIngredientInitMult`, `fAlchemySkillFactor`, and active perk entry points, while the shared-effect grouping and native-equivalent contribution are implemented in `alchemist/include/main.h`. Neither path claims exact parity for records or engine behavior that the public APIs do not expose.

#### Adapter weight/name behavior

The adapter models normal one-, two-, and three-or-more-effect weights as 0.3, 0.4, and 0.5, and pure Purity/Concentrated Poison overrides as 0.2, 0.3, and 0.4 (`alchemist/CACO/CACO.cpp:479-499`). The 0.5 normal three-or-more value is consistent with the English CACO description but is not an explicit write in the canonical Papyrus `CalculateWeight` fallback branch. Adapter naming and quality use the same effect-count and exemplar concepts (`:231-291,501-532`), and provider registration connects all callbacks (`:541-552`).

### Source index

The line references below use the source files available in this checkout. The CACO Papyrus source is under `caco\scripts\source`; the English MCM translation is under `caco\interface\translations`.

#### Direct CACO post-processing

- `caco\scripts\source\caco_adjustpotionthread.psc` — properties and CACO exemplar lists at lines 1-37; creation event and beneficial/harmful classification at 55-88; primary-effect and quality inputs at 93-201; display-name construction at 203-235; direct impure gold/effect-value changes at 238-254; reweighting at 257-294.
- `caco\scripts\source\caco_adjustpotionthread01.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread02.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread03.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread04.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread05.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread06.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread07.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread08.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread09.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread10.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread11.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread12.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread13.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread14.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread15.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread16.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread17.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread18.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread19.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread20.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread21.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread22.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread23.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread24.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread25.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread26.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread27.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread28.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread29.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.
- `caco\scripts\source\caco_adjustpotionthread30.psc` — line 1, a thread subclass of `CACO_AdjustPotionThread`.

#### Trigger, settings, maintenance, and replacement paths

- `caco\scripts\source\caco_createpotionplayerscript.psc` — registration/load handling at lines 23-45; the disable gate at 47-57; crafted-potion detection and one-dispatch-per-item routing at 89-105.
- `caco\scripts\source\caco_adjustpotionthreadmanager.psc` — 30-thread declarations/initialization at lines 1-70; dispatch and queue behavior at 72-169; wait/event signaling at 171-213.
- `caco\scripts\source\caco_mcmscript.psc` — potion globals and lists at lines 87-197; potion UI and handling controls at 372-415; weight-maintenance action at 544-596; six duration-setting handlers at 641-976; impure/reweight/disable/rename toggles at 1170-1293; Alchemy XP control at 1424-1440, 1525-1533, and 1592-1598; duration mutation helper at 1835-1849; stackability helper at 1852-1867; weight maintenance at 1993-2012.
- `caco\scripts\source\caco_playerloadgamealias.psc` — load initialization/perk setup at lines 201-243; inventory ingredient conversion at 245-277; weight and recipe maintenance at 334-366; duration/effect-record refresh at 447-485 and 531-573.
- `caco\scripts\source\caco_cruciblescript.psc` — player-created one-effect potion recognition at lines 33-58; duration-based exemplar replacement at 59-79; cure replacements at 80-85; magnitude-based exemplar replacement at 87-124.
- `caco\scripts\source\dp_extender.psc` — automatic-gold and price API declarations at lines 8-27 and 36-41; CACO uses the auto-calc flag declaration, but the custom price setters are not called by the inspected CACO scripts.
- `caco\interface\translations\complete alchemy & cooking overhaul_english.txt` — duration labels and potion controls at lines 37-54 and 127-145; duration/magnitude descriptions at 179-184; XP description at 204; weight-maintenance description at 229; normal weight, impure-value, and disable-handling descriptions at 233-235.

#### Repository-side compatibility adapter

These are not CACO’s Papyrus implementation; they are the adapter and prediction code in this repository that models CACO for preview purposes.

- `alchemist/CACO/CACO.h` — adapter contract for impurity, effect/gold penalties, weight, naming, and state at lines 12-42.
- `alchemist/CACO/CACO.cpp` — CACO constants/discovery at lines 27-63; exemplar lookup and quality inputs at 152-291; initialization and live toggle gates at 339-437; effect and gold emulation at 439-477; weight/name prediction at 479-539; provider registration at 541-552.
- `alchemist/include/main.h` — fallback effect-strength/cost model at lines 518-565; compatibility penalty ordering at 567-619; native-preview capture and application at 639-750.

#### Explicitly checked non-potion value writes

- `caco\scripts\source\caco_alchemyrecipescript.psc:203-208` and `caco_playerloadgamealias.psc:359-366` set read alchemy recipe-book gold to zero; these are books, not potion prices.
- `caco\scripts\source\caco_cookfoodplayerscript.psc:190-210` reads a food item's gold value to award cooking XP; it does not change potion gold or potion effects.

---

## CACO POTION HANDLING
CACO's Potion Handling is a scripted process used to manipulate potions and poisons; renaming them, reweighting them, and adjusting the values of impure potions and poisons (those with both positive and negative effects). Originally implemented with multithreading by kryptopyr and Chesko to be as performant as possible, it nevertheless still uses the Papyrus Engine and is therefore bound by its limitations.

Under normal gameplay CACO's Potion Handling works very well, but as it fires each time a potion is created, it can run into issues when potions are spam-crafted, particularly if the Papyrus Engine is already struggling. While our team has been able to replicate these issues on occasion by spam-crafting potions, they are not something we've encountered in normal gameplay. Nevertheless, out of an abundance of caution, we've decided to disable Potion Handling by default as of CACO 3.0.0, although an option has been added to the MCM to reenable it. Individual options to enable/disable Potion Renaming, Potion Reweighting, and Impure Potions have also been included in the MCM, to allow granular control of these options.

An alternative to CACO's Potion Handling is Alchemy Plus, which is a .dll mod which also provides potion renaming and impure potion functionality, along with several other functions not included in CACO (potion remodeling, rounded effect potency, and better UI hints). Alchemy Plus can even be used in conjunction with CACO's Potion Handling, although it is recommended to disable duplicate functions (potion renaming and impure potions) in one mod or the other. It should be noted that Alchemy Plus' potion renaming does not function exactly like CACO's and will provide less nuanced potion names, but we hope to eventually fork Alchemy Plus to provide renaming similar to CACO's and to incorporate potion reweighting as well.

### POTION RENAMING
When Potion Renaming is enabled, player-made potions will have their names adjusted to follow the consistent naming convention CACO has adopted for non-crafted potions. Potions will now sort alphabetically by strength and, while single-effect potions remain "Potions", all two-effect potions will now be called "Draughts," and potions with three or more effects will be named "Elixirs." Additionally, if the potion has a secondary effect, that effect will also be appended at the end of the potion's name. Of CACO's three Potion Handling functions, Potion Renaming is objectively the most performance heavy.

Alchemy Plus does not currently include naming based on the number of effects, but we hope to add this in the future. By default, potions with additional effects are identified with a (+1) or (+2); this can be changed in Alchemy Plus' translation files, but only supports global identifiers, not specific effects.

### POTION REWEIGHTING
Potion Reweighting adjusts the weight of player-crafted potions based on the number of effects they have, consistent with the weights CACO applies to non-crafted potions; potions with a single effect will weigh 0.3, potions with two effects will weigh 0.4, and potions with three or more effects will weigh 0.5.

Alchemy Plus does not currently include any reweighting functionality but, as noted above, we hope to be able to add this functionality in the future.

### IMPURE POTIONS
CACO considers impure potions (those with both positive and negative effects) to be rubbish or failed alchemical experiments. When the Impure Potions option is enabled, CACO applies a severe penalty to the gold value of all impure potions, treating them as a waste of ingredients and only good for learning new ingredient properties.

While Alchemy Plus uses a different formula than CACO for calculating the value of impure potions, it has a similar effect of reducing the gold value while also reducing the amount of experience impure potions provide (which CACO does not). Alchemy Plus also marks the impure potions as (Impure), although this text can be changed in the translation files or disabled entirely in the AlchemyPlus.json file.

## Restore/Damage Health/Magicka/Stamina FormLists
For CACO to function ingredients with Restore/Damage Health/Magicka/Stamina effects need to be added to a set of formlists based on the effect's position in the ingredient's effect list (1st, 2nd, 3rd, 4th). This was previously done by adding the ingredients to a set of leveled items, resolving the leveled item conflicts manually or with Wrye Bash, with CACO finally copying the entries via script from the leveled items into the appropriate formlists. With the advent of mods like FormList Manipluator, we can now add the entries directly to the formlists without any conflicts, so as of CACO version 3, we have begun including an FLM .ini with any relevant patches we make. While patch authors are free to use either method, our patches will continue to offer both the leveled item edits as well as the FLM.ini file. For a list of both the formlists and leveled items involved, please see the below:

### FormLists
CACO_RestoreIngH1st [FLST:XX0D03ED]
CACO_RestoreIngH2nd [FLST:XX0D03EE]
CACO_RestoreIngH3rd [FLST:XX0D03EF]
CACO_RestoreIngH4th [FLST:XX0D03F0]
CACO_RestoreIngM1st [FLST:XX0D03F1]
CACO_RestoreIngM2nd [FLST:XX0D03F2]
CACO_RestoreIngM3rd [FLST:XX0D03F3]
CACO_RestoreIngM4th [FLST:XX0D03F4]
CACO_RestoreIngS1st [FLST:XX0D03F5]
CACO_RestoreIngS2nd [FLST:XX0D03F6]
CACO_RestoreIngS3rd [FLST:XX0D03F7]
CACO_RestoreIngS4th [FLST:XX0D03F8]
CACO_DamageIngH1st [FLST:XX1B93CB]
CACO_DamageIngH2nd [FLST:XX1B93CC]
CACO_DamageIngH3rd [FLST:XX1B93CD]
CACO_DamageIngH4th [FLST:XX1B93CE]
CACO_DamageIngM1st [FLST:XX1B93CF]
CACO_DamageIngM2nd [FLST:XX1B93D0]
CACO_DamageIngM3rd [FLST:XX1B93D1]
CACO_DamageIngM4th [FLST:XX1B93D2]
CACO_DamageIngS1st [FLST:XX1B93D3]
CACO_DamageIngS2nd [FLST:XX1B93D4]
CACO_DamageIngS3rd [FLST:XX1B93D5]
CACO_DamageIngS4th [FLST:XX1B93D6]

### Leveled Items
CACO_IngrRestoreHealth1st [LVLI:01CCA051]
CACO_IngrRestoreHealth2nd [LVLI:01CCA052]
CACO_IngrRestoreHealth3rd [LVLI:01CCA053]
CACO_IngrRestoreHealth4th [LVLI:01CCA054]
CACO_IngrRestoreMagicka1st [LVLI:01CCA055]
CACO_IngrRestoreMagicka2nd [LVLI:01CCA056]
CACO_IngrRestoreMagicka3rd [LVLI:01CCA057]
CACO_IngrRestoreMagicka4th [LVLI:01CCA058]
CACO_IngrRestoreStamina1st [LVLI:01CCA059]
CACO_IngrRestoreStamina2nd [LVLI:01CCA060]
CACO_IngrRestoreStamina3rd [LVLI:01CCA061]
CACO_IngrRestoreStamina4th [LVLI:01CCA062]
CACO_IngrDamageHealth1st [LVLI:01CCA063]
CACO_IngrDamageHealth2nd [LVLI:01CCA064]
CACO_IngrDamageHealth3rd [LVLI:01CCA065]
CACO_IngrDamageHealth4th [LVLI:01CCA066]
CACO_IngrDamageMagicka1st [LVLI:01CCA067]
CACO_IngrDamageMagicka2nd [LVLI:01CCA068]
CACO_IngrDamageMagicka3rd [LVLI:01CCA069]
CACO_IngrDamageMagicka4th [LVLI:01CCA070]
CACO_IngrDamageStamina1st [LVLI:01CCA071]
CACO_IngrDamageStamina2nd [LVLI:01CCA072]
CACO_IngrDamageStamina3rd [LVLI:01CCA073]
CACO_IngrDamageStamina4th [LVLI:01CCA074]

## Current Prosperous Alchemist compatibility status

The repository implements the four compatibility paths described at the beginning of this document: vanilla, Alchemy Plus only, CACO only, and Automatic with both adapters active.

1. **Live CACO records:** `alchemist/CACO/CACO.cpp` resolves CACO's loaded effect records, ingredient-position lists, duration globals, option globals, game settings, and exemplar lists through editor IDs and documented FormID fallbacks. The evaluator uses those live records instead of a second hardcoded CACO cost table.
2. **Duration variants:** The six Restore/Damage Health, Magicka, and Stamina selectors are read from their CACO globals. The selected effect variant and ingredient position are used before the ordinary effect estimate is calculated.
3. **Impure processing:** When CACO handling and impure processing are active, the evaluator identifies the mixed beneficial/harmful result, scales effect fields as CACO does, and applies the truncated 20% gold adjustment to the saved pre-adjustment estimate. In Automatic mode, Alchemy Plus's supported impure-cost correction is applied before CACO's adjustment.
4. **Crucible replacements:** Pure one-effect results resolve the matching CACO quality exemplar or cure form and use its authored value when the live lists are available.
5. **Naming and weight:** When CACO options enable them, the prediction mirrors CACO's effect-count naming and weight rules without mutating the active crafting menu or creating a temporary potion.

The adapter remains fail-closed when required records or settings are incomplete, and the non-CACO path remains unchanged. Ordinary multi-effect predictions are still estimates because Skyrim's private automatic potion-construction operation is not exposed as a safe public API. Validation of exact in-game parity requires an external in-game comparison; the Developer Test Hub displays comparison records in memory and the plugin does not write them to a log file.
