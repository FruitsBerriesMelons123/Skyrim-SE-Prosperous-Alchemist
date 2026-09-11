#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace alchemist::tracker {
	struct QuestStage
	{
		std::uint16_t index = 0;
		bool executed = false;
		bool startUp = false;
		bool shutDown = false;
	};

	struct QuestObjective
	{
		std::uint32_t index = 0;
		std::string text;
		std::uint32_t state = 0;
		bool completed = false;
		bool dormant = false;
	};

	struct QuestInfo
	{
		std::string key;
		std::string title;
		std::uint32_t formIDValue = 0;
		std::string formID;
		std::string editorID;
		std::string modName;
		std::uint8_t typeID = 0;
		std::uint16_t currentStage = 0;
		bool running = false;
		bool active = false;
		bool completed = false;
		std::vector<QuestStage> stages;
		std::vector<QuestObjective> objectives;
	};

	enum class ObjectiveAction : std::uint8_t
	{
		kShow,
		kHide,
		kComplete,
		kFail
	};

	struct Requirement
	{
		std::string key;
		std::string source;
		std::string detail;
		std::string ingredient;
		int count = 1;
		bool completed = false;
		bool automatic = false;
		bool overridden = false;
		bool completionOverridden = false;
		int automaticCount = 0;
		int previousCount = 0;
	};

	struct EffectInfo
	{
		std::string key;
		std::string name;
		std::string editorID;
		std::uint32_t formIDValue = 0;
		bool selected = false;
		int protectedCount = 999;
	};

	bool RefreshDetection();
	std::vector<EffectInfo> GetEffects();
	std::vector<QuestInfo> GetQuests();
	std::vector<Requirement> GetRequirements();
	std::uint64_t GetRevision();
	std::string GetStatus();
	std::map<std::string, int> GetProtectedIngredients();
	void SetEffectSelected(const std::string& a_key, bool a_selected);
	void SetEffectProtectionCount(const std::string& a_key, int a_count);
	void AddManual(std::string a_source, std::string a_detail, std::string a_ingredient, int a_count);
	void UpdateRequirement(const Requirement& a_requirement);
	void RemoveManual(const std::string& a_key);
	void ClearOverrides();
	void ResetToDetected();
	void RequestQuestStage(std::uint32_t a_formID, std::uint16_t a_stage, bool a_force);
	void RequestQuestObjective(std::uint32_t a_formID, std::uint16_t a_objective, ObjectiveAction a_action);
}
