#include "main.h"
#include "AlchemyPlus/AlchemyPlus.h"
#include "DeveloperTestHub.h"
#include "MenuHandler.h"
#include "RenderHook.h"

#include <array>

namespace alchemist {
	Potion costliestPotion;
	set<Ingredient> lastIngredientList;
	set<Ingredient> ingredients;
	set<Potion> potions;
	int combinations;

	namespace {
		struct IngredientCombination
		{
			std::array<std::size_t, 3> indices{};
			std::size_t size = 0;
		};

		struct CandidateResult
		{
			IngredientCombination combination;
			Potion potion;
		};

		set<Effect> getPossibleEffects(const Ingredient& ingredient1, const Ingredient& ingredient2)
		{
			set<Effect> possibleEffects;
			possibleEffects.insert(ingredient1.effects.begin(), ingredient1.effects.end());
			possibleEffects.insert(ingredient2.effects.begin(), ingredient2.effects.end());
			for (const auto& effect : ingredient1.effects) {
				if (std::find(ingredient2.effects.begin(), ingredient2.effects.end(), effect) != ingredient2.effects.end()) {
					possibleEffects.erase(effect);
				}
			}
			return possibleEffects;
		}

		std::optional<Potion> evaluateCombination(
			const IngredientCombination& combination,
			const vector<const Ingredient*>& availableIngredients)
		{
			if (combination.size < 2 || combination.size > 3) {
				return std::nullopt;
			}
			vector<const Ingredient*> selectedIngredients;
			selectedIngredients.reserve(combination.size);
			for (std::size_t index = 0; index < combination.size; ++index) {
				if (combination.indices[index] >= availableIngredients.size() || !availableIngredients[combination.indices[index]]) {
					return std::nullopt;
				}
				selectedIngredients.push_back(availableIngredients[combination.indices[index]]);
			}

			const auto nativeResult = effect::evaluatePotion(selectedIngredients);
			if (!nativeResult.valid || !std::isfinite(nativeResult.cost)) {
				return std::nullopt;
			}
			const auto& ingredient1 = *selectedIngredients[0];
			const auto& ingredient2 = *selectedIngredients[1];
			if (combination.size == 2) {
				return Potion(2, ingredient1, ingredient2, nativeResult.effects,
					getPossibleEffects(ingredient1, ingredient2), nativeResult.controlEffect, nativeResult.isPoison, nativeResult.cost);
			}
			return Potion(3, ingredient1, ingredient2, *selectedIngredients[2],
				nativeResult.effects, nativeResult.controlEffect, nativeResult.isPoison, nativeResult.cost);
		}

		vector<IngredientCombination> buildPairCombinations(std::size_t ingredientCount)
		{
			vector<IngredientCombination> combinations;
			if (ingredientCount < 2) {
				return combinations;
			}
			combinations.reserve(ingredientCount * (ingredientCount - 1) / 2);
			for (std::size_t first = 0; first + 1 < ingredientCount; ++first) {
				for (std::size_t second = first + 1; second < ingredientCount; ++second) {
					combinations.push_back({ { first, second, 0 }, 2 });
				}
			}
			return combinations;
		}

		vector<IngredientCombination> buildTripleCombinations(std::size_t ingredientCount)
		{
			vector<IngredientCombination> combinations;
			if (ingredientCount < 3) {
				return combinations;
			}
			combinations.reserve(ingredientCount * (ingredientCount - 1) * (ingredientCount - 2) / 6);
			for (std::size_t first = 0; first + 2 < ingredientCount; ++first) {
				for (std::size_t second = first + 1; second + 1 < ingredientCount; ++second) {
					for (std::size_t third = second + 1; third < ingredientCount; ++third) {
						combinations.push_back({ { first, second, third }, 3 });
					}
				}
			}
			return combinations;
		}

		vector<CandidateResult> evaluateCombinations(
			const vector<IngredientCombination>& candidates,
			const vector<const Ingredient*>& availableIngredients,
			bool multithreaded)
		{
			if (candidates.empty()) {
				return {};
			}
			const auto hardwareThreads = std::thread::hardware_concurrency();
			const std::size_t requestedWorkers = hardwareThreads > 0 ? hardwareThreads : 1;
			const std::size_t workerCount = multithreaded ?
				(std::min)(requestedWorkers, candidates.size()) : 1;
			std::atomic<std::size_t> nextCandidate = 0;
			vector<vector<CandidateResult>> workerResults(workerCount);
			const auto evaluateWorker = [&](std::size_t workerIndex) {
				auto& results = workerResults[workerIndex];
				while (true) {
					const auto candidateIndex = nextCandidate.fetch_add(1, std::memory_order_relaxed);
					if (candidateIndex >= candidates.size()) {
						break;
					}
					if (auto potion = evaluateCombination(candidates[candidateIndex], availableIngredients)) {
						results.push_back({ candidates[candidateIndex], std::move(*potion) });
					}
				}
			};

			if (workerCount == 1) {
				evaluateWorker(0);
			} else {
				vector<thread> workers;
				workers.reserve(workerCount);
				for (std::size_t workerIndex = 0; workerIndex < workerCount; ++workerIndex) {
					workers.emplace_back(evaluateWorker, workerIndex);
				}
				for (auto& worker : workers) {
					worker.join();
				}
			}

			std::size_t resultCount = 0;
			for (const auto& results : workerResults) {
				resultCount += results.size();
			}
			vector<CandidateResult> evaluated;
			evaluated.reserve(resultCount);
			for (auto& results : workerResults) {
				for (auto& result : results) {
					evaluated.push_back(std::move(result));
				}
			}
			return evaluated;
		}

		bool isBetterPotion(const Potion& candidate)
		{
			if (costliestPotion.size <= 0) {
				return true;
			}
			if (candidate.cost != costliestPotion.cost) {
				return candidate.cost > costliestPotion.cost;
			}
			return candidate.id < costliestPotion.id;
		}

		void storePotion(Potion potion)
		{
			const auto [it, inserted] = potions.insert(std::move(potion));
			if (inserted && isBetterPotion(*it)) {
				costliestPotion = *it;
			}
		}

		void setCostliestDescription()
		{
			if (costliestPotion.size != 2 && costliestPotion.size != 3) {
				return;
			}
			string effectDescriptions;
			for (const auto& effect : costliestPotion.effects) {
				effectDescriptions += " " + effect::getPerkCalcDescription(effect, costliestPotion.controlEffect.beneficial);
			}
			const auto ingredientText = costliestPotion.size == 2 ?
				str::printSort2(costliestPotion.ingredient1.name, costliestPotion.ingredient2.name) :
				str::printSort3(costliestPotion.ingredient1.name, costliestPotion.ingredient2.name, costliestPotion.ingredient3.name);
			costliestPotion.description = costliestPotion.name + ":" + effectDescriptions +
				"\n Value: " + str::fromFloat(floor(costliestPotion.cost)) + "\n" + ingredientText;
		}

		void generatePotions(bool multithreaded)
		{
			potions.clear();
			costliestPotion = Potion();

			vector<const Ingredient*> availableIngredients;
			availableIngredients.reserve(ingredients.size());
			for (const auto& ingredient : ingredients) {
				availableIngredients.push_back(&ingredient);
			}

			const auto pairCandidates = buildPairCombinations(availableIngredients.size());
			const auto validPairs = evaluateCombinations(pairCandidates, availableIngredients, multithreaded);
			for (const auto& result : validPairs) {
				storePotion(result.potion);
			}

			const auto tripleCandidates = buildTripleCombinations(availableIngredients.size());
			const auto validTriples = evaluateCombinations(tripleCandidates, availableIngredients, multithreaded);
			for (const auto& result : validTriples) {
				storePotion(result.potion);
			}

			combinations = static_cast<int>(potions.size());
			setCostliestDescription();
		}
	}

	void makePotions()
	{
		generatePotions(true);
	}

	void makePotionsST()
	{
		generatePotions(false);
	}

	void initAlchemist() {
		caco::Adapter::Refresh();
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
			auto* ingredient = form && form->Is(RE::FormType::Ingredient) ? static_cast<IngredientItem*>(form) : nullptr;
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
						if (effect::isFortifyAlchemy(effect)) {
							player.fortifyAlchemyLevel += effect::getMagnitude(effect);
						}
					}
				}
			}

			if (const auto* playerCharacter = RE::PlayerCharacter::GetSingleton()) {
				if (auto* magicTarget = const_cast<RE::PlayerCharacter*>(playerCharacter)->GetMagicTarget()) {
					if (auto* activeEffects = magicTarget->GetActiveEffectList()) {
						for (const auto* activeEffect : *activeEffects) {
							if (!activeEffect || activeEffect->flags.any(RE::ActiveEffect::Flag::kInactive, RE::ActiveEffect::Flag::kDispelled)) {
								continue;
							}
							if (activeEffect->flags.any(RE::ActiveEffect::Flag::kEnchanting) ||
								(activeEffect->spell && activeEffect->spell->Is(RE::FormType::Enchantment))) {
								continue;
							}
							if (effect::isFortifyAlchemy(activeEffect->GetBaseObject())) {
								player.fortifyAlchemyLevel += activeEffect->GetMagnitude();
							}
						}
					}
				}
			}
		}

		const bool protectIngredients = kProtectIngredients.GetValue() != 0;
		map<string, int> moreIngredients;
		if (protectIngredients) {
			string additionalIngredients = kProtectedIngredients.GetValue();
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
			auto* ingredient = form && form->Is(RE::FormType::Ingredient) ? static_cast<IngredientItem*>(form) : nullptr;
			if (ingredient && (!protectIngredients || !ingredient::isProtected(ingredient, ingredientCount, moreIngredients))) {
				ingredients.insert(Ingredient(ingredient));
			}
		}
		if (ignorePlayer == 0) {
			player.captureAlchemyEvaluationContext();
			player.setState();
		}
	}

	void stressTest() {
		int stressTestCount = kNumberOfIngredientsToStressTest.GetValue();
		ingredients.clear();
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			return;
		}
		auto& allIngredients = dataHandler->GetFormArray<IngredientItem>();
		const auto requestedCount = stressTestCount > 0 ? static_cast<std::size_t>(stressTestCount) : 0;
		const std::size_t availableCount = allIngredients.size();
		const auto selectedCount = (std::min)(availableCount, requestedCount);
		for (std::size_t i = 0; i < selectedCount; ++i) {
			ingredients.insert(Ingredient(allIngredients[i]));
		}
	}

}

void MessageHandler(SKSE::MessagingInterface::Message* msg)
{
	if (!msg) {
		return;
	}
	if (msg->type == SKSE::MessagingInterface::kPreLoadGame) {
		alchemist::devhub::Shutdown();
	}
	if (msg->type == SKSE::MessagingInterface::kPostPostLoad) {
		alchemist::menu::Register();
		alchemist::render::Install();
	}
	if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
		alchemist::caco::Adapter::Initialize();
	}
	if (msg->type == SKSE::MessagingInterface::kInputLoaded) {
		alchemist::alchemyplus::Adapter::Initialize();
		alchemist::menu::Register();
		alchemist::render::Install();
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
	SKSE::Init(skse);

	REX::INI::SettingStore::GetSingleton()->Init("Data\\SKSE\\Plugins\\alchemist.ini", "");
	REX::INI::SettingStore::GetSingleton()->Load();
	REX::INI::SettingStore::GetSingleton()->Save();

	const auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging || messaging->Version() < SKSE::MessagingInterface::kVersion) {
		return false;
	}
	if (!messaging->RegisterListener("SKSE", MessageHandler)) {
		return false;
	}

	srand(static_cast<unsigned int>(time(nullptr)));
	return true;
}
