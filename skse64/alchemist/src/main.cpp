#include "version.h"  // VERSION_VERSTRING, VERSION_MAJOR
#include <ShlObj.h>  // CSIDL_MYDOCUMENTS

#include "main.h"

namespace alchemist {
	set<Ingredient> lastIngredientList;
	set<Ingredient> ingredients;
	set<Potion> potions;
	Potion costliestPotion;

	void improvePotion(Potion potion) {
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
					} else {
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

	void makePotions2(Ingredient ingredient1, Ingredient ingredient2) {
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
				} else {
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

	void makePotions() {
		potions.clear();
		auto it_end = ingredients.end();
		for (auto it1 = ingredients.begin(); it1 != it_end;) {
			Ingredient ingredient1 = *it1;
			for (auto it2 = ++it1; it2 != it_end; ++it2) {
				Ingredient ingredient2 = *it2;
				makePotions2(ingredient1, ingredient2);
			}
		}
		for (Potion potion : potions) {
			improvePotion(potion);
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
		} else if (costliestPotion.size == 3) {
			costliestPotion.description = costliestPotion.name + ":" + effectDescriptions +
				"\n Value: " + str::fromFloat(floor(costliestPotion.cost)) + "\n" + str::printSort3(costliestPotion.ingredient1.name, costliestPotion.ingredient2.name, costliestPotion.ingredient3.name);
		}
	}

	void initAlchemist() {
		player.init();
		ExtraContainerChanges *containerChanges = static_cast<ExtraContainerChanges*>((*g_thePlayer)->extraData.GetByType(kExtraData_ContainerChanges));
		ExtraContainerChanges::Data *containerData = containerChanges ? containerChanges->data : NULL;
		EntryDataList *objList = containerData->objList;
		set<Ingredient> ingredientCount;
		for (int i = 0; i < objList->Count(); ++i) {
			InventoryEntryData *obj = objList->GetNthItem(i);
			if (obj->type->formType == kFormType_Ingredient) {
				string name = CALL_MEMBER_FN(obj, GenerateName)();
				ingredientCount.insert(Ingredient(name, obj->countDelta));
			}
		}
		VMResultArray<TESForm*> playerForms = papyrusObjectReference::GetContainerForms(*g_thePlayer);
		ingredients.clear();
		for (TESForm *form : playerForms) {
			if (form->GetFormType() == kFormType_Weapon) {
				TESObjectWEAP *item = DYNAMIC_CAST(form, TESForm, TESObjectWEAP);
				InventoryEntryData::EquipData itemData;
				containerData->GetEquipItemData(itemData, item, item->formID);
				if (itemData.isTypeWorn) {
					if (item->enchantable.enchantment) {
						for (int i = 0; i < item->enchantable.enchantment->effectItemList.count; ++i) {
							MagicItem::EffectItem *effect = item->enchantable.enchantment->effectItemList[i];
							if (effect::getName(effect) == "Fortify Alchemy") {
								player.fortifyAlchemyLevel += effect::getMagnitude(effect);
							}
						}
					}
				}
			} else if (form->GetFormType() == kFormType_Armor) {
				TESObjectARMO *item = DYNAMIC_CAST(form, TESForm, TESObjectARMO);
				InventoryEntryData::EquipData itemData;
				containerData->GetEquipItemData(itemData, item, item->formID);
				if (itemData.isTypeWorn) {
					if (item->enchantable.enchantment) {
						for (int i = 0; i < item->enchantable.enchantment->effectItemList.count; ++i) {
							MagicItem::EffectItem *effect = item->enchantable.enchantment->effectItemList[i];
							if (effect::getName(effect) == "Fortify Alchemy") {
								player.fortifyAlchemyLevel += effect::getMagnitude(effect);
							}
						}
					}
				}
			}
		}
		for (TESForm *form : playerForms) {
			if (form->GetFormType() == kFormType_Ingredient) {
				IngredientItem *ingredient = DYNAMIC_CAST(form, TESForm, IngredientItem);
				ingredients.insert(Ingredient(ingredient));
			}
		}
	}
	
	// DEBUG tests lol
	// 115 ingredients = 56 seconds
	// 99 ingredients = 32 seconds
	// 90 ingredients = 23 seconds
	// 81 ingredients = 15 seconds
	// 70 ingredients = 9 seconds
	// 60 ingredients = 4 seconds
	// 53 ingredients = 3 seconds
	// 47 ingredients = 2 seconds
	// 34 ingredients = 1 second
	// RELEASE tests
	// 115 ingredients = 5 seconds
	// 108 ingredients = 4 seconds
	// 103 ingredients = 3 seconds
	// 91 ingredients = 2 seconds
	// 71 ingredients = 1 second
	void stressTest() {
		ingredients.clear();
		auto allIngredients = DataHandler::GetSingleton()->ingredients;
		for (int i = 0; i < allIngredients.count; ++i) {
		//for (int i = 0; i < 71; ++i) {
			ingredients.insert(Ingredient(allIngredients[i]));
		}
	}

	class Scaleform_RegisterGetBestRecipeNameHandler : public GFxFunctionHandler {
	public:
		virtual void Invoke(Args *args) {
			if (g_thePlayer) {
				//time_t start = time(NULL);
				initAlchemist();
				//stressTest();
				if (ingredients != lastIngredientList || player.state != player.lastState) {
					costliestPotion = Potion(0, "No potion recipes are currently available.");
					lastIngredientList = set<Ingredient>(ingredients);
					player.lastState = player.state;
					makePotions();
				}
				//time_t end = time(NULL);
				//_LOG(str::fromInt(end - start) + " seconds");
				//_LOG(str::fromInt(ingredients.size()) + " ingredients");
			}
			if (args->args[0].GetType() == GFxValue::kType_String) {
				args->result->CleanManaged();
				args->result->type = GFxValue::kType_String;
				args->result->data.string = costliestPotion.description.c_str();
			}
		}
	};

	class Scaleform_RegisterGetBestRecipeDescriptionHandler : public GFxFunctionHandler	{
	public:
		virtual void Invoke(Args *args) {
			if (args->args[0].GetType() == GFxValue::kType_String) {
				args->result->CleanManaged();
				args->result->type = GFxValue::kType_String;
				args->result->data.string = "";
			}
		}
	};

	bool RegisterScaleformHandlers(GFxMovieView *view, GFxValue *plugin) {
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
		} else if (a_skse->runtimeVersion != RUNTIME_VERSION_1_5_97) {
			_FATALERROR("[FATAL ERROR] Unsupported runtime version %08X!\n", a_skse->runtimeVersion);
			return false;
		}

		return true;
	}

	bool SKSEPlugin_Load(const SKSEInterface *a_skse) {
		_MESSAGE("[MESSAGE] alchemist loaded");

		SKSEScaleformInterface *scaleformInterface = (SKSEScaleformInterface*)a_skse->QueryInterface(kInterface_Scaleform);

		bool registerScaleformHandlers = scaleformInterface->Register("alchemist", alchemist::RegisterScaleformHandlers);

		if (registerScaleformHandlers) {
			srand(time(NULL));
			_MESSAGE("[MESSAGE] scaleform handlers registered");
		}

		return true;
	}
};
