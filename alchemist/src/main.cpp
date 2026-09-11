#include "main.h"
#include "AlchemistEngine.h"
#include "AlchemyPlus/AlchemyPlus.h"
#include "DeveloperTestHub.h"
#include "IngredientTracker.h"
#include "MenuHandler.h"
#include "RenderHook.h"
#include "Localization.h"

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
			const vector<const Ingredient*>& availableIngredients,
			const Player& evaluatedPlayer = player)
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

			const auto nativeResult = effect::evaluatePotion(selectedIngredients, evaluatedPlayer);
			if (!nativeResult.valid || !std::isfinite(nativeResult.cost)) {
				return std::nullopt;
			}
			const auto& ingredient1 = *selectedIngredients[0];
			const auto& ingredient2 = *selectedIngredients[1];
			if (combination.size == 2) {
				return Potion(2, ingredient1, ingredient2, nativeResult.effects,
					getPossibleEffects(ingredient1, ingredient2), nativeResult.controlEffect, nativeResult.isPoison, nativeResult.cost, evaluatedPlayer);
			}
			return Potion(3, ingredient1, ingredient2, *selectedIngredients[2],
				nativeResult.effects, nativeResult.controlEffect, nativeResult.isPoison, nativeResult.cost, evaluatedPlayer);
		}

		vector<IngredientCombination> buildPairCombinations(
			std::size_t ingredientCount,
			const std::vector<bool>* isNewIngredient = nullptr)
		{
			vector<IngredientCombination> combinations;
			if (ingredientCount < 2) {
				return combinations;
			}
			combinations.reserve(ingredientCount * (ingredientCount - 1) / 2);
			for (std::size_t first = 0; first + 1 < ingredientCount; ++first) {
				for (std::size_t second = first + 1; second < ingredientCount; ++second) {
					if (isNewIngredient && !(*isNewIngredient)[first] && !(*isNewIngredient)[second]) {
						continue;
					}
					combinations.push_back({ { first, second, 0 }, 2 });
				}
			}
			return combinations;
		}

		vector<IngredientCombination> buildTripleCombinations(
			const vector<const Ingredient*>& availableIngredients,
			const std::atomic<bool>* cancelToken = nullptr,
			const std::vector<bool>* isNewIngredient = nullptr)
		{
			const std::size_t ingredientCount = availableIngredients.size();
			vector<IngredientCombination> combinations;
			if (ingredientCount < 3) {
				return combinations;
			}

			std::vector<std::vector<const RE::EffectSetting*>> identities(ingredientCount);
			for (std::size_t i = 0; i < ingredientCount; ++i) {
				const auto& ing = *availableIngredients[i];
				for (std::size_t e = 0; e < ing.effects.size(); ++e) {
					const auto effect = effect::getAlgorithmEffect(ing, e);
					const auto* id = effect::getSourceIdentity(effect);
					if (id) {
						identities[i].push_back(id);
					}
				}
			}

			if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
				return combinations;
			}

			std::vector<std::vector<bool>> shares(ingredientCount, std::vector<bool>(ingredientCount, false));
			for (std::size_t i = 0; i < ingredientCount; ++i) {
				for (std::size_t j = i + 1; j < ingredientCount; ++j) {
					bool match = false;
					for (const auto* id1 : identities[i]) {
						for (const auto* id2 : identities[j]) {
							if (id1 == id2) {
								match = true;
								break;
							}
						}
						if (match) {
							break;
						}
					}
					shares[i][j] = match;
					shares[j][i] = match;
				}
			}

			combinations.reserve((std::min)(ingredientCount * (ingredientCount - 1) * (ingredientCount - 2) / 6, static_cast<std::size_t>(2000000)));
			for (std::size_t first = 0; first + 2 < ingredientCount; ++first) {
				if ((first & 0x7) == 0 && cancelToken && cancelToken->load(std::memory_order_relaxed)) {
					return {};
				}
				for (std::size_t second = first + 1; second + 1 < ingredientCount; ++second) {
					const bool ab = shares[first][second];
					for (std::size_t third = second + 1; third < ingredientCount; ++third) {
						if (isNewIngredient && !(*isNewIngredient)[first] && !(*isNewIngredient)[second] && !(*isNewIngredient)[third]) {
							continue;
						}
						if (ab) {
							if (shares[first][third] || shares[second][third]) {
								combinations.push_back({ { first, second, third }, 3 });
							}
						} else {
							if (shares[first][third] && shares[second][third]) {
								combinations.push_back({ { first, second, third }, 3 });
							}
						}
					}
				}
			}
			return combinations;
		}


		vector<CandidateResult> evaluateCombinations(
			const vector<IngredientCombination>& candidates,
			const vector<const Ingredient*>& availableIngredients,
			bool multithreaded,
			const Player& evaluatedPlayer = player,
			const std::atomic<bool>* cancelToken = nullptr,
			const std::function<void(std::size_t current, std::size_t total)>& progressCallback = nullptr)
		{
			if (candidates.empty()) {
				return {};
			}
			if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
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
					if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
						break;
					}
					const auto candidateIndex = nextCandidate.fetch_add(1, std::memory_order_relaxed);
					if (candidateIndex >= candidates.size()) {
						break;
					}
					if (progressCallback && ((candidateIndex & 0x1F) == 0 || candidateIndex + 1 == candidates.size())) {
						progressCallback(candidateIndex + 1, candidates.size());
					}
					if (auto potion = evaluateCombination(candidates[candidateIndex], availableIngredients, evaluatedPlayer)) {
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

			if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
				return {};
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

		bool isBetterPotion(const Potion& candidate, const Potion& currentBest)
		{
			if (currentBest.size <= 0) {
				return true;
			}
			if (candidate.cost != currentBest.cost) {
				return candidate.cost > currentBest.cost;
			}
			return candidate.id < currentBest.id;
		}

		void setCostliestDescription(Potion& targetCostliestPotion)
		{
			if (targetCostliestPotion.size != 2 && targetCostliestPotion.size != 3) {
				return;
			}
			string effectDescriptions;
			for (const auto& effect : targetCostliestPotion.effects) {
				effectDescriptions += " " + effect::getPerkCalcDescription(effect, targetCostliestPotion.controlEffect.beneficial);
			}
			const auto ingredientText = targetCostliestPotion.size == 2 ?
				str::printSort2(targetCostliestPotion.ingredient1.name, targetCostliestPotion.ingredient2.name) :
				str::printSort3(targetCostliestPotion.ingredient1.name, targetCostliestPotion.ingredient2.name, targetCostliestPotion.ingredient3.name);
			targetCostliestPotion.description = targetCostliestPotion.name + ":" + effectDescriptions +
				"\n Value: " + str::fromFloat(floor(targetCostliestPotion.cost)) + "\n" + ingredientText;
		}
	}

	RecipeCalculationOutput CalculateRecipesFromSnapshot(
		const vector<Ingredient>& inputIngredients,
		const Player& evaluatedPlayer,
		bool multithreaded,
		const std::atomic<bool>* cancelToken,
		const CalculationProgressCallback& progressCallback,
		const std::vector<bool>* isNewIngredient)
	{
		RecipeCalculationOutput output;
		if (inputIngredients.size() < 2) {
			if (progressCallback) {
				progressCallback(1.0f, "Completed", 0, 0);
			}
			return output;
		}

		vector<const Ingredient*> availableIngredients;
		availableIngredients.reserve(inputIngredients.size());
		for (const auto& ingredient : inputIngredients) {
			availableIngredients.push_back(&ingredient);
		}

		if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
			output.cancelled = true;
			return output;
		}

		if (progressCallback) {
			progressCallback(0.0f, "Evaluating 2-ingredient recipes", 0, 0);
		}

		const auto pairCandidates = buildPairCombinations(availableIngredients.size(), isNewIngredient);
		const auto pairProgress = [&](std::size_t curr, std::size_t tot) {
			if (progressCallback && tot > 0) {
				const float fraction = 0.05f * (static_cast<float>(curr) / static_cast<float>(tot));
				progressCallback(fraction, "Evaluating 2-ingredient recipes", curr, tot);
			}
		};
		const auto validPairs = evaluateCombinations(pairCandidates, availableIngredients, multithreaded, evaluatedPlayer, cancelToken, pairProgress);
		if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
			output.cancelled = true;
			return output;
		}

		for (const auto& result : validPairs) {
			if (static_cast<int>(std::floor(result.potion.cost)) >= 1) {
				if (isBetterPotion(result.potion, output.costliestPotion)) {
					output.costliestPotion = result.potion;
				}
				output.potions.push_back(result.potion);
			}
		}

		if (progressCallback) {
			progressCallback(0.05f, "Finding 3-ingredient combinations", 0, 0);
		}

		const auto tripleCandidates = buildTripleCombinations(availableIngredients, cancelToken, isNewIngredient);

		if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
			output.cancelled = true;
			return output;
		}

		if (progressCallback) {
			progressCallback(0.10f, "Evaluating 3-ingredient recipes", 0, tripleCandidates.size());
		}

		const auto tripleProgress = [&](std::size_t curr, std::size_t tot) {
			if (progressCallback && tot > 0) {
				const float fraction = 0.10f + 0.75f * (static_cast<float>(curr) / static_cast<float>(tot));
				progressCallback(fraction, "Evaluating 3-ingredient recipes", curr, tot);
			}
		};
		const auto validTriples = evaluateCombinations(tripleCandidates, availableIngredients, multithreaded, evaluatedPlayer, cancelToken, tripleProgress);
		if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
			output.cancelled = true;
			return output;
		}

		if (progressCallback) {
			progressCallback(0.85f, "Collecting recipes...", tripleCandidates.size(), tripleCandidates.size());
		}

		for (const auto& result : validTriples) {
			if (static_cast<int>(std::floor(result.potion.cost)) >= 1) {
				if (isBetterPotion(result.potion, output.costliestPotion)) {
					output.costliestPotion = result.potion;
				}
				output.potions.push_back(result.potion);
			}
		}

		if (progressCallback) {
			progressCallback(0.87f, "Preparing recipes for sorting...", output.potions.size(), output.potions.size());
		}

		output.combinations = static_cast<int>(output.potions.size());
		setCostliestDescription(output.costliestPotion);
		return output;
	}

	namespace {
		void generatePotions(bool multithreaded)
		{
			vector<Ingredient> inputList(ingredients.begin(), ingredients.end());
			auto output = CalculateRecipesFromSnapshot(inputList, player, multithreaded);
			potions.clear();
			for (auto& p : output.potions) {
				potions.insert(std::move(p));
			}
			costliestPotion = std::move(output.costliestPotion);
			combinations = output.combinations;
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
			for (const auto& [ingredient, count] : tracker::GetProtectedIngredients()) {
				auto found = moreIngredients.find(ingredient);
				if (found == moreIngredients.end() || found->second == 999 || count == 999) {
					moreIngredients[ingredient] = (found != moreIngredients.end() && found->second == 999) || count == 999 ? 999 : count;
				} else {
					found->second = (std::min)(999, found->second + count);
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

}

void MessageHandler(SKSE::MessagingInterface::Message* msg)
{
	if (!msg) {
		return;
	}
	if (msg->type == SKSE::MessagingInterface::kPreLoadGame) {
		alchemist::devhub::Shutdown();
		alchemist::engine::InvalidateMasterCache();
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
	alchemist::localization::Initialize(kLanguage.GetValue());

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
