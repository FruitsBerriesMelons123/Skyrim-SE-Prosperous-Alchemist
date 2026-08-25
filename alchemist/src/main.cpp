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
			NativePotionResult nativeResult = effect::evaluatePotion({
				potion.ingredient1.nativeIngredient,
				potion.ingredient2.nativeIngredient,
				ingredientIt->nativeIngredient
			});
			if (!nativeResult.valid || nativeResult.effects.size() <= potion.effects.size()) {
				continue;
			}
			Potion improvedPotion = Potion(3, potion.ingredient1, potion.ingredient2, *ingredientIt,
				nativeResult.effects, nativeResult.controlEffect, nativeResult.cost);
			alchemist_mutex.lock();
			//potions.insert(improvedPotion); // WHY IS THIS LINE HERE!?
			++combinations;
			if (floor(nativeResult.cost) > costliestPotion.cost) {
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
		set<Effect> possibleEffects = ingredient1.effects;
		possibleEffects.merge(ingredient2.effects);
		for (auto it1 = ingredient1.effects.begin(); it1 != ingredient1.effects.end(); ++it1) {
			auto it2 = ingredient2.effects.find(*it1);
			if (it2 != ingredient2.effects.end()) {
				possibleEffects.erase(*it1);
			}
		}
		NativePotionResult nativeResult = effect::evaluatePotion({
			ingredient1.nativeIngredient,
			ingredient2.nativeIngredient
		});
		if (!nativeResult.valid) {
			return getNextIngredients();
		}
		Potion potion = Potion(2, ingredient1, ingredient2, nativeResult.effects, possibleEffects,
			nativeResult.controlEffect, nativeResult.cost);
		alchemist_mutex.lock();
		potions.insert(potion);
		if (floor(nativeResult.cost) > costliestPotion.cost) {
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
			NativePotionResult nativeResult = effect::evaluatePotion({
				potion.ingredient1.nativeIngredient,
				potion.ingredient2.nativeIngredient,
				ingredientIt->nativeIngredient
			});
			if (!nativeResult.valid || nativeResult.effects.size() <= potion.effects.size()) {
				continue;
			}
			Potion improvedPotion = Potion(3, potion.ingredient1, potion.ingredient2, *ingredientIt,
				nativeResult.effects, nativeResult.controlEffect, nativeResult.cost);
			//potions.insert(improvedPotion); // WHY IS THIS LINE HERE!?
			++combinations;
			if (floor(nativeResult.cost) > costliestPotion.cost) {
				costliestPotion = improvedPotion;
			}
		}
	}

	void makePotionsST2(Ingredient ingredient1, Ingredient ingredient2) {
		if (ingredient1 == ingredient2) {
			return;
		}
		set<Effect> possibleEffects = ingredient1.effects;
		possibleEffects.merge(ingredient2.effects);
		for (auto it1 = ingredient1.effects.begin(); it1 != ingredient1.effects.end(); ++it1) {
			auto it2 = ingredient2.effects.find(*it1);
			if (it2 != ingredient2.effects.end()) {
				possibleEffects.erase(*it1);
			}
		}
		NativePotionResult nativeResult = effect::evaluatePotion({
			ingredient1.nativeIngredient,
			ingredient2.nativeIngredient
		});
		if (!nativeResult.valid) {
			return;
		}
		Potion potion = Potion(2, ingredient1, ingredient2, nativeResult.effects, possibleEffects,
			nativeResult.controlEffect, nativeResult.cost);
		potions.insert(potion);
		if (floor(nativeResult.cost) > costliestPotion.cost) {
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
		int ignorePlayer = kIgnorePlayer.GetValue();
		auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
		if (!playerCharacter) {
			return;
		}
		if (ignorePlayer == 0) {
			player.init();
			player.fortifyAlchemyLevel = 0;
		}
		auto inventory = playerCharacter->GetInventory();
		set<Ingredient> ingredientCount;
		for (const auto& [form, entry] : inventory) {
			auto* ingredient = form ? form->As<IngredientItem>() : nullptr;
			if (ingredient) {
				Ingredient ownedIngredient(ingredient);
				ownedIngredient.inventoryCount = entry.first;
				ingredientCount.insert(std::move(ownedIngredient));
			}
		}

		if (ignorePlayer == 0) {
			for (const auto& [form, entry] : inventory) {
				if (!entry.second || !entry.second->IsWorn()) {
					continue;
				}
				if (auto* enchantment = entry.second->GetEnchantment()) {
					for (auto* effect : enchantment->effects) {
						if (effect::getName(effect) == "Fortify Alchemy") {
							player.fortifyAlchemyLevel += effect::getMagnitude(effect);
						}
					}
				}
			}
		}

		int protectIngredients = kProtectIngredients.GetValue();
		vector<string> ingredientsToNotProtect = str::split(kIngredientsToUnprotect.GetValue(), ',');
		map<string, int> moreIngredients;
		if (protectIngredients > 0) {
			string additionalIngredients = kMoreIngredientsToProtect.GetValue();
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
		ingredients.clear();
		for (const auto& [form, entry] : inventory) {
			auto* ingredient = form ? form->As<IngredientItem>() : nullptr;
			if (ingredient && (protectIngredients == 0 || !ingredient::isProtected(ingredient, ingredientCount, moreIngredients, protectIngredients, ingredientsToNotProtect))) {
				ingredients.insert(Ingredient(ingredient));
			}
		}
		if (ignorePlayer == 0) {
			player.setState();
		}
	}

	// SINGLETHREADED: all ingredients in 2 seconds
	// MULTITHREADED: all ingredients in 1 second or less
	void stressTest() {
		int stressTestCount = kNumberOfIngredientsToStressTest.GetValue();
		ingredients.clear();
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			return;
		}
		auto& allIngredients = dataHandler->GetFormArray<IngredientItem>();
		for (std::uint32_t i = 0; i < allIngredients.size() && i < stressTestCount; ++i) {
			ingredients.insert(Ingredient(allIngredients[i]));
		}
	}

	void printIngredients() {
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			return;
		}
		auto& allIngredients = dataHandler->GetFormArray<IngredientItem>();
		for (auto* ingredient : allIngredients) {
			if (ingredient) {
				_LOG(ingredient->GetFullName());
			}
		}
	}

	class Scaleform_RegisterGetBestRecipeNameHandler : public RE::GFxFunctionHandler {
	public:
		void Call(Params& a_params) override {
			static int performStressTest = 0;
			performStressTest = kNumberOfIngredientsToStressTest.GetValue();
			if (performStressTest == -1) {
				printIngredients();
			}
			string translationStringCommas = kStringTranslations.GetValue();
			vector<string> tStrings = str::split(translationStringCommas, ',');
			string tAlchemy = "Alchemy";
			string tNoPotions = "No potion recipes are currently available.";
			if (tStrings.size() == 2) {
				tAlchemy = tStrings.at(0);
				tNoPotions = tStrings.at(1);
			}
			static string alchemist_result = "";
			alchemist_result = "";
			if (a_params.args && a_params.argCount > 0) {
				const auto& craftDescription = a_params.args[0];
				if (craftDescription.IsString()) {
					string craft_description = craftDescription.GetString();
					if (craft_description.find(tAlchemy) != string::npos) {
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
						if (kSinglethreaded.GetValue() == 1) {
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
			}
			if (a_params.retVal) {
				a_params.retVal->SetString(alchemist_result);
			}
		}
	};

	class Scaleform_RegisterGetBestRecipeDescriptionHandler : public RE::GFxFunctionHandler {
	public:
		void Call(Params& a_params) override {
			if (a_params.retVal) {
				a_params.retVal->SetString("");
			}
		}
	};

	bool RegisterScaleformHandlers(RE::GFxMovieView* view, RE::GFxValue* plugin) {
		if (!view || !plugin) {
			return false;
		}
		RE::GFxValue getBestRecipeName;
		RE::GFxValue getBestRecipeDescription;
		view->CreateFunction(&getBestRecipeName, new Scaleform_RegisterGetBestRecipeNameHandler());
		view->CreateFunction(&getBestRecipeDescription, new Scaleform_RegisterGetBestRecipeDescriptionHandler());
		return plugin->SetMember("GetBestRecipeName", getBestRecipeName) &&
			plugin->SetMember("GetBestRecipeDescription", getBestRecipeDescription);
	}
}

void MessageHandler(SKSE::MessagingInterface::Message* msg)
{
	if (msg && msg->type == SKSE::MessagingInterface::kInputLoaded) {
		alchemist::_LOG("...prosperous alchemist initialized!");
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
	SKSE::Init(skse);
	alchemist::_LOG("[MESSAGE] Initializing prosperous alchemist...");

	REX::INI::SettingStore::GetSingleton()->Init("Data\\SKSE\\Plugins\\alchemist.ini", "");
	REX::INI::SettingStore::GetSingleton()->Load();
	REX::INI::SettingStore::GetSingleton()->Save();

	const auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging || messaging->Version() < SKSE::MessagingInterface::kVersion) {
		alchemist::_LOG("Couldn't initialize messaging interface");
		return false;
	}
	if (!messaging->RegisterListener("SKSE", MessageHandler)) {
		alchemist::_LOG("Couldn't register message listener");
		return false;
	}

	const auto* scaleform = SKSE::GetScaleformInterface();
	if (!scaleform || !scaleform->Register(alchemist::RegisterScaleformHandlers, "alchemist")) {
		alchemist::_LOG("Couldn't register Scaleform handlers");
		return false;
	}

	srand(static_cast<unsigned int>(time(nullptr)));
	alchemist::_LOG("[MESSAGE] prosperous alchemist Scaleform handlers registered");
	return true;
}
