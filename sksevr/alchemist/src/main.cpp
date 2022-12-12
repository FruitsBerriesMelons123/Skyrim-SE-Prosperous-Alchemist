#include "version.h"  // VERSION_VERSTRING, VERSION_MAJOR
#include <ShlObj.h>  // CSIDL_MYDOCUMENTS

#include "main.h"

#include <mutex>
#include <vector>

using std::vector;
using std::mutex;

namespace alchemist {
	Potion costliestPotion;
	set<Ingredient> lastIngredientList;
	set<Ingredient> ingredients;
	set<Ingredient>::iterator ingredients_it1;
	set<Ingredient>::iterator ingredients_it2;
	set<Potion> potions;
	set<Potion>::iterator potion_it;
	mutex alchemist_mutex;
	vector<thread> threads;

	void improvePotion(Potion potion);
	void getNextPotion() {
		alchemist_mutex.lock();
		if (potion_it != potions.end()) {
			Potion potion = *potion_it;
			++potion_it;
			alchemist_mutex.unlock();
			return improvePotion(potion);
		}
		alchemist_mutex.unlock();
	}

	void improvePotion(Potion potion) {
		for (auto ingredientIt = ingredients.begin(); ingredientIt != ingredients.end(); ++ingredientIt) {
			if (potion.ingredient1 == *ingredientIt || potion.ingredient2 == *ingredientIt) {
				continue;
			}
			set<Effect> effects = potion.effects;
			Effect controlEffect = potion.controlEffect;
			double controlCost = potion.controlEffect.calcCost;
			for (Effect effect : potion.possibleEffects) {
				auto effectIt = ingredientIt->effects.find(effect);
				if (effectIt != ingredientIt->effects.end()) {
					double costCheck = 0;
					Effect effectCheck;
					if (effectIt->calcCost > effect.calcCost) {
						costCheck = effectIt->calcCost;
						effectCheck = *effectIt;
						effects.insert(*effectIt);
					}
					else {
						costCheck = effect.calcCost;
						effectCheck = effect;
						effects.insert(effect);
					}
					if (costCheck > controlCost) {
						controlEffect = effectCheck;
						controlCost = costCheck;
					}
				}
			}
			if (effects.size() == potion.effects.size()) {
				continue;
			}
			double cost = 0;
			for (Effect effect : effects) {
				if (!(player.hasPerkPurity && effect.beneficial && !controlEffect.beneficial) &&
					!(player.hasPerkPurity && !effect.beneficial && controlEffect.beneficial)) {
					cost += effect::getPerkCalcCost(effect, controlEffect.beneficial);
				}
			}
			Potion improvedPotion = Potion(3, potion.ingredient1, potion.ingredient2, *ingredientIt, effects, controlEffect, cost);
			alchemist_mutex.lock();
			//potions.insert(improvedPotion);
			if (floor(cost) > costliestPotion.cost) {
				costliestPotion = improvedPotion;
			}
			alchemist_mutex.unlock();
		}
		getNextPotion();
	}

	void makePotions2(Ingredient ingredient1, Ingredient ingredient2);
	void getNextIngredients() {
		alchemist_mutex.lock();
		while (ingredients_it1 != ingredients.end()) {
			ingredients_it2++;
			if (ingredients_it2 != ingredients.end()) {
				Ingredient ingredient1 = *ingredients_it1;
				Ingredient ingredient2 = *ingredients_it2;
				alchemist_mutex.unlock();
				return makePotions2(ingredient1, ingredient2);
			}
			else {
				ingredients_it1++;
				ingredients_it2 = ingredients_it1;
			}
		}
		alchemist_mutex.unlock();
	}

	void makePotions2(Ingredient ingredient1, Ingredient ingredient2) {
		if (ingredient1 == ingredient2) {
			return getNextIngredients();
		}
		set<Effect> effects;
		set<Effect> possibleEffects = ingredient1.effects;
		possibleEffects.merge(ingredient2.effects);
		Effect controlEffect;
		double controlCost = 0;
		for (auto it1 = ingredient1.effects.begin(); it1 != ingredient1.effects.end(); ++it1) {
			auto it2 = ingredient2.effects.find(*it1);
			if (it2 != ingredient2.effects.end()) {
				possibleEffects.erase(*it1);
				double costCheck = 0;
				Effect effectCheck;
				if (it1->calcCost > it2->calcCost) {
					costCheck = it1->calcCost;
					effectCheck = *it1;
					effects.insert(*it1);
				}
				else {
					costCheck = it2->calcCost;
					effectCheck = *it2;
					effects.insert(*it2);
				}
				if (costCheck > controlCost) {
					controlEffect = effectCheck;
					controlCost = costCheck;
				}
			}
		}
		if (controlCost == 0) {
			return getNextIngredients();
		}
		double cost = 0;
		for (Effect effect : effects) {
			if (!(player.hasPerkPurity && effect.beneficial && !controlEffect.beneficial) &&
				!(player.hasPerkPurity && !effect.beneficial && controlEffect.beneficial)) {
				cost += effect::getPerkCalcCost(effect, controlEffect.beneficial);
			}
		}
		Potion potion = Potion(2, ingredient1, ingredient2, effects, possibleEffects, controlEffect, cost);
		alchemist_mutex.lock();
		potions.insert(potion);
		if (floor(cost) > costliestPotion.cost) {
			costliestPotion = potion;
		}
		alchemist_mutex.unlock();
		getNextIngredients();
	}

	void makePotions() {
		potions.clear();

		//int max_threads = thread::hardware_concurrency();
		//int num_threads = 2;
		//if (max_threads > 2) {
		//	num_threads = max_threads - 1;
		//}
		int num_threads = kActualThreadsToUseInGame.GetData().i;
		alchemist_mutex.lock();
		ingredients_it1 = ingredients.begin();
		ingredients_it2 = ingredients_it1;
		for (int i = 0; i < num_threads; ++i) {
			while (ingredients_it1 != ingredients.end()) {
				ingredients_it2++;
				if (ingredients_it2 != ingredients.end()) {
					threads.push_back(thread(makePotions2, *ingredients_it1, *ingredients_it2));
					break;
				}
				else {
					ingredients_it1++;
					ingredients_it2 = ingredients_it1;
				}
			}
		}
		alchemist_mutex.unlock();

		for (auto& thread : threads) {
			if (thread.joinable()) {
				thread.join();
			}
		}

		threads.clear();

		alchemist_mutex.lock();
		potion_it = potions.begin();
		for (int i = 0; i < num_threads; ++i) {
			if (potion_it != potions.end()) {
				Potion potion = *potion_it;
				++potion_it;
				threads.push_back(thread(improvePotion, potion));
			}
		}
		alchemist_mutex.unlock();

		for (auto& thread : threads) {
			if (thread.joinable()) {
				thread.join();
			}
		}

		string effectDescriptions = "";
		for (auto effect : costliestPotion.effects) {
			if (!(player.hasPerkPurity && effect.beneficial && !costliestPotion.controlEffect.beneficial) &&
				!(player.hasPerkPurity && !effect.beneficial && costliestPotion.controlEffect.beneficial)) {
				effectDescriptions += " " + effect::getPerkCalcDescription(effect, costliestPotion.controlEffect.beneficial);
			}
		}
		if (costliestPotion.size == 2) {
			costliestPotion.description = costliestPotion.name + ":" + effectDescriptions +
				"\n Value: " + str::fromDouble(floor(costliestPotion.cost)) + "\n" + str::printSort2(costliestPotion.ingredient1.name, costliestPotion.ingredient2.name);
		}
		else if (costliestPotion.size == 3) {
			costliestPotion.description = costliestPotion.name + ":" + effectDescriptions +
				"\n Value: " + str::fromDouble(floor(costliestPotion.cost)) + "\n" + str::printSort3(costliestPotion.ingredient1.name, costliestPotion.ingredient2.name, costliestPotion.ingredient3.name);
		}
	}

	void improvePotionST(Potion potion) {
		for (auto ingredientIt = ingredients.begin(); ingredientIt != ingredients.end(); ++ingredientIt) {
			if (potion.ingredient1 == *ingredientIt || potion.ingredient2 == *ingredientIt) {
				continue;
			}
			set<Effect> effects = potion.effects;
			Effect controlEffect = potion.controlEffect;
			float controlCost = potion.controlEffect.calcCost;
			for (Effect effect : potion.possibleEffects) {
				auto effectIt = ingredientIt->effects.find(effect);
				if (effectIt != ingredientIt->effects.end()) {
					float costCheck = 0;
					Effect effectCheck;
					if (effectIt->calcCost > effect.calcCost) {
						costCheck = effectIt->calcCost;
						effectCheck = *effectIt;
						effects.insert(*effectIt);
					}
					else {
						costCheck = effect.calcCost;
						effectCheck = effect;
						effects.insert(effect);
					}
					if (costCheck > controlCost) {
						controlEffect = effectCheck;
						controlCost = costCheck;
					}
				}
			}
			if (effects.size() == potion.effects.size()) {
				continue;
			}
			float cost = 0;
			for (Effect effect : effects) {
				if (!(player.hasPerkPurity && effect.beneficial && !controlEffect.beneficial) &&
					!(player.hasPerkPurity && !effect.beneficial && controlEffect.beneficial)) {
					cost += effect::getPerkCalcCost(effect, controlEffect.beneficial);
				}
			}
			Potion improvedPotion = Potion(3, potion.ingredient1, potion.ingredient2, *ingredientIt, effects, controlEffect, cost);
			potions.insert(improvedPotion);
			if (floor(cost) > costliestPotion.cost) {
				costliestPotion = improvedPotion;
			}
		}
	}

	void makePotionsST2(Ingredient ingredient1, Ingredient ingredient2) {
		if (ingredient1 == ingredient2) {
			return;
		}
		set<Effect> effects;
		set<Effect> possibleEffects = ingredient1.effects;
		possibleEffects.merge(ingredient2.effects);
		Effect controlEffect;
		float controlCost = 0;
		for (auto it1 = ingredient1.effects.begin(); it1 != ingredient1.effects.end(); ++it1) {
			auto it2 = ingredient2.effects.find(*it1);
			if (it2 != ingredient2.effects.end()) {
				possibleEffects.erase(*it1);
				float costCheck = 0;
				Effect effectCheck;
				if (it1->calcCost > it2->calcCost) {
					costCheck = it1->calcCost;
					effectCheck = *it1;
					effects.insert(*it1);
				}
				else {
					costCheck = it2->calcCost;
					effectCheck = *it2;
					effects.insert(*it2);
				}
				if (costCheck > controlCost) {
					controlEffect = effectCheck;
					controlCost = costCheck;
				}
			}
		}
		if (controlCost == 0) {
			return;
		}
		float cost = 0;
		for (Effect effect : effects) {
			if (!(player.hasPerkPurity && effect.beneficial && !controlEffect.beneficial) &&
				!(player.hasPerkPurity && !effect.beneficial && controlEffect.beneficial)) {
				cost += effect::getPerkCalcCost(effect, controlEffect.beneficial);
			}
		}
		Potion potion = Potion(2, ingredient1, ingredient2, effects, possibleEffects, controlEffect, cost);
		potions.insert(potion);
		if (floor(cost) > costliestPotion.cost) {
			costliestPotion = potion;
		}
	}

	void makePotionsST() {
		potions.clear();
		auto it_end = ingredients.end();
		for (auto it1 = ingredients.begin(); it1 != it_end;) {
			Ingredient ingredient1 = *it1;
			for (auto it2 = ++it1; it2 != it_end; ++it2) {
				Ingredient ingredient2 = *it2;
				makePotionsST2(ingredient1, ingredient2);
			}
		}
		for (Potion potion : potions) {
			improvePotionST(potion);
		}

		string effectDescriptions = "";
		for (auto effect : costliestPotion.effects) {
			if (!(player.hasPerkPurity && effect.beneficial && !costliestPotion.controlEffect.beneficial) &&
				!(player.hasPerkPurity && !effect.beneficial && costliestPotion.controlEffect.beneficial)) {
				effectDescriptions += " " + effect::getPerkCalcDescription(effect, costliestPotion.controlEffect.beneficial);
			}
		}
		if (costliestPotion.size == 2) {
			costliestPotion.description = costliestPotion.name + ":" + effectDescriptions +
				"\n Value: " + str::fromFloat(floor(costliestPotion.cost)) + "\n" + str::printSort2(costliestPotion.ingredient1.name, costliestPotion.ingredient2.name);
		}
		else if (costliestPotion.size == 3) {
			costliestPotion.description = costliestPotion.name + ":" + effectDescriptions +
				"\n Value: " + str::fromFloat(floor(costliestPotion.cost)) + "\n" + str::printSort3(costliestPotion.ingredient1.name, costliestPotion.ingredient2.name, costliestPotion.ingredient3.name);
		}
	}

	void initAlchemist() {
		int ignorePlayer = kIgnorePlayer.GetData().i;
		if (ignorePlayer == 0) {
			player.init();
		}
		ExtraContainerChanges* containerChanges = static_cast<ExtraContainerChanges*>((*g_thePlayer)->extraData.GetByType(kExtraData_ContainerChanges));
		ExtraContainerChanges::Data* containerData = containerChanges ? containerChanges->data : NULL;
		EntryDataList* objList = containerData->objList;
		set<Ingredient> ingredientCount;
		for (int i = 0; i < objList->Count(); ++i) {
			InventoryEntryData* obj = objList->GetNthItem(i);
			if (obj->type->formType == kFormType_Ingredient) {
				string name = CALL_MEMBER_FN(obj, GenerateName)();
				ingredientCount.insert(Ingredient(name, obj->countDelta));
			}
		}
		VMResultArray<TESForm*> playerForms = papyrusObjectReference::GetContainerForms(*g_thePlayer);
		ingredients.clear();
		if (ignorePlayer == 0) {
			for (TESForm* form : playerForms) {
				if (form->GetFormType() == kFormType_Weapon) {
					TESObjectWEAP* item = DYNAMIC_CAST(form, TESForm, TESObjectWEAP);
					InventoryEntryData::EquipData itemData;
					containerData->GetEquipItemData(itemData, item, item->formID);
					if (itemData.isTypeWorn) {
						if (item->enchantable.enchantment) {
							for (int i = 0; i < item->enchantable.enchantment->effectItemList.count; ++i) {
								MagicItem::EffectItem* effect = item->enchantable.enchantment->effectItemList[i];
								if (effect::getName(effect) == "Fortify Alchemy") {
									player.fortifyAlchemyLevel += effect::getMagnitude(effect);
								}
							}
						}
					}
				}
				else if (form->GetFormType() == kFormType_Armor) {
					TESObjectARMO* item = DYNAMIC_CAST(form, TESForm, TESObjectARMO);
					InventoryEntryData::EquipData itemData;
					containerData->GetEquipItemData(itemData, item, item->formID);
					if (itemData.isTypeWorn) {
						if (item->enchantable.enchantment) {
							for (int i = 0; i < item->enchantable.enchantment->effectItemList.count; ++i) {
								MagicItem::EffectItem* effect = item->enchantable.enchantment->effectItemList[i];
								if (effect::getName(effect) == "Fortify Alchemy") {
									player.fortifyAlchemyLevel += effect::getMagnitude(effect);
								}
							}
						}
					}
				}
			}
		}
		int protectIngredients = kProtectIngredients.GetData().i;
		for (TESForm* form : playerForms) {
			if (form->GetFormType() == kFormType_Ingredient) {
				IngredientItem* ingredient = DYNAMIC_CAST(form, TESForm, IngredientItem);
				if (protectIngredients == 0 || !ingredient::isProtected(ingredient, ingredientCount)) {
					ingredients.insert(Ingredient(ingredient));
				}
			}
		}
		if (ignorePlayer == 0) {
			player.setState();
		}
	}

	// SINGLETHREADED: all 177 ingredients in 19 seconds, and no crashing 
	// MULTITHREADED: any more than 102 or 103 ingredients and it crashes (your results may differ)
	void stressTest() {
		int stressTestCount = kNumberOfIngredientsToStressTest.GetData().i;
		ingredients.clear();
		auto allIngredients = DataHandler::GetSingleton()->ingredients;
		for (int i = 0; i < allIngredients.count && i < stressTestCount; ++i) {
			ingredients.insert(Ingredient(allIngredients[i]));
		}
	}

	class Scaleform_RegisterGetBestRecipeNameHandler : public GFxFunctionHandler {
	public:
		virtual void Invoke(Args* args) {
			static int performStressTest = 0;
			performStressTest = kNumberOfIngredientsToStressTest.GetData().i;
			static string alchemist_result = "";
			alchemist_result = "";
			if (args && args->numArgs && args->numArgs > 0) {
				string craft_description = *(args->args[0].data.managedString);
				if (craft_description.find("Alchemy") != string::npos && g_thePlayer) {
					static time_t start;
					if (performStressTest > 0) {
						start = time(NULL);
					}
					initAlchemist();
					if (performStressTest > 0) {
						stressTest();
					}
					if (ingredients != lastIngredientList || player.state != player.lastState) {
						costliestPotion = Potion(0, "No potion recipes are currently available.");
						lastIngredientList = set<Ingredient>(ingredients);
						player.lastState = player.state;
						if (kActualThreadsToUseInGame.GetData().i < 2) {
							makePotionsST();
						}
						else {
							makePotions();
						}
					}
					alchemist_result = costliestPotion.description;
					if (performStressTest > 0) {
						time_t end = time(NULL);
						_LOG(str::fromInt(end - start) + " seconds");
						_LOG(str::fromInt(ingredients.size()) + " ingredients");
					}
				}
			}
			if (args && args->result) {
				args->result->CleanManaged();
				args->result->type = GFxValue::kType_String;
				args->result->data.string = alchemist_result.c_str();
			}
		}
	};

	class Scaleform_RegisterGetBestRecipeDescriptionHandler : public GFxFunctionHandler {
	public:
		virtual void Invoke(Args* args) {
			if (args && args->result) {
				args->result->CleanManaged();
				args->result->type = GFxValue::kType_String;
				args->result->data.string = "";
			}
		}
	};

	bool RegisterScaleformHandlers(GFxMovieView* view, GFxValue* plugin) {
		RegisterFunction<Scaleform_RegisterGetBestRecipeNameHandler>(plugin, view, "GetBestRecipeName");
		RegisterFunction<Scaleform_RegisterGetBestRecipeDescriptionHandler>(plugin, view, "GetBestRecipeDescription");
		return true;
	}
}

extern "C" {
	bool SKSEPlugin_Query(const SKSEInterface *a_skse, PluginInfo *a_info) {
		gLog.OpenRelative(CSIDL_MYDOCUMENTS, "\\My Games\\Skyrim Special Edition\\SKSE\\alchemist.log");
		gLog.SetPrintLevel(IDebugLog::kLevel_DebugMessage);
		gLog.SetLogLevel(IDebugLog::kLevel_DebugMessage);

		_MESSAGE("alchemist v%s", MYFP_VERSION_VERSTRING);

		a_info->infoVersion = PluginInfo::kInfoVersion;
		a_info->name = "alchemist";
		a_info->version = MYFP_VERSION_MAJOR;

		if (a_skse->isEditor) {
			_FATALERROR("[FATAL ERROR] Loaded in editor, marking as incompatible!\n");
			return false;
		} else if (a_skse->runtimeVersion != CURRENT_RELEASE_RUNTIME) {
			_FATALERROR("[FATAL ERROR] Unsupported runtime version %08X!\n", a_skse->runtimeVersion);
			return false;
		}

		return true;
	}

	bool SKSEPlugin_Load(const SKSEInterface *a_skse) {
		_MESSAGE("[MESSAGE] prosperous alchemist loaded");

		SKSEScaleformInterface *scaleformInterface = (SKSEScaleformInterface*)a_skse->QueryInterface(kInterface_Scaleform);

		bool registerScaleformHandlers = scaleformInterface->Register("alchemist", alchemist::RegisterScaleformHandlers);

		if (registerScaleformHandlers) {
			srand(time(NULL));
			_MESSAGE("[MESSAGE] prosperous alchemist scaleform handlers registered");
		}

		return true;
	}
};