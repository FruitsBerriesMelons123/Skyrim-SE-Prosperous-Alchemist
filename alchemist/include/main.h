#pragma once

#include "RE/Skyrim.h"
#include "REX/REX/INI.h"
#include "SKSE/SKSE.h"

#include "skillLevels.h"

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

using std::set;
using std::string;
using std::thread;
using std::map;
using std::vector;
using IngredientItem = RE::IngredientItem;
using GameEffect = RE::Effect;

inline REX::INI::I32<> kIgnorePlayer("General", "IgnorePlayer", 1);
inline REX::INI::I32<> kProtectIngredients("General", "ProtectIngredients", 0);
inline REX::INI::I32<> kSinglethreaded("General", "Singlethreaded", 0);
inline REX::INI::I32<> kNumberOfIngredientsToStressTest("General", "NumberOfIngredientsToStressTest", 0);
inline REX::INI::Str<> kMoreIngredientsToProtect("General", "MoreIngredientsToProtect", "");
inline REX::INI::Str<> kIngredientsToUnprotect("General", "IngredientsToUnprotect", "");
inline REX::INI::Str<> kStringTranslations("General", "StringTranslations", "Alchemy,No potion recipes are currently available.");
inline REX::INI::Str<> kPotionPoison("General", "PotionPoison", "Potion of,Poison of");

namespace alchemist {
	void _LOG(const string& s) {
		SKSE::log::info("{}", s);
	}

	float round_skyrim(float f) {
		return std::round(f);
	}

	class Effect {
	public:
		string name;
		bool beneficial;
		bool powerAffectsMagnitude;
		float magnitude;
		float calcMagnitude;
		bool powerAffectsDuration;
		float duration;
		float calcDuration;
		float baseCost;
		float calcCost;
		string description;
		bool operator< (const Effect& effect) const {
			return name < effect.name;
		}
		bool operator== (const Effect& effect) const {
			return name == effect.name;
		}
		Effect(GameEffect* effect);
		Effect() {
			beneficial = false;
			powerAffectsMagnitude = false;
			magnitude = 0;
			calcMagnitude = 0;
			powerAffectsDuration = false;
			duration = 0;
			calcDuration = 0;
			baseCost = 0;
			calcCost = 0;
		};
	};

	class Ingredient {
	public:
		string name;
		set<Effect> effects;
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
		std::vector<std::string> split(std::string const& str, char delim) {
			std::vector<std::string> tokens;
			size_t start;
			size_t end = 0;
			while ((start = str.find_first_not_of(delim, end)) != std::string::npos) {
				end = str.find(delim, start);
				tokens.push_back(str.substr(start, end - start));
			}
			return tokens;
		}

		int toInt(string s) {
			int i;
			std::istringstream(s) >> i;
			return i;
		}

		string fromChar(RE::BSFixedString c) {
			return c.c_str() ? c.c_str() : "";
		}

		string fromInt(int n) {
			char n_char[30];
			snprintf(n_char, 30, "%d", n);
			string n_str = n_char;
			return n_str;
		}

		string fromFloat(float f) {
			if (abs(f - int(f)) == 0) {
				return fromInt(f);
			}
			char f_char[30];
			snprintf(f_char, 30, "%.2f", f);
			string f_str = f_char;
			return f_str;
		}

		string getRandom() {
			srand(time(NULL));
			string alphanum = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
			string random = "";
			int pos;
			while (random.size() != 9) {
				pos = ((rand() % (alphanum.size() - 1)));
				random += alphanum.substr(pos, 1);
			}
			return random;
		}

		string replace(string target, string match, string replacement) {
			size_t pos;
			string random = getRandom();
			for (int i = 0; i < 5; ++i) {
				pos = target.find(random);
				if (pos != string::npos) {
					random = getRandom();
				}
				else if (pos == string::npos) {
					pos = target.find(match);
					while (pos != string::npos) {
						target.replace(pos, match.length(), random);
						pos = target.find(match);
					}
					break;
				}
				if (i == 4) {
					pos = target.find(match);
					if (pos != string::npos) {
						target.replace(pos, match.length(), replacement);
					}
					return target;
				}
			}
			pos = target.find(random);
			while (pos != string::npos) {
				target.replace(pos, random.length(), replacement);
				pos = target.find(random);
			}
			return target;
		}

		string printSort3(string item1, string item2, string item3) {
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

		string printSort2(string item1, string item2) {
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

	class Player {
	public:
		float alchemyLevel;
		float fortifyAlchemyLevel;
		float alchemistPerkLevel;
		bool hasPerkPurity;
		bool hasPerkPhysician;
		bool hasPerkBenefactor;
		bool hasPerkPoisoner;
		bool hasSeekerOfShadows;
		string state;
		string lastState;
		string miniState;

		int getPerkRank(string name) {
			int rank = 0;
			const auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			if (!playerCharacter) {
				return rank;
			}
			for (auto* perk : playerCharacter->GetPlayerRuntimeData().perks) {
				if (perk && string(perk->GetFullName()) == name) {
					++rank;
				}
			}
			return rank;
		}

		bool hasSpell(string name) {
			const auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			if (!playerCharacter) {
				return false;
			}
			for (auto* spell : playerCharacter->GetActorRuntimeData().addedSpells) {
				if (spell && string(spell->GetFullName()) == name) {
					return true;
				}
			}
			return false;
		}

		void setAlchemyLevel() {
			const auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			if (!playerCharacter || !playerCharacter->GetInfoRuntimeData().skills || !playerCharacter->GetInfoRuntimeData().skills->data) {
				alchemyLevel = 0;
				return;
			}
			alchemyLevel = playerCharacter->GetInfoRuntimeData().skills->data->skills[RE::PlayerCharacter::PlayerSkills::Data::Skills::kAlchemy].level;
		}

		void init() {
			setAlchemyLevel();
			fortifyAlchemyLevel = 0;
			alchemistPerkLevel = getPerkRank("Alchemist");
			hasPerkPurity = getPerkRank("Purity");
			hasPerkPhysician = getPerkRank("Physician");
			hasPerkBenefactor = getPerkRank("Benefactor");
			hasPerkPoisoner = getPerkRank("Poisoner");
			hasSeekerOfShadows = hasSpell("Seeker of Shadows");
		}

		void setMiniState() {
			miniState = str::fromInt(alchemyLevel) + "," + str::fromInt(fortifyAlchemyLevel) + "," + str::fromInt(alchemistPerkLevel) + "," +
				"," + str::fromInt(hasPerkPhysician) + "," + str::fromInt(hasPerkBenefactor) + "," + str::fromInt(hasPerkPoisoner) + "," + str::fromInt(hasSeekerOfShadows);
		}

		void setState() {
			state = str::fromInt(alchemyLevel) + "," + str::fromInt(fortifyAlchemyLevel) + "," + str::fromInt(alchemistPerkLevel) + "," + str::fromInt(hasPerkPurity) +
				"," + str::fromInt(hasPerkPhysician) + "," + str::fromInt(hasPerkBenefactor) + "," + str::fromInt(hasPerkPoisoner) + "," + str::fromInt(hasSeekerOfShadows);
		}

		Player() {
			alchemyLevel = 0;
			fortifyAlchemyLevel = 0;
			alchemistPerkLevel = 0;
			hasPerkPurity = false;
			hasPerkPhysician = false;
			hasPerkBenefactor = false;
			hasPerkPoisoner = false;
			hasSeekerOfShadows = false;
		};
	};

	Player player;

	namespace effect {
		string getName(GameEffect* effect) {
			string name = effect && effect->baseEffect ? effect->baseEffect->GetFullName() : "";
			return name;
		}

		bool powerAffectsMagnitude(GameEffect* effect) {
			return effect && effect->baseEffect && effect->baseEffect->data.flags.all(RE::EffectSetting::EffectSettingData::Flag::kPowerAffectsMagnitude);
		}

		bool hasKeyword(GameEffect* effect, string keyword) {
			return effect && effect->baseEffect && effect->baseEffect->HasKeywordString(keyword);
		}

		float getMagnitude(GameEffect* effect) {
			return effect ? effect->GetMagnitude() : 0.0f;
		}

		float getCalcMagnitude(GameEffect* effect) {
			float magnitude = getMagnitude(effect);
			//return round_skyrim(magnitude);
			return magnitude;
		}

		float getPerkCalcMagnitudeDebug(Effect effect, bool potion, Player player_debug) {
			float magnitude = effect.magnitude;
			if (!effect.powerAffectsMagnitude) {
				//return round_skyrim(magnitude);
				return magnitude;
			}
			float calcMagnitude = magnitude * (player_debug.alchemyLevel / 5 * 0.1 + 4) * (1 + player_debug.alchemistPerkLevel * 20 / 100) * (1 + player_debug.fortifyAlchemyLevel / 100);
			if (player_debug.hasPerkPhysician && (effect.name == "Restore Health" || effect.name == "Restore Magicka" || effect.name == "Restore Stamina")) {
				calcMagnitude = calcMagnitude * 1.25;
			}
			if (effect.beneficial && player_debug.hasPerkBenefactor && potion) {
				calcMagnitude = calcMagnitude * 1.25;
			}
			else if (!effect.beneficial && player_debug.hasPerkPoisoner && !potion) {
				calcMagnitude = calcMagnitude * 1.25;
			}
			if (player_debug.hasSeekerOfShadows) {
				calcMagnitude = calcMagnitude * 1.1;
			}
			//return round_skyrim(calcMagnitude);
			return calcMagnitude;
		}

		float getPerkCalcMagnitude(Effect effect, bool potion) {
			float magnitude = effect.magnitude;
			if (!effect.powerAffectsMagnitude) {
				return round_skyrim(magnitude);
				//return magnitude;
			}
			float calcMagnitude = magnitude * (player.alchemyLevel / 5 * 0.1 + 4) * (1 + player.alchemistPerkLevel * 20 / 100) * (1 + player.fortifyAlchemyLevel / 100);
			if (player.hasPerkPhysician && (effect.name == "Restore Health" || effect.name == "Restore Magicka" || effect.name == "Restore Stamina")) {
				calcMagnitude = calcMagnitude * 1.25;
			}
			if (effect.beneficial && player.hasPerkBenefactor && potion) {
				calcMagnitude = calcMagnitude * 1.25;
			}
			else if (!effect.beneficial && player.hasPerkPoisoner && !potion) {
				calcMagnitude = calcMagnitude * 1.25;
			}
			if (player.hasSeekerOfShadows) {
				calcMagnitude = calcMagnitude * 1.1;
			}
			return round_skyrim(calcMagnitude);
			//return calcMagnitude;
		}

		bool powerAffectsDuration(GameEffect* effect) {
			return effect && effect->baseEffect && effect->baseEffect->data.flags.all(RE::EffectSetting::EffectSettingData::Flag::kPowerAffectsDuration);
		}

		int getDuration(GameEffect* effect) {
			return effect ? static_cast<int>(effect->GetDuration()) : 0;
		}

		float getCalcDuration(GameEffect* effect) {
			float duration = getDuration(effect);
			//return round_skyrim(duration);
			return duration;
		}

		float getPerkCalcDurationDebug(Effect effect, bool potion, Player player_debug) {
			float duration = effect.duration;
			if (!effect.powerAffectsDuration) {
				//return round_skyrim(duration);
				return duration;
			}
			float calcDuration = duration * (player_debug.alchemyLevel / 5 * 0.1 + 4) * (1 + player_debug.alchemistPerkLevel * 20 / 100) * (1 + player_debug.fortifyAlchemyLevel / 100);
			if (player_debug.hasPerkPhysician && (effect.name == "Restore Health" || effect.name == "Restore Magicka" || effect.name == "Restore Stamina")) {
				calcDuration = calcDuration * 1.25;
			}
			if (effect.beneficial && player_debug.hasPerkBenefactor && potion) {
				calcDuration = calcDuration * 1.25;
			}
			else if (!effect.beneficial && player_debug.hasPerkPoisoner && !potion) {
				calcDuration = calcDuration * 1.25;
			}
			if (player_debug.hasSeekerOfShadows) {
				calcDuration = calcDuration * 1.1;
			}
			//return round_skyrim(calcDuration);
			return calcDuration;
		}

		float getPerkCalcDuration(Effect effect, bool potion) {
			float duration = effect.duration;
			if (!effect.powerAffectsDuration) {
				return round_skyrim(duration);
				//return duration;
			}
			float calcDuration = duration * (player.alchemyLevel / 5 * 0.1 + 4) * (1 + player.alchemistPerkLevel * 20 / 100) * (1 + player.fortifyAlchemyLevel / 100);
			if (player.hasPerkPhysician && (effect.name == "Restore Health" || effect.name == "Restore Magicka" || effect.name == "Restore Stamina")) {
				calcDuration = calcDuration * 1.25;
			}
			if (effect.beneficial && player.hasPerkBenefactor && potion) {
				calcDuration = calcDuration * 1.25;
			}
			else if (!effect.beneficial && player.hasPerkPoisoner && !potion) {
				calcDuration = calcDuration * 1.25;
			}
			if (player.hasSeekerOfShadows) {
				calcDuration = calcDuration * 1.1;
			}
			return round_skyrim(calcDuration);
			//return calcDuration;
		}

		float getCost(GameEffect* effect) {
			return effect ? effect->cost : 0.0f;
		}

		float getBaseCost(GameEffect* effect) {
			return effect && effect->baseEffect ? effect->baseEffect->data.baseCost : 0.0f;
		}

		float getCalcCost(GameEffect* effect) {
			float baseCost = getBaseCost(effect);
			float magnitude = getCalcMagnitude(effect);
			float duration = getCalcDuration(effect);
			if (magnitude > 0 && duration > 0) {
				return baseCost * pow(magnitude, 1.1) * pow(duration / 10, 1.1);
			}
			else if (magnitude > 0) {
				return baseCost * pow(magnitude, 1.1);
			}
			else {
				return baseCost * pow(duration / 10, 1.1);
			}
		}

		float getPerkCalcCost(Effect effect, bool potion) {
			float magnitude = getPerkCalcMagnitude(effect, potion);
			float duration = getPerkCalcDuration(effect, potion);
			if (magnitude > 0 && duration > 0) {
				return effect.baseCost * pow(magnitude, 1.1) * pow(duration / 10, 1.1);
			}
			else if (magnitude > 0) {
				return effect.baseCost * pow(magnitude, 1.1);
			}
			else {
				return effect.baseCost * pow(duration / 10, 1.1);
			}
		}

		GameEffect* getBestEffectDuplicate(vector<GameEffect*> effects1, vector<GameEffect*> effects2, GameEffect* effect3) {
			GameEffect* bestEffect = NULL;
			float cost3 = getCalcCost(effect3);
			float bestCost = 0;
			string name3 = getName(effect3);
			for (int i = 0; i < effects1.size(); ++i) {
				if (getName(effects1[i]) == name3) {
					float cost = getCalcCost(effects1[i]);
					if (cost >= cost3 && cost > bestCost) {
						bestCost = cost;
						bestEffect = effects1[i];
					}
					else if (cost3 > bestCost) {
						bestCost = cost3;
						bestEffect = effect3;
					}
				}
			}
			for (int i = 0; i < effects2.size(); ++i) {
				if (getName(effects2[i]) == name3) {
					float cost = getCalcCost(effects2[i]);
					if (cost >= cost3 && cost > bestCost) {
						bestCost = cost;
						bestEffect = effects2[i];
					}
					else if (cost3 > bestCost) {
						bestCost = cost3;
						bestEffect = effect3;
					}
				}
			}
			return bestEffect;
		}

		vector<string> getKeywords(GameEffect* effect) {
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

		string getGenericDescription(GameEffect* effect) {
			string description = effect && effect->baseEffect ? effect->baseEffect->magicItemDescription.c_str() : "";
			//description = str::replace(description, "%", "%%");
			description = str::replace(description, "<", "");
			description = str::replace(description, ">", "");
			return description;
		}

		string getDescription(GameEffect* effect) {
			string description = effect && effect->baseEffect ? effect->baseEffect->magicItemDescription.c_str() : "";
			description = str::replace(description, "<mag>", str::fromFloat(getMagnitude(effect)));
			description = str::replace(description, "<dur>", str::fromInt(getDuration(effect)));
			//description = str::replace(description, "%", "%%");
			description = str::replace(description, "<", "");
			description = str::replace(description, ">", "");
			return description;
		}

		string getPerkCalcDescriptionDebug(Effect effect, bool potion) {
			string description = effect.description;
			description = str::replace(description, "<mag>", str::fromFloat(getPerkCalcMagnitudeDebug(effect, potion, player)));
			description = str::replace(description, "<dur>", str::fromFloat(getPerkCalcDurationDebug(effect, potion, player)));
			//description = str::replace(description, "%", "%%");
			description = str::replace(description, "<", "");
			description = str::replace(description, ">", "");
			return description;
		}

		string getPerkCalcDescription(Effect effect, bool potion) {
			string description = effect.description;
			description = str::replace(description, "<mag>", str::fromFloat(getPerkCalcMagnitude(effect, potion)));
			description = str::replace(description, "<dur>", str::fromInt(getPerkCalcDuration(effect, potion)));
			//description = str::replace(description, "%", "%%");
			description = str::replace(description, "<", "");
			description = str::replace(description, ">", "");
			return description;
		}

		string printEffect(GameEffect* effect) {
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
		string getName(IngredientItem* ingredient) {
			string name = ingredient->GetFullName();
			return name;
		}

		int getValue(IngredientItem* ingredient) {
			return RE::TESValueForm::GetFormValue(ingredient);
		}

		int getIngredientValue(IngredientItem* ingredient) {
			return ingredient->data.costOverride;
		}

		vector<string> getKeywords(IngredientItem* ingredient) {
			vector<string> keywords;
			for (auto* keyword : ingredient->GetKeywords()) {
				if (keyword) {
					keywords.emplace_back(keyword->GetFormEditorID());
				}
			}
			return keywords;
		}

		vector<GameEffect*> getEffects(IngredientItem* ingredient) {
			vector<GameEffect*> effects;
			for (auto* effect : ingredient->effects) {
				if (effect) {
					effects.push_back(effect);
				}
			}
			return effects;
		}

		bool hasEffect(IngredientItem* ingredient, string eName) {
			for (auto* effect : ingredient->effects) {
				string iName = effect && effect->baseEffect ? effect->baseEffect->GetFullName() : "";
				if (iName == eName) {
					return true;
				}
			}
			return false;
		}

		bool isProtected(IngredientItem* ingredient, set<Ingredient> ingredients, map<string, int> moreIngredients, int protectIngredients, vector<string> ingredientsToNotProtect) {
			string name = ingredient ? ingredient->GetFullName() : "";
			for (string unprotectIngredient : ingredientsToNotProtect) {
				if (name == unprotectIngredient) {
					return false;
				}
			}
			auto countIt = ingredients.find(Ingredient(name, 0));
			int count = countIt != ingredients.end() ? countIt->inventoryCount : 0;
			if (protectIngredients == 1) {
				if (name == "Berit's Ashes") {
					return true; // quest
				}
				else if (name == "Bliss Bug Thorax") {
					if (count <= 11) {
						return true; // atronach forge
					}
				}
				else if (name == "Bone Hawk Claw") {
					return true; // crafting
				}
				else if (name == "Briar Heart") {
					if (count <= 3) {
						return true; // quest
					}
				}
				else if (name == "Corkbulb Root") {
					return true; // crafting
				}
				else if (name == "Corrupted Human Heart") {
					return true; // quest
				}
				else if (name == "Crimson Nirnroot") {
					if (count <= 31) {
						return true; // quest
					}
				}
				else if (name == "Daedra Heart") {
					return true;
				}
				else if (name == "Deathbell") {
					if (count <= 32) {
						return true; // atronach forge (10) + quest (21)
					}
				}
				else if (name == "Dragon's Tongue") {
					if (count <= 11) {
						return true; // atronach forge
					}
				}
				else if (name == "Daedra Heart") {
					return true;
				}
				else if (name == "Ectoplasm") {
					if (count <= 11) {
						return true; // atronach forge
					}
				}
				else if (name == "Farengar's Frost Salt") {
					return true; // quest
				}
				else if (name == "Fine-Cut Void Salts") {
					return true; // quest
				}
				else if (name == "Fire Salts") {
					if (count <= 21) {
						return true; // atronach forge (10) + quest (10)
					}
				}
				else if (name == "Frost Mirriam") {
					if (count <= 11) {
						return true; // atronach forge
					}
				}
				else if (name == "Frost Salts") {
					if (count <= 11) {
						return true; // atronach forge
					}
				}
				else if (name == "Giant's Toe") {
					if (count <= 3) {
						return true; // quest
					}
				}
				else if (name == "Goldfish") {
					if (count <= 2) {
						return true; // quest
					}
				}
				else if (name == "Hagraven Claw") {
					if (count <= 2) {
						return true; // quest
					}
				}
				else if (name == "Hagraven Feathers") {
					if (count <= 2) {
						return true; // quest
					}
				}
				else if (name == "Human Heart") {
					return true; // atronach forge
				}
				else if (name == "Ice Wraith Teeth") {
					if (count <= 6) {
						return true; // quest
					}
				}
				else if (name == "Ironwood Fruit") {
					if (count <= 2) {
						return true; // quest
					}
				}
				else if (name == "Jarrin Root") {
					return true; // quest
				}
				else if (name == "Jazbay Grapes") {
					if (count <= 21) {
						return true; // quest
					}
				}
				else if (name == "Juniper Berries") {
					if (count <= 2) {
						return true; // quest
					}
				}
				else if (name == "Juvenile Mudcrab") {
					if (count <= 2) {
						return true; // quest
					}
				}
				else if (name == "Large Antlers") {
					return true; // hearthfire
				}
				else if (name == "Mudcrab Chitin") {
					return true; // hearthfire
				}
				else if (name == "Netch Jelly") {
					if (count <= 6) {
						return true; // quest
					}
				}
				else if (name == "Nightshade") {
					if (count <= 21) {
						return true; // quest
					}
				}
				else if (name == "Nirnroot") {
					if (count <= 21) {
						return true; // quest
					}
				}
				else if (name == "Salt Pile") {
					if (count <= 11) {
						return true; // atronach forge
					}
				}
				else if (name == "Scathecraw") {
					if (count <= 11) {
						return true; // quest
					}
				}
				else if (name == "Simon Rodayne's Heart") {
					return true; // quest
				}
				else if (name == "Slaughterfish Scales") {
					return true; // hearthfire
				}
				else if (name == "Taproot") {
					if (count <= 4) {
						return true; // quest
					}
				}
				else if (name == "Torchbug Abdomen" || name == "Torchbug Thorax") {
					if (count <= 11) {
						return true; // atronach forge
					}
				}
				else if (name == "Troll Fat") {
					if (count <= 2) {
						return true; // quest
					}
				}
				else if (name == "Vampire Dust") {
					if (count <= 3) {
						return true; // quest
					}
				}
				else if (name == "Void Salts") {
					if (count <= 12) {
						return true; // atronach forge (10) + quest (1)
					}
				}
				if (hasEffect(ingredient, "Fortify Enchanting") || hasEffect(ingredient, "Fortify Smithing")) {
					return true;
				}
			}
			for (map<string, int>::iterator i = moreIngredients.begin(); i != moreIngredients.end(); i++) {
				if (name == (*i).first) {
					if (count <= (*i).second || (*i).second == 999) {
						return true;
					}
				}
			}
			return false;
		}

		string printEffects(IngredientItem* ingredient) {
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

		string printIngredient(IngredientItem* ingredient) {
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

	Effect::Effect(GameEffect* effect) {
		name = effect::getName(effect);
		beneficial = effect::hasKeyword(effect, "MagicAlchBeneficial");
		powerAffectsMagnitude = effect::powerAffectsMagnitude(effect);
		magnitude = effect::getMagnitude(effect);
		calcMagnitude = effect::getCalcMagnitude(effect);
		powerAffectsDuration = effect::powerAffectsDuration(effect);
		duration = effect::getDuration(effect);
		calcDuration = effect::getCalcDuration(effect);
		baseCost = effect::getBaseCost(effect);
		calcCost = effect::getCalcCost(effect);
		description = effect && effect->baseEffect ? effect->baseEffect->magicItemDescription.c_str() : "";
	};

	Ingredient::Ingredient(IngredientItem* ingredient) {
		name = ingredient::getName(ingredient);
		vector<GameEffect*> iEffects = ingredient::getEffects(ingredient);
		for (int i = 0; i < iEffects.size(); ++i) {
			effects.insert(Effect(iEffects[i]));
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
		set<Effect> effects;
		set<Effect> possibleEffects;
		float cost;
		Effect controlEffect;
		string description;
		string getName() {
			string translationPotionPoison = kPotionPoison.GetValue();
			vector<string> tStrings = str::split(translationPotionPoison, ',');
			string tPotion = "Potion of";
			string tPoison = "Poison of";
			if (tStrings.size() == 2) {
				tPotion = tStrings.at(0);
				tPoison = tStrings.at(1);
			}
			string title = tPoison;
			if (controlEffect.beneficial) {
				title = tPotion;
			}
			return title + " " + controlEffect.name;
		}
		bool operator< (const Potion& potion) const {
			return id < potion.id;
		}
		bool operator== (const Potion& potion) const {
			return id == potion.id;
		}
		Potion(int s, Ingredient i1, Ingredient i2, set<Effect> e, set<Effect> pe, Effect ce, float c) {
			size = s;
			id = i1.name + "," + i2.name;
			ingredient1 = i1;
			ingredient2 = i2;
			effects = e;
			possibleEffects = pe;
			controlEffect = ce;
			cost = c;
			name = getName();
		};
		Potion(int s, Ingredient i1, Ingredient i2, Ingredient i3, set<Effect> e, Effect ce, float c) {
			size = s;
			id = i1.name + "," + i2.name + "," + i3.name;
			ingredient1 = i1;
			ingredient2 = i2;
			ingredient3 = i3;
			effects = e;
			controlEffect = ce;
			cost = c;
			name = getName();
		};
		Potion(float c, string d) {
			cost = c;
			description = d;
		};
		Potion() {
			size = 0;
			cost = 0;
		};
	};
}