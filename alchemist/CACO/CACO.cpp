#include "CACO.h"

#include <RE/A/AlchemyItem.h>
#include <RE/B/BGSKeyword.h>
#include <RE/B/BGSListForm.h>
#include <RE/B/BGSPerk.h>
#include <RE/B/BGSEntryPointFunctionDataOneValue.h>
#include <RE/B/BGSEntryPointPerkEntry.h>
#include <RE/E/Effect.h>
#include <RE/E/EffectSetting.h>
#include <RE/G/GameSettingCollection.h>
#include <RE/I/IngredientItem.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESFile.h>
#include <RE/T/TESGlobal.h>
#include <RE/T/TESCondition.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <set>
#include <string>
#include <string_view>

namespace alchemist::caco
{
	namespace
	{
		constexpr std::string_view kCacoPlugin = "Complete Alchemy & Cooking Overhaul.esp";
		constexpr std::string_view kCacoPluginEsm = "Complete Alchemy & Cooking Overhaul.esm";
		constexpr std::string_view kLegacyPlugin = "Update.esm";
		constexpr std::array<std::string_view, 2> kCacoPlugins{ kCacoPlugin, kCacoPluginEsm };
		constexpr RE::FormID kAlchemyEffectsListFormID = 0x0022DD8F;
		constexpr RE::FormID kAlchemyAllPotionListFormID = 0x0022DD90;
		constexpr RE::FormID kDisablePotionHandlingFormID = 0x00AAB031;
		constexpr RE::FormID kImpurePotionsFormID = 0x00AAB030;
		constexpr RE::FormID kReweightPotionsFormID = 0x00AAB02F;
		constexpr RE::FormID kRenamePotionsFormID = 0x0039A855;
		constexpr std::string_view kBeneficialKeyword = "MagicAlchBeneficial";
		constexpr std::string_view kHarmfulKeyword = "MagicAlchHarmful";
		constexpr std::string_view kDurationBasedKeyword = "MagicAlchDurationBased";
		constexpr std::string_view kDurationModifierPerk = "CACO_AlchDurationModifier";
		constexpr std::string_view kCureDiseaseKeyword = "MagicAlchCureDisease_CACO";
		constexpr std::string_view kCurePoisonKeyword = "MagicAlchCurePoison_CACO";
		constexpr std::string_view kBloodKeyword = "MagicAlchBloodEffect_CACO";
		constexpr float kAlchemyItemType = 17.0f;

		struct FamilyDefinition
		{
			std::string_view keyword;
			std::string_view editorPrefix;
			std::string_view alternateEditorPrefix;
			std::string_view durationGlobal;
			RE::FormID durationGlobalFormID;
			std::array<std::string_view, 4> ingredientLists;
			std::array<RE::FormID, 4> ingredientListFormIDs;
			std::array<std::string_view, 3> durationEffects;
			std::array<std::string_view, 3> alternateDurationEffects;
		};

		constexpr std::array<FamilyDefinition, 6> kFamilies{
			{
				{
					"MagicAlchRestoreHealth",
					"AlchRestoreHealth",
					"AlchRestoreHealthBlood",
					"CACO_RestoreHealthDuration",
					0x01CCA010,
					{ "CACO_RestoreIngH1st", "CACO_RestoreIngH2nd", "CACO_RestoreIngH3rd", "CACO_RestoreIngH4th" },
					{ 0x000D03ED, 0x000D03EE, 0x000D03EF, 0x000D03F0 },
					{ "AlchRestoreHealth_1sec", "AlchRestoreHealth_5sec", "AlchRestoreHealth_10sec" },
					{ "AlchRestoreHealthBlood_1sec", "AlchRestoreHealthBlood_5sec", "AlchRestoreHealthBlood_10sec" }
				},
				{
					"MagicAlchRestoreMagicka",
					"AlchRestoreMagicka",
					"",
					"CACO_RestoreMagickaDuration",
					0x01CCA011,
					{ "CACO_RestoreIngM1st", "CACO_RestoreIngM2nd", "CACO_RestoreIngM3rd", "CACO_RestoreIngM4th" },
					{ 0x000D03F1, 0x000D03F2, 0x000D03F3, 0x000D03F4 },
					{ "AlchRestoreMagicka_1sec", "AlchRestoreMagicka_5sec", "AlchRestoreMagicka_10sec" },
					{ "", "", "" }
				},
				{
					"MagicAlchRestoreStamina",
					"AlchRestoreStamina",
					"",
					"CACO_RestoreStaminaDuration",
					0x01CCA012,
					{ "CACO_RestoreIngS1st", "CACO_RestoreIngS2nd", "CACO_RestoreIngS3rd", "CACO_RestoreIngS4th" },
					{ 0x000D03F5, 0x000D03F6, 0x000D03F7, 0x000D03F8 },
					{ "AlchRestoreStamina_1sec", "AlchRestoreStamina_5sec", "AlchRestoreStamina_10sec" },
					{ "", "", "" }
				},
				{
					"MagicAlchDamageHealth",
					"AlchDamageHealth",
					"AlchDamageUndeadHealth",
					"CACO_DamageHealthDuration",
					0x01CCA013,
					{ "CACO_DamageIngH1st", "CACO_DamageIngH2nd", "CACO_DamageIngH3rd", "CACO_DamageIngH4th" },
					{ 0x001B93CB, 0x001B93CC, 0x001B93CD, 0x001B93CE },
					{ "AlchDamageHealth_1sec", "AlchDamageHealth_5sec", "AlchDamageHealth_10sec" },
					{ "AlchDamageUndead_1sec", "AlchDamageUndead_5sec", "AlchDamageUndead_10sec" }
				},
				{
					"MagicAlchDamageMagicka",
					"AlchDamageMagicka",
					"",
					"CACO_DamageMagickaDuration",
					0x01CCA014,
					{ "CACO_DamageIngM1st", "CACO_DamageIngM2nd", "CACO_DamageIngM3rd", "CACO_DamageIngM4th" },
					{ 0x001B93CF, 0x001B93D0, 0x001B93D1, 0x001B93D2 },
					{ "AlchDamageMagicka_1sec", "AlchDamageMagicka_5sec", "AlchDamageMagicka_10sec" },
					{ "", "", "" }
				},
				{
					"MagicAlchDamageStamina",
					"AlchDamageStamina",
					"",
					"CACO_DamageStaminaDuration",
					0x01CCA015,
					{ "CACO_DamageIngS1st", "CACO_DamageIngS2nd", "CACO_DamageIngS3rd", "CACO_DamageIngS4th" },
					{ 0x001B93D3, 0x001B93D4, 0x001B93D5, 0x001B93D6 },
					{ "AlchDamageStamina_1sec", "AlchDamageStamina_5sec", "AlchDamageStamina_10sec" },
					{ "", "", "" }
				}
			}
		};

		struct FamilyState
		{
			RE::TESGlobal* durationGlobal = nullptr;
			std::array<RE::BGSListForm*, 4> ingredientLists{};
			std::array<RE::EffectSetting*, 3> durationEffects{};
			std::array<RE::EffectSetting*, 3> alternateDurationEffects{};
			std::array<RE::EffectSetting*, 3> legacyEffects{};
		};

		struct State
		{
			bool initialized = false;
			bool detected = false;
			bool active = false;
			bool potionHandlingEnabled = false;
			bool impureProcessingEnabled = false;
			bool reweightingEnabled = false;
			bool renamingEnabled = false;
			bool alchemySettingsReady = false;
			float alchemyIngredientInitMultiplier = 0.0f;
			float alchemySkillFactor = 0.0f;
			std::string pluginName;
			std::array<FamilyState, kFamilies.size()> families{};
			std::array<int, kFamilies.size()> durationIndices{};
			std::uint64_t calculationRevision = 0;
			RE::TESGlobal* disablePotionHandling = nullptr;
			RE::TESGlobal* impurePotions = nullptr;
			RE::TESGlobal* reweightPotions = nullptr;
			RE::TESGlobal* renamePotions = nullptr;
			RE::BGSListForm* alchemyEffectsList = nullptr;
			RE::BGSListForm* allPotionList = nullptr;
			RE::AlchemyItem* cureDisease = nullptr;
			RE::AlchemyItem* curePoison = nullptr;
		};

		State g_state;

		struct PluginMatch
		{
			const RE::TESFile* file = nullptr;
			std::string name;
		};

		bool EqualsIgnoreCase(std::string_view a_left, std::string_view a_right) noexcept
		{
			if (a_left.size() != a_right.size()) {
				return false;
			}
			for (std::size_t i = 0; i < a_left.size(); ++i) {
				const auto left = a_left[i] >= 'A' && a_left[i] <= 'Z' ?
					static_cast<char>(a_left[i] + ('a' - 'A')) : a_left[i];
				const auto right = a_right[i] >= 'A' && a_right[i] <= 'Z' ?
					static_cast<char>(a_right[i] + ('a' - 'A')) : a_right[i];
				if (left != right) {
					return false;
				}
			}
			return true;
		}

		bool HasAlchemyEffectKeyword(
			const RE::EffectSetting* a_effect,
			const RE::BGSKeyword* a_keyword) noexcept
		{
			if (!a_effect || !a_keyword) {
				return false;
			}
			const auto* editorID = a_keyword->GetFormEditorID();
			if (editorID && EqualsIgnoreCase(editorID, kBeneficialKeyword)) {
				return Adapter::HasBeneficialKeyword(a_effect);
			}
			if (editorID && EqualsIgnoreCase(editorID, kHarmfulKeyword)) {
				return Adapter::HasHarmfulKeyword(a_effect);
			}
			if (editorID && EqualsIgnoreCase(editorID, kDurationBasedKeyword)) {
				return Adapter::IsDurationBased(a_effect);
			}
			return a_effect->HasKeyword(a_keyword);
		}

		bool MatchesEditorPrefix(std::string_view a_editorID, std::string_view a_prefix) noexcept
		{
			if (a_editorID.empty() || a_prefix.empty() || a_editorID.size() < a_prefix.size()) {
				return false;
			}
			if (!EqualsIgnoreCase(a_editorID.substr(0, a_prefix.size()), a_prefix)) {
				return false;
			}
			return a_editorID.size() == a_prefix.size() || a_editorID[a_prefix.size()] == '_';
		}

		bool ContainsIgnoreCase(std::string_view a_value, std::string_view a_fragment) noexcept
		{
			if (a_fragment.empty()) {
				return true;
			}
			if (a_fragment.size() > a_value.size()) {
				return false;
			}
			for (std::size_t offset = 0; offset <= a_value.size() - a_fragment.size(); ++offset) {
				if (EqualsIgnoreCase(a_value.substr(offset, a_fragment.size()), a_fragment)) {
					return true;
				}
			}
			return false;
		}

		PluginMatch MakePluginMatch(const RE::TESFile* a_file, std::string_view a_candidate)
		{
			PluginMatch result;
			result.file = a_file;
			if (a_file) {
				const auto filename = a_file->GetFilename();
				result.name.assign(filename.data(), filename.size());
			}
			if (result.name.empty()) {
				result.name.assign(a_candidate.data(), a_candidate.size());
			}
			return result;
		}

		PluginMatch FindCacoPlugin()
		{
			PluginMatch result;
			try {
				auto* dataHandler = RE::TESDataHandler::GetSingleton();
				if (!dataHandler) {
					return result;
				}

				for (const auto candidate : kCacoPlugins) {
					try {
						if (auto* file = dataHandler->LookupLoadedModByName(candidate)) {
							return MakePluginMatch(file, candidate);
						}
					} catch (...) {
					}
					try {
						if (auto* file = dataHandler->LookupLoadedLightModByName(candidate)) {
							return MakePluginMatch(file, candidate);
						}
					} catch (...) {
					}
				}
			} catch (...) {
			}
			return result;
		}

		template <class T>
		T* ResolveEditorID(std::string_view a_editorID) noexcept
		{
			if (a_editorID.empty()) {
				return nullptr;
			}
			try {
				if (auto* form = RE::TESForm::LookupByEditorID<T>(a_editorID)) {
					return form;
				}
			} catch (...) {
			}

			try {
				auto* dataHandler = RE::TESDataHandler::GetSingleton();
				if (!dataHandler) {
					return nullptr;
				}
				for (auto* form : dataHandler->GetFormArray<T>()) {
					if (!form) {
						continue;
					}
					const auto* editorID = form->GetFormEditorID();
					if (editorID && EqualsIgnoreCase(editorID, a_editorID)) {
						return form;
					}
				}
			} catch (...) {
			}
			return nullptr;
		}

		template <class T>
		T* ResolveRecord(std::string_view a_editorID, RE::FormID a_localFormID) noexcept
		{
			if (auto* form = ResolveEditorID<T>(a_editorID)) {
				return form;
			}
			if (a_localFormID == 0) {
				return nullptr;
			}

			try {
				auto* dataHandler = RE::TESDataHandler::GetSingleton();
				if (!dataHandler) {
					return nullptr;
				}
				auto lookup = [&](std::string_view a_plugin) -> T* {
					if (a_plugin.empty()) {
						return nullptr;
					}
					try {
						return dataHandler->LookupForm<T>(a_localFormID, a_plugin);
					} catch (...) {
						return nullptr;
					}
				};

				if (auto* form = lookup(g_state.pluginName)) {
					return form;
				}
				for (const auto plugin : kCacoPlugins) {
					if (auto* form = lookup(plugin)) {
						return form;
					}
				}
				return lookup(kLegacyPlugin);
			} catch (...) {
				return nullptr;
			}
		}

		bool IsOne(RE::TESGlobal* a_global) noexcept
		{
			return a_global && std::isfinite(a_global->value) && a_global->value == 1.0f;
		}

		int GetDurationIndex(std::size_t a_familyIndex) noexcept
		{
			if (a_familyIndex >= g_state.families.size()) {
				return 0;
			}
			const auto* global = g_state.families[a_familyIndex].durationGlobal;
			if (!global || !std::isfinite(global->value)) {
				return 0;
			}
			return (std::clamp)(static_cast<int>(global->value), 0, 2);
		}

		void RefreshCalculationRevision() noexcept
		{
			std::array<int, kFamilies.size()> durationIndices{};
			for (std::size_t i = 0; i < durationIndices.size(); ++i) {
				durationIndices[i] = GetDurationIndex(i);
			}
			if (durationIndices != g_state.durationIndices) {
				g_state.durationIndices = durationIndices;
				if (g_state.calculationRevision < (std::numeric_limits<std::uint64_t>::max)()) {
					++g_state.calculationRevision;
				}
			}
		}

		int FindFamily(const RE::EffectSetting* a_effect) noexcept
		{
			if (!a_effect) {
				return -1;
			}

			for (std::size_t i = 0; i < kFamilies.size(); ++i) {
				const auto* editorID = a_effect->GetFormEditorID();
				if (a_effect->HasKeywordString(kFamilies[i].keyword) ||
					MatchesEditorPrefix(editorID ? std::string_view(editorID) : std::string_view{}, kFamilies[i].editorPrefix) ||
					MatchesEditorPrefix(editorID ? std::string_view(editorID) : std::string_view{}, kFamilies[i].alternateEditorPrefix)) {
					return static_cast<int>(i);
				}
			}
			return -1;
		}

		bool IsIngredientListedInFamily(
			const RE::IngredientItem* a_ingredient,
			const FamilyState& a_family) noexcept
		{
			if (!a_ingredient) {
				return false;
			}
			return std::any_of(a_family.ingredientLists.begin(), a_family.ingredientLists.end(),
				[a_ingredient](const auto* list) { return list && list->HasForm(a_ingredient); });
		}

		std::size_t GetComparisonPosition(const RE::EffectSetting* a_effect) noexcept
		{
			const int familyIndex = FindFamily(a_effect);
			return familyIndex >= 0 ? static_cast<std::size_t>(GetDurationIndex(static_cast<std::size_t>(familyIndex))) : 0;
		}

		bool IsSameFamily(const RE::EffectSetting* a_left, const RE::EffectSetting* a_right) noexcept
		{
			if (!a_left || !a_right) {
				return false;
			}
			if (a_left == a_right) {
				return true;
			}
			const int leftFamily = FindFamily(a_left);
			return leftFamily >= 0 && leftFamily == FindFamily(a_right);
		}

		RE::EffectSetting* ResolveDurationEffect(
			std::size_t a_familyIndex,
			const RE::EffectSetting* a_sourceEffect) noexcept
		{
			if (a_familyIndex >= kFamilies.size()) {
				return nullptr;
			}

			const auto& family = g_state.families[a_familyIndex];
			const int durationIndex = GetDurationIndex(a_familyIndex);
			if (durationIndex < 0 || durationIndex >= 3) {
				return nullptr;
			}

			const auto* sourceEditorID = a_sourceEffect ? a_sourceEffect->GetFormEditorID() : nullptr;
			const bool useAlternate = sourceEditorID &&
				(ContainsIgnoreCase(sourceEditorID, "Blood") || ContainsIgnoreCase(sourceEditorID, "Undead"));

			if (useAlternate) {
				if (family.alternateDurationEffects[durationIndex]) {
					return family.alternateDurationEffects[durationIndex];
				}
				if (family.legacyEffects[durationIndex]) {
					return family.legacyEffects[durationIndex];
				}
				return nullptr;
			}

			return family.durationEffects[durationIndex];
		}

		const RE::BGSListForm* FindPotionList(const RE::EffectSetting* a_primaryEffect) noexcept
		{
			if (!g_state.alchemyEffectsList || !g_state.allPotionList || !a_primaryEffect) {
				return nullptr;
			}

			std::size_t effectIndex = g_state.alchemyEffectsList->forms.size();
			for (std::size_t i = 0; i < g_state.alchemyEffectsList->forms.size(); ++i) {
				const auto* effect = g_state.alchemyEffectsList->forms[i] ?
					g_state.alchemyEffectsList->forms[i]->As<RE::EffectSetting>() : nullptr;
				if (effect == a_primaryEffect) {
					effectIndex = i;
					break;
				}
			}
			if (effectIndex == g_state.alchemyEffectsList->forms.size()) {
				for (std::size_t i = 0; i < g_state.alchemyEffectsList->forms.size(); ++i) {
					const auto* effect = g_state.alchemyEffectsList->forms[i] ?
						g_state.alchemyEffectsList->forms[i]->As<RE::EffectSetting>() : nullptr;
					if (IsSameFamily(effect, a_primaryEffect)) {
						effectIndex = i;
						break;
					}
				}
			}
			if (effectIndex >= g_state.allPotionList->forms.size()) {
				return nullptr;
			}
			return g_state.allPotionList->forms[effectIndex] ?
				g_state.allPotionList->forms[effectIndex]->As<RE::BGSListForm>() : nullptr;
		}

		const RE::AlchemyItem* GetPotionAt(const RE::BGSListForm* a_list, std::size_t a_index) noexcept
		{
			if (!a_list || a_index >= a_list->forms.size() || !a_list->forms[a_index]) {
				return nullptr;
			}
			return a_list->forms[a_index]->As<RE::AlchemyItem>();
		}

		bool GetExemplarValue(
			const RE::AlchemyItem* a_potion,
			std::size_t a_effectIndex,
			bool a_duration,
			float& a_value) noexcept
		{
			if (!a_potion || a_effectIndex >= a_potion->effects.size() || !a_potion->effects[a_effectIndex]) {
				return false;
			}
			const auto* effect = a_potion->effects[a_effectIndex];
			a_value = a_duration ? static_cast<float>(effect->GetDuration()) : effect->GetMagnitude();
			return std::isfinite(a_value);
		}

		bool SelectQuality(
			const RE::BGSListForm* a_compareList,
			float a_value,
			bool a_duration,
			std::size_t a_effectIndex,
			std::size_t& a_quality) noexcept
		{
			const auto* potion1 = GetPotionAt(a_compareList, 1);
			const auto* potion2 = GetPotionAt(a_compareList, 2);
			const auto* potion3 = GetPotionAt(a_compareList, 3);
			const auto* potion4 = GetPotionAt(a_compareList, 4);
			float threshold1 = 0.0f;
			float threshold2 = 0.0f;
			float threshold3 = 0.0f;
			float threshold4 = 0.0f;
			if (!potion1 || !potion2 || !potion3 ||
				!GetExemplarValue(potion1, a_effectIndex, a_duration, threshold1) ||
				!GetExemplarValue(potion2, a_effectIndex, a_duration, threshold2) ||
				!GetExemplarValue(potion3, a_effectIndex, a_duration, threshold3)) {
				return false;
			}

			if (a_value < threshold1) {
				a_quality = 0;
			} else if (a_value < threshold2) {
				a_quality = 1;
			} else if (a_value < threshold3) {
				a_quality = 2;
			} else if (!potion4) {
				a_quality = 3;
			} else {
				if (!GetExemplarValue(potion4, a_effectIndex, a_duration, threshold4)) {
					return false;
				}
				a_quality = a_value < threshold4 ? 3 : 4;
			}
			return true;
		}

		bool IsCureEffect(const RE::EffectSetting* a_effect) noexcept
		{
			return a_effect && (a_effect->HasKeywordString(kCureDiseaseKeyword) ||
				a_effect->HasKeywordString(kCurePoisonKeyword) ||
				a_effect->HasKeywordString(kBloodKeyword));
		}

		void AdoptPluginName(const RE::TESForm* a_form)
		{
			if (!g_state.pluginName.empty() || !a_form) {
				return;
			}
			try {
				const auto* file = a_form->GetFile();
				if (!file) {
					return;
				}
				const auto filename = file->GetFilename();
				if (filename.empty()) {
					return;
				}
				g_state.pluginName.assign(filename.data(), filename.size());
			} catch (...) {
			}
		}

		bool TryGetCureExemplar(const RE::EffectSetting* a_effect, ExemplarResult& a_result) noexcept
		{
			if (!a_effect) {
				return false;
			}
			if (a_effect->HasKeywordString(kCureDiseaseKeyword) && g_state.cureDisease) {
				a_result.potion = g_state.cureDisease;
				a_result.goldValue = g_state.cureDisease->GetGoldValue();
				return true;
			}
			if (a_effect->HasKeywordString(kCurePoisonKeyword) && g_state.curePoison) {
				a_result.potion = g_state.curePoison;
				a_result.goldValue = g_state.curePoison->GetGoldValue();
				return true;
			}
			return false;
		}

		bool ReadGameSetting(std::string_view a_name, float& a_value) noexcept
		{
			try {
				auto* collection = RE::GameSettingCollection::GetSingleton();
				if (!collection) {
					return false;
				}
				const std::string name(a_name);
				auto* setting = collection->GetSetting(name.c_str());
				if (!setting || setting->GetType() != RE::Setting::Type::kFloat) {
					return false;
				}
				const float value = setting->GetFloat();
				if (!std::isfinite(value) || value <= 0.0f) {
					return false;
				}
				a_value = value;
				return true;
			} catch (...) {
				return false;
			}
		}

		bool CompareCondition(float a_left, float a_right, RE::CONDITION_ITEM_DATA::OpCode a_opCode) noexcept
		{
			switch (a_opCode) {
			case RE::CONDITION_ITEM_DATA::OpCode::kEqualTo:
				return a_left == a_right;
			case RE::CONDITION_ITEM_DATA::OpCode::kNotEqualTo:
				return a_left != a_right;
			case RE::CONDITION_ITEM_DATA::OpCode::kGreaterThan:
				return a_left > a_right;
			case RE::CONDITION_ITEM_DATA::OpCode::kGreaterThanOrEqualTo:
				return a_left >= a_right;
			case RE::CONDITION_ITEM_DATA::OpCode::kLessThan:
				return a_left < a_right;
			case RE::CONDITION_ITEM_DATA::OpCode::kLessThanOrEqualTo:
				return a_left <= a_right;
			default:
				return false;
			}
		}

		bool GetConditionComparisonValue(
			const RE::CONDITION_ITEM_DATA& a_condition,
			float& a_value) noexcept
		{
			if (a_condition.flags.global) {
				if (!a_condition.comparisonValue.g || !std::isfinite(a_condition.comparisonValue.g->value)) {
					return false;
				}
				a_value = a_condition.comparisonValue.g->value;
				return true;
			}
			if (!std::isfinite(a_condition.comparisonValue.f)) {
				return false;
			}
			a_value = a_condition.comparisonValue.f;
			return true;
		}

		bool HasActivePerk(
			const AlchemyEvaluationContext& a_context,
			const RE::BGSPerk* a_perk) noexcept
		{
			if (!a_perk) {
				return false;
			}
			if (a_context.seeker.nativeContract && a_context.seeker.perk == a_perk) {
				return true;
			}
			return std::find(a_context.activePerks.begin(), a_context.activePerks.end(), a_perk) != a_context.activePerks.end();
		}

		bool EvaluateConditionItem(
			const RE::TESConditionItem* a_item,
			const RE::EffectSetting* a_effect,
			bool a_potion,
			const AlchemyEvaluationContext& a_context,
			bool& a_effectSpecific,
			bool& a_typeSpecific) noexcept
		{
			bool result = true;
			bool first = true;
			for (auto* item = a_item; item; item = item->next) {
				float comparison = 0.0f;
				if (!GetConditionComparisonValue(item->data, comparison)) {
					return false;
				}
				float value = 0.0f;
				bool conditionResult = false;
				const auto function = item->data.functionData.function.get();
				switch (function) {
				case RE::FUNCTION_DATA::FunctionID::kEPAlchemyEffectHasKeyword: {
					a_effectSpecific = true;
					if (!a_effect || !item->data.functionData.params[0]) {
						return false;
					}
					const auto* form = static_cast<const RE::TESForm*>(item->data.functionData.params[0]);
					const auto* keyword = form ? form->As<RE::BGSKeyword>() : nullptr;
					value = HasAlchemyEffectKeyword(a_effect, keyword) ? 1.0f : 0.0f;
					conditionResult = CompareCondition(value, comparison, item->data.flags.opCode);
					break;
				}
				case RE::FUNCTION_DATA::FunctionID::kHasKeyword: {
					a_effectSpecific = true;
					if (!a_effect || !item->data.functionData.params[0]) {
						return false;
					}
					const auto* form = static_cast<const RE::TESForm*>(item->data.functionData.params[0]);
					const auto* keyword = form ? form->As<RE::BGSKeyword>() : nullptr;
					value = HasAlchemyEffectKeyword(a_effect, keyword) ? 1.0f : 0.0f;
					conditionResult = CompareCondition(value, comparison, item->data.flags.opCode);
					break;
				}
				case RE::FUNCTION_DATA::FunctionID::kEPAlchemyGetMakingPoison:
					a_typeSpecific = true;
					value = a_potion ? 0.0f : 1.0f;
					conditionResult = CompareCondition(value, comparison, item->data.flags.opCode);
					break;
				case RE::FUNCTION_DATA::FunctionID::kGetIsUsedItemType:
					value = kAlchemyItemType;
					conditionResult = CompareCondition(value, comparison, item->data.flags.opCode);
					break;
				case RE::FUNCTION_DATA::FunctionID::kHasPerk: {
					if (!item->data.functionData.params[0]) {
						return false;
					}
					const auto* form = static_cast<const RE::TESForm*>(item->data.functionData.params[0]);
					const auto* perk = form ? form->As<RE::BGSPerk>() : nullptr;
					value = HasActivePerk(a_context, perk) ? 1.0f : 0.0f;
					conditionResult = CompareCondition(value, comparison, item->data.flags.opCode);
					break;
				}
				case RE::FUNCTION_DATA::FunctionID::kGetGlobalValue: {
					if (!item->data.functionData.params[0]) {
						return false;
					}
					const auto* form = static_cast<const RE::TESForm*>(item->data.functionData.params[0]);
					const auto* global = form ? form->As<RE::TESGlobal>() : nullptr;
					if (!global || !std::isfinite(global->value)) {
						return false;
					}
					value = global->value;
					conditionResult = CompareCondition(value, comparison, item->data.flags.opCode);
					break;
				}
				default:
					return false;
				}

				if (first) {
					result = conditionResult;
					first = false;
				} else if (item->data.flags.isOR) {
					result = result || conditionResult;
				} else {
					result = result && conditionResult;
				}
			}
			return result;
		}

		bool EvaluateEntryConditions(
			const RE::BGSEntryPointPerkEntry* a_entry,
			const RE::EffectSetting* a_effect,
			bool a_potion,
			const AlchemyEvaluationContext& a_context,
			bool& a_effectSpecific,
			bool& a_typeSpecific) noexcept
		{
			a_effectSpecific = false;
			a_typeSpecific = false;
			try {
				bool result = true;
				bool first = true;
				for (const auto& condition : a_entry->conditions) {
					if (!condition.head) {
						continue;
					}
					const bool conditionResult = EvaluateConditionItem(
						condition.head, a_effect, a_potion, a_context, a_effectSpecific, a_typeSpecific);
					if (first) {
						result = conditionResult;
						first = false;
					} else if (condition.head->data.flags.isOR) {
						result = result || conditionResult;
					} else {
						result = result && conditionResult;
					}
				}
				return result;
			} catch (...) {
				return false;
			}
		}

		bool IsAlchemistPerk(const RE::BGSPerk* a_perk) noexcept
		{
			if (!a_perk) {
				return false;
			}
			const auto formID = a_perk->GetFormID();
			constexpr std::array<RE::FormID, 5> vanillaAlchemistFormIDs{
				0x000BE127,
				0x000C07CA,
				0x000C07CB,
				0x000C07CC,
				0x000C07CD
			};
			if (std::find(vanillaAlchemistFormIDs.begin(), vanillaAlchemistFormIDs.end(), formID) != vanillaAlchemistFormIDs.end()) {
				return true;
			}
			const auto* editorID = a_perk->GetFormEditorID();
			if (!editorID) {
				return false;
			}
			const std::string_view value(editorID);
			constexpr std::string_view alchemistPrefix = "Alchemist";
			return (value.size() > alchemistPrefix.size() &&
				EqualsIgnoreCase(value.substr(0, alchemistPrefix.size()), alchemistPrefix)) ||
				ContainsIgnoreCase(value, "ORD_Alc_AlchemyMastery");
		}

		bool IsPerkIdentity(const RE::BGSPerk* a_perk, RE::FormID a_localFormID,
			std::string_view a_firstEditorID, std::string_view a_secondEditorID) noexcept
		{
			if (!a_perk) {
				return false;
			}
			if (a_perk->GetFormID() == a_localFormID) {
				return true;
			}
			const auto* editorID = a_perk->GetFormEditorID();
			return editorID && (EqualsIgnoreCase(editorID, a_firstEditorID) ||
				EqualsIgnoreCase(editorID, a_secondEditorID));
		}

		bool IsPhysicianPerk(const RE::BGSPerk* a_perk) noexcept
		{
			return IsPerkIdentity(a_perk, 0x00058215, "Physician", "AlchPhysician");
		}

		bool IsBenefactorPerk(const RE::BGSPerk* a_perk) noexcept
		{
			return IsPerkIdentity(a_perk, 0x00058216, "Benefactor", "AlchBenefactor");
		}

		bool IsPoisonerPerk(const RE::BGSPerk* a_perk) noexcept
		{
			return IsPerkIdentity(a_perk, 0x00058217, "Poisoner", "AlchPoisoner");
		}

		bool IsPhysicianEffect(const RE::EffectSetting* a_effect) noexcept
		{
			return a_effect && (a_effect->HasKeywordString("MagicAlchRestoreHealth") ||
				a_effect->HasKeywordString("MagicAlchRestoreMagicka") ||
				a_effect->HasKeywordString("MagicAlchRestoreStamina"));
		}

		void GetAlchemyPerkMultipliers(
			const RE::EffectSetting* a_effect,
			float a_fallback,
			bool a_potion,
			bool a_includeTypePerks,
			bool a_mixedPotion,
			const AlchemyEvaluationContext& a_context,
			float& a_magnitudeMultiplier,
			float& a_durationMultiplier) noexcept
		{
			const float fallback = std::isfinite(a_fallback) && a_fallback > 0.0f ? a_fallback : 1.0f;
			const float rankFallback = a_context.alchemistPerkRank > 0 ?
				1.0f + static_cast<float>(a_context.alchemistPerkRank) * 0.2f : 1.0f;
			const double contextFallback = a_context.captured ?
				static_cast<double>((std::max)(fallback, rankFallback)) : static_cast<double>(fallback);
			a_magnitudeMultiplier = static_cast<float>(contextFallback);
			a_durationMultiplier = static_cast<float>(contextFallback);
			if (!a_context.captured) {
				return;
			}

			double alchemistMagnitude = 1.0;
			double alchemistDuration = 1.0;
			bool hasAlchemistMagnitude = false;
			bool hasAlchemistDuration = false;
			double otherMagnitude = 1.0;
			double otherDuration = 1.0;
			std::set<const RE::BGSPerk*> inspectedPerks;
			bool physicianApplied = false;
			bool benefactorApplied = false;
			bool benefactorEntryPointFound = false;
			bool poisonerApplied = false;
			bool seekerApplied = false;

			const auto applyValue = [&](const RE::BGSPerk* a_perk, float a_value, bool a_affectsMagnitude, bool a_affectsDuration) {
				if (IsAlchemistPerk(a_perk)) {
					if (a_affectsMagnitude) {
						alchemistMagnitude = hasAlchemistMagnitude ? (std::max)(alchemistMagnitude, static_cast<double>(a_value)) : static_cast<double>(a_value);
						hasAlchemistMagnitude = true;
					}
					if (a_affectsDuration) {
						alchemistDuration = hasAlchemistDuration ? (std::max)(alchemistDuration, static_cast<double>(a_value)) : static_cast<double>(a_value);
						hasAlchemistDuration = true;
					}
				} else {
					if (a_affectsMagnitude) {
						otherMagnitude *= static_cast<double>(a_value);
					}
					if (a_affectsDuration) {
						otherDuration *= static_cast<double>(a_value);
					}
				}
			};

			const auto inspectPerk = [&](const RE::BGSPerk* a_perk) {
				if (!a_perk || !inspectedPerks.insert(a_perk).second ||
					(a_perk == a_context.seeker.perk && !a_context.seeker.nativeContract)) {
					return;
				}
				const bool benefactorPerk = IsBenefactorPerk(a_perk);
				const bool poisonerPerk = IsPoisonerPerk(a_perk);
				if ((benefactorPerk && !algorithm::ShouldApplyBenefactor(
						a_potion, a_effect && Adapter::HasBeneficialKeyword(a_effect), a_includeTypePerks, a_mixedPotion)) ||
					(poisonerPerk && !algorithm::ShouldApplyPoisoner(
						a_potion, a_effect && Adapter::HasHarmfulKeyword(a_effect), a_includeTypePerks))) {
					return;
				}
				for (const auto* entry : a_perk->perkEntries) {
					if (!entry || entry->GetType() != RE::PERK_ENTRY_TYPE::kEntryPoint) {
						continue;
					}
					const auto* entryPoint = static_cast<const RE::BGSEntryPointPerkEntry*>(entry);
					if (!entryPoint->IsEntryPoint(RE::BGSEntryPoint::ENTRY_POINTS::kModAlchemyEffectiveness) ||
						!entryPoint->functionData ||
						entryPoint->functionData->GetType() != RE::BGSEntryPointFunctionData::ENTRY_POINT_FUNCTION_DATA::kOneValue) {
						continue;
					}
					const auto* functionData = static_cast<const RE::BGSEntryPointFunctionDataOneValue*>(entryPoint->functionData);
					if (!std::isfinite(functionData->data) || functionData->data <= 0.0f ||
						entryPoint->entryData.function.get() != RE::BGSEntryPointFunction::ENTRY_POINT_FUNCTIONS::kMultiplyValue) {
						continue;
					}
					if (IsBenefactorPerk(a_perk)) {
						benefactorEntryPointFound = true;
					}
					bool effectSpecific = false;
					bool typeSpecific = false;
					if (!EvaluateEntryConditions(entryPoint, a_effect, a_potion, a_context, effectSpecific, typeSpecific) ||
						(typeSpecific && !a_includeTypePerks)) {
						continue;
					}
					const bool affectsMagnitude = !a_effect || a_effect->data.flags.all(
						RE::EffectSetting::EffectSettingData::Flag::kPowerAffectsMagnitude);
					const bool affectsDuration = !a_effect || a_effect->data.flags.all(
						RE::EffectSetting::EffectSettingData::Flag::kPowerAffectsDuration);
					if (affectsMagnitude || affectsDuration) {
						physicianApplied = physicianApplied || IsPhysicianPerk(a_perk);
						benefactorApplied = benefactorApplied || IsBenefactorPerk(a_perk);
						poisonerApplied = poisonerApplied || IsPoisonerPerk(a_perk);
						seekerApplied = seekerApplied || a_perk == a_context.seeker.perk;
						applyValue(a_perk, functionData->data, affectsMagnitude, affectsDuration);
					}
				}
			};

			try {
				for (const auto* perk : a_context.activePerks) {
					inspectPerk(perk);
				}
				if (a_context.seeker.nativeContract) {
					inspectPerk(a_context.seeker.perk);
				}
			} catch (...) {
			}

			if (algorithm::ShouldApplyPhysicianFallback(
				Adapter::IsActive(),
				a_context.hasPhysician,
				physicianApplied,
				IsPhysicianEffect(a_effect))) {
				otherMagnitude *= 1.25;
				otherDuration *= 1.25;
			}
			if (algorithm::ShouldApplyBenefactorFallback(
				a_potion,
				a_effect && Adapter::HasBeneficialKeyword(a_effect),
				a_includeTypePerks,
				a_context.hasBenefactor,
				benefactorApplied,
				benefactorEntryPointFound,
				a_mixedPotion)) {
				otherMagnitude *= 1.25;
				otherDuration *= 1.25;
			}
			if (algorithm::ShouldApplyPoisoner(
				a_potion, a_effect && Adapter::HasHarmfulKeyword(a_effect), a_includeTypePerks) &&
				a_context.hasPoisoner && !poisonerApplied) {
				otherMagnitude *= 1.25;
				otherDuration *= 1.25;
			}
			if (a_context.hasSeekerOfShadows && !seekerApplied) {
				otherMagnitude *= 1.1;
				otherDuration *= 1.1;
			}

			const double magnitude = otherMagnitude * (hasAlchemistMagnitude ? (std::max)(contextFallback, alchemistMagnitude) : contextFallback);
			const double duration = otherDuration * (hasAlchemistDuration ? (std::max)(contextFallback, alchemistDuration) : contextFallback);
			a_magnitudeMultiplier = std::isfinite(magnitude) && magnitude > 0.0 ? static_cast<float>(magnitude) : static_cast<float>(contextFallback);
			a_durationMultiplier = std::isfinite(duration) && duration > 0.0 ? static_cast<float>(duration) : static_cast<float>(contextFallback);
		}

		void RefreshOptions() noexcept
		{
			const bool previousPotionHandling = g_state.potionHandlingEnabled;
			const bool previousImpureProcessing = g_state.impureProcessingEnabled;
			const bool previousReweighting = g_state.reweightingEnabled;
			const bool previousRenaming = g_state.renamingEnabled;
			if (!g_state.detected || !g_state.active) {
				g_state.potionHandlingEnabled = false;
				g_state.impureProcessingEnabled = false;
				g_state.reweightingEnabled = false;
				g_state.renamingEnabled = false;
			} else {
				g_state.potionHandlingEnabled = g_state.disablePotionHandling && !IsOne(g_state.disablePotionHandling);
				g_state.impureProcessingEnabled = g_state.potionHandlingEnabled && IsOne(g_state.impurePotions);
				g_state.reweightingEnabled = g_state.potionHandlingEnabled && IsOne(g_state.reweightPotions);
				g_state.renamingEnabled = g_state.potionHandlingEnabled && IsOne(g_state.renamePotions);
			}
			if (g_state.initialized && (previousPotionHandling != g_state.potionHandlingEnabled ||
				previousImpureProcessing != g_state.impureProcessingEnabled ||
				previousReweighting != g_state.reweightingEnabled ||
				previousRenaming != g_state.renamingEnabled) &&
				g_state.calculationRevision < (std::numeric_limits<std::uint64_t>::max)()) {
				++g_state.calculationRevision;
			}
		}
	}

	void Adapter::Initialize() noexcept
	{
		g_state = {};
		try {
			const auto plugin = FindCacoPlugin();
			if (plugin.file) {
				g_state.pluginName = plugin.name;
			}
			for (std::size_t i = 0; i < kFamilies.size(); ++i) {
				const auto& definition = kFamilies[i];
				auto& family = g_state.families[i];
				family.durationGlobal = ResolveRecord<RE::TESGlobal>(definition.durationGlobal, definition.durationGlobalFormID);
				for (std::size_t position = 0; position < definition.ingredientLists.size(); ++position) {
					family.ingredientLists[position] = ResolveRecord<RE::BGSListForm>(
						definition.ingredientLists[position], definition.ingredientListFormIDs[position]);
				}
				for (std::size_t d = 0; d < 3; ++d) {
					if (!definition.durationEffects[d].empty()) {
						family.durationEffects[d] = ResolveEditorID<RE::EffectSetting>(definition.durationEffects[d]);
					}
					if (!definition.alternateDurationEffects[d].empty()) {
						family.alternateDurationEffects[d] = ResolveEditorID<RE::EffectSetting>(definition.alternateDurationEffects[d]);
					}
				}
				if (i == 0) {
					static constexpr std::array<std::string_view, 3> legacyBloodEffects{
						"DLC1AlchRestoreHealthBlood_1sec",
						"DLC1AlchRestoreHealthBlood_5sec",
						"DLC1AlchRestoreHealthBlood_10sec"
					};
					for (std::size_t d = 0; d < 3; ++d) {
						family.legacyEffects[d] = ResolveEditorID<RE::EffectSetting>(legacyBloodEffects[d]);
					}
				} else if (i == 3) {
					static constexpr std::array<std::string_view, 3> legacyUndeadEffects{
						"AlchDamageUndeadHealth_1sec",
						"AlchDamageUndeadHealth_5sec",
						"AlchDamageUndeadHealth_10sec"
					};
					for (std::size_t d = 0; d < 3; ++d) {
						family.legacyEffects[d] = ResolveEditorID<RE::EffectSetting>(legacyUndeadEffects[d]);
					}
				}
			}

			g_state.disablePotionHandling = ResolveRecord<RE::TESGlobal>(
				"CACO_OptionDisableAllPotionHandling", kDisablePotionHandlingFormID);
			if (!g_state.disablePotionHandling) {
				g_state.disablePotionHandling = ResolveEditorID<RE::TESGlobal>("CACO_OptionDisablePotionHandling");
			}
			g_state.impurePotions = ResolveRecord<RE::TESGlobal>("CACO_OptionImpurePotions", kImpurePotionsFormID);
			g_state.reweightPotions = ResolveRecord<RE::TESGlobal>("CACO_OptionReweightPotions", kReweightPotionsFormID);
			g_state.renamePotions = ResolveRecord<RE::TESGlobal>("CACO_OptionRenamePotions", kRenamePotionsFormID);
			if (!g_state.renamePotions) {
				g_state.renamePotions = ResolveEditorID<RE::TESGlobal>("CACORenamePotionOption_KRY");
			}
			g_state.alchemyEffectsList = ResolveRecord<RE::BGSListForm>(
				"CACO_AlchemyEffectsList", kAlchemyEffectsListFormID);
			g_state.allPotionList = ResolveRecord<RE::BGSListForm>(
				"CACO_AlchemyAllPotionList", kAlchemyAllPotionListFormID);
			g_state.cureDisease = ResolveEditorID<RE::AlchemyItem>("CureDisease");
			g_state.curePoison = ResolveEditorID<RE::AlchemyItem>("CurePoison");
			if (g_state.pluginName.empty()) {
				AdoptPluginName(g_state.alchemyEffectsList);
				AdoptPluginName(g_state.allPotionList);
			}
			g_state.alchemySettingsReady =
				ReadGameSetting("fAlchemyIngredientInitMult", g_state.alchemyIngredientInitMultiplier) &&
				ReadGameSetting("fAlchemySkillFactor", g_state.alchemySkillFactor);
			RefreshCalculationRevision();

			const bool coreListsFound = g_state.alchemyEffectsList && g_state.allPotionList;
			std::size_t ingredientListsFound = 0;
			std::size_t durationGlobalsFound = 0;
			for (const auto& family : g_state.families) {
				durationGlobalsFound += family.durationGlobal ? 1 : 0;
				ingredientListsFound += static_cast<std::size_t>(std::count_if(
					family.ingredientLists.begin(), family.ingredientLists.end(), [](const auto* list) { return list != nullptr; }));
			}
			const bool optionRecordsFound = g_state.disablePotionHandling || g_state.impurePotions ||
				g_state.reweightPotions || g_state.renamePotions;
			const bool cacoRecordsFound = coreListsFound || ingredientListsFound != 0 || durationGlobalsFound != 0;
			const bool pluginFound = plugin.file != nullptr;
			g_state.detected = pluginFound || cacoRecordsFound || optionRecordsFound || g_state.cureDisease || g_state.curePoison;
			const bool calculationRecordsFound = pluginFound || coreListsFound || ingredientListsFound != 0;
			g_state.active = calculationRecordsFound && g_state.alchemySettingsReady;
			g_state.initialized = true;
			RefreshOptions();

		} catch (const std::exception&) {
			g_state = {};
			g_state.initialized = true;
		} catch (...) {
			g_state = {};
			g_state.initialized = true;
		}
	}

	void Adapter::Refresh() noexcept
	{
		if (!g_state.initialized) {
			return;
		}
		try {
			RefreshOptions();
			RefreshCalculationRevision();
		} catch (const std::exception&) {
			g_state.potionHandlingEnabled = false;
			g_state.impureProcessingEnabled = false;
			g_state.reweightingEnabled = false;
			g_state.renamingEnabled = false;
		} catch (...) {
			g_state.potionHandlingEnabled = false;
			g_state.impureProcessingEnabled = false;
			g_state.reweightingEnabled = false;
			g_state.renamingEnabled = false;
		}
	}

	bool Adapter::IsDetected() noexcept
	{
		return g_state.initialized && g_state.detected;
	}

	bool Adapter::IsActive() noexcept
	{
		return g_state.initialized && g_state.active;
	}

	std::uint64_t Adapter::GetCalculationRevision() noexcept
	{
		return IsActive() ? g_state.calculationRevision : 0;
	}

	bool Adapter::IsPotionHandlingEnabled() noexcept
	{
		return IsActive() && g_state.potionHandlingEnabled;
	}

	bool Adapter::IsImpureProcessingEnabled() noexcept
	{
		return IsActive() && g_state.impureProcessingEnabled;
	}

	bool Adapter::IsReweightingEnabled() noexcept
	{
		return IsActive() && g_state.reweightingEnabled;
	}

	bool Adapter::IsRenamingEnabled() noexcept
	{
		return IsActive() && g_state.renamingEnabled;
	}

	bool Adapter::TryGetAlchemyEffectivenessMultiplier(
		float a_alchemyLevel,
		float a_fallbackPerkMultiplier,
		const AlchemyEvaluationContext& a_context,
		float& a_multiplier) noexcept
	{
		return TryGetAlchemyEffectivenessMultiplier(
			nullptr,
			a_alchemyLevel,
			a_fallbackPerkMultiplier,
			false,
			false,
			false,
			a_context,
			a_multiplier);
	}

	bool Adapter::TryGetAlchemyEffectivenessMultiplier(
		const RE::EffectSetting* a_effect,
		float a_alchemyLevel,
		float a_fallbackPerkMultiplier,
		bool a_potion,
		bool a_includeTypePerks,
		bool a_mixedPotion,
		const AlchemyEvaluationContext& a_context,
		float& a_multiplier) noexcept
	{
		float durationMultiplier = 1.0f;
		return TryGetAlchemyEffectivenessMultipliers(
			a_effect,
			a_alchemyLevel,
			a_fallbackPerkMultiplier,
			a_potion,
			a_includeTypePerks,
			a_mixedPotion,
			a_context,
			a_multiplier,
			durationMultiplier);
	}

	bool Adapter::TryGetAlchemyEffectivenessMultipliers(
		const RE::EffectSetting* a_effect,
		float a_alchemyLevel,
		float a_fallbackPerkMultiplier,
		bool a_potion,
		bool a_includeTypePerks,
		bool a_mixedPotion,
		const AlchemyEvaluationContext& a_context,
		float& a_magnitudeMultiplier,
		float& a_durationMultiplier) noexcept
	{
		if (!IsActive()) {
			return false;
		}
		if (!g_state.alchemySettingsReady) {
			return false;
		}
		if (!std::isfinite(a_alchemyLevel) || !std::isfinite(a_fallbackPerkMultiplier) ||
			a_fallbackPerkMultiplier <= 0.0f) {
			return false;
		}
		const float baseEffectiveness = algorithm::CalculateAlchemyEffectiveness(
			g_state.alchemyIngredientInitMultiplier, g_state.alchemySkillFactor, a_alchemyLevel, 1.0f);
		if (!std::isfinite(baseEffectiveness) || baseEffectiveness <= 0.0f) {
			return false;
		}
		float magnitudePerkMultiplier = 1.0f;
		float durationPerkMultiplier = 1.0f;
		GetAlchemyPerkMultipliers(
			a_effect,
			a_fallbackPerkMultiplier,
			a_potion,
			a_includeTypePerks,
			a_mixedPotion,
			a_context,
			magnitudePerkMultiplier,
			durationPerkMultiplier);
		a_magnitudeMultiplier = baseEffectiveness * magnitudePerkMultiplier;
		a_durationMultiplier = baseEffectiveness * durationPerkMultiplier;
		if (!std::isfinite(a_magnitudeMultiplier) || a_magnitudeMultiplier <= 0.0f ||
			!std::isfinite(a_durationMultiplier) || a_durationMultiplier <= 0.0f) {
			return false;
		}
		return true;
	}

	bool Adapter::TryGetVanillaAlchemyEffectivenessMultipliers(
		const RE::EffectSetting* a_effect,
		float a_alchemyLevel,
		float a_fallbackAlchemistMultiplier,
		bool a_potion,
		bool a_includeTypePerks,
		bool a_mixedPotion,
		const AlchemyEvaluationContext& a_context,
		float& a_magnitudeMultiplier,
		float& a_durationMultiplier) noexcept
	{
		if (!a_context.captured || !std::isfinite(a_alchemyLevel) ||
			!std::isfinite(a_fallbackAlchemistMultiplier) || a_fallbackAlchemistMultiplier <= 0.0f) {
			return false;
		}
		const float baseEffectiveness = algorithm::CalculateVanillaAlchemyEffectiveness(
			a_alchemyLevel, 1.0f);
		if (!std::isfinite(baseEffectiveness) || baseEffectiveness <= 0.0f) {
			return false;
		}
		float magnitudePerkMultiplier = 1.0f;
		float durationPerkMultiplier = 1.0f;
		GetAlchemyPerkMultipliers(
			a_effect,
			a_fallbackAlchemistMultiplier,
			a_potion,
			a_includeTypePerks,
			a_mixedPotion,
			a_context,
			magnitudePerkMultiplier,
			durationPerkMultiplier);
		a_magnitudeMultiplier = baseEffectiveness * magnitudePerkMultiplier;
		a_durationMultiplier = baseEffectiveness * durationPerkMultiplier;
		return std::isfinite(a_magnitudeMultiplier) && a_magnitudeMultiplier > 0.0f &&
			std::isfinite(a_durationMultiplier) && a_durationMultiplier > 0.0f;
	}

	bool Adapter::HasBeneficialKeyword(const RE::EffectSetting* a_effect) noexcept
	{
		return a_effect && (a_effect->HasKeywordString(kBeneficialKeyword) ||
			(!a_effect->HasKeywordString(kHarmfulKeyword) && !a_effect->IsHostile()));
	}

	bool Adapter::HasHarmfulKeyword(const RE::EffectSetting* a_effect) noexcept
	{
		return a_effect && (a_effect->HasKeywordString(kHarmfulKeyword) || a_effect->IsHostile());
	}

	bool Adapter::IsDurationBased(const RE::EffectSetting* a_effect) noexcept
	{
		if (!a_effect) {
			return false;
		}
		if (a_effect->HasKeywordString(kDurationBasedKeyword)) {
			return true;
		}
		if (!a_effect->data.flags.all(RE::EffectSetting::EffectSettingData::Flag::kPowerAffectsDuration)) {
			return false;
		}
		// CACO reclassifies effects like Silence, Resist Disease, and Slow to scale with duration
		// instead of magnitude. The no-magnitude flag is not reliable on these records, but
		// power-affects-magnitude being false is the validated signal.
		return a_effect->data.flags.all(RE::EffectSetting::EffectSettingData::Flag::kNoMagnitude) ||
			!a_effect->data.flags.all(RE::EffectSetting::EffectSettingData::Flag::kPowerAffectsMagnitude);
	}

	RE::EffectSetting* Adapter::ResolveIngredientEffect(
		const RE::IngredientItem* a_ingredient,
		RE::EffectSetting* a_sourceEffect) noexcept
	{
		if (!IsActive() || !a_ingredient || !a_sourceEffect) {
			return a_sourceEffect;
		}
		const int familyIndex = FindFamily(a_sourceEffect);
		if (familyIndex < 0 || !IsIngredientListedInFamily(
				a_ingredient, g_state.families[static_cast<std::size_t>(familyIndex)])) {
			return a_sourceEffect;
		}
		if (auto* activeEffect = ResolveDurationEffect(static_cast<std::size_t>(familyIndex), a_sourceEffect)) {
			return activeEffect;
		}
		return a_sourceEffect;
	}

	float Adapter::ApplyImpureMagnitude(float a_value) noexcept
	{
		if (!std::isfinite(a_value)) {
			return a_value;
		}
		return a_value * 0.2f;
	}

	float Adapter::ApplyImpureDuration(float a_value) noexcept
	{
		if (!std::isfinite(a_value)) {
			return a_value;
		}
		const double reduced = std::trunc(static_cast<double>(a_value) * 0.2);
		const double minimum = static_cast<double>((std::numeric_limits<std::int32_t>::min)());
		const double maximum = static_cast<double>((std::numeric_limits<std::int32_t>::max)());
		return static_cast<float>(static_cast<std::int32_t>((std::clamp)(reduced, minimum, maximum)));
	}

	float Adapter::ApplyImpureGold(float a_value) noexcept
	{
		if (!std::isfinite(a_value) || a_value <= 0.0f) {
			return 0.0f;
		}
		const double preAdjustment = (std::min)(std::floor(static_cast<double>(a_value)),
			static_cast<double>((std::numeric_limits<std::int32_t>::max)()));
		return static_cast<float>(algorithm::ApplyImpureGold(static_cast<std::int32_t>(preAdjustment)));
	}

	std::int32_t Adapter::ApplyImpureGold(std::int32_t a_value) noexcept
	{
		return algorithm::ApplyImpureGold(a_value);
	}

	bool Adapter::TryGetCrucibleExemplar(
		const RE::EffectSetting* a_primaryEffect,
		float a_magnitude,
		float a_duration,
		ExemplarResult& a_result) noexcept
	{
		a_result = {};
		if (!IsActive() || !a_primaryEffect || !std::isfinite(a_magnitude) || !std::isfinite(a_duration)) {
			return false;
		}
		if (TryGetCureExemplar(a_primaryEffect, a_result)) {
			return true;
		}
		const auto* compareList = FindPotionList(a_primaryEffect);
		if (!compareList) {
			return false;
		}
		const bool durationBased = IsDurationBased(a_primaryEffect);
		const float value = durationBased ? a_duration : a_magnitude;
		const std::size_t comparisonPosition = durationBased ? 0 : GetComparisonPosition(a_primaryEffect);
		std::size_t quality = 0;
		if (!SelectQuality(compareList, value, durationBased, comparisonPosition, quality)) {
			return false;
		}
		const auto* exemplar = GetPotionAt(compareList, quality);
		if (!exemplar) {
			return false;
		}
		a_result.potion = exemplar;
		a_result.goldValue = exemplar->GetGoldValue();
		return true;
	}

	bool Adapter::TryGetPotionName(
		std::size_t a_effectCount,
		const RE::EffectSetting* a_primaryEffect,
		float a_magnitude,
		float a_duration,
		std::string_view a_primaryName,
		std::string_view a_secondaryName,
		bool a_isPoison,
		bool a_impure,
		std::string_view a_potionPrefix,
		std::string_view a_poisonPrefix,
		std::string& a_name) noexcept
	{
		a_name.clear();
		if (!IsRenamingEnabled() || !a_primaryEffect || a_effectCount == 0 || a_primaryName.empty()) {
			return false;
		}
		try {
			std::string quality;
			if (a_impure) {
				quality = "Impure ";
			} else if (!IsCureEffect(a_primaryEffect)) {
				const auto* compareList = FindPotionList(a_primaryEffect);
				std::size_t qualityIndex = 0;
				const bool durationBased = IsDurationBased(a_primaryEffect);
				const std::size_t comparisonPosition = durationBased ? 0 : GetComparisonPosition(a_primaryEffect);
				if (!compareList || !SelectQuality(compareList, durationBased ? a_duration : a_magnitude,
					durationBased, comparisonPosition, qualityIndex)) {
					return false;
				}
				static constexpr std::array<std::string_view, 5> harmfulQualities{ "Weak ", "Standard ", "Potent ", "Malign ", "Devastating " };
				static constexpr std::array<std::string_view, 5> beneficialQualities{ "Weak ", "Standard ", "Quality ", "Potent ", "Grand " };
				quality = std::string(a_isPoison ? harmfulQualities[qualityIndex] : beneficialQualities[qualityIndex]);
			}

			const auto prefix = a_isPoison ? a_poisonPrefix : a_potionPrefix;
			a_name = quality;
			a_name += prefix;
			if (!a_name.empty() && a_name.back() != ' ') {
				a_name += ' ';
			}
			a_name += a_primaryName;
			if (a_effectCount == 2 && !a_secondaryName.empty()) {
				a_name += " & ";
				a_name += a_secondaryName;
			}
			return true;
		} catch (...) {
			a_name.clear();
			return false;
		}
	}

	bool Adapter::TryGetPotionWeight(
		std::size_t a_effectCount,
		bool a_hasBeneficial,
		bool a_hasHarmful,
		bool a_hasPurity,
		bool a_hasConcentratedPoison,
		float& a_weight) noexcept
	{
		if (!IsReweightingEnabled() || a_effectCount == 0) {
			return false;
		}
		const bool pureBeneficial = a_hasBeneficial && !a_hasHarmful && a_hasPurity;
		const bool pureHarmful = a_hasHarmful && !a_hasBeneficial && a_hasConcentratedPoison;
		const bool reducedWeight = pureBeneficial || pureHarmful;
		if (a_effectCount == 1) {
			a_weight = reducedWeight ? 0.2f : 0.3f;
		} else if (a_effectCount == 2) {
			a_weight = reducedWeight ? 0.3f : 0.4f;
		} else {
			a_weight = reducedWeight ? 0.4f : 0.5f;
		}
		return true;
	}
}
