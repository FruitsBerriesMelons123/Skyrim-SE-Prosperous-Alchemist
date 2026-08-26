#include "main.h"
#include "AlchemistEngine.h"

#include <Windows.h>

#include <mutex>
#include <thread>
#include <cmath>
#include <iomanip>
#include <cctype>
#include <cstdint>
#include <limits>

namespace alchemist::engine {
	namespace {
		std::mutex snapshotMutex;
		std::mutex recalculateMutex;
		vector<RecipeResult> cachedRecipes;
		std::uint64_t lastCacoCalculationRevision = 0;

		string GetNoRecipeMessage()
		{
			const auto translations = str::split(kStringTranslations.GetValue(), ',');
			return translations.size() == 2 ? translations.at(1) : "No potion recipes are currently available.";
		}

		string FormatIngredients(const Potion& potion)
		{
			if (potion.size == 2) {
				return str::printSort2(potion.ingredient1.name, potion.ingredient2.name);
			}
			if (potion.size == 3) {
				return str::printSort3(potion.ingredient1.name, potion.ingredient2.name, potion.ingredient3.name);
			}
			return {};
		}

		string FormatIngredientDetails(const Potion& potion)
		{
			std::ostringstream result;
			// Recipe details describe one craft, not the player's available inventory quantity.
			const auto append = [&result](const Ingredient& ingredient) {
				if (!ingredient.nativeIngredient) {
					return;
				}
				if (result.tellp() > 0) {
					result << "; ";
				}
				result << ingredient.name << " [form=0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
					<< ingredient.nativeIngredient->GetFormID() << std::dec << std::setfill(' ') << ", count=1]";
			};
			append(potion.ingredient1);
			append(potion.ingredient2);
			if (potion.size == 3) {
				append(potion.ingredient3);
			}
			return result.str();
		}

		string FormatIngredientDetails(const vector<Ingredient>& ingredients)
		{
			std::ostringstream result;
			for (const auto& ingredient : ingredients) {
				if (!ingredient.nativeIngredient) {
					continue;
				}
				if (result.tellp() > 0) {
					result << "; ";
				}
				result << ingredient.name << " [form=0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
					<< ingredient.nativeIngredient->GetFormID() << std::dec << std::setfill(' ') << ", count=1]";
			}
			return result.str();
		}

		string FormatEffects(const EffectList& effects, const Effect& controlEffect)
		{
			string result;
			for (const auto& effect : effects) {
				if (!result.empty()) {
					result += "\n";
				}
				result += effect::getPerkCalcDescription(effect, controlEffect.beneficial);
			}
			return result;
		}

		string FormatEffects(const Potion& potion)
		{
			return FormatEffects(potion.effects, potion.controlEffect);
		}

		string FormatEffects(const NativePotionResult& result)
		{
			return FormatEffects(result.effects, result.controlEffect);
		}

		string FormatCalculationDetails(const EffectList& effects)
		{
			std::ostringstream result;
			bool first = true;
			for (const auto& effect : effects) {
				if (!first) {
					result << "; ";
				}
				first = false;
				result << effect.name
					<< " base_cost=" << std::setprecision(9) << effect.baseCost
					<< " source_magnitude=" << effect.magnitude
					<< " predicted_magnitude=" << effect.calcMagnitude
					<< " source_duration=" << effect.duration
					<< " predicted_duration=" << effect.calcDuration
					<< " predicted_effect_cost=" << effect.calcCost;
			}
			return result.str().empty() ? "unavailable" : result.str();
		}

		string FormatCalculationDetails(const NativePotionResult& result)
		{
			return FormatCalculationDetails(result.effects);
		}

		string FormatCalculationDetails(const Potion& potion)
		{
			return FormatCalculationDetails(potion.effects);
		}

		bool ParseIngredientFormIDs(const std::string& details, std::vector<std::uint32_t>& formIDs)
		{
			formIDs.clear();
			std::size_t position = 0;
			while ((position = details.find("form=0x", position)) != std::string::npos) {
				position += 7;
				const auto first = position;
				while (position < details.size() && std::isxdigit(static_cast<unsigned char>(details[position]))) {
					++position;
				}
				if (first == position) {
					return false;
				}
				try {
					const auto value = std::stoull(details.substr(first, position - first), nullptr, 16);
					if (value > (std::numeric_limits<std::uint32_t>::max)()) {
						return false;
					}
					formIDs.push_back(static_cast<std::uint32_t>(value));
				} catch (...) {
					return false;
				}
			}
			return formIDs.size() == 2 || formIDs.size() == 3;
		}

		RecipeResult ToRecipeResult(const Potion& potion, bool isBest)
		{
			return RecipeResult{
			.name = potion.name,
			.effects = FormatEffects(potion),
			.ingredients = FormatIngredients(potion),
			.ingredientDetails = FormatIngredientDetails(potion),
			.calculationDetails = FormatCalculationDetails(potion),
			.calculatedValue = potion.cost,
			.displayedValue = static_cast<int>(std::floor(potion.cost)),
			.isBest = isBest
			};
		}
	}

	void Recalculate(bool a_force)
	{
		std::scoped_lock recalculateLock(recalculateMutex);
		const auto stressTestCount = kNumberOfIngredientsToStressTest.GetValue();

		initAlchemist();
		if (stressTestCount > 0) {
			stressTest();
		}

		const auto cacoCalculationRevision = caco::Adapter::GetCalculationRevision();
		const bool rebuild = a_force || ingredients != lastIngredientList || player.state != player.lastState ||
			cacoCalculationRevision != lastCacoCalculationRevision;
		if (rebuild) {
			costliestPotion = Potion(0, GetNoRecipeMessage());
			lastIngredientList = ingredients;
			player.lastState = player.state;
			lastCacoCalculationRevision = cacoCalculationRevision;
			if (kSinglethreaded.GetValue() != 0) {
				makePotionsST();
			} else {
				makePotions();
			}
		}

		vector<RecipeResult> results;
		results.reserve(potions.size());
		for (const auto& potion : potions) {
			if (potion.size > 0 && !FormatIngredients(potion).empty()) {
				results.push_back(ToRecipeResult(potion, potion.id == costliestPotion.id));
			}
		}
		std::sort(results.begin(), results.end(), [](const RecipeResult& left, const RecipeResult& right) {
			if (left.calculatedValue != right.calculatedValue) {
				return left.calculatedValue > right.calculatedValue;
			}
			return left.ingredients < right.ingredients;
		});

		{
			std::lock_guard lock(snapshotMutex);
			cachedRecipes = std::move(results);
		}
	}

	void RecalculateAsync(std::function<void()> a_onComplete, bool a_force)
	{
		const auto stressTestCount = kNumberOfIngredientsToStressTest.GetValue();

		initAlchemist();
		if (stressTestCount > 0) {
			stressTest();
		}

		const auto cacoCalculationRevision = caco::Adapter::GetCalculationRevision();
		const bool rebuild = a_force || ingredients != lastIngredientList || player.state != player.lastState ||
			cacoCalculationRevision != lastCacoCalculationRevision;

		if (!rebuild) {
			if (a_onComplete) {
				a_onComplete();
			}
			return;
		}

		std::thread([a_onComplete, cacoCalculationRevision]() {
			std::scoped_lock recalculateLock(recalculateMutex);
			costliestPotion = Potion(0, GetNoRecipeMessage());
			lastIngredientList = ingredients;
			player.lastState = player.state;
			lastCacoCalculationRevision = cacoCalculationRevision;
			if (kSinglethreaded.GetValue() != 0) {
				makePotionsST();
			} else {
				makePotions();
			}

			vector<RecipeResult> results;
			results.reserve(potions.size());
			for (const auto& potion : potions) {
				if (potion.size > 0 && !FormatIngredients(potion).empty()) {
					results.push_back(ToRecipeResult(potion, potion.id == costliestPotion.id));
				}
			}
			std::sort(results.begin(), results.end(), [](const RecipeResult& left, const RecipeResult& right) {
				if (left.calculatedValue != right.calculatedValue) {
					return left.calculatedValue > right.calculatedValue;
				}
				return left.ingredients < right.ingredients;
			});

			{
				std::lock_guard lock(snapshotMutex);
				cachedRecipes = std::move(results);
			}

			if (a_onComplete) {
				a_onComplete();
			}
		}).detach();
	}

	vector<RecipeResult> GetCachedRecipes()
	{
		std::lock_guard lock(snapshotMutex);
		return cachedRecipes;
	}

	AlgorithmMatrixResult RunAlgorithmMatrix()
	{
		AlgorithmMatrixResult result;
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			return result;
		}
		result.alchemyPlusActive = alchemyplus::Adapter::IsActive();
		result.cacoActive = caco::Adapter::IsActive();
		result.combinedActive = result.alchemyPlusActive && result.cacoActive;

		std::vector<Ingredient> availableIngredients;
		for (auto* nativeIngredient : dataHandler->GetFormArray<IngredientItem>()) {
			if (nativeIngredient && !nativeIngredient->IsDeleted()) {
				availableIngredients.emplace_back(nativeIngredient);
			}
		}
		result.ingredientCount = availableIngredients.size();

		const std::array<EvaluationAlgorithm, 4> algorithms{
			EvaluationAlgorithm::Vanilla,
			EvaluationAlgorithm::AlchemyPlus,
			EvaluationAlgorithm::CACO,
			EvaluationAlgorithm::Automatic
		};
		result.algorithms.reserve(algorithms.size());
		for (const auto algorithm : algorithms) {
			AlgorithmTestResult algorithmResult;
			algorithmResult.algorithm = algorithm;
			algorithmResult.ingredientCount = availableIngredients.size();
			algorithmResult.pairCandidates = availableIngredients.size() > 1 ?
				availableIngredients.size() * (availableIngredients.size() - 1) / 2 : 0;
			algorithmResult.tripleCandidates = availableIngredients.size() > 2 ?
				availableIngredients.size() * (availableIngredients.size() - 1) * (availableIngredients.size() - 2) / 6 : 0;
			algorithmResult.liveCompatibilityRecords = algorithm == EvaluationAlgorithm::AlchemyPlus ?
				result.alchemyPlusActive : algorithm == EvaluationAlgorithm::CACO ? result.cacoActive :
				algorithm == EvaluationAlgorithm::Automatic && (result.alchemyPlusActive || result.cacoActive);

			const bool useCacoNative = algorithm == EvaluationAlgorithm::CACO && caco::Adapter::IsActive();
			std::vector<std::vector<const RE::EffectSetting*>> effectIdentities;
			effectIdentities.reserve(availableIngredients.size());
			for (const auto& ingredient : availableIngredients) {
				std::vector<const RE::EffectSetting*> identities;
				identities.reserve(ingredient.effects.size());
				for (std::size_t effectIndex = 0; effectIndex < ingredient.effects.size(); ++effectIndex) {
					const auto effect = effect::getAlgorithmEffect(ingredient, effectIndex, useCacoNative);
					const auto* identity = effect::getSourceIdentity(effect);
					if (identity && std::find(identities.begin(), identities.end(), identity) == identities.end()) {
						identities.push_back(identity);
					}
				}
				effectIdentities.push_back(std::move(identities));
			}

			const auto shareEffect = [&effectIdentities](std::size_t first, std::size_t second) {
				for (const auto* identity : effectIdentities[first]) {
					if (std::find(effectIdentities[second].begin(), effectIdentities[second].end(), identity) != effectIdentities[second].end()) {
						return true;
					}
				}
				return false;
			};
			std::vector<const Ingredient*> selectedIngredients;
			selectedIngredients.reserve(3);
			const auto evaluateResult = [&](std::size_t first, std::size_t second, std::size_t third) {
				selectedIngredients.clear();
				selectedIngredients.push_back(&availableIngredients[first]);
				selectedIngredients.push_back(&availableIngredients[second]);
				if (third != (std::numeric_limits<std::size_t>::max)()) {
					selectedIngredients.push_back(&availableIngredients[third]);
				}
				return effect::evaluatePotion(selectedIngredients, player, false, algorithm);
			};
			const auto evaluate = [&](std::size_t first, std::size_t second, std::size_t third) {
				return evaluateResult(first, second, third).valid;
			};
			const auto isBlueMountainFlowerWheat = [](const Ingredient& first, const Ingredient& second) {
				constexpr RE::FormID blueMountainFlower = 0x00077E1C;
				constexpr RE::FormID wheat = 0x0004B0BA;
				const auto firstFormID = first.nativeIngredient ? first.nativeIngredient->GetFormID() : 0;
				const auto secondFormID = second.nativeIngredient ? second.nativeIngredient->GetFormID() : 0;
				return (firstFormID == blueMountainFlower && secondFormID == wheat) ||
					(firstFormID == wheat && secondFormID == blueMountainFlower);
			};
			std::array<std::size_t, 3> blueMountainFlowerCanisJarrinIndices{
				(std::numeric_limits<std::size_t>::max)(),
				(std::numeric_limits<std::size_t>::max)(),
				(std::numeric_limits<std::size_t>::max)()
			};
			constexpr std::array<RE::FormID, 3> blueMountainFlowerCanisJarrinForms{
				0x00077E1C,
				0x0006ABCB,
				0x0001BCBC
			};
			for (std::size_t index = 0; index < availableIngredients.size(); ++index) {
				const auto formID = availableIngredients[index].nativeIngredient ?
					availableIngredients[index].nativeIngredient->GetFormID() : 0;
				for (std::size_t target = 0; target < blueMountainFlowerCanisJarrinForms.size(); ++target) {
					if (formID == blueMountainFlowerCanisJarrinForms[target]) {
						blueMountainFlowerCanisJarrinIndices[target] = index;
						break;
					}
				}
			}
			if (std::all_of(blueMountainFlowerCanisJarrinIndices.begin(), blueMountainFlowerCanisJarrinIndices.end(),
				[](const auto index) { return index != (std::numeric_limits<std::size_t>::max)(); })) {
				const auto targetResult = evaluateResult(
					blueMountainFlowerCanisJarrinIndices[0],
					blueMountainFlowerCanisJarrinIndices[1],
					blueMountainFlowerCanisJarrinIndices[2]);
				algorithmResult.blueMountainFlowerCanisJarrin = targetResult.valid;
				if (targetResult.valid) {
					algorithmResult.blueMountainFlowerCanisJarrinValue = targetResult.cost;
				}
			}

			const auto ingredientCount = availableIngredients.size();
			for (std::size_t first = 0; first + 1 < ingredientCount; ++first) {
				for (std::size_t second = first + 1; second < ingredientCount; ++second) {
					const bool candidate = shareEffect(first, second);
					const bool target = isBlueMountainFlowerWheat(availableIngredients[first], availableIngredients[second]);
					const bool valid = (candidate || target) &&
						evaluate(first, second, (std::numeric_limits<std::size_t>::max)());
					if (valid) {
						++algorithmResult.craftablePairs;
					}
					if (target) {
						algorithmResult.blueMountainFlowerWheat = valid;
					}
				}
			}
			for (std::size_t first = 0; first + 2 < ingredientCount; ++first) {
				for (std::size_t second = first + 1; second + 1 < ingredientCount; ++second) {
					for (std::size_t third = second + 1; third < ingredientCount; ++third) {
						if ((shareEffect(first, second) || shareEffect(first, third) || shareEffect(second, third)) &&
							evaluate(first, second, third)) {
							++algorithmResult.craftableTriples;
						}
					}
				}
			}
			result.algorithms.push_back(std::move(algorithmResult));
		}

		if (!result.algorithms.empty()) {
			result.totalsAgree = std::all_of(result.algorithms.begin() + 1, result.algorithms.end(),
				[&result](const auto& algorithm) {
					return algorithm.craftablePairs == result.algorithms.front().craftablePairs &&
						algorithm.craftableTriples == result.algorithms.front().craftableTriples;
				});
		}
		result.completed = true;
		return result;
	}

	std::optional<RecipeResult> FindRecipeForIngredients(const std::string& a_ingredients)
	{
		std::lock_guard lock(snapshotMutex);
		for (const auto& recipe : cachedRecipes) {
			if (recipe.ingredients == a_ingredients) {
				return recipe;
			}
		}
		return std::nullopt;
	}

	std::optional<RecipeResult> FindRecipeForIngredientDetails(const std::string& a_ingredientDetails)
	{
		std::lock_guard lock(snapshotMutex);
		for (const auto& recipe : cachedRecipes) {
			if (recipe.ingredientDetails == a_ingredientDetails) {
				return recipe;
			}
		}
		return std::nullopt;
	}

	std::optional<RecipeResult> FindRecipeForIngredientDetailsAtState(const std::string& a_ingredientDetails, const Player& a_player)
	{
		RecipeResult result;
		{
			std::lock_guard lock(snapshotMutex);
			const auto found = std::find_if(cachedRecipes.begin(), cachedRecipes.end(), [&a_ingredientDetails](const auto& recipe) {
				return recipe.ingredientDetails == a_ingredientDetails;
			});
			if (found == cachedRecipes.end()) {
				return std::nullopt;
			}
			result = *found;
		}

		std::vector<std::uint32_t> formIDs;
		if (!ParseIngredientFormIDs(a_ingredientDetails, formIDs)) {
			return std::nullopt;
		}
		std::vector<Ingredient> selectedIngredients;
		selectedIngredients.reserve(formIDs.size());
		for (const auto formID : formIDs) {
			auto* form = RE::TESForm::LookupByID(formID);
			auto* ingredient = form ? form->As<IngredientItem>() : nullptr;
			if (!ingredient) {
				return std::nullopt;
			}
			selectedIngredients.emplace_back(ingredient);
		}

		std::vector<const Ingredient*> ingredientPointers;
		ingredientPointers.reserve(selectedIngredients.size());
		for (const auto& ingredient : selectedIngredients) {
			ingredientPointers.push_back(&ingredient);
		}
		const auto nativeResult = effect::evaluatePotion(ingredientPointers, a_player);
		if (!nativeResult.valid) {
			return std::nullopt;
		}

		std::vector<std::string> ingredientNames;
		ingredientNames.reserve(selectedIngredients.size());
		for (const auto& ingredient : selectedIngredients) {
			ingredientNames.push_back(ingredient.name);
		}
		std::sort(ingredientNames.begin(), ingredientNames.end());
		if (ingredientNames.size() == 2) {
			result.ingredients = str::printSort2(ingredientNames[0], ingredientNames[1]);
		} else {
			result.ingredients = str::printSort3(ingredientNames[0], ingredientNames[1], ingredientNames[2]);
		}
		result.ingredientDetails = FormatIngredientDetails(selectedIngredients);
		result.effects = FormatEffects(nativeResult);
		result.calculationDetails = FormatCalculationDetails(nativeResult);
		result.calculatedValue = nativeResult.cost;
		result.displayedValue = static_cast<int>(std::floor(nativeResult.cost));
		return result;
	}
}
