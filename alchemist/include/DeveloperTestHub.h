#pragma once

#include "AlchemistEngine.h"
#include "CACO/CACO.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace alchemist::devhub {
	struct FixtureDefinition {
		std::string displayName;
		std::string formName;
		int defaultQuantity = 3;
		int inventoryCount = -1;
		std::string coverage;
		std::string notes;
	};

	struct InventoryItem {
		std::uint32_t formId = 0;
		std::string editorId;
		std::string name;
		std::string type;
		int count = 0;
		int value = 0;
		int costOverride = 0;
		std::string effects;
	};

	struct ActiveState {
		bool available = false;
		int alchemySkill = -1;
		float effectiveAlchemy = -1.0f;
		std::vector<std::string> perks;
		std::vector<std::string> spells;
		std::vector<std::string> gear;
		bool seekerSpellAvailable = false;
		std::uint32_t seekerSpellFormId = 0;
		bool seekerSpellListed = false;
		bool seekerSpellActive = false;
		bool seekerPerkAvailable = false;
		std::uint32_t seekerPerkFormId = 0;
		int seekerPerkRank = 0;
		bool seekerRewardGlobalAvailable = false;
		std::uint32_t seekerRewardGlobalFormId = 0;
		float seekerRewardGlobalValue = 0.0f;
		bool seekerNativeContract = false;
		caco::AlchemyEvaluationContext alchemyEvaluationContext;
	};

	struct ComparisonRecord {
		std::uint64_t recordId = 0;
		std::uint32_t potionFormId = 0;
		std::string potionEditorId;
		std::string potionName;
		std::string potionType;
		std::string effects = "unavailable";
		int inventoryCount = -1;
		int currentInventoryValue = 0;
		int inventoryCostOverride = 0;
		bool hasEnteredPreCraftValue = false;
		int enteredPreCraftValue = 0;
		bool hasPredictedRawValue = false;
		float predictedRawValue = 0.0f;
		bool hasPredictedDisplayedValue = false;
		int predictedDisplayedValue = 0;
		bool hasCurrentInventoryValue = false;
		int differenceCurrentMinusEntered = 0;
		int differenceCurrentMinusPredicted = 0;
		int differencePredictedMinusEntered = 0;
		std::string status = "Incomplete";
		std::string recipeIngredients = "unavailable";
		std::string recipeIngredientDetails = "unavailable";
		std::string predictionDetails = "unavailable";
		int alchemySkill = -1;
		float effectiveAlchemy = -1.0f;
		int creationAlchemySkill = -1;
		float creationEffectiveAlchemy = -1.0f;
		caco::AlchemyEvaluationContext creationAlchemyEvaluationContext;
		std::vector<std::string> perks;
		std::vector<std::string> spells;
		std::vector<std::string> gear;
		bool seekerSpellAvailable = false;
		std::uint32_t seekerSpellFormId = 0;
		bool seekerSpellListed = false;
		bool seekerSpellActive = false;
		bool seekerPerkAvailable = false;
		std::uint32_t seekerPerkFormId = 0;
		int seekerPerkRank = 0;
		bool seekerRewardGlobalAvailable = false;
		std::uint32_t seekerRewardGlobalFormId = 0;
		float seekerRewardGlobalValue = 0.0f;
		bool seekerNativeContract = false;
	};

	struct View {
		bool initialized = false;
		bool busy = false;
		std::string message;
		std::vector<FixtureDefinition> fixtures;
		std::vector<InventoryItem> inventory;
		std::vector<ComparisonRecord> records;
		ComparisonRecord current;
		bool hasCurrent = false;
		bool hasAlgorithmMatrix = false;
		engine::AlgorithmMatrixResult algorithmMatrix;
		std::uint32_t selectedFormId = 0;
		bool selectionUnavailable = false;
		ActiveState activeState;
		std::map<std::string, int> pendingPerkRanks;
	};

	void Open();
	bool Draw();
	void Close();
	void OnMenuClosed();
	void Shutdown();
	bool IsOpen();
	bool ShouldSuppressInventoryRecalculation();
	View GetView();

	void ProvisionAll();
	void ProvisionBeneficial();
	void ProvisionPoison();
	void ProvisionThreeEffect();
	void ProvisionFortifyEnchanting();
	void ProvisionHighValue();
	void ProvisionFiveEffectCombo();
	void ProvisionParalysis();
	void ProvisionFixture(std::size_t index, int quantity);
	void ProvisionFortifyEnchantingPotions(int quantity = 1);
	void ProvisionFortifyAlchemyPotions(int quantity = 1);
	void RemoveTestIngredients();
	void RemoveAllPotions();
	void RemoveProvisionedItems();
	void RemoveAllNonEquippedItems();
	void ApplySkill(int value);
	void SetPerk(const std::string& name, int rank);
	void GrantAllPerks();
	void ClearTestPerks();
	void ToggleSeekerOfShadows(bool grant);
	void GiveTestGear();
	void EquipTestGear();
	void UnequipTestGear();
	void RefreshInventory();
	void SelectPotion(std::uint32_t formId);
	void SetEnteredValue(int value);
	void ClearEnteredValue();
	void CaptureCreationState();
	void SetCreationAlchemySkill(int value);
	void SetCreationEffectiveAlchemy(float value);
	void ClearCreationState();
	void SelectPrediction(std::size_t recipeIndex);
	void AddRecord();
	void RemoveRecord(std::size_t index);
	void RunAlgorithmMatrix();
}
