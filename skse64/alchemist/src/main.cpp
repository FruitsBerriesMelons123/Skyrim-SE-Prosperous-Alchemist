#include "version.h"  // VERSION_VERSTRING, VERSION_MAJOR
#include <ShlObj.h>  // CSIDL_MYDOCUMENTS

#include "main.h"

#include <mutex>

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
	int combinations;

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
			alchemist_mutex.lock();
			//potions.insert(improvedPotion); // WHY IS THIS LINE HERE!?
			++combinations;
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
			return getNextIngredients();
		}
		float cost = 0;
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

		alchemist_mutex.lock();
		ingredients_it1 = ingredients.begin();
		ingredients_it2 = ingredients_it1;
		for (int i = 0; i < ingredients.size(); ++i) {
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
		combinations = potions.size();
		potion_it = potions.begin();
		for (int i = 0; i < ingredients.size(); ++i) {
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
				"\n Value: " + str::fromFloat(floor(costliestPotion.cost)) + "\n" + str::printSort2(costliestPotion.ingredient1.name, costliestPotion.ingredient2.name);
		}
		else if (costliestPotion.size == 3) {
			costliestPotion.description = costliestPotion.name + ":" + effectDescriptions +
				"\n Value: " + str::fromFloat(floor(costliestPotion.cost)) + "\n" + str::printSort3(costliestPotion.ingredient1.name, costliestPotion.ingredient2.name, costliestPotion.ingredient3.name);
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
			//potions.insert(improvedPotion); // WHY IS THIS LINE HERE!?
			++combinations;
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

		combinations = potions.size();

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
		if (containerData == NULL) {
			_LOG("containerData was NULL... initAlchemist has been aborted...");
			return;
		}
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
		vector<string> ingredientsToNotProtect = str::split(kIngredientsToUnprotect.GetData().s, ',');
		map<string, int> moreIngredients;
		if (protectIngredients > 0) {
			string additionalIngredients = kMoreIngredientsToProtect.GetData().s;
			vector<string> iTokens = str::split(additionalIngredients, ',');
			for (auto& iToken : iTokens) {
				vector<string> iParts = str::split(iToken, '|');
				if (iParts.size() == 1) {
					moreIngredients[iParts.at(0)] = 999;
				}
				else if (iParts.size() == 2) {
					moreIngredients[iParts.at(0)] = str::toInt(iParts.at(1));
				}
			}
		}
		for (TESForm* form : playerForms) {
			if (form->GetFormType() == kFormType_Ingredient) {
				IngredientItem* ingredient = DYNAMIC_CAST(form, TESForm, IngredientItem);
				if (protectIngredients == 0 || !ingredient::isProtected(ingredient, ingredientCount, moreIngredients, protectIngredients, ingredientsToNotProtect)) {
					ingredients.insert(Ingredient(ingredient));
				}
			}
		}
		if (ignorePlayer == 0) {
			player.setState();
		}
	}

	// SINGLETHREADED: all ingredients in 2 seconds
	// MULTITHREADED: all ingredients in 1 second or less
	void stressTest() {
		int stressTestCount = kNumberOfIngredientsToStressTest.GetData().i;
		ingredients.clear();
		auto allIngredients = DataHandler::GetSingleton()->ingredients;
		for (int i = 0; i < allIngredients.count && i < stressTestCount; ++i) {
			ingredients.insert(Ingredient(allIngredients[i]));
		}
	}

	void printIngredients() {
		auto allIngredients = DataHandler::GetSingleton()->ingredients;
		for (int i = 0; i < allIngredients.count; ++i) {
			_LOG(allIngredients[i]->fullName.name.c_str());
		}
	}

	class Scaleform_RegisterGetBestRecipeNameHandler : public GFxFunctionHandler {
	public:
		virtual void Invoke(Args* args) {
			static int performStressTest = 0;
			performStressTest = kNumberOfIngredientsToStressTest.GetData().i;
			if (performStressTest == -1) {
				printIngredients();
			}
			string translationStringCommas = kStringTranslations.GetData().s;
			vector<string> tStrings = str::split(translationStringCommas, ',');
			string tAlchemy = "Alchemy";
			string tNoPotions = "No potion recipes are currently available.";
			if (tStrings.size() == 2) {
				tAlchemy = tStrings.at(0);
				tNoPotions = tStrings.at(1);
			}
			static string alchemist_result = "";
			alchemist_result = "";
			if (args && args->numArgs && args->numArgs > 0) {
				string craft_description = *(args->args[0].data.managedString);
				if (craft_description.find(tAlchemy) != string::npos && g_thePlayer) {
					static time_t start;
					if (performStressTest > 0) {
						start = time(NULL);
					}
					initAlchemist();
					if (performStressTest > 0) {
						stressTest();
					}
					if (ingredients != lastIngredientList || player.state != player.lastState) {
						costliestPotion = Potion(0, tNoPotions);
						lastIngredientList = set<Ingredient>(ingredients);
						player.lastState = player.state;
						if (kSinglethreaded.GetData().i == 1) {
							makePotionsST();
						}
						else {
							makePotions();
						}
						_LOG("Calculated ingredients from " + std::to_string(combinations) + " different possible combinations.");
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
	bool SKSEPlugin_Query(const SKSEInterface* a_skse, PluginInfo* a_info) {
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
		}
		else if (a_skse->runtimeVersion != CURRENT_RELEASE_RUNTIME) {
			_FATALERROR("[FATAL ERROR] Unsupported runtime version %08X!\n", a_skse->runtimeVersion);
			return false;
		}

		return true;
	}

	void MessageHandler(SKSEMessagingInterface::Message* msg)
	{
		switch (msg->type)
		{
		case SKSEMessagingInterface::kMessage_InputLoaded:
		{
			_MESSAGE("...prosperous alchemist initialized!");
		}
		break;
		}
	}

	bool SKSEPlugin_Load(const SKSEInterface* a_skse) {
		gLog.OpenRelative(CSIDL_MYDOCUMENTS, "\\My Games\\Skyrim Special Edition\\SKSE\\alchemist.log");
		_MESSAGE("[MESSAGE] Initializing prosperous alchemist...");

		kPluginHandle = a_skse->GetPluginHandle();
		kMsgInterface = (SKSEMessagingInterface*)a_skse->QueryInterface(kInterface_Messaging);

		if (!kMsgInterface)
		{
			_MESSAGE("Couldn't initialize messaging interface");
			return false;
		}
		else if (kMsgInterface->interfaceVersion < 2)
		{
			_MESSAGE("Messaging interface too old (%d expected %d)", kMsgInterface->interfaceVersion, 2);
			return false;
		}

		_MESSAGE("Initializing prosperous alchemist INI Manager");
		AlchemistINIManager::Instance.Initialize("Data\\SKSE\\Plugins\\alchemist.ini", nullptr);

		if (kMsgInterface->RegisterListener(kPluginHandle, "SKSE", MessageHandler) == false)
		{
			_MESSAGE("Couldn't register message listener");
			return false;
		}

		SKSEScaleformInterface* scaleformInterface = (SKSEScaleformInterface*)a_skse->QueryInterface(kInterface_Scaleform);

		bool registerScaleformHandlers = scaleformInterface->Register("alchemist", alchemist::RegisterScaleformHandlers);

		if (registerScaleformHandlers) {
			srand(time(NULL));
			_MESSAGE("[MESSAGE] prosperous alchemist scaleform handlers registered");
		}

		return true;
	}

	//Skyrim AE induced SKSE versioning
	SKSEPluginVersionData SKSEPlugin_Version =
	{
		SKSEPluginVersionData::kVersion,

		1,
		"Prosperous Alchemist",
		"FruitsBerriesMelons123",
		"",

		0,	// not version independent
		{ CURRENT_RELEASE_RUNTIME, 0 },

		0,	// works with any version of the script extender. you probably do not need to put anything here
	};
};