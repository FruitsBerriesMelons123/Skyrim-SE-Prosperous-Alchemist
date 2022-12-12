#pragma once

#include "IDebugLog.h"
#include "skse64_common/skse_version.h"
#include "skse64/PapyrusActorValueInfo.cpp"
#include "skse64/PapyrusForm.cpp"
#include "skse64/PapyrusIngredient.h"
#include "skse64/PapyrusMagicEffect.h"
#include "skse64/PapyrusObjectReference.cpp"
#include "skse64/PluginAPI.h"
#include "skse64/ScaleformCallbacks.h"
#include "skse64/ScaleformMovie.h"

#include "SME_Prefix.h"
#include "INIManager.h"

#include "skillLevels.h"

#include <time.h>
#include <string>
#include <set>
#include <thread>
#include <algorithm>
#include <sstream>
#include <iterator>
#include <iostream>
#include <vector>
#include <map>

using std::set;
using std::string;
using std::thread;
using std::map;
using std::vector;

extern IDebugLog						gLog;

PluginHandle					kPluginHandle;
SKSEMessagingInterface* kMsgInterface;

extern SME::INI::INISetting				kIgnorePlayer;
extern SME::INI::INISetting				kProtectIngredients;
extern SME::INI::INISetting				kMaximumThreadsForCurrentHardware;
extern SME::INI::INISetting				kActualThreadsToUseInGame;
extern SME::INI::INISetting				kNumberOfIngredientsToStressTest;
extern SME::INI::INISetting				kMoreIngredientsToProtect;

class AlchemistINIManager : public SME::INI::INIManager
{
public:
	void								Initialize(const char* INIPath, void* Parameter) override;

	static AlchemistINIManager			Instance;
};

AlchemistINIManager		AlchemistINIManager::Instance;

SME::INI::INISetting	kIgnorePlayer("IgnorePlayer",
	"General",
	"Ignore player state when calculating potion cost.",
	(SInt32)1);

SME::INI::INISetting	kProtectIngredients("ProtectIngredients",
	"General",
	"Protect certain ingredients",
	(SInt32)0);

SME::INI::INISetting	kMaximumThreadsForCurrentHardware("MaximumThreadsForCurrentHardware",
	"General",
	"Not used in game. Number of threads recommended for the current hardware.",
	(SInt32)(thread::hardware_concurrency() - 1));

SME::INI::INISetting	kActualThreadsToUseInGame("ActualThreadsToUseInGame",
	"General",
	"Number of threads to use in game.",
	(SInt32)(thread::hardware_concurrency() - 1));

SME::INI::INISetting	kNumberOfIngredientsToStressTest("NumberOfIngredientsToStressTest",
	"General",
	"Use this number of ingredients in a stress test.",
	(SInt32)0);

SME::INI::INISetting	kMoreIngredientsToProtect("MoreIngredientsToProtect",
	"General",
	"Comma separated list of additional ingredients to protect.",
	"");

void AlchemistINIManager::Initialize(const char* INIPath, void* Parameter)
{
	this->INIFilePath = INIPath;
	_MESSAGE("prosperous alchemist INI Path: %s", INIPath);

	std::fstream INIStream(INIPath, std::fstream::in);
	bool CreateINI = false;

	if (INIStream.fail())
	{
		_MESSAGE("prosperous alchemist INI File not found; Creating one...");
		CreateINI = true;
	}

	INIStream.close();
	INIStream.clear();

	RegisterSetting(&kIgnorePlayer);
	RegisterSetting(&kProtectIngredients);
	RegisterSetting(&kMaximumThreadsForCurrentHardware);
	RegisterSetting(&kActualThreadsToUseInGame);
	RegisterSetting(&kNumberOfIngredientsToStressTest);
	RegisterSetting(&kMoreIngredientsToProtect);

	if (CreateINI)
		Save();
}

namespace alchemist {
	void _LOG(string s) {
		_MESSAGE(s.c_str());
	}

	double round_skyrim(double f) {
		//if (...figure out why skyrim sometimes rounds toward zero) {
			//return ceil(f - 0.5); // round toward zero
		//} // else
		return floor(f + 0.5); // round away from zero
	}

	class Effect {
	public:
		string name;
		bool beneficial;
		bool powerAffectsMagnitude;
		double magnitude;
		double calcMagnitude;
		bool powerAffectsDuration;
		double duration;
		double calcDuration;
		double baseCost;
		double calcCost;
		string description;
		bool operator< (const Effect& effect) const {
			return name < effect.name;
		}
		bool operator== (const Effect& effect) const {
			return name == effect.name;
		}
		Effect(IngredientItem::EffectItem* effect);
		Effect() {};
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
		Ingredient() {};
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

		string fromChar(BSFixedString c) {
			string str_c = c;
			return str_c;
		}

		string fromInt(int n) {
			char n_char[30];
			snprintf(n_char, 30, "%d", n);
			string n_str = n_char;
			return n_str;
		}

		string fromDouble(double f) {
			if (abs(f - int(f)) == 0) {
				return fromInt(f);
			}
			char f_char[30];
			snprintf(f_char, 30, "%.2f", f);
			string f_str = f_char;
			return f_str;
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
		double alchemyLevel;
		double fortifyAlchemyLevel;
		double alchemistPerkLevel;
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
			for (int i = 0; i < (*g_thePlayer)->perks.count; ++i) {
				if (str::fromChar((*g_thePlayer)->perks[i]->fullName.name) == name) {
					++rank;
				}
			}
			return rank;
		}

		bool hasSpell(string name) {
			for (int i = 0; i < (*g_thePlayer)->addedSpells.Length(); ++i) {
				if (str::fromChar((*g_thePlayer)->addedSpells.Get(i)->fullName.name) == name) {
					return true;
				}
			}
			return false;
		}

		void setAlchemyLevel() {
			PlayerSkills* skills = (*g_thePlayer)->skills;
			int alchemySkillID = skills->ResolveAdvanceableSkillId(16);
			double level = skillLevel::fromExp(skills->data->levelData[alchemySkillID].pointsMax);
			alchemyLevel = level;
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

		Player() {};
	};

	Player player;

	namespace effect {
		string getName(IngredientItem::EffectItem* effect) {
			string name = effect->mgef->fullName.name;
			return name;
		}

		bool powerAffectsMagnitude(IngredientItem::EffectItem* effect) {
			return papyrusMagicEffect::IsEffectFlagSet(effect->mgef, EffectSetting::Properties::kEffectType_Magnitude);
		}

		bool hasKeyword(IngredientItem::EffectItem* effect, string keyword) {
			VMResultArray<BGSKeyword*> pKeywords = papyrusForm::GetKeywords(effect->mgef);
			for (int i = 0; i < effect->mgef->keywordForm.numKeywords; ++i) {
				if (str::fromChar(pKeywords.at(i)->keyword) == keyword) {
					return true;
				}
			}
			return false;
		}

		double getMagnitude(IngredientItem::EffectItem* effect) {
			return effect->magnitude;
		}

		double getCalcMagnitude(IngredientItem::EffectItem* effect) {
			double magnitude = effect->magnitude;
			//return round_skyrim(magnitude);
			return magnitude;
		}

		double getPerkCalcMagnitudeDebug(Effect effect, bool potion, Player player_debug) {
			double magnitude = effect.magnitude;
			if (!effect.powerAffectsMagnitude) {
				//return round_skyrim(magnitude);
				return magnitude;
			}
			double calcMagnitude = magnitude * (player_debug.alchemyLevel / 5 * 0.1 + 4) * (1 + player_debug.alchemistPerkLevel * 20 / 100) * (1 + player_debug.fortifyAlchemyLevel / 100);
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

		double getPerkCalcMagnitude(Effect effect, bool potion) {
			double magnitude = effect.magnitude;
			if (!effect.powerAffectsMagnitude) {
				return round_skyrim(magnitude);
				//return magnitude;
			}
			double calcMagnitude = magnitude * (player.alchemyLevel / 5 * 0.1 + 4) * (1 + player.alchemistPerkLevel * 20 / 100) * (1 + player.fortifyAlchemyLevel / 100);
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

		bool powerAffectsDuration(IngredientItem::EffectItem* effect) {
			return papyrusMagicEffect::IsEffectFlagSet(effect->mgef, EffectSetting::Properties::kEffectType_Duration);
		}

		int getDuration(IngredientItem::EffectItem* effect) {
			return effect->duration;
		}

		double getCalcDuration(IngredientItem::EffectItem* effect) {
			double duration = effect->duration;
			//return round_skyrim(duration);
			return duration;
		}

		double getPerkCalcDurationDebug(Effect effect, bool potion, Player player_debug) {
			double duration = effect.duration;
			if (!effect.powerAffectsDuration) {
				//return round_skyrim(duration);
				return duration;
			}
			double calcDuration = duration * (player_debug.alchemyLevel / 5 * 0.1 + 4) * (1 + player_debug.alchemistPerkLevel * 20 / 100) * (1 + player_debug.fortifyAlchemyLevel / 100);
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

		double getPerkCalcDuration(Effect effect, bool potion) {
			double duration = effect.duration;
			if (!effect.powerAffectsDuration) {
				return round_skyrim(duration);
				//return duration;
			}
			double calcDuration = duration * (player.alchemyLevel / 5 * 0.1 + 4) * (1 + player.alchemistPerkLevel * 20 / 100) * (1 + player.fortifyAlchemyLevel / 100);
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

		double getCost(IngredientItem::EffectItem* effect) {
			return effect->cost;
		}

		double getBaseCost(IngredientItem::EffectItem* effect) {
			return effect->mgef->properties.baseCost;
		}

		double getCalcCost(IngredientItem::EffectItem* effect) {
			double baseCost = getBaseCost(effect);
			double magnitude = getCalcMagnitude(effect);
			double duration = getCalcDuration(effect);
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

		double getPerkCalcCost(Effect effect, bool potion) {
			double magnitude = getPerkCalcMagnitude(effect, potion);
			double duration = getPerkCalcDuration(effect, potion);
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

		IngredientItem::EffectItem* getBestEffectDuplicate(tArray<IngredientItem::EffectItem*> effects1, tArray<IngredientItem::EffectItem*> effects2, IngredientItem::EffectItem* effect3) {
			IngredientItem::EffectItem* bestEffect = NULL;
			double cost3 = getCalcCost(effect3);
			double bestCost = 0;
			string name3 = getName(effect3);
			for (int i = 0; i < effects1.count; ++i) {
				if (getName(effects1[i]) == name3) {
					double cost = getCalcCost(effects1[i]);
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
			for (int i = 0; i < effects2.count; ++i) {
				if (getName(effects2[i]) == name3) {
					double cost = getCalcCost(effects2[i]);
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

		tArray<string> getKeywords(IngredientItem::EffectItem* effect) {
			tArray<string> keywords = tArray<string>();
			VMResultArray<BGSKeyword*> pKeywords = papyrusForm::GetKeywords(effect->mgef);
			for (int i = 0; i < effect->mgef->keywordForm.numKeywords; ++i) {
				string keyword = pKeywords.at(i)->keyword;
				keywords.Push(keyword);
			}
			return keywords;
		}

		string getGenericDescription(IngredientItem::EffectItem* effect) {
			string description = effect->mgef->description;
			//description = str::replace(description, "%", "%%");
			description = str::replace(description, "<", "");
			description = str::replace(description, ">", "");
			return description;
		}

		string getDescription(IngredientItem::EffectItem* effect) {
			string description = effect->mgef->description;
			description = str::replace(description, "<mag>", str::fromDouble(getMagnitude(effect)));
			description = str::replace(description, "<dur>", str::fromInt(getDuration(effect)));
			//description = str::replace(description, "%", "%%");
			description = str::replace(description, "<", "");
			description = str::replace(description, ">", "");
			return description;
		}

		string getPerkCalcDescriptionDebug(Effect effect, bool potion) {
			string description = effect.description;
			description = str::replace(description, "<mag>", str::fromDouble(getPerkCalcMagnitudeDebug(effect, potion, player)));
			description = str::replace(description, "<dur>", str::fromDouble(getPerkCalcDurationDebug(effect, potion, player)));
			//description = str::replace(description, "%", "%%");
			description = str::replace(description, "<", "");
			description = str::replace(description, ">", "");
			return description;
		}

		string getPerkCalcDescription(Effect effect, bool potion) {
			string description = effect.description;
			description = str::replace(description, "<mag>", str::fromDouble(getPerkCalcMagnitude(effect, potion)));
			description = str::replace(description, "<dur>", str::fromInt(getPerkCalcDuration(effect, potion)));
			//description = str::replace(description, "%", "%%");
			description = str::replace(description, "<", "");
			description = str::replace(description, ">", "");
			return description;
		}

		string printEffect(IngredientItem::EffectItem* effect) {
			string data = getName(effect) + "," +
				str::fromDouble(getMagnitude(effect)) + "," +
				str::fromInt(getDuration(effect)) + "," +
				str::fromDouble(getCost(effect)) + "," +
				str::fromDouble(getBaseCost(effect));
			tArray<string> keywords = getKeywords(effect);
			for (int i = 0; i < 3; ++i) {
				if (i < keywords.count) {
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
			string name = ingredient->fullName.name;
			return name;
		}

		int getValue(IngredientItem* ingredient) {
			int value = ingredient->value.value;
			return value;
		}

		int getIngredientValue(IngredientItem* ingredient) {
			int ingredientValue = ingredient->unk130;
			return ingredientValue;
		}

		tArray<string> getKeywords(IngredientItem* ingredient) {
			tArray<string> keywords = tArray<string>();
			VMResultArray<BGSKeyword*> pKeywords = papyrusForm::GetKeywords(ingredient);
			for (int i = 0; i < ingredient->keyword.numKeywords; ++i) {
				string keyword = pKeywords.at(i)->keyword;
				keywords.Push(keyword);
			}
			return keywords;
		}

		tArray<IngredientItem::EffectItem*> getEffects(IngredientItem* ingredient) {
			tArray<IngredientItem::EffectItem*> effects = tArray<IngredientItem::EffectItem*>();
			for (int i = 0; i < ingredient->effectItemList.count; ++i) {
				IngredientItem::EffectItem* effect;
				ingredient->effectItemList.GetNthItem(i, effect);
				effects.Push(effect);
			}
			return effects;
		}

		bool hasEffect(IngredientItem* ingredient, string eName) {
			for (int i = 0; i < ingredient->effectItemList.count; ++i) {
				IngredientItem::EffectItem* effect;
				ingredient->effectItemList.GetNthItem(i, effect);
				string iName = effect->mgef->fullName.name;
				if (iName == eName) {
					return true;
				}
			}
			return false;
		}

		bool isProtected(IngredientItem* ingredient, set<Ingredient> ingredients, map<string,int> moreIngredients) {
			string name = ingredient->fullName.name;
			auto countIt = ingredients.find(Ingredient(name, 0));
			int count = countIt->inventoryCount;
			if (name == "Berit's Ashes") {
				return true; // quest
			}
			else if (name == "Bone Hawk Claw") {
				return true; // crafting
			}
			else if (name == "Briar Heart") {
				if (count <= 3) {
					return true; // quest
				}
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
			else if (name == "Ectoplasm") {
				if (count <= 11) {
					return true; // atronach forge
				}
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
			for (map<string,int>::iterator i = moreIngredients.begin(); i != moreIngredients.end(); i++) {
				if ((*i).first == name) {
					if (count <= (*i).second || (*i).second == 999) {
						return true;
					}
				}
			}
			return false;
		}

		string printEffects(IngredientItem* ingredient) {
			string data = "";
			tArray<IngredientItem::EffectItem*> effects = getEffects(ingredient);
			for (int i = 0; i < effects.count; ++i) {
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
			tArray<string> keywords = getKeywords(ingredient);
			for (int i = 0; i < 3; ++i) {
				if (i < keywords.count) {
					data += "," + keywords[i];
				}
				else {
					data += ",";
				}
			}
			tArray<IngredientItem::EffectItem*> effects = getEffects(ingredient);
			for (int i = 0; i < effects.count; ++i) {
				data += "," + effect::printEffect(effects[i]);
			}
			return data;
		}
	}

	Effect::Effect(IngredientItem::EffectItem* effect) {
		name = effect->mgef->fullName.name;
		beneficial = effect::hasKeyword(effect, "MagicAlchBeneficial");
		powerAffectsMagnitude = papyrusMagicEffect::IsEffectFlagSet(effect->mgef, EffectSetting::Properties::kEffectType_Magnitude);
		magnitude = effect->magnitude;
		calcMagnitude = effect::getCalcMagnitude(effect);
		powerAffectsDuration = papyrusMagicEffect::IsEffectFlagSet(effect->mgef, EffectSetting::Properties::kEffectType_Duration);
		duration = effect->duration;
		calcDuration = effect::getCalcDuration(effect);
		baseCost = effect->mgef->properties.baseCost;
		calcCost = effect::getCalcCost(effect);
		description = effect->mgef->description;
	};

	Ingredient::Ingredient(IngredientItem* ingredient) {
		name = ingredient->fullName.name;
		tArray<IngredientItem::EffectItem*> iEffects = ingredient::getEffects(ingredient);
		for (int i = 0; i < iEffects.count; ++i) {
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
		double cost;
		Effect controlEffect;
		string description;
		string getName() {
			string title = "Poison of ";
			if (controlEffect.beneficial) {
				title = "Potion of ";
			}
			return title + controlEffect.name;
		}
		bool operator< (const Potion& potion) const {
			return id < potion.id;
		}
		bool operator== (const Potion& potion) const {
			return id == potion.id;
		}
		Potion(int s, Ingredient i1, Ingredient i2, set<Effect> e, set<Effect> pe, Effect ce, double c) {
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
		Potion(int s, Ingredient i1, Ingredient i2, Ingredient i3, set<Effect> e, Effect ce, double c) {
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
		Potion(double c, string d) {
			cost = c;
			description = d;
		};
		Potion() {};
	};
}