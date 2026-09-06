#pragma once

#include <string>
#include <optional>
#include <vector>
#include <functional>
#include <cstddef>
#include <cstdint>

namespace alchemist {
	class Player;

	enum class EvaluationAlgorithm {
		Automatic,
		Vanilla,
		AlchemyPlus,
		CACO
	};

	namespace engine {

	struct RecipeResult {
		std::string name;
		std::string effects;
		std::string ingredients;
		std::string ingredientDetails;
		std::vector<std::uint32_t> ingredientFormIDs;
		std::string calculationDetails;
		float calculatedValue = 0.0f;
		int displayedValue = 0;
		bool isBest = false;
	};

	struct AlgorithmTestResult {
		EvaluationAlgorithm algorithm = EvaluationAlgorithm::Vanilla;
		std::size_t ingredientCount = 0;
		std::size_t pairCandidates = 0;
		std::size_t tripleCandidates = 0;
		std::size_t craftablePairs = 0;
		std::size_t craftableTriples = 0;
		bool blueMountainFlowerWheat = false;
		bool blueMountainFlowerCanisJarrin = false;
		float blueMountainFlowerCanisJarrinValue = 0.0f;
		bool liveCompatibilityRecords = false;
	};

	struct AlgorithmMatrixResult {
		std::size_t ingredientCount = 0;
		std::vector<AlgorithmTestResult> algorithms;
		bool alchemyPlusActive = false;
		bool cacoActive = false;
		bool combinedActive = false;
		bool totalsAgree = false;
		bool completed = false;
	};

	struct CalculationProgress {
		bool isUpdating = false;
		float progressFraction = 0.0f;
		std::string phase;
		std::size_t current = 0;
		std::size_t total = 0;
	};

	void Recalculate(bool a_force = false);
	void RecalculateAsync(std::function<void()> a_onComplete = nullptr, bool a_force = false);
	std::vector<RecipeResult> GetCachedRecipes();
	std::size_t GetTotalAvailableRecipes();
	bool IsRecipeListIncomplete();
	void LoadAllCachedRecipes();
	std::uint64_t GetRecipeCacheGeneration();
	CalculationProgress GetCalculationProgress();
	std::optional<RecipeResult> FindRecipeForIngredients(const std::string& a_ingredients);
	std::optional<RecipeResult> FindRecipeForIngredientDetails(const std::string& a_ingredientDetails);
	std::optional<RecipeResult> FindRecipeForIngredientDetailsAtState(const std::string& a_ingredientDetails, const Player& a_player);
	void NotifyAlchemyMenuOpened();

	void NotifyAlchemyMenuClosed();
	void InvalidateMasterCache();
	bool IsRecipeListStale();
	std::string GetStaleReason();
	void RequestManualRecalculate();
	std::uint32_t GetLastCalculationDurationMs();
	AlgorithmMatrixResult RunAlgorithmMatrix();
}
}


