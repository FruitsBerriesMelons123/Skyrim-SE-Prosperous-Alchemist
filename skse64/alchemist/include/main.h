#pragma once

#include "common/IDebugLog.h"
#include "skse64_common/skse_version.h"
#include "skse64\PapyrusActorValueInfo.cpp"
#include "skse64/PapyrusForm.cpp"
#include "skse64/PapyrusIngredient.h"
#include "skse64\PapyrusMagicEffect.h"
#include "skse64/PapyrusObjectReference.cpp"
#include "skse64/PluginAPI.h"
#include "skse64/ScaleformCallbacks.h"
#include "skse64/ScaleformMovie.h"

#include "skillLevels.h"

#include <time.h>
#include <string>
#include <set>

using std::set;
using std::string;

namespace alchemist {
	void _LOG(string s) {
		_MESSAGE(s.c_str());
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
		bool operator< (const Effect & effect) const {
			return name < effect.name;
		}
		bool operator== (const Effect & effect) const {
			return name == effect.name;
		}
		Effect(IngredientItem::EffectItem *effect);
		Effect() {};
	};

	class Ingredient {
	public:
		string name;
		set<Effect> effects;
		int inventoryCount;
		bool operator< (const Ingredient & ingredient) const {
			return name < ingredient.name;
		}
		bool operator== (const Ingredient & ingredient) const {
			return name == ingredient.name;
		}
		Ingredient(IngredientItem *ingredient);
		Ingredient(string n, int ic) {
			name = n;
			inventoryCount = ic;
		};
		Ingredient() {};
	};

	namespace str {
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
			while(random.size() != 9) {
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
				} else if (pos == string::npos) {
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
			if (item1 < item2  && item1 < item3) {
				if (item2 < item3) {
					sorted = item1 + ", " + item2 + ", " + item3;
				} else {
					sorted = item1 + ", " + item3 + ", " + item2;
				}
			} else if (item2 < item1 && item2 < item3) {
				if (item1 < item3) {
					sorted = item2 + ", " + item1 + ", " + item3;
				} else {
					sorted = item2 + ", " + item3 + ", " + item1;
				}
			} else if (item1 < item2) {
				sorted = item3 + ", " + item1 + ", " + item2;
			} else {
				sorted = item3 + ", " + item2 + ", " + item1;
			}
			return sorted;
		}

		string printSort2(string item1, string item2) {
			string sorted = "";
			if (item1 < item2) {
				sorted = item1 + ", " + item2;
			} else {
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
			float level = skillLevel::fromExp(skills->data->levelData[alchemySkillID].pointsMax);
			alchemyLevel = level;
		}

		void init() {
			setAlchemyLevel();
			fortifyAlchemyLevel = 0;
			hasPerkPurity = getPerkRank("Purity");
			alchemistPerkLevel = getPerkRank("Alchemist");
			hasPerkPhysician = getPerkRank("Physician");
			hasPerkBenefactor = getPerkRank("Benefactor");
			hasPerkPoisoner = getPerkRank("Poisoner");
			hasSeekerOfShadows = hasSpell("Seeker of Shadows");
			state = str::fromInt(alchemyLevel) + str::fromInt(fortifyAlchemyLevel) + str::fromInt(hasPerkPurity) +
				str::fromInt(hasPerkPhysician) + str::fromInt(hasPerkBenefactor) + str::fromInt(hasPerkPoisoner) + str::fromInt(hasSeekerOfShadows);
		}

		Player() {};
	};

	Player player;

	namespace effect {
		string getName(IngredientItem::EffectItem *effect) {
			string name = effect->mgef->fullName.name;
			return name;
		}

		bool powerAffectsMagnitude(IngredientItem::EffectItem* effect) {
			return papyrusMagicEffect::IsEffectFlagSet(effect->mgef, EffectSetting::Properties::kEffectType_Magnitude);
		}

		bool hasKeyword(IngredientItem::EffectItem *effect, string keyword) {
			VMResultArray<BGSKeyword*> pKeywords = papyrusForm::GetKeywords(effect->mgef);
			for (int i = 0; i < effect->mgef->keywordForm.numKeywords; ++i) {
				if (str::fromChar(pKeywords.at(i)->keyword) == keyword) {
					return true;
				}
			}
			return false;
		}

		float getMagnitude(IngredientItem::EffectItem *effect) {
			return effect->magnitude;
		}

		float getCalcMagnitude(IngredientItem::EffectItem *effect) {
			float magnitude = effect->magnitude;
			if (!powerAffectsMagnitude(effect)) {
				return round(magnitude);
			}
			return round(magnitude * (player.alchemyLevel / 5 * 0.1 + 4) * (1 + player.alchemistPerkLevel * 20 / 100) * (1 + player.fortifyAlchemyLevel / 100));
		}

		float getPerkCalcMagnitude(Effect effect, bool potion) {
			float magnitude = effect.magnitude;
			if (!effect.powerAffectsMagnitude) {
				return round(magnitude);
			}
			float calcMagnitude = magnitude * (player.alchemyLevel / 5 * 0.1 + 4) * (1 + player.alchemistPerkLevel * 20 / 100) * (1 + player.fortifyAlchemyLevel / 100);
			if (player.hasPerkPhysician && (effect.name == "Restore Health" || effect.name == "Restore Magicka" || effect.name == "Restore Stamina")) {
				calcMagnitude = calcMagnitude * 1.25;
			}
			if (effect.beneficial && player.hasPerkBenefactor && potion) {
				calcMagnitude = calcMagnitude * 1.25;
			} else if (!effect.beneficial && player.hasPerkPoisoner && !potion) {
				calcMagnitude = calcMagnitude * 1.25;
			}
			if (player.hasSeekerOfShadows) {
				calcMagnitude = calcMagnitude * 1.1;
			}
			return round(calcMagnitude);
		}

		bool powerAffectsDuration(IngredientItem::EffectItem* effect) {
			return papyrusMagicEffect::IsEffectFlagSet(effect->mgef, EffectSetting::Properties::kEffectType_Duration);
		}

		int getDuration(IngredientItem::EffectItem *effect) {
			return effect->duration;
		}

		float getCalcDuration(IngredientItem::EffectItem *effect) {
			float duration = effect->duration;
			if (!powerAffectsDuration(effect)) {
				return round(duration);
			}
			return round(duration * (player.alchemyLevel / 5 * 0.1 + 4) * (1 + player.alchemistPerkLevel * 20 / 100) * (1 + player.fortifyAlchemyLevel / 100));
		}

		float getPerkCalcDuration(Effect effect, bool potion) {
			float duration = effect.duration;
			if (!effect.powerAffectsDuration) {
				return round(duration);
			}
			float calcDuration = duration * (player.alchemyLevel / 5 * 0.1 + 4) * (1 + player.alchemistPerkLevel * 20 / 100) * (1 + player.fortifyAlchemyLevel / 100);
			if (player.hasPerkPhysician && (effect.name == "Restore Health" || effect.name == "Restore Magicka" || effect.name == "Restore Stamina")) {
				calcDuration = calcDuration * 1.25;
			}
			if (effect.beneficial && player.hasPerkBenefactor && potion) {
				calcDuration = calcDuration * 1.25;
			} else if (!effect.beneficial && player.hasPerkPoisoner && !potion) {
				calcDuration = calcDuration * 1.25;
			}
			if (player.hasSeekerOfShadows) {
				calcDuration = calcDuration * 1.1;
			}
			return round(calcDuration);
		}

		float getCost(IngredientItem::EffectItem *effect) {
			return effect->cost;
		}

		float getBaseCost(IngredientItem::EffectItem *effect) {
			return effect->mgef->properties.baseCost;
		}

		float getCalcCost(IngredientItem::EffectItem* effect) {
			float baseCost = getBaseCost(effect);
			float magnitude = getCalcMagnitude(effect);
			float duration = getCalcDuration(effect);
			if (magnitude > 0 && duration > 0) {
				return baseCost * pow(magnitude, 1.1) * pow(duration / 10, 1.1);
			} else if (magnitude > 0) {
				return baseCost * pow(magnitude, 1.1);
			} else {
				return baseCost * pow(duration / 10, 1.1);
			}
		}

		float getPerkCalcCost(Effect effect, bool potion) {
			float magnitude = getPerkCalcMagnitude(effect, potion);
			float duration = getPerkCalcDuration(effect, potion);
			if (magnitude > 0 && duration > 0) {
				return effect.baseCost * pow(magnitude, 1.1) * pow(duration / 10, 1.1);
			} else if (magnitude > 0) {
				return effect.baseCost * pow(magnitude, 1.1);
			} else {
				return effect.baseCost * pow(duration / 10, 1.1);
			}
		}

		IngredientItem::EffectItem* getBestEffectDuplicate(tArray<IngredientItem::EffectItem*> effects1, tArray<IngredientItem::EffectItem*> effects2, IngredientItem::EffectItem *effect3) {
			IngredientItem::EffectItem *bestEffect = NULL;
			float cost3 = getCalcCost(effect3);
			float bestCost = 0;
			string name3 = getName(effect3);
			for (int i = 0; i < effects1.count; ++i) {
				if (getName(effects1[i]) == name3) {
					float cost = getCalcCost(effects1[i]);
					if (cost >= cost3 && cost > bestCost) {
						bestCost = cost;
						bestEffect = effects1[i];
					} else if (cost3 > bestCost) {
						bestCost = cost3;
						bestEffect = effect3;
					}
				}
			}
			for (int i = 0; i < effects2.count; ++i) {
				if (getName(effects2[i]) == name3) {
					float cost = getCalcCost(effects2[i]);
					if (cost >= cost3 && cost > bestCost) {
						bestCost = cost;
						bestEffect = effects2[i];
					} else if (cost3 > bestCost) {
						bestCost = cost3;
						bestEffect = effect3;
					}
				}
			}
			return bestEffect;
		}

		tArray<string> getKeywords(IngredientItem::EffectItem *effect) {
			tArray<string> keywords = tArray<string>();
			VMResultArray<BGSKeyword*> pKeywords = papyrusForm::GetKeywords(effect->mgef);
			for (int i = 0; i < effect->mgef->keywordForm.numKeywords; ++i) {
				string keyword = pKeywords.at(i)->keyword;
				keywords.Push(keyword);
			}
			return keywords;
		}

		string getGenericDescription(IngredientItem::EffectItem *effect) {
			string description = effect->mgef->description;
			//description = str::replace(description, "%", "%%");
			description = str::replace(description, "<", "");
			description = str::replace(description, ">", "");
			return description;
		}

		string getDescription(IngredientItem::EffectItem *effect) {
			string description = effect->mgef->description;
			description = str::replace(description, "<mag>", str::fromFloat(getMagnitude(effect)));
			description = str::replace(description, "<dur>", str::fromInt(getDuration(effect)));
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

		string printEffect(IngredientItem::EffectItem *effect) {
			string data = getName(effect) + "," +
				str::fromFloat(getMagnitude(effect)) + "," +
				str::fromInt(getDuration(effect)) + "," +
				str::fromFloat(getCost(effect)) + "," +
				str::fromFloat(getBaseCost(effect));
			tArray<string> keywords = getKeywords(effect);
			for (int i = 0; i < 3; ++i) {
				if (i < keywords.count) {
					data += "," + keywords[i];
				} else {
					data += ",";
				}
			}
			data += "," + getDescription(effect);
			return data;
		}
	}

	namespace ingredient {
		string getName(IngredientItem *ingredient) {
			string name = ingredient->fullName.name;
			return name;
		}

		int getValue(IngredientItem *ingredient) {
			int value = ingredient->value.value;
			return value;
		}

		int getIngredientValue(IngredientItem *ingredient) {
			int ingredientValue = ingredient->unk130;
			return ingredientValue;
		}

		tArray<string> getKeywords(IngredientItem *ingredient) {
			tArray<string> keywords = tArray<string>();
			VMResultArray<BGSKeyword*> pKeywords = papyrusForm::GetKeywords(ingredient);
			for (int i = 0; i < ingredient->keyword.numKeywords; ++i) {
				string keyword = pKeywords.at(i)->keyword;
				keywords.Push(keyword);
			}
			return keywords;
		}

		tArray<IngredientItem::EffectItem*> getEffects(IngredientItem *ingredient) {
			tArray<IngredientItem::EffectItem*> effects = tArray<IngredientItem::EffectItem*>();
			for (int i = 0; i < ingredient->effectItemList.count; ++i) {
				IngredientItem::EffectItem *effect;
				ingredient->effectItemList.GetNthItem(i, effect);
				effects.Push(effect);
			}
			return effects;
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

		string printIngredient(IngredientItem *ingredient) {
			string data = getName(ingredient) + "," +
				str::fromInt(getValue(ingredient)) + "," +
				str::fromInt(getIngredientValue(ingredient));
			tArray<string> keywords = getKeywords(ingredient);
			for (int i = 0; i < 3; ++i) {
				if (i < keywords.count) {
					data += "," + keywords[i];
				} else {
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

	Effect::Effect(IngredientItem::EffectItem *effect) {
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

	Ingredient::Ingredient(IngredientItem *ingredient) {
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
		float cost;
		Effect controlEffect;
		string description;
		string getName() {
			string title = "Poison of ";
			if (controlEffect.beneficial) {
				title = "Potion of ";
			}
			return title + controlEffect.name;
		}
		bool operator< (const Potion & potion) const {
			return id < potion.id;
		}
		bool operator== (const Potion & potion) const {
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
		Potion() {};
	};
}
