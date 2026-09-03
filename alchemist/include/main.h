#pragma once

#include "RE/Skyrim.h"
#include "REX/REX/INI.h"
#include "SKSE/SKSE.h"

#include "AlchemistEngine.h"
#include "AlchemyPlus/AlchemyPlus.h"
#include "CACO/CACO.h"
#include "SeekerOfShadows.h"

#include <time.h>
#include <cmath>
#include <string>
#include <set>
#include <thread>
#include <algorithm>
#include <sstream>
#include <iterator>
#include <filesystem>
#include <memory>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <functional>

using std::set;
using std::string;
using std::thread;
using std::map;
using std::vector;
using IngredientItem = RE::IngredientItem;
using GameEffect = RE::Effect;

inline REX::INI::I32<> kIgnorePlayer("General", "IgnorePlayer", 0);
inline REX::INI::I32<> kDeveloper("General", "developer", 0);
inline REX::INI::I32<> kProtectIngredients("General", "ProtectIngredients", 0);
inline REX::INI::I32<> kSinglethreaded("General", "Singlethreaded", 0);
inline REX::INI::I32<> kNumberOfIngredientsToStressTest("General", "NumberOfIngredientsToStressTest", 0);
inline constexpr char kDefaultProtectedIngredients[] = "Berit's Ashes,Bliss Bug Thorax|11,Bone Hawk Claw,Briar Heart|3,Corkbulb Root,Corrupted Human Heart,Crimson Nirnroot|31,Daedra Heart,Deathbell|32,Dragon's Tongue|11,Ectoplasm|11,Farengar's Frost Salt,Fine-Cut Void Salts,Fire Salts|21,Frost Mirriam|11,Frost Salts|11,Giant's Toe|3,Goldfish|2,Hagraven Claw|2,Hagraven Feathers|2,Human Heart,Ice Wraith Teeth|6,Ironwood Fruit|2,Jarrin Root,Jazbay Grapes|21,Juniper Berries|2,Juvenile Mudcrab|2,Large Antlers,Mudcrab Chitin,Netch Jelly|6,Nightshade|21,Nirnroot|21,Salt Pile|11,Scathecraw|11,Simon Rodayne's Heart,Slaughterfish Scales,Taproot|4,Torchbug Abdomen|11,Torchbug Thorax|11,Troll Fat|2,Vampire Dust|3,Void Salts|12";
inline REX::INI::Str<> kProtectedIngredients("General", "ProtectedIngredients", kDefaultProtectedIngredients);
inline REX::INI::Str<> kPotionPoison("General", "PotionPoison", "Potion of,Poison of");

namespace alchemist {
	class Effect {
	public:
		string name;
		bool beneficial;
		bool harmful;
		bool hostile;
		bool powerAffectsMagnitude;
		float magnitude;
		float calcMagnitude;
		bool powerAffectsDuration;
		bool durationBased;
		float duration;
		float calcDuration;
		float baseCost;
		float calcCost;
		double nativeCost;
		float sourceCost;
		double nativeOrderCost;
		RE::EffectSetting* sourceBaseEffect;
		RE::EffectSetting* baseEffect;
		string description;
		GameEffect* sourceEffect;
		bool operator< (const Effect& effect) const {
			const auto* leftIdentity = sourceBaseEffect ? sourceBaseEffect : baseEffect;
			const auto* rightIdentity = effect.sourceBaseEffect ? effect.sourceBaseEffect : effect.baseEffect;
			if (leftIdentity && rightIdentity) {
				const auto leftFormID = leftIdentity->GetFormID();
				const auto rightFormID = rightIdentity->GetFormID();
				if (leftFormID != rightFormID) {
					return leftFormID < rightFormID;
				}
				if (leftIdentity != rightIdentity) {
					return std::less<const RE::EffectSetting*>{}(leftIdentity, rightIdentity);
				}
				return false;
			}
			if (leftIdentity != rightIdentity) {
				return leftIdentity != nullptr;
			}
			return name < effect.name;
		}
		bool operator== (const Effect& effect) const {
			const auto* leftIdentity = sourceBaseEffect ? sourceBaseEffect : baseEffect;
			const auto* rightIdentity = effect.sourceBaseEffect ? effect.sourceBaseEffect : effect.baseEffect;
			if (leftIdentity || rightIdentity) {
				return leftIdentity == rightIdentity;
			}
			return name == effect.name;
		}
		Effect(GameEffect* effect, RE::EffectSetting* baseEffectOverride = nullptr);
		Effect() {
			beneficial = false;
			harmful = false;
			hostile = false;
			powerAffectsMagnitude = false;
			magnitude = 0;
			calcMagnitude = 0;
			powerAffectsDuration = false;
			duration = 0;
			calcDuration = 0;
			baseCost = 0;
			calcCost = 0;
			nativeCost = 0;
			sourceCost = 0;
			nativeOrderCost = 0;
			sourceEffect = nullptr;
			sourceBaseEffect = nullptr;
			baseEffect = nullptr;
		};
	};

	using EffectList = vector<Effect>;

	struct NativePotionResult {
		EffectList effects;
		Effect controlEffect;
		float cost = 0;
		// The integer value before CACO's optional post-processing; compatibility code keeps this separate from cost.
		std::int32_t preAdjustmentGold = 0;
		bool hasBeneficial = false;
		bool hasHarmful = false;
		bool isPoison = false;
		bool usedCrucibleExemplar = false;
		std::int32_t exemplarGoldValue = 0;
		bool valid = false;
	};

	class Ingredient {
	public:
		string name;
		IngredientItem* nativeIngredient = nullptr;
		EffectList effects;
		int inventoryCount;
		bool operator< (const Ingredient& ingredient) const {
			return name < ingredient.name;
		}
		bool operator== (const Ingredient& ingredient) const {
			return name == ingredient.name;
		}
		Ingredient(IngredientItem* ingredient);
		Ingredient(string n, int ic) {
			name = n;
			inventoryCount = ic;
		};
		Ingredient() {
			inventoryCount = 0;
		};
	};

	namespace str {
		inline std::vector<std::string> split(std::string const& str, char delim) {
			std::vector<std::string> tokens;
			size_t start;
			size_t end = 0;
			while ((start = str.find_first_not_of(delim, end)) != std::string::npos) {
				end = str.find(delim, start);
				tokens.push_back(str.substr(start, end - start));
			}
			return tokens;
		}

		inline int toInt(string s) {
			int i;
			std::istringstream(s) >> i;
			return i;
		}

		inline string fromChar(RE::BSFixedString c) {
			return c.c_str() ? c.c_str() : "";
		}

		inline string fromInt(int n) {
			char n_char[30];
			snprintf(n_char, 30, "%d", n);
			string n_str = n_char;
			return n_str;
		}

		inline string fromFloat(float f) {
			if (abs(f - int(f)) == 0) {
				return fromInt(f);
			}
			char f_char[30];
			snprintf(f_char, 30, "%.2f", f);
			string f_str = f_char;
			return f_str;
		}

		inline string replace(string target, const string& match, const string& replacement) {
			if (match.empty()) {
				return target;
			}
			size_t pos = 0;
			while ((pos = target.find(match, pos)) != string::npos) {
				target.replace(pos, match.length(), replacement);
				pos += replacement.length();
			}
			return target;
		}

		inline string printSort3(string item1, string item2, string item3) {
			string sorted = "";
			if (item1 < item2 && item1 < item3) {
				if (item2 < item3) {
					sorted = item1 + ", " + item2 + ", " + item3;
				}
				else {
					sorted = item1 + ", " + item3 + ", " + item2;
				}
			}
			else if (item2 < item1 && item2 < item3) {
				if (item1 < item3) {
					sorted = item2 + ", " + item1 + ", " + item3;
				}
				else {
					sorted = item2 + ", " + item3 + ", " + item1;
				}
			}
			else if (item1 < item2) {
				sorted = item3 + ", " + item1 + ", " + item2;
			}
			else {
				sorted = item3 + ", " + item2 + ", " + item1;
			}
			return sorted;
		}

		inline string printSort2(string item1, string item2) {
			string sorted = "";
			if (item1 < item2) {
				sorted = item1 + ", " + item2;
			}
			else {
				sorted = item2 + ", " + item1;
			}
			return sorted;
		}
	}

	struct PotionPrefixes {
		string potion = "Potion of";
		string poison = "Poison of";
	};

	inline PotionPrefixes getPotionPrefixes() {
		PotionPrefixes prefixes;
		const auto configuredPrefixes = str::split(kPotionPoison.GetValue(), ',');
		if (!configuredPrefixes.empty()) {
			prefixes.potion = configuredPrefixes.front();
		}
		if (configuredPrefixes.size() > 1) {
			prefixes.poison = configuredPrefixes.at(1);
		}
		return prefixes;
	}

	class Player {
	public:
		float alchemyLevel;
		float fortifyAlchemyLevel;
		float alchemistPerkLevel;
		float alchemistPerkMultiplier;
		bool hasPerkPurity;
		bool hasPerkPhysician;
		bool hasPerkBenefactor;
		bool hasPerkPoisoner;
		bool hasPerkConcentratedPoison;
		bool hasSeekerOfShadows;
		caco::AlchemyEvaluationContext alchemyEvaluationContext;
		string state;
		string lastState;
		string miniState;

		bool isPerkNamed(const RE::BGSPerk* a_perk, const string& name) {
			if (!a_perk) {
				return false;
			}
			if (name == "Alchemist") {
				const auto formId = a_perk->GetFormID();
				if (formId == 0x000BE127 || formId == 0x000C07CA || formId == 0x000C07CB ||
					formId == 0x000C07CC || formId == 0x000C07CD) {
					return true;
				}
				const auto* edid = a_perk->GetFormEditorID();
				if (edid && (_strnicmp(edid, "Alchemist", 9) == 0 || _strnicmp(edid, "ORD_Alc_AlchemyMastery", 22) == 0)) {
					return true;
				}
			} else if (name == "Physician") {
				if (a_perk->GetFormID() == 0x00058215) return true;
				const auto* edid = a_perk->GetFormEditorID();
				if (edid && (_stricmp(edid, "Physician") == 0 || _stricmp(edid, "AlchPhysician") == 0)) return true;
			} else if (name == "Benefactor") {
				if (a_perk->GetFormID() == 0x00058216) return true;
				const auto* edid = a_perk->GetFormEditorID();
				if (edid && (_stricmp(edid, "Benefactor") == 0 || _stricmp(edid, "AlchBenefactor") == 0)) return true;
			} else if (name == "Poisoner") {
				if (a_perk->GetFormID() == 0x00058217) return true;
				const auto* edid = a_perk->GetFormEditorID();
				if (edid && (_stricmp(edid, "Poisoner") == 0 || _stricmp(edid, "AlchPoisoner") == 0)) return true;
			} else if (name == "Concentrated Poison") {
				if (a_perk->GetFormID() == 0x00105F2F) return true;
				const auto* edid = a_perk->GetFormEditorID();
				if (edid && (_stricmp(edid, "ConcentratedPoison") == 0 || _stricmp(edid, "AlchConcentratedPoison") == 0)) return true;
			} else if (name == "Purity") {
				if (a_perk->GetFormID() == 0x0005821D) return true;
				const auto* edid = a_perk->GetFormEditorID();
				if (edid && (_stricmp(edid, "Purity") == 0 || _stricmp(edid, "AlchPurity") == 0)) return true;
			}
			const auto* fullName = a_perk->GetFullName();
			return fullName && string(fullName) == name;
		}

		int getAlchemistTier(const RE::BGSPerk* a_perk) {
			if (!a_perk) {
				return 0;
			}
			const auto formId = a_perk->GetFormID();
			switch (formId) {
			case 0x000BE127: return 1;
			case 0x000C07CA: return 2;
			case 0x000C07CB: return 3;
			case 0x000C07CC: return 4;
			case 0x000C07CD: return 5;
			default: break;
			}
			const auto* editorId = a_perk->GetFormEditorID();
			if (editorId && *editorId) {
				std::string edid(editorId);
				if (edid == "Alchemist00" || edid.find("00") != std::string::npos) return 1;
				if (edid == "Alchemist20" || edid.find("20") != std::string::npos) return 2;
				if (edid == "Alchemist40" || edid.find("40") != std::string::npos) return 3;
				if (edid == "Alchemist60" || edid.find("60") != std::string::npos) return 4;
				if (edid == "Alchemist80" || edid.find("80") != std::string::npos) return 5;
			}
			try {
				if (auto* dataHandler = RE::TESDataHandler::GetSingleton()) {
					int tier = 0;
					for (const auto* perk : dataHandler->GetFormArray<RE::BGSPerk>()) {
						if (!perk || !isPerkNamed(perk, "Alchemist")) {
							continue;
						}
						++tier;
						if (perk == a_perk) {
							return tier;
						}
					}
				}
			} catch (...) {
			}
			for (const auto* entry : a_perk->perkEntries) {
				if (entry && entry->GetType() == RE::PERK_ENTRY_TYPE::kEntryPoint) {
					const auto* entryPointPerk = static_cast<const RE::BGSEntryPointPerkEntry*>(entry);
					if (entryPointPerk->IsEntryPoint(RE::BGSEntryPoint::ENTRY_POINTS::kModAlchemyEffectiveness)) {
						if (entryPointPerk->functionData && entryPointPerk->functionData->GetType() == RE::BGSEntryPointFunctionData::ENTRY_POINT_FUNCTION_DATA::kOneValue) {
							const auto* fnData = static_cast<const RE::BGSEntryPointFunctionDataOneValue*>(entryPointPerk->functionData);
							const float mult = fnData->data;
							if (mult > 1.0f) {
								return (std::clamp)(static_cast<int>(std::round((mult - 1.0f) / 0.2f)), 1, 5);
							}
						}
					}
				}
			}
			return 0;
		}

		int getPerkRank(string name) {
			int rank = 0;
			const auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			if (!playerCharacter) {
				return rank;
			}
			bool hasAddedPerk = false;
			for (const auto* entry : playerCharacter->GetPlayerRuntimeData().addedPerks) {
				if (entry && entry->perk && isPerkNamed(entry->perk, name)) {
					hasAddedPerk = true;
					int entryRank = static_cast<int>(entry->currentRank);
					if (name == "Alchemist" || isPerkNamed(entry->perk, "Alchemist")) {
						const int tier = getAlchemistTier(entry->perk);
						if (tier > 0) {
							entryRank = tier;
						}
					}
					rank = (std::max)(rank, entryRank);
				}
			}
			if (hasAddedPerk) {
				return rank;
			}
			for (auto* perk : playerCharacter->GetPlayerRuntimeData().perks) {
				if (perk && isPerkNamed(perk, name)) {
					if (name == "Alchemist" || isPerkNamed(perk, "Alchemist")) {
						const int tier = getAlchemistTier(perk);
						if (tier > 0) {
							return tier;
						}
					}
					++rank;
				}
			}
			return rank;
		}

		int getPerkRank(RE::BGSPerk* a_perk) {
			if (!a_perk) {
				return 0;
			}
			const auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			if (!playerCharacter) {
				return 0;
			}
			int rank = 0;
			for (const auto* entry : playerCharacter->GetPlayerRuntimeData().addedPerks) {
				if (entry && entry->perk == a_perk) {
					int entryRank = static_cast<int>(entry->currentRank);
					if (isPerkNamed(a_perk, "Alchemist")) {
						const int tier = getAlchemistTier(a_perk);
						if (tier > 0) {
							entryRank = tier;
						}
					}
					rank = (std::max)(rank, entryRank);
				}
			}
			if (rank > 0) {
				return rank;
			}
			for (const auto* perk : playerCharacter->GetPlayerRuntimeData().perks) {
				if (perk == a_perk) {
					rank = (std::max)(rank, 1);
				}
			}
			return rank;
		}

		bool isSpellNamed(const RE::MagicItem* a_spell, const string& name) {
			if (!a_spell) return false;
			if (name == "Seeker of Shadows") {
				if (a_spell == seeker::GetSpell()) return true;
				const auto* edid = a_spell->GetFormEditorID();
				if (edid && strstr(edid, "Seeker") && strstr(edid, "Shadow")) return true;
			}
			const auto* fullName = a_spell->GetFullName();
			return fullName && string(fullName) == name;
		}

		bool hasSpell(string name) {
			const auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			if (!playerCharacter) {
				return false;
			}
			for (auto* spell : playerCharacter->GetActorRuntimeData().addedSpells) {
				if (spell && isSpellNamed(spell, name)) {
					return true;
				}
			}
			return false;
		}

		bool hasSpell(RE::SpellItem* a_spell) {
			if (!a_spell) {
				return false;
			}
			const auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			if (!playerCharacter) {
				return false;
			}
			return std::find(playerCharacter->GetActorRuntimeData().addedSpells.begin(), playerCharacter->GetActorRuntimeData().addedSpells.end(), a_spell) != playerCharacter->GetActorRuntimeData().addedSpells.end();
		}

		bool hasActiveSpell(string name) {
			const auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			if (!playerCharacter) {
				return false;
			}
			auto* magicTarget = const_cast<RE::PlayerCharacter*>(playerCharacter)->GetMagicTarget();
			auto* activeEffects = magicTarget ? magicTarget->GetActiveEffectList() : nullptr;
			if (!activeEffects) {
				return false;
			}
			return std::any_of(activeEffects->begin(), activeEffects->end(), [this, &name](const auto* effect) {
				return effect && effect->spell && effect->flags.none(RE::ActiveEffect::Flag::kInactive, RE::ActiveEffect::Flag::kDispelled) && isSpellNamed(effect->spell, name);
			});
		}

		bool hasActiveSpell(RE::SpellItem* a_spell) {
			if (!a_spell) {
				return false;
			}
			const auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			if (!playerCharacter) {
				return false;
			}
			auto* magicTarget = const_cast<RE::PlayerCharacter*>(playerCharacter)->GetMagicTarget();
			auto* activeEffects = magicTarget ? magicTarget->GetActiveEffectList() : nullptr;
			if (!activeEffects) {
				return false;
			}
			return std::any_of(activeEffects->begin(), activeEffects->end(), [a_spell](const auto* effect) {
				return effect && effect->spell == a_spell && effect->flags.none(RE::ActiveEffect::Flag::kInactive, RE::ActiveEffect::Flag::kDispelled);
			});
		}

		bool hasSeekerRewardState() {
			auto* spell = seeker::GetSpell();
			auto* perk = seeker::GetPerk();
			auto* rewardGlobal = seeker::GetRewardGlobal();
			return spell && perk && rewardGlobal && hasSpell(spell) && hasActiveSpell(spell) &&
				getPerkRank(perk) == 1 && std::isfinite(rewardGlobal->value) && rewardGlobal->value == seeker::kShadowsRewardValue;
		}

		void captureAlchemyEvaluationContext() {
			alchemyEvaluationContext = {};
			const auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			if (!playerCharacter) {
				return;
			}

			set<const RE::BGSPerk*> activePerks;
			const auto& runtimeData = playerCharacter->GetPlayerRuntimeData();
			for (const auto* entry : runtimeData.addedPerks) {
				if (entry && entry->perk && entry->currentRank > 0) {
					activePerks.insert(entry->perk);
				}
			}
			for (const auto* perk : runtimeData.perks) {
				if (perk) {
					activePerks.insert(perk);
				}
			}
			alchemyEvaluationContext.activePerks.assign(activePerks.begin(), activePerks.end());
			alchemyEvaluationContext.alchemistPerkRank = getPerkRank("Alchemist");
			alchemyEvaluationContext.fortifyAlchemyLevel = fortifyAlchemyLevel;
			alchemyEvaluationContext.hasPhysician = getPerkRank("Physician") > 0;
			alchemyEvaluationContext.hasBenefactor = getPerkRank("Benefactor") > 0;
			alchemyEvaluationContext.hasPoisoner = getPerkRank("Poisoner") > 0;
			alchemyEvaluationContext.hasPurity = getPerkRank("Purity") > 0;
			alchemyEvaluationContext.hasConcentratedPoison = getPerkRank("Concentrated Poison") > 0;

			auto& seekerState = alchemyEvaluationContext.seeker;
			seekerState.spell = seeker::GetSpell();
			seekerState.spellListed = hasSpell(seekerState.spell ? const_cast<RE::SpellItem*>(seekerState.spell) : nullptr);
			seekerState.spellActive = seekerState.spellListed && hasActiveSpell(seekerState.spell ? const_cast<RE::SpellItem*>(seekerState.spell) : nullptr);
			seekerState.perk = seeker::GetPerk();
			seekerState.perkRank = getPerkRank(seekerState.perk ? const_cast<RE::BGSPerk*>(seekerState.perk) : nullptr);
			seekerState.rewardGlobal = seeker::GetRewardGlobal();
			seekerState.rewardGlobalAvailable = seekerState.rewardGlobal != nullptr;
			if (seekerState.rewardGlobalAvailable) {
				seekerState.rewardGlobalValue = seekerState.rewardGlobal->value;
			}
			seekerState.nativeContract = hasSeekerRewardState();
			alchemyEvaluationContext.hasSeekerOfShadows = seekerState.nativeContract;
			alchemyEvaluationContext.captured = true;
			alchemistPerkMultiplier = getAlchemyEffectivenessMultiplier();
		}

		float getAlchemyEffectivenessMultiplier() {
			float maxMultiplier = 1.0f + alchemistPerkLevel * 0.2f;
			const auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			if (!playerCharacter) {
				return maxMultiplier;
			}
			const auto& runtimeData = playerCharacter->GetPlayerRuntimeData();
			std::set<const RE::BGSPerk*> playerPerks;
			for (const auto* entry : runtimeData.addedPerks) {
				if (entry && entry->perk && entry->currentRank > 0) {
					playerPerks.insert(entry->perk);
				}
			}
			for (const auto* perk : runtimeData.perks) {
				if (perk) {
					playerPerks.insert(perk);
				}
			}
			for (const auto* perk : playerPerks) {
				if (!perk || !isPerkNamed(perk, "Alchemist")) continue;
				for (const auto* entry : perk->perkEntries) {
					if (!entry || entry->GetType() != RE::PERK_ENTRY_TYPE::kEntryPoint) {
						continue;
					}
					const auto* entryPoint = static_cast<const RE::BGSEntryPointPerkEntry*>(entry);
					if (!entryPoint->IsEntryPoint(RE::BGSEntryPoint::ENTRY_POINTS::kModAlchemyEffectiveness) ||
						!entryPoint->functionData ||
						entryPoint->functionData->GetType() != RE::BGSEntryPointFunctionData::ENTRY_POINT_FUNCTION_DATA::kOneValue) {
						continue;
					}
					const auto* fnData = static_cast<const RE::BGSEntryPointFunctionDataOneValue*>(entryPoint->functionData);
					if (std::isfinite(fnData->data) && fnData->data > 0.0f) {
						if (entryPoint->entryData.function.get() == RE::BGSEntryPointFunction::ENTRY_POINT_FUNCTIONS::kMultiplyValue) {
							maxMultiplier = (std::max)(maxMultiplier, fnData->data);
						}
					}
				}
			}
			return maxMultiplier;
		}

		void setAlchemyLevel() {
			const auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			if (!playerCharacter) {
				alchemyLevel = 0.0f;
				return;
			}
			if (auto* actorValueOwner = const_cast<RE::PlayerCharacter*>(playerCharacter)->AsActorValueOwner()) {
				alchemyLevel = actorValueOwner->GetActorValue(RE::ActorValue::kAlchemy);
				return;
			}
			if (!playerCharacter->GetInfoRuntimeData().skills || !playerCharacter->GetInfoRuntimeData().skills->data) {
				alchemyLevel = 0.0f;
				return;
			}
			alchemyLevel = static_cast<float>(playerCharacter->GetInfoRuntimeData().skills->data->skills[RE::PlayerCharacter::PlayerSkills::Data::Skills::kAlchemy].level);
		}

		void init() {
			setAlchemyLevel();
			fortifyAlchemyLevel = 0;
			alchemistPerkLevel = getPerkRank("Alchemist");
			hasPerkPurity = getPerkRank("Purity");
			hasPerkPhysician = getPerkRank("Physician");
			hasPerkBenefactor = getPerkRank("Benefactor");
			hasPerkPoisoner = getPerkRank("Poisoner");
			hasPerkConcentratedPoison = getPerkRank("Concentrated Poison");
			hasSeekerOfShadows = hasSeekerRewardState();
			alchemistPerkMultiplier = getAlchemyEffectivenessMultiplier();
			captureAlchemyEvaluationContext();
		}

		void setMiniState() {
			miniState = str::fromInt(alchemyLevel) + "," + str::fromInt(fortifyAlchemyLevel) + "," + str::fromInt(alchemistPerkLevel) + "," +
				str::fromInt(static_cast<int>(alchemistPerkMultiplier * 1000.0f)) + "," +
				str::fromInt(hasPerkPhysician) + "," + str::fromInt(hasPerkBenefactor) + "," + str::fromInt(hasPerkPoisoner) + "," + str::fromInt(hasSeekerOfShadows);
		}

		void setState() {
			state = str::fromInt(alchemyLevel) + "," + str::fromInt(fortifyAlchemyLevel) + "," + str::fromInt(alchemistPerkLevel) + "," +
				str::fromInt(static_cast<int>(alchemistPerkMultiplier * 1000.0f)) + "," + str::fromInt(hasPerkPurity) + "," +
				str::fromInt(hasPerkPhysician) + "," + str::fromInt(hasPerkBenefactor) + "," + str::fromInt(hasPerkPoisoner) + "," + str::fromInt(hasSeekerOfShadows);
		}

		Player() {
			alchemyLevel = 0;
			fortifyAlchemyLevel = 0;
			alchemistPerkLevel = 0;
			alchemistPerkMultiplier = 1.0f;
			hasPerkPurity = false;
			hasPerkPhysician = false;
			hasPerkBenefactor = false;
			hasPerkPoisoner = false;
			hasPerkConcentratedPoison = false;
			hasSeekerOfShadows = false;
		};
	};

	inline Player player;

	namespace effect {
		inline string getName(const GameEffect* effect) {
			string name = effect && effect->baseEffect ? effect->baseEffect->GetFullName() : "";
			return name;
		}

		inline bool isFortifyAlchemy(const RE::EffectSetting* baseEffect) {
			if (!baseEffect) {
				return false;
			}
			if (baseEffect->data.primaryAV == RE::ActorValue::kAlchemy ||
				baseEffect->data.primaryAV == RE::ActorValue::kAlchemyPowerModifier) {
				return true;
			}
			if (baseEffect->HasKeywordString("EnchFortifyAlchemyBase") ||
				baseEffect->HasKeywordString("MagicFortifyAlchemy")) {
				return true;
			}
			const auto* edid = baseEffect->GetFormEditorID();
			if (edid) {
				if (strstr(edid, "FortifyAlchemy") != nullptr || strstr(edid, "AlchemyPower") != nullptr) {
					return true;
				}
			}
			const auto* fullName = baseEffect->GetFullName();
			if (fullName && _stricmp(fullName, "Fortify Alchemy") == 0) {
				return true;
			}
			return false;
		}

		inline bool isFortifyAlchemy(const GameEffect* effect) {
			return effect && isFortifyAlchemy(effect->baseEffect);
		}

		inline bool powerAffectsMagnitude(const GameEffect* effect) {
			return effect && effect->baseEffect && effect->baseEffect->data.flags.all(RE::EffectSetting::EffectSettingData::Flag::kPowerAffectsMagnitude);
		}

		inline bool hasKeyword(const GameEffect* effect, string keyword) {
			return effect && effect->baseEffect && effect->baseEffect->HasKeywordString(keyword);
		}

		inline float getMagnitude(const GameEffect* effect) {
			return effect ? effect->GetMagnitude() : 0.0f;
		}

		inline bool powerAffectsDuration(const GameEffect* effect) {
			return effect && effect->baseEffect && effect->baseEffect->data.flags.all(RE::EffectSetting::EffectSettingData::Flag::kPowerAffectsDuration);
		}

		inline int getDuration(const GameEffect* effect) {
			return effect ? static_cast<int>(effect->GetDuration()) : 0;
		}
		inline float getCost(const GameEffect* effect) {
			return effect ? effect->cost : 0.0f;
		}

		inline float getBaseCost(const GameEffect* effect) {
			return effect && effect->baseEffect ? effect->baseEffect->data.baseCost : 0.0f;
		}

		inline bool isPhysicianEffect(const Effect& effect) {
			return effect.baseEffect && (effect.baseEffect->HasKeywordString("MagicAlchRestoreHealth") ||
				effect.baseEffect->HasKeywordString("MagicAlchRestoreMagicka") ||
				effect.baseEffect->HasKeywordString("MagicAlchRestoreStamina") ||
				effect.name == "Restore Health" || effect.name == "Restore Magicka" || effect.name == "Restore Stamina");
		}

		inline bool noMagnitude(const Effect& effect) {
			return effect.baseEffect && effect.baseEffect->data.flags.all(
				RE::EffectSetting::EffectSettingData::Flag::kNoMagnitude);
		}

		inline bool noDuration(const Effect& effect) {
			return effect.baseEffect && effect.baseEffect->data.flags.all(
				RE::EffectSetting::EffectSettingData::Flag::kNoDuration);
		}

		inline float getFallbackAlchemistMultiplier(const Player& evaluatedPlayer) {
			return (std::isfinite(evaluatedPlayer.alchemistPerkMultiplier) && evaluatedPlayer.alchemistPerkMultiplier > 0.0f) ?
				evaluatedPlayer.alchemistPerkMultiplier : (1.0f + evaluatedPlayer.alchemistPerkLevel * 0.2f);
		}

		inline bool calculateVanillaPowerFactors(
			const Effect& effect,
			bool potion,
			bool includeTypePerks,
			float& magnitudePowerFactor,
			float& durationPowerFactor,
			const Player& evaluatedPlayer) {
			if (evaluatedPlayer.alchemyEvaluationContext.captured) {
				return caco::Adapter::TryGetVanillaAlchemyEffectivenessMultipliers(
					effect.baseEffect,
					evaluatedPlayer.alchemyLevel,
					getFallbackAlchemistMultiplier(evaluatedPlayer),
					potion,
					includeTypePerks,
					evaluatedPlayer.alchemyEvaluationContext,
					magnitudePowerFactor,
					durationPowerFactor);
			}

			const float effectiveness = caco::algorithm::CalculateVanillaAlchemyEffectiveness(
				evaluatedPlayer.alchemyLevel, getFallbackAlchemistMultiplier(evaluatedPlayer));
			magnitudePowerFactor = effectiveness;
			durationPowerFactor = effectiveness;
			if (evaluatedPlayer.hasPerkPhysician && isPhysicianEffect(effect)) {
				magnitudePowerFactor *= 1.25f;
				durationPowerFactor *= 1.25f;
			}
			if (includeTypePerks) {
				if (effect.beneficial && potion && evaluatedPlayer.hasPerkBenefactor) {
					magnitudePowerFactor *= 1.25f;
					durationPowerFactor *= 1.25f;
				} else if (!effect.beneficial && !potion && evaluatedPlayer.hasPerkPoisoner) {
					magnitudePowerFactor *= 1.25f;
					durationPowerFactor *= 1.25f;
				}
			}
			if (evaluatedPlayer.hasSeekerOfShadows) {
				magnitudePowerFactor *= 1.1f;
				durationPowerFactor *= 1.1f;
			}
			return std::isfinite(magnitudePowerFactor) && magnitudePowerFactor > 0.0f &&
				std::isfinite(durationPowerFactor) && durationPowerFactor > 0.0f;
		}

		inline float calculateLegacyEffectComponent(
			const Effect& effect,
			float value,
			bool affectsValue,
			bool magnitude,
			bool potion,
			bool includeTypePerks,
			const Player& evaluatedPlayer) {
			if (!affectsValue || value <= 0.0f) {
				return value;
			}

			float magnitudePowerFactor = 1.0f;
			float durationPowerFactor = 1.0f;
			if (!calculateVanillaPowerFactors(
				effect, potion, includeTypePerks, magnitudePowerFactor, durationPowerFactor, evaluatedPlayer)) {
				return value;
			}
			const float playerFactor = caco::algorithm::CalculateAlchemyActorValueMultiplier(
				evaluatedPlayer.fortifyAlchemyLevel);
			if (!std::isfinite(playerFactor) || playerFactor <= 0.0f) {
				return value;
			}
			const float powerFactor = magnitude ? magnitudePowerFactor : durationPowerFactor;
			const float calculatedValue = value * powerFactor * playerFactor;
			if (!std::isfinite(calculatedValue)) {
				return value;
			}
			return std::round(calculatedValue);
		}

		inline bool calculateNativePowerFactors(
			const Effect& effect,
			bool potion,
			bool includeTypePerks,
			float& magnitudePowerFactor,
			float& durationPowerFactor,
			const Player& evaluatedPlayer,
			bool useCacoNative) {
			magnitudePowerFactor = 1.0f;
			durationPowerFactor = 1.0f;
			if (!effect.powerAffectsMagnitude && !effect.powerAffectsDuration) {
				return true;
			}

			if (useCacoNative) {
				const float alchemistMult = getFallbackAlchemistMultiplier(evaluatedPlayer);
				float cacoMagnitudePowerFactor = 1.0f;
				float cacoDurationPowerFactor = 1.0f;
				if (!caco::Adapter::TryGetAlchemyEffectivenessMultipliers(
					effect.baseEffect,
					evaluatedPlayer.alchemyLevel,
					alchemistMult,
					potion,
					includeTypePerks,
					evaluatedPlayer.alchemyEvaluationContext,
					cacoMagnitudePowerFactor,
					cacoDurationPowerFactor)) {
					return false;
				}
				magnitudePowerFactor = cacoMagnitudePowerFactor;
				durationPowerFactor = cacoDurationPowerFactor;
			} else {
				if (!calculateVanillaPowerFactors(
					effect, potion, includeTypePerks, magnitudePowerFactor, durationPowerFactor, evaluatedPlayer)) {
					return false;
				}
			}
			const float playerFactor = caco::algorithm::CalculateAlchemyActorValueMultiplier(
				evaluatedPlayer.fortifyAlchemyLevel);
			if (!std::isfinite(playerFactor) || playerFactor <= 0.0f) {
				return false;
			}
			magnitudePowerFactor *= playerFactor;
			durationPowerFactor *= playerFactor;
			return std::isfinite(magnitudePowerFactor) && magnitudePowerFactor > 0.0f &&
				std::isfinite(durationPowerFactor) && durationPowerFactor > 0.0f;
		}

		inline double calculateEffectCostFromComponents(const Effect& effect, float magnitude, float duration) {
			return caco::algorithm::CalculateEffectCostPrecise(
				effect.baseCost, magnitude, duration, noMagnitude(effect), noDuration(effect));
		}

		inline double calculateNativeEffectContribution(
			const Effect& effect,
			const caco::algorithm::EffectInput& input) {
			return caco::algorithm::CalculateEffectContribution(
				effect.baseCost, input, noMagnitude(effect), noDuration(effect));
		}

		inline bool calculateNativeEffectInput(
			const Effect& effect,
			bool potion,
			bool includeTypePerks,
			bool includePlayerFactors,
			caco::algorithm::EffectInput& input,
			const Player& evaluatedPlayer,
			bool useCacoNative) {
			float magnitudePowerFactor = 1.0f;
			float durationPowerFactor = 1.0f;
			if (includePlayerFactors && !calculateNativePowerFactors(
				effect, potion, includeTypePerks, magnitudePowerFactor, durationPowerFactor, evaluatedPlayer, useCacoNative)) {
				return false;
			}
			input = caco::algorithm::CalculateEffectInput(
				effect.magnitude,
				effect.duration,
				noMagnitude(effect),
				noDuration(effect),
				effect.powerAffectsMagnitude,
				effect.powerAffectsDuration,
				magnitudePowerFactor,
				durationPowerFactor);
			return input.valid;
		}

		inline bool applyAlchemyPlusRoundingToInput(
			const Effect& effect,
			caco::algorithm::EffectInput& input,
			bool applyRounding) {
			if (!applyRounding) {
				return true;
			}
			if (effect.baseEffect && effect.powerAffectsMagnitude) {
				input.magnitude = alchemyplus::Adapter::ApplyMagnitudeRounding(
					effect.baseEffect, input.magnitude);
			}
			if (effect.baseEffect && effect.powerAffectsDuration) {
				input.duration = alchemyplus::Adapter::ApplyDurationRounding(
					effect.baseEffect, input.duration);
			}
			return std::isfinite(input.magnitude) && std::isfinite(input.duration);
		}

		inline bool calculateNativeEffectOrder(
			const Effect& effect,
			const Player& evaluatedPlayer,
			double& contribution,
			bool useCacoNative,
			bool applyAlchemyPlusRounding) {
			caco::algorithm::EffectInput input;
			if (!calculateNativeEffectInput(effect, false, false, true, input, evaluatedPlayer, useCacoNative)) {
				return false;
			}
			if (!applyAlchemyPlusRoundingToInput(effect, input, applyAlchemyPlusRounding)) {
				return false;
			}
			contribution = calculateNativeEffectContribution(effect, input);
			return std::isfinite(contribution) && contribution > 0.0;
		}

		inline Effect calculateLegacyEffect(
			const Effect& effect,
			bool potion,
			bool includeTypePerks,
			const Player& evaluatedPlayer,
			bool applyAlchemyPlusRounding) {
			Effect calculatedEffect = effect;
			calculatedEffect.calcMagnitude = calculateLegacyEffectComponent(calculatedEffect, calculatedEffect.magnitude,
				calculatedEffect.powerAffectsMagnitude, true, potion, includeTypePerks, evaluatedPlayer);
			if (applyAlchemyPlusRounding && calculatedEffect.baseEffect && calculatedEffect.powerAffectsMagnitude) {
				calculatedEffect.calcMagnitude = alchemyplus::Adapter::ApplyMagnitudeRounding(
					calculatedEffect.baseEffect, calculatedEffect.calcMagnitude);
			}
			calculatedEffect.calcDuration = calculateLegacyEffectComponent(calculatedEffect, calculatedEffect.duration,
				calculatedEffect.powerAffectsDuration, false, potion, includeTypePerks, evaluatedPlayer);
			if (applyAlchemyPlusRounding && calculatedEffect.baseEffect && calculatedEffect.powerAffectsDuration) {
				calculatedEffect.calcDuration = alchemyplus::Adapter::ApplyDurationRounding(
					calculatedEffect.baseEffect, calculatedEffect.calcDuration);
			}
			calculatedEffect.calcCost = calculateEffectCostFromComponents(calculatedEffect,
				calculatedEffect.calcMagnitude, calculatedEffect.calcDuration);
			calculatedEffect.nativeCost = calculatedEffect.calcCost;
			calculatedEffect.nativeOrderCost = calculatedEffect.calcCost;
			return calculatedEffect;
		}

		inline bool calculateNativeEffect(
			const Effect& effect,
			bool potion,
			bool includeTypePerks,
			bool includePlayerFactors,
			Effect& result,
			const Player& evaluatedPlayer,
			bool useCacoNative,
			bool applyAlchemyPlusRounding) {
			caco::algorithm::EffectInput nativeInput;
			if (!calculateNativeEffectInput(
				effect, potion, includeTypePerks, includePlayerFactors, nativeInput, evaluatedPlayer, useCacoNative)) {
				return false;
			}
			const double nativeContribution = calculateNativeEffectContribution(effect, nativeInput);
			if (!std::isfinite(nativeContribution) || nativeContribution <= 0.0) {
				return false;
			}

			caco::algorithm::EffectInput constructedInput = nativeInput;
			if (includePlayerFactors && !applyAlchemyPlusRoundingToInput(
					effect, constructedInput, applyAlchemyPlusRounding)) {
				return false;
			}
			const double constructedContribution = calculateNativeEffectContribution(effect, constructedInput);
			if (!std::isfinite(constructedContribution) || constructedContribution <= 0.0) {
				return false;
			}
			Effect calculatedEffect = effect;
			calculatedEffect.calcMagnitude = constructedInput.magnitude;
			calculatedEffect.calcDuration = constructedInput.duration;
			calculatedEffect.calcCost = static_cast<float>(caco::algorithm::FloorGoldValue(constructedContribution));
			calculatedEffect.nativeCost = constructedContribution;
			calculatedEffect.nativeOrderCost = constructedContribution;
			result = std::move(calculatedEffect);
			return true;
		}

		inline const RE::EffectSetting* getSourceIdentity(const Effect& effect) noexcept {
			return effect.sourceBaseEffect ? effect.sourceBaseEffect : effect.baseEffect;
		}

		inline bool useCacoAlgorithm(EvaluationAlgorithm algorithm) noexcept {
			return algorithm == EvaluationAlgorithm::CACO ||
				(algorithm == EvaluationAlgorithm::Automatic && caco::Adapter::IsActive());
		}

		inline bool useAlchemyPlusAlgorithm(EvaluationAlgorithm algorithm) noexcept {
			return algorithm == EvaluationAlgorithm::Automatic || algorithm == EvaluationAlgorithm::AlchemyPlus;
		}

		inline Effect getAlgorithmEffect(
			const Ingredient& ingredient,
			std::size_t effectIndex,
			bool useCacoNative) {
			if (effectIndex >= ingredient.effects.size()) {
				return {};
			}
			const auto& source = ingredient.effects[effectIndex];
			if (!source.sourceEffect) {
				return source;
			}
			auto* baseEffect = const_cast<RE::EffectSetting*>(source.sourceBaseEffect);
			if (useCacoNative && ingredient.nativeIngredient) {
				baseEffect = caco::Adapter::ResolveIngredientEffect(
					ingredient.nativeIngredient, baseEffect);
			}
			return Effect(source.sourceEffect, baseEffect);
		}

		inline NativePotionResult evaluatePotion(
			const vector<const Ingredient*>& ingredients,
			const Player& evaluatedPlayer = player,
			bool useCrucibleExemplar = false,
			EvaluationAlgorithm algorithm = EvaluationAlgorithm::Automatic) {
			NativePotionResult result;
			const bool cacoAlgorithm = useCacoAlgorithm(algorithm);
			const bool cacoNative = cacoAlgorithm && caco::Adapter::IsActive();
			const bool alchemyPlusAlgorithm = useAlchemyPlusAlgorithm(algorithm);
			const bool alchemyPlusRounding = alchemyPlusAlgorithm && alchemyplus::Adapter::IsRoundingEnabled();
			const bool alchemyPlusImpureCostFix = alchemyPlusAlgorithm && alchemyplus::Adapter::IsImpureCostFixEnabled();

			struct CandidateGroup
			{
				vector<Effect> effects;
				set<const Ingredient*> ingredients;
			};
			map<const RE::EffectSetting*, CandidateGroup, std::less<const RE::EffectSetting*>> effectsBySourceIdentity;
			for (auto* ingredient : ingredients) {
				if (!ingredient) {
					continue;
				}
				set<const RE::EffectSetting*> ingredientEffects;
				for (std::size_t effectIndex = 0; effectIndex < ingredient->effects.size(); ++effectIndex) {
					const auto effect = getAlgorithmEffect(*ingredient, effectIndex, cacoNative);
					const auto* effectIdentity = getSourceIdentity(effect);
					if (effectIdentity && ingredientEffects.insert(effectIdentity).second) {
						auto& group = effectsBySourceIdentity[effectIdentity];
						if (group.ingredients.insert(ingredient).second) {
							group.effects.push_back(effect);
						}
					}
				}
			}

			struct SelectedEffect
			{
				Effect source;
				double nativeOrderCost = 0.0;
				const RE::EffectSetting* identity = nullptr;
			};
			vector<SelectedEffect> selectedEffects;
			for (const auto& entry : effectsBySourceIdentity) {
				const auto& candidateGroup = entry.second;
				if (candidateGroup.ingredients.size() < 2) {
					continue;
				}
				const Effect* selected = nullptr;
				double selectedPriority = -1.0;
				for (const auto& candidate : candidateGroup.effects) {
					double candidateOrderCost = 0.0;
					bool calculated = false;
					if (cacoNative) {
						calculated = calculateNativeEffectOrder(
							candidate, evaluatedPlayer, candidateOrderCost, cacoNative, alchemyPlusRounding);
					} else {
						const auto candidateOrder = calculateLegacyEffect(
							candidate, false, false, evaluatedPlayer, alchemyPlusRounding);
						candidateOrderCost = candidateOrder.nativeOrderCost;
						calculated = true;
					}
					if (!calculated || !std::isfinite(candidateOrderCost) || candidateOrderCost < 0.0) {
						continue;
					}
					if (!selected || candidateOrderCost > selectedPriority) {
						selected = &candidate;
						selectedPriority = candidateOrderCost;
					}
				}
				if (!selected) {
					continue;
				}
				selectedEffects.push_back({ *selected, selectedPriority, entry.first });
			}

			if (selectedEffects.empty()) {
				return result;
			}

			const auto compareOrder = [](const SelectedEffect& left, const SelectedEffect& right) {
				if (left.nativeOrderCost != right.nativeOrderCost) {
					return left.nativeOrderCost > right.nativeOrderCost;
				}
				const auto* leftIdentity = left.identity;
				const auto* rightIdentity = right.identity;
				const auto leftFormID = leftIdentity ? leftIdentity->GetFormID() : 0;
				const auto rightFormID = rightIdentity ? rightIdentity->GetFormID() : 0;
				if (leftFormID != rightFormID) {
					return leftFormID < rightFormID;
				}
				if (left.source.name != right.source.name) {
					return left.source.name < right.source.name;
				}
				return std::less<const RE::EffectSetting*>{}(left.identity, right.identity);
			};
			std::sort(selectedEffects.begin(), selectedEffects.end(), compareOrder);
			const bool initialPotion = !selectedEffects.front().source.harmful;
			const bool hasPurity = evaluatedPlayer.alchemyEvaluationContext.captured ?
				evaluatedPlayer.alchemyEvaluationContext.hasPurity : evaluatedPlayer.hasPerkPurity;
			if (hasPurity) {
				selectedEffects.erase(std::remove_if(selectedEffects.begin(), selectedEffects.end(),
					[initialPotion](const auto& selected) {
						return caco::algorithm::ShouldRemoveOpposingAlchemyEffect(
							initialPotion, selected.source.beneficial, selected.source.harmful);
					}), selectedEffects.end());
			}
			if (selectedEffects.empty()) {
				return result;
			}
			const bool potion = !selectedEffects.front().source.harmful;
			EffectList calculatedEffects;
			calculatedEffects.reserve(selectedEffects.size());
			for (const auto& selected : selectedEffects) {
				Effect calculatedEffect;
				if (cacoNative) {
					if (!calculateNativeEffect(
						selected.source, potion, true, true, calculatedEffect, evaluatedPlayer, cacoNative, alchemyPlusRounding)) {
						return result;
					}
				} else {
					calculatedEffect = calculateLegacyEffect(
						selected.source, potion, true, evaluatedPlayer, alchemyPlusRounding);
				}
				calculatedEffect.nativeOrderCost = selected.nativeOrderCost;
				calculatedEffects.push_back(std::move(calculatedEffect));
			}
			const auto* controlIdentity = selectedEffects.front().identity;
			const bool isPoison = selectedEffects.front().source.harmful;

			if (calculatedEffects.empty()) {
				return result;
			}
			result.isPoison = isPoison;
			result.effects = std::move(calculatedEffects);
			result.controlEffect = result.effects.front();
			const auto control = std::find_if(result.effects.begin(), result.effects.end(), [controlIdentity](const auto& effect) {
				return getSourceIdentity(effect) == controlIdentity;
			});
			if (control != result.effects.end()) {
				result.controlEffect = *control;
			}
			for (const auto& effect : result.effects) {
				result.hasBeneficial = result.hasBeneficial || effect.beneficial;
				result.hasHarmful = result.hasHarmful || effect.harmful;
			}

			const bool cacoImpureProcessing = cacoNative && result.hasBeneficial && result.hasHarmful &&
				caco::Adapter::IsImpureProcessingEnabled();
			bool impure = false;
			double totalCost = 0.0;
			for (const auto& effect : result.effects) {
				double effectCost = cacoNative ? effect.nativeCost : static_cast<double>(effect.calcCost);
				if (alchemyPlusImpureCostFix) {
					const float adjustedCost = alchemyplus::Adapter::AdjustImpureEffectCost(
						static_cast<float>(effectCost), isPoison, effect.hostile, impure);
					if (std::isfinite(adjustedCost)) {
						totalCost += static_cast<double>(adjustedCost);
					}
				} else if (std::isfinite(effectCost) && effectCost > 0.0) {
					totalCost += effectCost;
				}
			}
			if (impure) {
				totalCost = static_cast<double>(alchemyplus::Adapter::FinalizeImpureCost(static_cast<float>(totalCost)));
			}
			result.preAdjustmentGold = caco::algorithm::FloorGoldValue(totalCost);
			result.cost = cacoNative ? static_cast<float>(result.preAdjustmentGold) :
				(std::isfinite(totalCost) ? static_cast<float>(totalCost) : 0.0f);

			if (useCrucibleExemplar && cacoNative && result.effects.size() == 1 && !(result.hasBeneficial && result.hasHarmful)) {
				const auto& primaryEffect = result.effects.front();
				caco::ExemplarResult exemplar;
				if (caco::Adapter::TryGetCrucibleExemplar(primaryEffect.baseEffect, primaryEffect.calcMagnitude,
					primaryEffect.calcDuration, exemplar)) {
					result.cost = static_cast<float>(exemplar.goldValue);
					result.preAdjustmentGold = exemplar.goldValue;
					result.usedCrucibleExemplar = true;
					result.exemplarGoldValue = exemplar.goldValue;
				}
			}

			if (cacoImpureProcessing) {
				const auto preAdjustmentGold = result.preAdjustmentGold;
				for (auto& effect : result.effects) {
					if (caco::Adapter::IsDurationBased(effect.baseEffect)) {
						effect.calcDuration = caco::Adapter::ApplyImpureDuration(effect.calcDuration);
					} else {
						effect.calcMagnitude = caco::Adapter::ApplyImpureMagnitude(effect.calcMagnitude);
					}
					if (getSourceIdentity(effect) == controlIdentity) {
						result.controlEffect = effect;
					}
				}
				result.cost = static_cast<float>(caco::Adapter::ApplyImpureGold(preAdjustmentGold));
			}
			result.valid = true;
			return result;
		}

		inline vector<string> getKeywords(GameEffect* effect) {
			vector<string> keywords;
			if (!effect || !effect->baseEffect) {
				return keywords;
			}
			for (auto* keyword : effect->baseEffect->GetKeywords()) {
				if (keyword) {
					keywords.emplace_back(keyword->GetFormEditorID());
				}
			}
			return keywords;
		}

		inline string getGenericDescription(GameEffect* effect) {
			string description = effect && effect->baseEffect ? effect->baseEffect->magicItemDescription.c_str() : "";
			description = str::replace(description, "<", "");
			description = str::replace(description, ">", "");
			return description;
		}

		inline string getDescription(GameEffect* effect) {
			string description = effect && effect->baseEffect ? effect->baseEffect->magicItemDescription.c_str() : "";
			description = str::replace(description, "<mag>", str::fromFloat(getMagnitude(effect)));
			description = str::replace(description, "<dur>", str::fromInt(getDuration(effect)));
			description = str::replace(description, "<", "");
			description = str::replace(description, ">", "");
			return description;
		}

		inline string getPerkCalcDescriptionDebug(Effect effect, bool potion) {
			string description = effect.description;
			description = str::replace(description, "<mag>", str::fromFloat(effect.calcMagnitude));
			description = str::replace(description, "<dur>", str::fromFloat(effect.calcDuration));
			description = str::replace(description, "<", "");
			description = str::replace(description, ">", "");
			return description;
		}

		inline string getPerkCalcDescription(Effect effect, bool potion) {
			string description = effect.description;
			description = str::replace(description, "<mag>", str::fromFloat(effect.calcMagnitude));
			description = str::replace(description, "<dur>", str::fromFloat(effect.calcDuration));
			description = str::replace(description, "<", "");
			description = str::replace(description, ">", "");
			return description;
		}

		inline string printEffect(GameEffect* effect) {
			string data = getName(effect) + "," +
				str::fromFloat(getMagnitude(effect)) + "," +
				str::fromInt(getDuration(effect)) + "," +
				str::fromFloat(getCost(effect)) + "," +
				str::fromFloat(getBaseCost(effect));
			vector<string> keywords = getKeywords(effect);
			for (int i = 0; i < 3; ++i) {
				if (i < keywords.size()) {
					data += "," + keywords[i];
				}
				else {
					data += ",";
				}
			}
			data += "," + getDescription(effect);
			return data;
		}
	}

	namespace ingredient {
		inline string getName(IngredientItem* ingredient) {
			string name = ingredient->GetFullName();
			return name;
		}

		inline int getValue(IngredientItem* ingredient) {
			return static_cast<int>(ingredient->CalculateMagickaCost(nullptr));
		}

		inline int getIngredientValue(IngredientItem* ingredient) {
			return ingredient->data.costOverride;
		}

		inline vector<string> getKeywords(IngredientItem* ingredient) {
			vector<string> keywords;
			for (auto* keyword : ingredient->GetKeywords()) {
				if (keyword) {
					keywords.emplace_back(keyword->GetFormEditorID());
				}
			}
			return keywords;
		}

		inline vector<GameEffect*> getEffects(IngredientItem* ingredient) {
			vector<GameEffect*> effects;
			for (auto* effect : ingredient->effects) {
				if (effect) {
					effects.push_back(effect);
				}
			}
			return effects;
		}

		inline bool hasEffect(IngredientItem* ingredient, string eName) {
			for (auto* effect : ingredient->effects) {
				string iName = effect && effect->baseEffect ? effect->baseEffect->GetFullName() : "";
				if (iName == eName) {
					return true;
				}
			}
			return false;
		}

		inline RE::FormID getDefaultIngredientFormID(std::string_view a_name) {
			static const std::unordered_map<std::string_view, RE::FormID> kDefaultMap = {
				{ "Berit's Ashes", 0x000703C7 },
				{ "Bliss Bug Thorax", 0x040019C0 },
				{ "Bone Hawk Claw", 0x02002998 },
				{ "Briar Heart", 0x0003AD61 },
				{ "Corkbulb Root", 0x0400083B },
				{ "Corrupted Human Heart", 0x0400083A },
				{ "Crimson Nirnroot", 0x000B701A },
				{ "Daedra Heart", 0x0003AD5B },
				{ "Deathbell", 0x000516C8 },
				{ "Dragon's Tongue", 0x000889A2 },
				{ "Ectoplasm", 0x0003AD63 },
				{ "Farengar's Frost Salt", 0x0006BCB0 },
				{ "Fine-Cut Void Salts", 0x0002E4E3 },
				{ "Fire Salts", 0x0003AD5E },
				{ "Frost Mirriam", 0x00034CDF },
				{ "Frost Salts", 0x0003AD5F },
				{ "Giant's Toe", 0x0003AD64 },
				{ "Goldfish", 0x04000808 },
				{ "Hagraven Claw", 0x0006B689 },
				{ "Hagraven Feathers", 0x0007EDF5 },
				{ "Human Heart", 0x000B18CD },
				{ "Ice Wraith Teeth", 0x0003AD6A },
				{ "Ironwood Fruit", 0x0400083C },
				{ "Jarrin Root", 0x0001BCBC },
				{ "Jazbay Grapes", 0x0006AC4A },
				{ "Juniper Berries", 0x0005076E },
				{ "Juvenile Mudcrab", 0x04000805 },
				{ "Large Antlers", 0x0006BC9A },
				{ "Mudcrab Chitin", 0x0006BC9E },
				{ "Netch Jelly", 0x0401CD74 },
				{ "Nightshade", 0x0002F44C },
				{ "Nirnroot", 0x00059B86 },
				{ "Salt Pile", 0x00034CDF },
				{ "Scathecraw", 0x04017E97 },
				{ "Simon Rodayne's Heart", 0x0001BCBC },
				{ "Slaughterfish Scales", 0x0003AD70 },
				{ "Taproot", 0x0003AD71 },
				{ "Torchbug Abdomen", 0x04000839 },
				{ "Torchbug Thorax", 0x0004DA73 },
				{ "Troll Fat", 0x0003AD72 },
				{ "Vampire Dust", 0x0003AD76 },
				{ "Void Salts", 0x0003AD60 }
			};
			const auto it = kDefaultMap.find(a_name);
			return it != kDefaultMap.end() ? it->second : 0;
		}

		inline bool isIngredientMatch(const IngredientItem* ingredient, const string& key) {
			if (!ingredient || key.empty()) {
				return false;
			}
			const auto formId = ingredient->GetFormID();
			if (key.size() >= 3 && key[0] == '0' && (key[1] == 'x' || key[1] == 'X')) {
				try {
					const auto keyFormId = static_cast<RE::FormID>(std::stoul(key, nullptr, 16));
					if (keyFormId == formId || (keyFormId & 0x00FFFFFF) == (formId & 0x00FFFFFF)) {
						return true;
					}
				} catch (...) {}
			}
			const auto defaultFormId = getDefaultIngredientFormID(key);
			if (defaultFormId != 0 && ((defaultFormId & 0x00FFFFFF) == (formId & 0x00FFFFFF))) {
				return true;
			}
			const auto* edid = ingredient->GetFormEditorID();
			if (edid && _stricmp(edid, key.c_str()) == 0) {
				return true;
			}
			const auto* fullName = ingredient->GetFullName();
			if (fullName && _stricmp(fullName, key.c_str()) == 0) {
				return true;
			}
			return false;
		}

		inline bool isProtected(IngredientItem* ingredient, set<Ingredient> ingredients, map<string, int> moreIngredients) {
			if (!ingredient) {
				return false;
			}
			string name = ingredient->GetFullName() ? ingredient->GetFullName() : "";
			auto countIt = ingredients.find(Ingredient(name, 0));
			int count = countIt != ingredients.end() ? countIt->inventoryCount : 0;
			for (const auto& [protectedKey, protectedCount] : moreIngredients) {
				if (isIngredientMatch(ingredient, protectedKey) && (protectedCount == 999 || count <= protectedCount)) {
					return true;
				}
			}
			return false;
		}

		inline string printEffects(IngredientItem* ingredient) {
			string data = "";
			vector<GameEffect*> effects = getEffects(ingredient);
			for (int i = 0; i < effects.size(); ++i) {
				data += getName(ingredient) + "," +
					str::fromInt(getValue(ingredient)) + "," +
					str::fromInt(getIngredientValue(ingredient)) + "," +
					effect::printEffect(effects[i]);
				if (i < 3) {
					data += "\n";
				}
			}
			return data;
		}

		inline string printIngredient(IngredientItem* ingredient) {
			string data = getName(ingredient) + "," +
				str::fromInt(getValue(ingredient)) + "," +
				str::fromInt(getIngredientValue(ingredient));
			vector<string> keywords = getKeywords(ingredient);
			for (int i = 0; i < 3; ++i) {
				if (i < keywords.size()) {
					data += "," + keywords[i];
				}
				else {
					data += ",";
				}
			}
			vector<GameEffect*> effects = getEffects(ingredient);
			for (int i = 0; i < effects.size(); ++i) {
				data += "," + effect::printEffect(effects[i]);
			}
			return data;
		}
	}

	inline Effect::Effect(GameEffect* effect, RE::EffectSetting* baseEffectOverride) {
		sourceEffect = effect;
		sourceBaseEffect = effect ? effect->baseEffect : nullptr;
		baseEffect = baseEffectOverride ? baseEffectOverride : (effect ? effect->baseEffect : nullptr);
		name = baseEffect ? baseEffect->GetFullName() : effect::getName(effect);
		beneficial = caco::Adapter::HasBeneficialKeyword(baseEffect);
		harmful = caco::Adapter::HasHarmfulKeyword(baseEffect);
		hostile = baseEffect && baseEffect->IsHostile();
		powerAffectsMagnitude = baseEffect && baseEffect->data.flags.all(RE::EffectSetting::EffectSettingData::Flag::kPowerAffectsMagnitude);
		magnitude = effect::getMagnitude(effect);
		// Calculation paths begin with the source values and replace them when player or compatibility factors apply.
		calcMagnitude = magnitude;
		powerAffectsDuration = baseEffect && baseEffect->data.flags.all(RE::EffectSetting::EffectSettingData::Flag::kPowerAffectsDuration);
		durationBased = caco::Adapter::IsDurationBased(baseEffect);
		duration = effect::getDuration(effect);
		calcDuration = duration;
		baseCost = baseEffect ? baseEffect->data.baseCost : effect::getBaseCost(effect);
		calcCost = effect::getCost(effect);
			nativeCost = calcCost;
		sourceCost = effect ? effect->cost : 0.0f;
		nativeOrderCost = calcCost;
		description = baseEffect ? baseEffect->magicItemDescription.c_str() : "";
	};

	inline Ingredient::Ingredient(IngredientItem* ingredient) {
		name = ingredient::getName(ingredient);
		nativeIngredient = ingredient;
		if (!ingredient) {
			return;
		}
		for (std::size_t i = 0; i < ingredient->effects.size(); ++i) {
			GameEffect* nativeEffect = ingredient->effects[i];
			if (!nativeEffect) {
				continue;
			}
			RE::EffectSetting* activeEffect = nativeEffect ? caco::Adapter::ResolveIngredientEffect(
				ingredient, nativeEffect->baseEffect) : nullptr;
			effects.push_back(Effect(nativeEffect, activeEffect));
		}
	};

	class Potion {
	public:
		string name;
		string id;
		int size;
		Ingredient ingredient1;
		Ingredient ingredient2;
		Ingredient ingredient3;
		EffectList effects;
		set<Effect> possibleEffects;
		float cost;
		float weight;
		Effect controlEffect;
		bool isPoison = false;
		string description;
		string getName() {
			const auto prefixes = getPotionPrefixes();
			if (!effects.empty()) {
				const auto& primaryEffect = effects.front();
				const bool impure = std::any_of(effects.begin(), effects.end(), [](const auto& effect) {
					return effect.harmful;
				}) && std::any_of(effects.begin(), effects.end(), [](const auto& effect) {
					return effect.beneficial;
				});
				string cacoName;
				const auto secondaryEffect = effects.size() > 1 ? effects[1].name : string{};
				if (caco::Adapter::TryGetPotionName(effects.size(), primaryEffect.baseEffect,
					primaryEffect.calcMagnitude, primaryEffect.calcDuration, primaryEffect.name,
					secondaryEffect, isPoison, impure, prefixes.potion, prefixes.poison, cacoName)) {
					return cacoName;
				}
			}
			string title = isPoison ? prefixes.poison : prefixes.potion;
			const std::string separator = (!title.empty() && title.back() == ' ') ? "" : " ";
			return title + separator + controlEffect.name;
		}
		bool operator< (const Potion& potion) const {
			return id < potion.id;
		}
		bool operator== (const Potion& potion) const {
			return id == potion.id;
		}
		Potion(int s, Ingredient i1, Ingredient i2, EffectList e, set<Effect> pe, Effect ce, bool poison, float c) {
			size = s;
			id = i1.name + "," + i2.name;
			ingredient1 = i1;
			ingredient2 = i2;
			effects = e;
			possibleEffects = pe;
			controlEffect = ce;
			isPoison = poison;
			cost = c;
			weight = 0.0f;
			if (!caco::Adapter::TryGetPotionWeight(effects.size(),
				std::any_of(effects.begin(), effects.end(), [](const auto& effect) { return effect.beneficial; }),
				std::any_of(effects.begin(), effects.end(), [](const auto& effect) { return effect.harmful; }),
				player.hasPerkPurity, player.hasPerkConcentratedPoison, weight)) {
				weight = 0.0f;
			}
			name = getName();
		};
		Potion(int s, Ingredient i1, Ingredient i2, Ingredient i3, EffectList e, Effect ce, bool poison, float c) {
			size = s;
			id = i1.name + "," + i2.name + "," + i3.name;
			ingredient1 = i1;
			ingredient2 = i2;
			ingredient3 = i3;
			effects = e;
			controlEffect = ce;
			isPoison = poison;
			cost = c;
			weight = 0.0f;
			if (!caco::Adapter::TryGetPotionWeight(effects.size(),
				std::any_of(effects.begin(), effects.end(), [](const auto& effect) { return effect.beneficial; }),
				std::any_of(effects.begin(), effects.end(), [](const auto& effect) { return effect.harmful; }),
				player.hasPerkPurity, player.hasPerkConcentratedPoison, weight)) {
				weight = 0.0f;
			}
			name = getName();
		};
		Potion(float c, string d) {
			size = 0;
			cost = c;
			weight = 0.0f;
			description = d;
		};
		Potion() {
			size = 0;
			cost = 0;
			weight = 0.0f;
		};
	};

	extern Potion costliestPotion;
	extern set<Ingredient> lastIngredientList;
	extern set<Ingredient> ingredients;
	extern set<Potion> potions;
	extern Player player;
	extern int combinations;

	void initAlchemist();
	void stressTest();
	void makePotions();
	void makePotionsST();
}