#include "AlchemistWindow.h"
#include "AlchemistEngine.h"
#include "DeveloperTestHub.h"
#include "IngredientTracker.h"
#include "MenuHandler.h"
#include "RenderHook.h"
#include "Localization.h"
#include "main.h"

#include "REX/REX/INI.h"

#include <Windows.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string_view>
#include <vector>

namespace alchemist::ui {
	namespace {
		constexpr auto defaultWindowWidth = 620.0f;
		constexpr auto defaultWindowHeight = 600.0f;
		constexpr auto minimumWindowWidth = 360.0f;
		constexpr auto minimumWindowHeight = 220.0f;
		constexpr auto screenMargin = 40.0f;
		constexpr int kUnlimitedProtectionCount = 999;

		int GetFiniteProtectionCount(const tracker::Requirement& a_requirement)
		{
			if (a_requirement.previousCount > 0 && a_requirement.previousCount < kUnlimitedProtectionCount) {
				return a_requirement.previousCount;
			}
			if (a_requirement.automaticCount > 0 && a_requirement.automaticCount < kUnlimitedProtectionCount) {
				return a_requirement.automaticCount;
			}
			return 1;
		}

		REX::INI::F32<> windowPositionX("Window", "PositionX", -1.0f);
		REX::INI::F32<> windowPositionY("Window", "PositionY", -1.0f);
		REX::INI::F32<> windowWidth("Window", "Width", -1.0f);
		REX::INI::F32<> windowHeight("Window", "Height", -1.0f);

		std::atomic_bool isWindowOpen = false;
		std::atomic_bool leftMouseButtonDown = false;
		std::atomic<float> mouseWheelDelta = 0.0f;
		std::atomic_bool cursorOverWindow = false;
		std::atomic_bool protectedIngredientPopupInputCapture = false;
		ImVec2 skyrimCursorPosition(-1.0f, -1.0f);
		bool protectedIngredientWindowVisible = false;
		ImVec2 protectedIngredientWindowMin(-1.0f, -1.0f);
		ImVec2 protectedIngredientWindowMax(-1.0f, -1.0f);
		enum class PendingInputType
		{
			kCharacter,
			kKey
		};
		struct PendingInput
		{
			PendingInputType type = PendingInputType::kCharacter;
			std::uint32_t value = 0;
			bool pressed = false;
		};
		std::mutex pendingInputMutex;
		std::vector<PendingInput> pendingInput;
		bool previousLeftMouseButtonDown = false;
		bool draggingWindow = false;
		bool resizingWindow = false;
		bool windowCollapsed = false;
		bool windowSizeIsCollapsed = false;
		bool windowStateInitialized = false;
		ImVec2 expandedWindowSize(defaultWindowWidth, defaultWindowHeight);
		ImVec2 dragOffset;
		ImVec2 resizeStartCursor;
		ImVec2 resizeStartSize;
		ImVec2 searchRectMin(-1.0f, -1.0f);
		ImVec2 searchRectMax(-1.0f, -1.0f);
		bool focusSearch = false;
		std::atomic_bool searchInputFocused = false;
		char searchText[512]{};
		int sortMode = 0;
		int currentPage = 1;
		std::string lastSearchText;
		int lastSortMode = -1;
		bool pageChanged = false;
		std::string selectedRecipeIngredientDetails;

		struct PaginationInfo {
			int totalPages = 0;
			int startIndex = 0;
			int endIndex = 0;
			int pageItemCount = 0;
		};

		PaginationInfo CalculatePagination(std::size_t totalItems, int page)
		{
			PaginationInfo info;
			if (totalItems == 0) {
				return info;
			}

			if (totalItems <= 200) {
				info.totalPages = 1;
				info.startIndex = 0;
				info.endIndex = static_cast<int>(totalItems);
				info.pageItemCount = static_cast<int>(totalItems);
				return info;
			}

			// totalItems > 200: find the number S between 100 and 200 that most evenly splits the total number
			// (so its remainder is as close to the rest as possible)
			int bestS = 200;
			int bestDiff = (std::numeric_limits<int>::max)();
			int bestPages = 1;
			int bestRemainder = 0;

			const int total = static_cast<int>(totalItems);
			for (int S = 200; S >= 100; --S) {
				const int q = total / S;
				const int r = total % S;
				const int diff = (r == 0) ? 0 : (S - r);
				if (diff < bestDiff) {
					bestDiff = diff;
					bestS = S;
					bestRemainder = (r == 0) ? S : r;
					bestPages = (r == 0) ? q : (q + 1);
					if (diff == 0) {
						break;
					}
				}

			}

			info.totalPages = (std::max)(1, bestPages);
			const int clampedPage = std::clamp(page, 1, info.totalPages);
			const int p = clampedPage - 1;

			if (p < info.totalPages - 1) {
				info.pageItemCount = bestS;
				info.startIndex = p * bestS;
			} else {
				info.pageItemCount = bestRemainder;
				info.startIndex = p * bestS;
			}

			info.endIndex = info.startIndex + info.pageItemCount;
			return info;
		}

		int FindRecipePage(const std::vector<engine::RecipeResult>& recipes, const std::string& ingredientDetails)
		{
			const auto selectedRecipe = std::find_if(recipes.begin(), recipes.end(), [&ingredientDetails](const auto& recipe) {
				return recipe.ingredientDetails == ingredientDetails;
			});
			if (selectedRecipe == recipes.end()) {
				return 0;
			}

			const auto selectedIndex = static_cast<std::size_t>(std::distance(recipes.begin(), selectedRecipe));
			const auto firstPage = CalculatePagination(recipes.size(), 1);
			for (int page = 1; page <= firstPage.totalPages; ++page) {
				const auto pageInfo = CalculatePagination(recipes.size(), page);
				if (selectedIndex >= static_cast<std::size_t>(pageInfo.startIndex) &&
					selectedIndex < static_cast<std::size_t>(pageInfo.endIndex)) {
					return page;
				}
			}

			return 0;
		}

		bool showEffectsColumn = false;
		bool settingsOpen = false;
		bool trackingOpen = false;
		bool developerTestHubOpen = false;
		bool settingsBuffersInitialized = false;
		bool focusProtectedIngredientSearch = false;
		char potionPrefix[512]{};
		char poisonPrefix[512]{};
		struct ProtectedIngredientEntry {
			std::string name;
			int count = -1;
			int previousCount = 1;
		};
		enum class ProtectionCategory : std::size_t {
			kCustom,
			kQuest,
			kCraftable,
			kEffect,
			kCount
		};
		struct ProtectionCategoryCounts {
			std::array<int, static_cast<std::size_t>(ProtectionCategory::kCount)> values{};
			bool manualOverride = false;
		};
		std::vector<ProtectedIngredientEntry> protectedIngredients;
		char protectedIngredientSearch[512]{};
		bool openProtectedIngredientWindow = false;
		bool positionProtectedIngredientWindow = false;
		bool trackingBuffersInitialized = false;
		struct RequirementEditBuffers
		{
			char source[512]{};
			char detail[1024]{};
			char ingredient[512]{};
		};
		std::map<std::string, RequirementEditBuffers> trackingRequirementBuffers;
		char trackingSource[512]{};
		char trackingDetail[1024]{};
		char trackingIngredient[512]{};
		char trackingDetectionSearch[512]{};
		int trackingCount = 1;
		int trackingQuestGroupMode = 0;
		bool trackingOnlyRunning = false;
		std::string selectedTrackingIngredient;
		bool openTrackingIngredientDetails = false;
		std::uint32_t selectedTrackingQuestFormID = 0;
		char trackingStageInput[16]{};
		bool trackingStageForce = false;

		bool IsCursorOverProtectedIngredientWindow(const ImVec2& a_position)
		{
			return protectedIngredientWindowVisible &&
				a_position.x >= protectedIngredientWindowMin.x && a_position.x <= protectedIngredientWindowMax.x &&
				a_position.y >= protectedIngredientWindowMin.y && a_position.y <= protectedIngredientWindowMax.y;
		}

		std::string Text(std::string_view a_key, std::string_view a_fallback)
		{
			return localization::Translate(a_key, a_fallback);
		}

		std::string FormatText(
			std::string_view a_key,
			std::string_view a_fallback,
			std::initializer_list<localization::FormatArgument> a_arguments)
		{
			return localization::Format(a_key, a_fallback, a_arguments);
		}

		ProtectionCategory GetProtectionCategory(std::string_view a_key)
		{
			if (a_key.starts_with("quest:")) {
				return ProtectionCategory::kQuest;
			}
			if (a_key.starts_with("constructible:")) {
				return ProtectionCategory::kCraftable;
			}
			if (a_key.starts_with("effect:")) {
				return ProtectionCategory::kEffect;
			}
			return ProtectionCategory::kCustom;
		}

		bool IsTrackingRequirementVisible(const tracker::Requirement& a_requirement, bool a_manualProtectionOnly)
		{
			return !a_manualProtectionOnly || !a_requirement.automatic || a_requirement.key.starts_with("effect:");
		}

		void AddProtectionCount(ProtectionCategoryCounts& a_counts, ProtectionCategory a_category, int a_count)
		{
			a_count = (std::max)(1, a_count);
			auto& total = a_counts.values[static_cast<std::size_t>(a_category)];
			if (total == 999 || a_count == 999) {
				total = 999;
			} else {
				total = (std::min)(999, total + a_count);
			}
		}

		std::wstring Utf8ToWide(std::string_view a_text)
		{
			if (a_text.empty()) {
				return {};
			}
			const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, a_text.data(), static_cast<int>(a_text.size()), nullptr, 0);
			if (length <= 0) {
				return {};
			}
			std::wstring result(static_cast<std::size_t>(length), L'\0');
			if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, a_text.data(), static_cast<int>(a_text.size()), result.data(), length) <= 0) {
				return {};
			}
			return result;
		}

		std::string WideToUtf8(std::wstring_view a_text)
		{
			if (a_text.empty()) {
				return {};
			}
			const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, a_text.data(), static_cast<int>(a_text.size()), nullptr, 0, nullptr, nullptr);
			if (length <= 0) {
				return {};
			}
			std::string result(static_cast<std::size_t>(length), '\0');
			if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, a_text.data(), static_cast<int>(a_text.size()), result.data(), length, nullptr, nullptr) <= 0) {
				return {};
			}
			return result;
		}

		std::string FoldForSearch(std::string_view a_text)
		{
			const auto wideText = Utf8ToWide(a_text);
			if (wideText.empty()) {
				return std::string(a_text);
			}
			const int foldedLength = LCMapStringEx(
				LOCALE_NAME_INVARIANT,
				LCMAP_LOWERCASE,
				wideText.data(),
				static_cast<int>(wideText.size()),
				nullptr,
				0,
				nullptr,
				nullptr,
				static_cast<LPARAM>(0));
			if (foldedLength <= 0) {
				return std::string(a_text);
			}
			std::wstring folded(static_cast<std::size_t>(foldedLength), L'\0');
			if (LCMapStringEx(
					LOCALE_NAME_INVARIANT,
					LCMAP_LOWERCASE,
					wideText.data(),
					static_cast<int>(wideText.size()),
					folded.data(),
					foldedLength,
					nullptr,
					nullptr,
					static_cast<LPARAM>(0)) <= 0) {
				return std::string(a_text);
			}
			const auto result = WideToUtf8(folded);
			return result.empty() ? std::string(a_text) : result;
		}

		bool ContainsInsensitive(std::string_view value, std::string_view query)
		{
			if (query.empty()) {
				return true;
			}
			const auto foldedValue = FoldForSearch(value);
			const auto foldedQuery = FoldForSearch(query);
			if (foldedQuery.size() > foldedValue.size()) {
				return false;
			}
			return foldedValue.find(foldedQuery) != std::string::npos;
		}

		std::string QuestTypeLabel(const tracker::QuestInfo& a_quest)
		{
			switch (a_quest.typeID) {
			case 0: return Text("tracking.questTypeNone", "None");
			case 1: return Text("tracking.questTypeMain", "Main Quest");
			case 2: return Text("tracking.questTypeMages", "Mages Guild");
			case 3: return Text("tracking.questTypeThieves", "Thieves Guild");
			case 4: return Text("tracking.questTypeDarkBrotherhood", "Dark Brotherhood");
			case 5: return Text("tracking.questTypeCompanions", "Companions");
			case 6: return Text("tracking.questTypeMiscellaneous", "Miscellaneous");
			case 7: return Text("tracking.questTypeDaedric", "Daedric");
			case 8: return Text("tracking.questTypeSide", "Side Quest");
			case 9: return Text("tracking.questTypeCivilWar", "Civil War");
			case 10: return Text("tracking.questTypeDawnguard", "Dawnguard");
			case 11: return Text("tracking.questTypeDragonborn", "Dragonborn");
			default: return Text("tracking.questTypeUnknown", "Unknown");
			}
		}

		std::string QuestStatusLabel(const tracker::QuestInfo& a_quest)
		{
			return a_quest.completed ? Text("tracking.questCompleted", "Completed") :
				a_quest.active ? Text("tracking.questActive", "Active") : Text("tracking.questFuture", "Inactive / future");
		}

		bool QuestMatchesSearch(const tracker::QuestInfo& a_quest, std::string_view a_query)
		{
			const auto title = a_quest.title.empty() ? Text("tracking.unnamedQuest", "<unnamed quest>") : a_quest.title;
			const auto editorID = a_quest.editorID.empty() ? Text("tracking.noneValue", "none") : a_quest.editorID;
			const auto modName = a_quest.modName.empty() ? Text("tracking.noneValue", "none") : a_quest.modName;
			const auto questStatus = QuestStatusLabel(a_quest);
			const auto questType = QuestTypeLabel(a_quest);
			const auto runningStatus = a_quest.running ? Text("tracking.questRunning", "Running") : Text("tracking.questStopped", "Stopped");
			if (a_query.empty() || ContainsInsensitive(title, a_query) || ContainsInsensitive(a_quest.key, a_query) ||
				ContainsInsensitive(a_quest.formID, a_query) || ContainsInsensitive(std::to_string(a_quest.formIDValue), a_query) ||
				ContainsInsensitive(editorID, a_query) || ContainsInsensitive(modName, a_query) ||
				ContainsInsensitive(questType, a_query) || ContainsInsensitive(questStatus, a_query) ||
				ContainsInsensitive(runningStatus, a_query) || ContainsInsensitive(std::to_string(a_quest.currentStage), a_query)) {
				return true;
			}
			return std::any_of(a_quest.objectives.begin(), a_quest.objectives.end(), [a_query](const auto& objective) {
				const auto objectiveStatus = objective.completed ? Text("tracking.objectiveCompleted", "Completed") :
					objective.dormant ? Text("tracking.objectiveDormant", "Dormant") : Text("tracking.objectiveDisplayed", "Displayed");
				return ContainsInsensitive(objective.text, a_query) || ContainsInsensitive(std::to_string(objective.index), a_query) ||
					ContainsInsensitive(std::to_string(objective.state), a_query) || ContainsInsensitive(objectiveStatus, a_query);
			});
		}

		bool RequirementMatchesSearch(const tracker::Requirement& a_requirement, std::string_view a_query)
		{
			const auto typeText = Text(a_requirement.automatic ? "tracking.detected" : "tracking.manualLabel", a_requirement.automatic ? "Detected" : "Manual");
			const auto sourceText = a_requirement.source.empty() ? Text("tracking.unknownSource", "Unspecified source") : a_requirement.source;
			const auto completionText = a_requirement.completed ? Text("tracking.completed", "Completed") : Text("tracking.incomplete", "Incomplete");
			return a_query.empty() || ContainsInsensitive(a_requirement.key, a_query) || ContainsInsensitive(typeText, a_query) ||
				ContainsInsensitive(a_requirement.source, a_query) || ContainsInsensitive(sourceText, a_query) ||
				ContainsInsensitive(a_requirement.detail, a_query) || ContainsInsensitive(a_requirement.ingredient, a_query) ||
				ContainsInsensitive(std::to_string(a_requirement.count), a_query) || ContainsInsensitive(completionText, a_query);
		}

		void TextWrappedInCell(const char* a_text)
		{
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + (std::max)(0.0f, ImGui::GetContentRegionAvail().x));
			ImGui::TextWrapped("%s", a_text);
			ImGui::PopTextWrapPos();
		}

		void TextColoredWrappedInCell(const ImVec4& a_color, const char* a_text)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, a_color);
			TextWrappedInCell(a_text);
			ImGui::PopStyleColor();
		}

		void ApplyTheme()
		{
			auto& style = ImGui::GetStyle();
			style.WindowRounding = 3.0f;
			style.FrameRounding = 2.0f;
			style.ScrollbarRounding = 2.0f;
			style.WindowBorderSize = 1.0f;
			style.Colors[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.065f, 0.055f, 0.97f);
			style.Colors[ImGuiCol_ChildBg] = ImVec4(0.055f, 0.050f, 0.045f, 0.95f);
			style.Colors[ImGuiCol_Border] = ImVec4(0.38f, 0.30f, 0.18f, 0.75f);
			style.Colors[ImGuiCol_Header] = ImVec4(0.27f, 0.20f, 0.11f, 1.0f);
			style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.45f, 0.33f, 0.14f, 1.0f);
			style.Colors[ImGuiCol_Button] = ImVec4(0.25f, 0.18f, 0.10f, 1.0f);
			style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.42f, 0.30f, 0.12f, 1.0f);
			style.Colors[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.11f, 0.08f, 1.0f);
			style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.23f, 0.18f, 0.10f, 1.0f);
		}

		bool IsValidSavedValue(float a_value)
		{
			return std::isfinite(a_value) && a_value >= 0.0f;
		}

		bool IsValidSavedSize(float a_value)
		{
			return std::isfinite(a_value) && a_value > 0.0f;
		}

		void SaveWindowState(bool a_saveSize)
		{
			const auto position = ImGui::GetWindowPos();
			bool changed = false;
			if (std::fabs(windowPositionX.GetValue() - position.x) > 0.01f) {
				windowPositionX.SetValue(position.x);
				changed = true;
			}
			if (std::fabs(windowPositionY.GetValue() - position.y) > 0.01f) {
				windowPositionY.SetValue(position.y);
				changed = true;
			}
			if (a_saveSize) {
				const auto size = ImGui::GetWindowSize();
				expandedWindowSize = size;
				if (std::fabs(windowWidth.GetValue() - size.x) > 0.01f) {
					windowWidth.SetValue(size.x);
					changed = true;
				}
				if (std::fabs(windowHeight.GetValue() - size.y) > 0.01f) {
					windowHeight.SetValue(size.y);
					changed = true;
				}
			}

			if (changed) {
				REX::INI::SettingStore::GetSingleton()->Save();
			}
		}

		template <std::size_t Size>
		void CopySettingText(char (&a_buffer)[Size], const std::string& a_value)
		{
			std::size_t length = (std::min)(Size - 1, a_value.size());
			while (length > 0 && (static_cast<unsigned char>(a_value[length]) & 0xC0) == 0x80) {
				--length;
			}
			std::memcpy(a_buffer, a_value.data(), length);
			a_buffer[length] = '\0';
		}

		RequirementEditBuffers& GetRequirementEditBuffers(const tracker::Requirement& a_requirement)
		{
			auto [iterator, inserted] = trackingRequirementBuffers.try_emplace(a_requirement.key);
			if (inserted) {
				CopySettingText(iterator->second.source, a_requirement.source);
				CopySettingText(iterator->second.detail, a_requirement.detail);
				CopySettingText(iterator->second.ingredient, a_requirement.ingredient);
			}
			return iterator->second;
		}

		void LoadProtectedIngredients();

		void LoadSettingsBuffers()
		{
			const auto prefixes = getPotionPrefixes();
			CopySettingText(potionPrefix, prefixes.potion);
			CopySettingText(poisonPrefix, prefixes.poison);
			settingsBuffersInitialized = true;
		}

		void LoadTrackingBuffers()
		{
			LoadProtectedIngredients();
			trackingRequirementBuffers.clear();
			openProtectedIngredientWindow = false;
			positionProtectedIngredientWindow = false;
			protectedIngredientWindowVisible = false;
			protectedIngredientWindowMin = ImVec2(-1.0f, -1.0f);
			protectedIngredientWindowMax = ImVec2(-1.0f, -1.0f);
			focusProtectedIngredientSearch = false;
			protectedIngredientSearch[0] = '\0';
			trackingSource[0] = '\0';
			trackingDetail[0] = '\0';
			trackingIngredient[0] = '\0';
			trackingDetectionSearch[0] = '\0';
			trackingCount = 1;
			trackingOnlyRunning = false;
			selectedTrackingIngredient.clear();
			selectedTrackingQuestFormID = 0;
			trackingStageInput[0] = '\0';
			trackingStageForce = false;
			trackingBuffersInitialized = true;
		}

		void SaveSettings()
		{
			REX::INI::SettingStore::GetSingleton()->Save();
		}

		void LoadProtectedIngredients()
		{
			protectedIngredients.clear();
			auto* dataHandler = RE::TESDataHandler::GetSingleton();
			for (const auto& token : str::split(kProtectedIngredients.GetValue(), ',')) {
				if (token.empty()) {
					continue;
				}
				const auto parts = str::split(token, '|');
				std::string key = parts.front();
				std::string displayName = key;
				if (dataHandler) {
					RE::FormID formId = 0;
					if (key.size() >= 3 && key[0] == '0' && (key[1] == 'x' || key[1] == 'X')) {
						try {
							formId = static_cast<RE::FormID>(std::stoul(key, nullptr, 16));
						} catch (...) {}
					}
					if (formId == 0) {
						formId = ingredient::getDefaultIngredientFormID(key);
					}
					if (formId != 0) {
						for (const auto* ing : dataHandler->GetFormArray<IngredientItem>()) {
							if (ing && (ing->GetFormID() == formId || (ing->GetFormID() & 0x00FFFFFF) == (formId & 0x00FFFFFF))) {
								if (ing->GetFullName() && *ing->GetFullName()) {
									displayName = ing->GetFullName();
									break;
								}
							}
						}
					}
				}
				ProtectedIngredientEntry entry{ .name = std::move(displayName) };
				if (parts.size() > 1) {
					entry.count = (std::max)(1, str::toInt(parts.at(1)));
					entry.previousCount = entry.count;
				}
				if (std::none_of(protectedIngredients.begin(), protectedIngredients.end(), [&entry](const auto& existing) {
						return existing.name == entry.name;
					})) {
					protectedIngredients.push_back(std::move(entry));
				}
			}
		}

		void SaveProtectedIngredients()
		{
			std::string value;
			for (const auto& entry : protectedIngredients) {
				if (!value.empty()) {
					value += ",";
				}
				value += entry.name;
				if (entry.count > 0) {
					value += "|" + std::to_string(entry.count);
				}
			}
			kProtectedIngredients.SetValue(std::move(value));
			SaveSettings();
		}

		std::vector<std::string> GetIngredientNames()
		{
			std::vector<std::string> names;
			auto* dataHandler = RE::TESDataHandler::GetSingleton();
			if (!dataHandler) {
				return names;
			}
			for (auto* ingredient : dataHandler->GetFormArray<IngredientItem>()) {
				if (ingredient && ingredient->GetFullName() && *ingredient->GetFullName()) {
					names.emplace_back(ingredient->GetFullName());
				}
			}
			std::sort(names.begin(), names.end());
			return names;
		}

		int IngredientMatchScore(std::string_view a_name, std::string_view a_query)
		{
			const auto name = FoldForSearch(a_name);
			const auto query = FoldForSearch(a_query);
			const auto position = name.find(query);
			if (position == std::string::npos) {
				return -1;
			}
			return position == 0 ? 0 : static_cast<int>(position) + 1;
		}

		void AddProtectedIngredient(std::string a_name)
		{
			if (std::none_of(protectedIngredients.begin(), protectedIngredients.end(), [&a_name](const auto& entry) {
					return entry.name == a_name;
				})) {
				protectedIngredients.push_back(ProtectedIngredientEntry{ .name = std::move(a_name) });
				SaveProtectedIngredients();
			}
		}

		void OpenTrackingIngredientDetails(std::string_view a_name)
		{
			selectedTrackingIngredient = a_name;
			openTrackingIngredientDetails = true;
		}

		void SetProtectedIngredient(std::string_view a_name, int a_count, bool a_protectAll)
		{
			const auto found = std::find_if(protectedIngredients.begin(), protectedIngredients.end(), [a_name](const auto& entry) {
				return entry.name == a_name;
			});
			if (found == protectedIngredients.end()) {
				protectedIngredients.push_back(ProtectedIngredientEntry{
					.name = std::string(a_name),
					.count = a_protectAll ? -1 : (std::max)(1, a_count),
					.previousCount = (std::max)(1, a_count)
				});
			} else if (a_protectAll) {
				found->previousCount = (std::max)(1, a_count);
				found->count = -1;
			} else {
				found->count = (std::max)(1, a_count);
				found->previousCount = found->count;
			}
			SaveProtectedIngredients();
		}

		void ToggleDeveloperTestHub()
		{
			if (developerTestHubOpen) {
				developerTestHubOpen = false;
				devhub::Close();
			} else {
				developerTestHubOpen = true;
				devhub::Open();
			}
			searchInputFocused.store(false, std::memory_order_release);
			ImGui::ClearActiveID();
		}

		void DrawDeveloperToggle()
		{
			const auto label = Text("ui.test", "Test") + "##DeveloperTest";
			if (ImGui::Button(label.c_str())) {
				ToggleDeveloperTestHub();
			}
		}

		std::string LocalizedProgressPhase(std::string_view a_phase)
		{
			if (a_phase == "Starting calculation") {
				return Text("progress.starting", "Starting calculation");
			}
			if (a_phase == "Starting recalculation...") {
				return Text("progress.startingRecalculation", "Starting recalculation...");
			}
			if (a_phase == "Evaluating 2-ingredient recipes") {
				return Text("progress.evaluatingTwoIngredient", "Evaluating 2-ingredient recipes");
			}
			if (a_phase == "Finding 3-ingredient combinations") {
				return Text("progress.findingThreeIngredient", "Finding 3-ingredient combinations");
			}
			if (a_phase == "Evaluating 3-ingredient recipes") {
				return Text("progress.evaluatingThreeIngredient", "Evaluating 3-ingredient recipes");
			}
			if (a_phase == "Collecting recipes...") {
				return Text("progress.collecting", "Collecting recipes...");
			}
			if (a_phase == "Preparing recipes for sorting...") {
				return Text("progress.preparingSort", "Preparing recipes for sorting...");
			}
			if (a_phase == "Re-evaluating recipes...") {
				return Text("progress.reevaluating", "Re-evaluating recipes...");
			}
			if (a_phase == "Recalculating recipes...") {
				return Text("progress.recalculatingRecipes", "Recalculating recipes...");
			}
			if (a_phase == "Filtering recipe cache...") {
				return Text("progress.filteringCache", "Filtering recipe cache...");
			}
			if (a_phase == "Evaluating new ingredient combinations...") {
				return Text("progress.evaluatingNewIngredients", "Evaluating new ingredient combinations...");
			}
			if (a_phase == "Sorting recipes...") {
				return Text("progress.sorting", "Sorting recipes...");
			}
			if (a_phase == "Formatting recipes...") {
				return Text("progress.formatting", "Formatting recipes...");
			}
			if (a_phase == "Finalizing potion list...") {
				return Text("progress.finalizing", "Finalizing potion list...");
			}
			if (a_phase == "Completed") {
				return Text("progress.completed", "Completed");
			}
			return std::string(a_phase);
		}

		std::string LocalizedStaleReason(std::string_view a_reason)
		{
			if (a_reason == "Alchemy level changed") {
				return Text("stale.alchemyLevelChanged", "Alchemy level changed");
			}
			if (a_reason == "New ingredients available") {
				return Text("stale.newIngredients", "New ingredients available");
			}
			return std::string(a_reason);
		}

		void DrawRecipes()
		{
			const bool developerEnabled = kDeveloper.GetValue() == 1;
			const auto& style = ImGui::GetStyle();
			const auto trackText = Text("ui.track", "Track");
			const auto settingsText = Text("ui.settings", "Settings");
			const auto effectsText = Text("ui.effects", "Effects");
			const auto searchHint = Text("ui.searchRecipes", "Search recipes or ingredients");
			const std::array<std::string, 4> sortLabels = {
				Text("sort.valueDescending", "Value ↓"),
				Text("sort.valueAscending", "Value ↑"),
				Text("sort.nameAscending", "Name ↑"),
				Text("sort.nameDescending", "Name ↓")
			};
			const auto& currentSortLabel = (sortMode >= 0 && sortMode < 4) ? sortLabels[sortMode] : sortLabels[0];
			const float trackWidth = ImGui::CalcTextSize(trackText.c_str()).x + style.FramePadding.x * 2.0f;
			const float settingsWidth = ImGui::CalcTextSize(settingsText.c_str()).x + style.FramePadding.x * 2.0f;
			const float sortWidth = ImGui::CalcTextSize(currentSortLabel.c_str()).x + style.FramePadding.x * 2.0f + ImGui::GetFrameHeight();
			const float effectsCheckboxWidth = ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(effectsText.c_str()).x;
			float searchWidth = ImGui::GetContentRegionAvail().x - trackWidth - settingsWidth - sortWidth - effectsCheckboxWidth - style.ItemSpacing.x * 4.0f;
			if (developerEnabled) {
				const auto testText = Text("ui.test", "Test");
				searchWidth -= ImGui::CalcTextSize(testText.c_str()).x + style.FramePadding.x * 2.0f + style.ItemSpacing.x;
			}
			ImGui::SetNextItemWidth((std::max)(1.0f, searchWidth));
			if (focusSearch) {
				ImGui::SetKeyboardFocusHere();
				focusSearch = false;
			}
			ImGui::InputTextWithHint("##RecipeSearch", searchHint.c_str(), searchText, sizeof(searchText));
			searchInputFocused.store(ImGui::IsItemActive(), std::memory_order_release);
			searchRectMin = ImGui::GetItemRectMin();
			searchRectMax = ImGui::GetItemRectMax();
			ImGui::SameLine();
			if (developerEnabled) {
				DrawDeveloperToggle();
				ImGui::SameLine();
			}
			if (ImGui::Button((trackText + "##Track").c_str())) {
				trackingOpen = true;
				settingsOpen = false;
				trackingBuffersInitialized = false;
				tracker::RefreshDetection();
				menu::RequestRecalculation(true);
				searchInputFocused.store(false, std::memory_order_release);
				ImGui::ClearActiveID();
			}
			ImGui::SameLine();
			if (ImGui::Button((settingsText + "##Settings").c_str())) {
				settingsOpen = true;
				trackingOpen = false;
				settingsBuffersInitialized = false;
				searchInputFocused.store(false, std::memory_order_release);
				ImGui::ClearActiveID();
			}
			ImGui::SameLine();
			ImGui::Checkbox((effectsText + "##EffectsColumn").c_str(), &showEffectsColumn);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(sortWidth);
			std::string sortItems;
			for (const auto& sortLabel : sortLabels) {
				sortItems += sortLabel;
				sortItems.push_back('\0');
			}
			sortItems.push_back('\0');
			ImGui::Combo("##RecipeSort", &sortMode, sortItems.c_str());

			if (std::string_view(searchText) != lastSearchText) {
				currentPage = 1;
				pageChanged = true;
				lastSearchText = searchText;
			}
			if (sortMode != lastSortMode) {
				currentPage = 1;
				pageChanged = true;
				lastSortMode = sortMode;
			}

			const auto progress = engine::GetCalculationProgress();
			const bool isUpdating = progress.isUpdating;

			static bool wasUpdating = false;
			static std::chrono::steady_clock::time_point lastCompletionTime{};

			if (wasUpdating && !isUpdating) {
				lastCompletionTime = std::chrono::steady_clock::now();
			}
			wasUpdating = isUpdating;

			const auto now = std::chrono::steady_clock::now();
			const bool showCompletedBanner = !isUpdating && (lastCompletionTime.time_since_epoch().count() > 0) &&
				(std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCompletionTime).count() < 1500);

			static std::vector<engine::RecipeResult> processedRecipes;
			static std::uint64_t lastProcessedCacheGeneration = 0;
			static std::string lastProcessedSearchText;
			static int lastProcessedSortMode = -1;
			static bool lastProcessedFilterPotionsBySelectedIngredients = false;
			static std::vector<std::uint32_t> lastProcessedSelectedIngredientFormIDs;

			const std::uint64_t currentCacheGen = engine::GetRecipeCacheGeneration();
			const std::string_view currentQuery(searchText);
			const bool filterPotionsBySelectedIngredients = kFilterPotionsBySelectedIngredients.GetValue() != 0;
			const auto selectedIngredientFormIDs = filterPotionsBySelectedIngredients ?
				menu::GetSelectedIngredientFormIDs() : std::vector<std::uint32_t>{};
			const bool selectedIngredientFilterChanged = filterPotionsBySelectedIngredients != lastProcessedFilterPotionsBySelectedIngredients ||
				selectedIngredientFormIDs != lastProcessedSelectedIngredientFormIDs;

			if (currentCacheGen != lastProcessedCacheGeneration || currentQuery != lastProcessedSearchText || sortMode != lastProcessedSortMode ||
				selectedIngredientFilterChanged) {
				processedRecipes = engine::GetCachedRecipes();
				processedRecipes.erase(std::remove_if(processedRecipes.begin(), processedRecipes.end(), [](const engine::RecipeResult& recipe) {
					return recipe.displayedValue < 1;
				}), processedRecipes.end());
				if (!currentQuery.empty()) {
					processedRecipes.erase(std::remove_if(processedRecipes.begin(), processedRecipes.end(), [currentQuery](const engine::RecipeResult& recipe) {
						return !ContainsInsensitive(recipe.name, currentQuery) && !ContainsInsensitive(recipe.ingredients, currentQuery) &&
							!ContainsInsensitive(recipe.effects, currentQuery);
					}), processedRecipes.end());
				}
				if (filterPotionsBySelectedIngredients && !selectedIngredientFormIDs.empty()) {
					processedRecipes.erase(std::remove_if(processedRecipes.begin(), processedRecipes.end(), [&selectedIngredientFormIDs](const auto& recipe) {
						return !std::all_of(selectedIngredientFormIDs.begin(), selectedIngredientFormIDs.end(), [&recipe](const auto formID) {
							return std::find(recipe.ingredientFormIDs.begin(), recipe.ingredientFormIDs.end(), formID) != recipe.ingredientFormIDs.end();
						});
					}), processedRecipes.end());
				}

				if (sortMode == 0) {
					std::sort(processedRecipes.begin(), processedRecipes.end(), [](const auto& left, const auto& right) {
						if (left.calculatedValue != right.calculatedValue) {
							return left.calculatedValue > right.calculatedValue;
						}
						if (left.name != right.name) {
							return left.name < right.name;
						}
						return left.ingredients < right.ingredients;
					});
				} else if (sortMode == 1) {
					std::sort(processedRecipes.begin(), processedRecipes.end(), [](const auto& left, const auto& right) {
						if (left.calculatedValue != right.calculatedValue) {
							return left.calculatedValue < right.calculatedValue;
						}
						if (left.name != right.name) {
							return left.name < right.name;
						}
						return left.ingredients < right.ingredients;
					});
				} else if (sortMode == 2) {
					std::sort(processedRecipes.begin(), processedRecipes.end(), [](const auto& left, const auto& right) {
						if (left.name != right.name) {
							return left.name < right.name;
						}
						if (left.calculatedValue != right.calculatedValue) {
							return left.calculatedValue > right.calculatedValue;
						}
						return left.ingredients < right.ingredients;
					});
				} else if (sortMode == 3) {
					std::sort(processedRecipes.begin(), processedRecipes.end(), [](const auto& left, const auto& right) {
						if (left.name != right.name) {
							return left.name > right.name;
						}
						if (left.calculatedValue != right.calculatedValue) {
							return left.calculatedValue > right.calculatedValue;
						}
						return left.ingredients < right.ingredients;
					});
				}

				if (selectedIngredientFilterChanged && !selectedRecipeIngredientDetails.empty()) {
					const int selectedRecipePage = FindRecipePage(processedRecipes, selectedRecipeIngredientDetails);
					if (selectedRecipePage > 0 && selectedRecipePage != currentPage) {
						currentPage = selectedRecipePage;
						pageChanged = true;
					}
				}

				lastProcessedCacheGeneration = currentCacheGen;
				lastProcessedSearchText = currentQuery;
				lastProcessedSortMode = sortMode;
				lastProcessedFilterPotionsBySelectedIngredients = filterPotionsBySelectedIngredients;
				lastProcessedSelectedIngredientFormIDs = selectedIngredientFormIDs;
			}

			const auto& recipes = processedRecipes;

			const bool selectedIngredientFilterActive = filterPotionsBySelectedIngredients && !selectedIngredientFormIDs.empty();
			if (!selectedRecipeIngredientDetails.empty() && !selectedIngredientFilterActive) {
				const bool stillPossible = std::any_of(recipes.begin(), recipes.end(), [](const auto& recipe) {
					return recipe.ingredientDetails == selectedRecipeIngredientDetails;
				});
				if (!stillPossible) {
					selectedRecipeIngredientDetails.clear();
				}
			}

			if (isUpdating && recipes.empty()) {
				const auto avail = ImGui::GetContentRegionAvail();
				const float barWidth = (std::min)(avail.x * 0.85f, 380.0f);
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (std::max)(10.0f, (avail.y - 90.0f) * 0.35f));
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail.x - barWidth) * 0.5f);
				ImGui::BeginGroup();
				const auto updatingText = Text("progress.updating", "Updating potion list...");
				ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), "%s", updatingText.c_str());
				char overlay[128];
				if (progress.total > 0) {
					const auto progressText = FormatText(
						"progress.percentWithCounts",
						"{percent}% ({current} / {total})",
						{{ "percent", std::to_string(static_cast<int>(progress.progressFraction * 100.0f)) },
							{ "current", std::to_string(progress.current) },
							{ "total", std::to_string(progress.total) }});
					strncpy_s(overlay, progressText.c_str(), _TRUNCATE);
				} else {
					const auto progressText = FormatText(
						"progress.percent",
						"{percent}%",
						{{ "percent", std::to_string(static_cast<int>(progress.progressFraction * 100.0f)) }});
					strncpy_s(overlay, progressText.c_str(), _TRUNCATE);
				}
				ImGui::ProgressBar(progress.progressFraction, ImVec2(barWidth, 22.0f), overlay);
				if (!progress.phase.empty()) {
					const auto phaseText = LocalizedProgressPhase(progress.phase);
					ImGui::TextDisabled("%s", phaseText.c_str());
				}
				ImGui::EndGroup();
				return;
			}

			if (isUpdating || showCompletedBanner) {
				char overlay[128];
				float fraction = progress.progressFraction;
				if (showCompletedBanner) {
					fraction = 1.0f;
					const auto completeText = FormatText(
						"progress.complete",
						"Recalculation complete! - {percent}%",
						{{ "percent", "100" }});
					strncpy_s(overlay, completeText.c_str(), _TRUNCATE);
				} else if (progress.total > 0) {
					const auto overlayText = FormatText(
						"progress.phaseWithCounts",
						"{phase} ({current} / {total}) - {percent}%",
						{{ "phase", LocalizedProgressPhase(progress.phase) },
							{ "current", std::to_string(progress.current) },
							{ "total", std::to_string(progress.total) },
							{ "percent", std::to_string(static_cast<int>(fraction * 100.0f)) }});
					strncpy_s(overlay, overlayText.c_str(), _TRUNCATE);
				} else if (!progress.phase.empty()) {
					const auto overlayText = FormatText(
						"progress.phaseWithPercent",
						"{phase} - {percent}%",
						{{ "phase", LocalizedProgressPhase(progress.phase) },
							{ "percent", std::to_string(static_cast<int>(fraction * 100.0f)) }});
					strncpy_s(overlay, overlayText.c_str(), _TRUNCATE);
				} else {
					const auto overlayText = FormatText(
						"progress.recalculating",
						"Recalculating... {percent}%",
						{{ "percent", std::to_string(static_cast<int>(fraction * 100.0f)) }});
					strncpy_s(overlay, overlayText.c_str(), _TRUNCATE);
				}
				ImGui::Spacing();
				if (showCompletedBanner) {
					ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.75f, 0.3f, 1.0f));
					ImGui::ProgressBar(1.0f, ImVec2(-1.0f, 18.0f), overlay);
					ImGui::PopStyleColor();
				} else {
					ImGui::ProgressBar(fraction, ImVec2(-1.0f, 18.0f), overlay);
				}
				ImGui::Spacing();
			} else if (engine::IsRecipeListStale()) {
				const auto reason = engine::GetStaleReason();
				ImGui::Spacing();
				const ImVec4 amber(1.0f, 0.75f, 0.2f, 1.0f);
				if (!reason.empty()) {
					const auto noticeText = FormatText(
						"stale.noticeWithReason",
						"Notice: Potion list may be outdated ({reason}).",
						{{ "reason", LocalizedStaleReason(reason) }});
					ImGui::TextColored(amber, "%s", noticeText.c_str());
				} else {
					const auto noticeText = Text("stale.notice", "Notice: Potion list may be outdated.");
					ImGui::TextColored(amber, "%s", noticeText.c_str());
				}
				ImGui::SameLine();
				if (ImGui::Button((Text("ui.recalculate", "Recalculate") + "##ManualStale").c_str())) {
					engine::RequestManualRecalculate();
				}
				ImGui::Spacing();
			}

			const std::size_t totalPotions = recipes.size();
			auto pageInfo = CalculatePagination(totalPotions, currentPage);
			if (pageInfo.totalPages > 0 && currentPage > pageInfo.totalPages) {
				currentPage = pageInfo.totalPages;
				pageChanged = true;
				pageInfo = CalculatePagination(totalPotions, currentPage);
			}
			if (currentPage < 1) {
				currentPage = 1;
				pageChanged = true;
				if (pageInfo.totalPages > 0) {
					pageInfo = CalculatePagination(totalPotions, currentPage);
				}
			}

			const int startIndex = pageInfo.startIndex;
			const int pageItemCount = pageInfo.pageItemCount;
			bool scrollToSelectedRecipe = selectedIngredientFilterChanged && !selectedRecipeIngredientDetails.empty();

			const float footerChildHeight = ImGui::GetFrameHeight() + 6.0f;
			const float footerSpacing = style.ItemSpacing.y;
			const float tableHeight = -(footerChildHeight + footerSpacing);

			const int columnCount = showEffectsColumn ? 4 : 3;
			const char* tableId = showEffectsColumn ? "RecipeTableEffects" : "RecipeTableNoEffects";
			if (ImGui::BeginTable(tableId, columnCount, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
				ImVec2(0.0f, tableHeight))) {
				if (pageChanged) {
					ImGui::SetScrollY(0.0f);
					pageChanged = false;
				}
				const auto recipeHeader = Text("table.recipe", "Recipe");
				const auto ingredientsHeader = Text("table.ingredients", "Ingredients");
				const auto valueHeader = Text("table.value", "Value");
				const auto effectsHeader = Text("table.effects", "Effects");
				const float recipeColumnWidth = (std::max)(170.0f, ImGui::CalcTextSize(recipeHeader.c_str()).x + 24.0f);
				const float valueColumnWidth = (std::max)(90.0f, ImGui::CalcTextSize(valueHeader.c_str()).x + 24.0f);
				ImGui::TableSetupColumn(recipeHeader.c_str(), ImGuiTableColumnFlags_WidthFixed, recipeColumnWidth);
				ImGui::TableSetupColumn(ingredientsHeader.c_str(), ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn(valueHeader.c_str(), ImGuiTableColumnFlags_WidthFixed, valueColumnWidth);
				if (showEffectsColumn) {
					ImGui::TableSetupColumn(effectsHeader.c_str(), ImGuiTableColumnFlags_WidthStretch);
				}
				ImGui::TableHeadersRow();

				ImGuiContext& g = *GImGui;
				ImGuiTable* table = g.CurrentTable;
				bool anyRowClicked = false;

				for (int index = 0; index < pageItemCount; ++index) {
					const auto& recipe = recipes[static_cast<std::size_t>(startIndex + index)];
					ImGui::TableNextRow();
					const float rowY1 = table ? table->RowPosY1 : ImGui::GetCursorScreenPos().y;

					ImGui::TableSetColumnIndex(0);
					TextWrappedInCell(recipe.name.c_str());
					ImGui::TableSetColumnIndex(1);
					TextWrappedInCell(recipe.ingredients.c_str());
					ImGui::TableSetColumnIndex(2);
					ImGui::Text("%d", recipe.displayedValue);
					if (showEffectsColumn) {
						ImGui::TableSetColumnIndex(3);
						TextWrappedInCell(recipe.effects.c_str());
					}

					if (table) {
						const float rowY2 = (std::max)(table->RowPosY2, rowY1 + ImGui::GetTextLineHeightWithSpacing());
						const ImVec2 mousePos = g.IO.MousePos;
						const bool isRowHovered = (table->HoveredColumnBody >= 0 && table->HoveredColumnBody < table->ColumnsCount &&
							table->HoveredColumnBorder == -1 && table->ResizedColumn == -1 &&
							mousePos.y >= rowY1 && mousePos.y < rowY2 &&
							table->InnerClipRect.Contains(mousePos) &&
							!ImGui::IsAnyItemActive());

						if (isRowHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
							selectedRecipeIngredientDetails = recipe.ingredientDetails;
							anyRowClicked = true;
						}

						const bool rowNowSelected = (!selectedRecipeIngredientDetails.empty() && recipe.ingredientDetails == selectedRecipeIngredientDetails);
						if (rowNowSelected) {
							ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImGuiCol_Header));
							if (scrollToSelectedRecipe) {
								ImGui::SetScrollHereY(0.5f);
								scrollToSelectedRecipe = false;
							}
						}
					}
				}

				if (table && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && cursorOverWindow.load(std::memory_order_acquire) && !anyRowClicked) {
					const ImVec2 mousePos = g.IO.MousePos;
					const bool onScrollbar = (table->InnerWindow && mousePos.x >= table->InnerClipRect.Max.x);
					const bool resizingColumn = (table->ResizedColumn != -1 || table->HoveredColumnBorder != -1);
					if (!onScrollbar && !resizingColumn) {
						selectedRecipeIngredientDetails.clear();
					}
				}

				ImGui::EndTable();
			}

			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.08f, 0.06f, 0.90f));
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 3.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 2.0f));
			if (ImGui::BeginChild("RecipePaginationFooter", ImVec2(0.0f, footerChildHeight), true, ImGuiWindowFlags_NoScrollbar)) {
				if (isUpdating || (totalPotions == 0 && (progress.isUpdating || (progress.progressFraction > 0.0f && progress.progressFraction < 1.0f)))) {
					ImGui::AlignTextToFramePadding();
					const auto finalizingText = Text("pagination.finalizing", "Finalizing potion list... Loading page data");
					ImGui::TextColored(ImVec4(0.92f, 0.82f, 0.45f, 1.0f), "%s", finalizingText.c_str());
				} else if (totalPotions == 0) {
					ImGui::AlignTextToFramePadding();
					if (searchText[0] != '\0') {
						const auto noMatchText = Text("pagination.noSearchResults", "No recipes match the search filter");
						ImGui::TextDisabled("%s", noMatchText.c_str());
					} else {
						const auto noRecipesText = Text("pagination.noRecipes", "No recipes available");
						ImGui::TextDisabled("%s", noRecipesText.c_str());
					}
				} else if (pageInfo.totalPages <= 1) {
					ImGui::AlignTextToFramePadding();
					const auto potionCountText = FormatText(
						"pagination.potions",
						"{count} potions",
						{{ "count", std::to_string(totalPotions) }});
					ImGui::TextColored(ImVec4(0.92f, 0.82f, 0.45f, 1.0f), "%s", potionCountText.c_str());
				} else {
					const float btnPaddingX = 5.0f;
					ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f, style.ItemSpacing.y));
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(btnPaddingX, style.FramePadding.y));

					const int startPrev = (std::max)(1, currentPage - 3);
					const int endNext = (std::min)(pageInfo.totalPages, currentPage + 3);

					const bool disableFirst = (currentPage <= 1);
					if (disableFirst) {
						ImGui::BeginDisabled();
					}
					const float firstBtnWidth = ImGui::CalcTextSize("<<").x + btnPaddingX * 2.0f;
					if (ImGui::Button("<<##FirstPage", ImVec2(firstBtnWidth, 0.0f))) {
						currentPage = 1;
						pageChanged = true;
					}
					if (disableFirst) {
						ImGui::EndDisabled();
					}

					for (int p = startPrev; p < currentPage; ++p) {
						ImGui::SameLine();
						char numStr[16];
						snprintf(numStr, sizeof(numStr), "%d", p);
						const float btnWidth = ImGui::CalcTextSize(numStr).x + btnPaddingX * 2.0f;
						char btnLabel[32];
						snprintf(btnLabel, sizeof(btnLabel), "%s##Page%d", numStr, p);
						if (ImGui::Button(btnLabel, ImVec2(btnWidth, 0.0f))) {
							currentPage = p;
							pageChanged = true;
						}
					}

					ImGui::SameLine();
					ImGui::AlignTextToFramePadding();
				const auto pageText = FormatText(
					"pagination.page",
					"Page {page} of {pages} ({count} potions)",
					{{ "page", std::to_string(currentPage) },
						{ "pages", std::to_string(pageInfo.totalPages) },
						{ "count", std::to_string(totalPotions) }});
				ImGui::TextColored(ImVec4(0.92f, 0.82f, 0.45f, 1.0f), "%s", pageText.c_str());

					for (int p = currentPage + 1; p <= endNext; ++p) {
						ImGui::SameLine();
						char numStr[16];
						snprintf(numStr, sizeof(numStr), "%d", p);
						const float btnWidth = ImGui::CalcTextSize(numStr).x + btnPaddingX * 2.0f;
						char btnLabel[32];
						snprintf(btnLabel, sizeof(btnLabel), "%s##Page%d", numStr, p);
						if (ImGui::Button(btnLabel, ImVec2(btnWidth, 0.0f))) {
							currentPage = p;
							pageChanged = true;
						}
					}

					ImGui::SameLine();
					const bool disableLast = (currentPage >= pageInfo.totalPages);
					if (disableLast) {
						ImGui::BeginDisabled();
					}
					const float lastBtnWidth = ImGui::CalcTextSize(">>").x + btnPaddingX * 2.0f;
					if (ImGui::Button(">>##LastPage", ImVec2(lastBtnWidth, 0.0f))) {
						currentPage = pageInfo.totalPages;
						pageChanged = true;
					}
					if (disableLast) {
						ImGui::EndDisabled();
					}

					ImGui::PopStyleVar(2);
				}
			}
			ImGui::EndChild();
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor();
		}

		bool DrawSettings()
		{
			if (!settingsBuffersInitialized) {
				LoadSettingsBuffers();
			}

			bool recalculate = false;
			bool textInputActive = false;
			if (ImGui::Button((Text("settings.back", "< Back to recipes") + "##BackToRecipes").c_str())) {
				settingsOpen = false;
				settingsBuffersInitialized = false;
				searchInputFocused.store(false, std::memory_order_release);
				ImGui::ClearActiveID();
			}
			ImGui::SameLine();
			if (ImGui::Button((Text("settings.reset", "Reset all settings") + "##ResetSettings").c_str())) {
				kIgnorePlayer.SetValue(kIgnorePlayer.GetValueDefault());
				kSinglethreaded.SetValue(kSinglethreaded.GetValueDefault());
				kPotionPoison.SetValue(kPotionPoison.GetValueDefault());
				kCacheDurationSeconds.SetValue(kCacheDurationSeconds.GetValueDefault());
				kStaleRecalculateThresholdMs.SetValue(kStaleRecalculateThresholdMs.GetValueDefault());
				kCraftDebounceMs.SetValue(kCraftDebounceMs.GetValueDefault());
				kFilterPotionsBySelectedIngredients.SetValue(kFilterPotionsBySelectedIngredients.GetValueDefault());
				kProtectIngredients.SetValue(kProtectIngredients.GetValueDefault());
				kManualProtectionOnly.SetValue(kManualProtectionOnly.GetValueDefault());
				LoadSettingsBuffers();
				SaveSettings();
				recalculate = true;
			}

			ImGui::Separator();
			const auto settingsText = Text("settings.title", "Settings");
			ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), "%s", settingsText.c_str());
			const auto settingsSavedText = Text("settings.saved", "Changes are saved to alchemist.ini automatically.");
			ImGui::TextDisabled("%s", settingsSavedText.c_str());
			ImGui::BeginChild("SettingsScroll", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
			ImGui::SeparatorText(Text("settings.calculation", "Calculation").c_str());

			bool usePlayerStats = kIgnorePlayer.GetValue() == 0;
			if (ImGui::Checkbox(Text("settings.usePlayerStats", "Use player's Alchemy stats").c_str(), &usePlayerStats)) {
				kIgnorePlayer.SetValue(usePlayerStats ? 0 : 1);
				SaveSettings();
				recalculate = true;
			}
			ImGui::TextDisabled("%s", Text("settings.usePlayerStatsDescription", "Includes Alchemy skill, perks, and worn Fortify Alchemy equipment.").c_str());

			bool useSingleThreadedCalculation = kSinglethreaded.GetValue() != 0;
			if (ImGui::Checkbox(Text("settings.singleThreaded", "Use single-threaded calculation").c_str(), &useSingleThreadedCalculation)) {
				kSinglethreaded.SetValue(useSingleThreadedCalculation ? 1 : 0);
				SaveSettings();
				recalculate = true;
			}
			ImGui::TextDisabled("%s", Text("settings.singleThreadedDescription", "Disable this option to use the multithreaded calculation path.").c_str());

			int cacheDuration = kCacheDurationSeconds.GetValue();
			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::InputInt(Text("settings.cacheDuration", "Cache duration (seconds)").c_str(), &cacheDuration, 10, 60)) {
				kCacheDurationSeconds.SetValue((std::max)(0, cacheDuration));
				SaveSettings();
			}
			ImGui::TextDisabled("%s", Text("settings.cacheDurationDescription", "How long to keep master recipe cache in memory after closing the alchemy menu (default 180s).").c_str());

			int staleThreshold = kStaleRecalculateThresholdMs.GetValue();
			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::InputInt(Text("settings.staleThreshold", "Stale recalculate threshold (ms)").c_str(), &staleThreshold, 50, 250)) {
				kStaleRecalculateThresholdMs.SetValue((std::max)(0, staleThreshold));
				SaveSettings();
			}
			ImGui::TextDisabled("%s", Text("settings.staleThresholdDescription", "If calculation took longer than this, defer recalculations during crafting and show manual Recalculate button (default 500ms).").c_str());

			int debounceMs = kCraftDebounceMs.GetValue();
			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::InputInt(Text("settings.debounce", "Craft debounce delay (ms)").c_str(), &debounceMs, 50, 100)) {
				kCraftDebounceMs.SetValue((std::max)(0, debounceMs));
				SaveSettings();
			}
			ImGui::TextDisabled("%s", Text("settings.debounceDescription", "Delay before running background recalculation after rapid crafting clicks (default 400ms).").c_str());

			bool filterPotionsBySelectedIngredients = kFilterPotionsBySelectedIngredients.GetValue() != 0;
			if (ImGui::Checkbox(Text("settings.filterSelected", "Filter potions by selected ingredients").c_str(), &filterPotionsBySelectedIngredients)) {
				kFilterPotionsBySelectedIngredients.SetValue(filterPotionsBySelectedIngredients ? 1 : 0);
				SaveSettings();
			}
			ImGui::TextDisabled("%s", Text("settings.filterSelectedDescription", "Show only potions made from ingredients currently selected in the Skyrim alchemy menu. With no ingredients selected, all potions are shown.").c_str());

			ImGui::SeparatorText(Text("settings.naming", "Naming").c_str());
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::InputText(Text("settings.potionPrefix", "Potion prefix").c_str(), potionPrefix, sizeof(potionPrefix))) {
				kPotionPoison.SetValue(std::string(potionPrefix) + "," + poisonPrefix);
				SaveSettings();
			}
			textInputActive = textInputActive || ImGui::IsItemActive();
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				recalculate = true;
			}

			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::InputText(Text("settings.poisonPrefix", "Poison prefix").c_str(), poisonPrefix, sizeof(poisonPrefix))) {
				kPotionPoison.SetValue(std::string(potionPrefix) + "," + poisonPrefix);
				SaveSettings();
			}
			textInputActive = textInputActive || ImGui::IsItemActive();
			ImGui::TextDisabled("%s", Text("settings.prefixDescription", "Prefixes are applied to beneficial potion and harmful poison names.").c_str());
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				recalculate = true;
			}

			ImGui::EndChild();
			searchInputFocused.store(textInputActive, std::memory_order_release);
			return recalculate;
		}

		bool DrawTracking()
		{
			if (!trackingBuffersInitialized) {
				LoadTrackingBuffers();
			}
			protectedIngredientWindowVisible = false;
			protectedIngredientWindowMin = ImVec2(-1.0f, -1.0f);
			protectedIngredientWindowMax = ImVec2(-1.0f, -1.0f);

			bool recalculate = false;
			bool textInputActive = false;
			const bool developerEnabled = kDeveloper.GetValue() == 1;
			if (ImGui::Button((Text("tracking.back", "< Back to recipes") + "##BackToRecipesFromTracking").c_str())) {
				trackingOpen = false;
				trackingBuffersInitialized = false;
				openProtectedIngredientWindow = false;
				positionProtectedIngredientWindow = false;
				protectedIngredientWindowVisible = false;
				protectedIngredientWindowMin = ImVec2(-1.0f, -1.0f);
				protectedIngredientWindowMax = ImVec2(-1.0f, -1.0f);
				focusProtectedIngredientSearch = false;
				selectedTrackingIngredient.clear();
				ImGui::CloseCurrentPopup();
				searchInputFocused.store(false, std::memory_order_release);
				ImGui::ClearActiveID();
			}
			textInputActive = textInputActive || ImGui::GetIO().WantTextInput;

			ImGui::Separator();
			const auto trackingTitle = Text("tracking.title", "Ingredient protection and tracking");
			ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), "%s", trackingTitle.c_str());
			bool protectIngredients = kProtectIngredients.GetValue() != 0;
			if (ImGui::Checkbox(Text("tracking.enableProtection", "Enable ingredient protection and tracking").c_str(), &protectIngredients)) {
				kProtectIngredients.SetValue(protectIngredients ? 1 : 0);
				SaveSettings();
				recalculate = true;
			}
			ImGui::TextDisabled("%s", Text("tracking.enableProtectionDescription", "Static reservations, craftable-item requirements, selected effects, and unfinished tracking requirements are used while this is enabled.").c_str());
			bool manualProtectionOnly = kManualProtectionOnly.GetValue() != 0;
			if (ImGui::Checkbox(Text("tracking.manualProtectionOnly", "Use only manual/custom protection (keep protected effects)").c_str(), &manualProtectionOnly)) {
				kManualProtectionOnly.SetValue(manualProtectionOnly ? 1 : 0);
				SaveSettings();
				recalculate = true;
			}
			ImGui::TextDisabled("%s", Text("tracking.manualProtectionOnlyDescription", "Disable automatic quest and craftable reservations. Selected protected effects remain active and visible.").c_str());
			const bool trackingEnabled = kProtectIngredients.GetValue() != 0;
			ImGui::TextDisabled("%s", Text(trackingEnabled ? "tracking.enabled" : "tracking.disabled",
				trackingEnabled ? "Tracking protection is enabled in this window." : "Tracking protection is disabled in this window.").c_str());
			if (developerEnabled) {
				ImGui::TextWrapped("%s", Text("tracking.description", "All loaded quests are listed below, including future inactive quests. Quest matching uses objective text, so verify detected ingredient rows and edit or mark them complete when needed.").c_str());
			}
			ImGui::TextDisabled("%s", Text("tracking.questCompletionNotice", "Completed quests are automatically marked complete and excluded from the protected ingredient list.").c_str());

			ImGui::BeginChild("TrackingScroll", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
			ImGui::SetNextItemOpen(false, ImGuiCond_Once);
			if (ImGui::CollapsingHeader((Text("tracking.effects", "Ingredient effects to protect") + "##TrackingEffects").c_str())) {
				ImGui::TextDisabled("%s", Text("tracking.effectsDescription", "Select effects to reserve every ingredient that provides them. Set each selected effect to protect all copies or a finite quantity per ingredient; open an ingredient below to adjust an individual detection.").c_str());
				const auto effects = tracker::GetEffects();
				if (effects.empty()) {
					ImGui::TextDisabled("%s", Text("tracking.noEffects", "No ingredient effects are available from loaded records.").c_str());
				} else {
					if (ImGui::Button((Text("tracking.addEffect", "Add effect") + "##AddTrackingEffect").c_str())) {
						ImGui::OpenPopup("TrackingEffectSuggestions");
					}
					if (ImGui::BeginPopup("TrackingEffectSuggestions")) {
						bool availableEffect = false;
						for (const auto& effect : effects) {
							if (effect.selected) {
								continue;
							}
							availableEffect = true;
							ImGui::PushID(effect.key.c_str());
							if (ImGui::Selectable(effect.name.c_str())) {
								tracker::SetEffectSelected(effect.key, true);
								tracker::RefreshDetection();
								recalculate = true;
								ImGui::CloseCurrentPopup();
							}
							if (!effect.editorID.empty()) {
								ImGui::SameLine();
								ImGui::TextDisabled("[%s]", effect.editorID.c_str());
							}
							ImGui::PopID();
						}
						if (!availableEffect) {
							ImGui::TextDisabled("%s", Text("tracking.noAvailableEffects", "All available effects are already protected.").c_str());
						}
						ImGui::EndPopup();
					}

					bool selectedEffect = false;
					for (const auto& effect : effects) {
						if (!effect.selected) {
							continue;
						}
						selectedEffect = true;
						ImGui::PushID(effect.key.c_str());
						ImGui::TextUnformatted(effect.name.c_str());
						if (!effect.editorID.empty()) {
							ImGui::SameLine();
							ImGui::TextDisabled("[%s]", effect.editorID.c_str());
						}
						ImGui::SameLine();
						bool protectAll = effect.protectedCount >= 999;
						if (ImGui::Checkbox((Text("tracking.effectProtectAll", "Protect all") + "##ProtectAllTrackingEffect").c_str(), &protectAll)) {
							tracker::SetEffectProtectionCount(effect.key, protectAll ? 999 : 1);
							tracker::RefreshDetection();
							recalculate = true;
						}
						if (!protectAll) {
							ImGui::SameLine();
							int protectedCount = (std::clamp)(effect.protectedCount, 1, 998);
							ImGui::SetNextItemWidth(120.0f);
							if (ImGui::InputInt((Text("tracking.effectQuantity", "Quantity per ingredient") + "##TrackingEffectQuantity").c_str(), &protectedCount, 1, 10)) {
								tracker::SetEffectProtectionCount(effect.key, (std::clamp)(protectedCount, 1, 998));
								tracker::RefreshDetection();
								recalculate = true;
							}
						}
						ImGui::SameLine();
						if (ImGui::SmallButton((Text("tracking.remove", "Remove") + "##RemoveTrackingEffect").c_str())) {
							tracker::SetEffectSelected(effect.key, false);
							tracker::RefreshDetection();
							recalculate = true;
						}
						ImGui::PopID();
					}
					if (!selectedEffect) {
						ImGui::TextDisabled("%s", Text("tracking.noSelectedEffects", "No ingredient effects selected.").c_str());
					}
				}
			}

			const auto requirements = tracker::GetRequirements();
			const auto manualOverrideCount = static_cast<std::size_t>(std::count_if(requirements.begin(), requirements.end(), [](const auto& requirement) {
				return requirement.automatic && requirement.overridden && requirement.completionOverridden;
			}));
			std::map<std::string, ProtectionCategoryCounts> protectionCounts;
			for (const auto& requirement : requirements) {
				if (!IsTrackingRequirementVisible(requirement, manualProtectionOnly) || requirement.ingredient.empty()) {
					continue;
				}
				auto& counts = protectionCounts[requirement.ingredient];
				if (requirement.automatic && requirement.overridden) {
					counts.manualOverride = true;
				}
				if (requirement.completed) {
					continue;
				}
				const auto count = (std::max)(1, requirement.count);
				AddProtectionCount(counts, GetProtectionCategory(requirement.key), count);
			}
			for (const auto& entry : protectedIngredients) {
				AddProtectionCount(protectionCounts[entry.name], ProtectionCategory::kCustom, entry.count < 0 ? 999 : entry.count);
			}

			ImGui::SetNextItemOpen(false, ImGuiCond_Once);
			if (ImGui::CollapsingHeader((Text("tracking.ingredients", "Protected ingredients") + "##TrackingIngredients").c_str())) {
				const bool addProtectedButtonReleased = ImGui::Button((Text("tracking.addProtected", "Add protected ingredient") + "##AddProtectedIngredient").c_str());
				const bool addProtectedButtonPressed = ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
				if (addProtectedButtonReleased || addProtectedButtonPressed) {
					protectedIngredientSearch[0] = '\0';
					focusProtectedIngredientSearch = true;
					openProtectedIngredientWindow = true;
					positionProtectedIngredientWindow = true;
				}
				if (developerEnabled) {
					if (ImGui::Button((Text("tracking.refresh", "Refresh detection") + "##RefreshTracking").c_str())) {
						if (tracker::RefreshDetection()) {
							recalculate = true;
						}
					}
					ImGui::TextWrapped("%s", Text("tracking.refreshDescription", "Rebuilds automatic requirements from currently loaded quest objectives, constructible records, and selected ingredient effects. Use it after a quest objective changes or new records are loaded; manual tracking rows and saved overrides are not removed.").c_str());
				}
				if (ImGui::Button((Text("tracking.reset", "Reset to detected") + "##ResetTracking").c_str())) {
					tracker::ResetToDetected();
					tracker::RefreshDetection();
					recalculate = true;
				}
				ImGui::TextWrapped("%s", Text("tracking.resetDescription", "Deletes manually added tracking rows and all saved overrides on automatic rows, then rescans currently loaded data. Custom protected ingredients and selected protected effects are not changed. Use it when you want every detected row to return to its current automatic values.").c_str());
				ImGui::BeginDisabled(manualOverrideCount == 0);
				if (ImGui::Button((Text("tracking.clearOverrides", "Clear manual overrides") + "##ClearTrackingOverrides").c_str())) {
					tracker::ClearOverrides();
					tracker::RefreshDetection();
					recalculate = true;
				}
				ImGui::EndDisabled();
				ImGui::TextWrapped("%s", Text("tracking.manualOverrideDescription", "Clears manual completion overrides only. This resets automatic rows whose Completed checkbox you changed, including user-marked complete rows, to their current automatically detected source, ingredient, quantity, and completion state. It does not clear automatic detections, manually added tracking rows, or custom protected ingredients.").c_str());

				ImGui::TextDisabled("%s", FormatText("tracking.protectionSummaryText", "{count} ingredients have active protection requirements or manual overrides.",
					{ { "count", std::to_string(protectionCounts.size()) } }).c_str());
				if (protectionCounts.empty()) {
					ImGui::TextDisabled("%s", Text("tracking.noProtectionComparison", "No detected ingredients or protected-list entries to compare.").c_str());
				} else if (ImGui::BeginTable("TrackingProtectionComparison", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
				const auto ingredientHeader = Text("tracking.comparisonIngredient", "Ingredient");
				const auto customHeader = Text("tracking.comparisonCustom", "Custom");
				const auto questHeader = Text("tracking.comparisonQuest", "Quest");
				const auto craftableHeader = Text("tracking.comparisonCraftable", "Craftable");
				const auto effectHeader = Text("tracking.comparisonEffect", "Effect");
				const auto overrideHeader = Text("tracking.comparisonOverride", "Override");
				ImGui::TableSetupColumn(ingredientHeader.c_str());
				ImGui::TableSetupColumn(customHeader.c_str(), ImGuiTableColumnFlags_WidthFixed, 90.0f);
				ImGui::TableSetupColumn(questHeader.c_str(), ImGuiTableColumnFlags_WidthFixed, 90.0f);
				ImGui::TableSetupColumn(craftableHeader.c_str(), ImGuiTableColumnFlags_WidthFixed, 90.0f);
				ImGui::TableSetupColumn(effectHeader.c_str(), ImGuiTableColumnFlags_WidthFixed, 90.0f);
				ImGui::TableSetupColumn(overrideHeader.c_str(), ImGuiTableColumnFlags_WidthFixed, 110.0f);
				ImGui::TableHeadersRow();
				for (const auto& [name, categoryCounts] : protectionCounts) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::PushID(name.c_str());
					const auto& ingredientButtonIO = ImGui::GetIO();
					const auto ingredientButtonLabel = name + "##TrackingComparisonIngredientDetails";
					const bool ingredientButtonClicked = ImGui::SmallButton(ingredientButtonLabel.c_str());
					const auto ingredientButtonMin = ImGui::GetItemRectMin();
					const auto ingredientButtonMax = ImGui::GetItemRectMax();
					const bool ingredientButtonHovered = ImGui::IsItemHovered();
					const bool ingredientCursorInButton = ingredientButtonIO.MousePos.x >= ingredientButtonMin.x && ingredientButtonIO.MousePos.x <= ingredientButtonMax.x &&
						ingredientButtonIO.MousePos.y >= ingredientButtonMin.y && ingredientButtonIO.MousePos.y <= ingredientButtonMax.y;
					const bool ingredientInputEdge = ingredientButtonIO.MouseClicked[ImGuiMouseButton_Left] || ingredientButtonIO.MouseReleased[ImGuiMouseButton_Left];
					if (ingredientButtonClicked || (ingredientInputEdge && (ingredientButtonHovered || ingredientCursorInButton))) {
					}
					ImGui::PopID();
					if (ingredientButtonClicked) {
						OpenTrackingIngredientDetails(name);
					}
					for (std::size_t category = 0; category < static_cast<std::size_t>(ProtectionCategory::kCount); ++category) {
						ImGui::TableSetColumnIndex(static_cast<int>(category + 1));
						const auto count = categoryCounts.values[category];
						if (count == 0) {
							ImGui::TextUnformatted("-");
						} else if (count == 999) {
							ImGui::TextUnformatted(Text("tracking.protectAllValue", "All").c_str());
						} else {
							ImGui::Text("%d", count);
						}
					}
					ImGui::TableSetColumnIndex(5);
					if (categoryCounts.manualOverride) {
						ImGui::TextUnformatted(Text("tracking.manualOverrideLabel", "Manual override").c_str());
					} else {
						ImGui::TextUnformatted("-");
					}
				}
					ImGui::EndTable();
				}
			}

			std::size_t detectedCount = 0;
			std::size_t detectedActiveCount = 0;
			std::size_t activeCount = 0;
			for (const auto& requirement : requirements) {
				if (!IsTrackingRequirementVisible(requirement, manualProtectionOnly)) {
					continue;
				}
				detectedCount += requirement.automatic ? 1 : 0;
				detectedActiveCount += requirement.automatic && !requirement.completed ? 1 : 0;
				activeCount += !requirement.completed ? 1 : 0;
			}

			const auto drawRequirement = [&](const tracker::Requirement& original) {
				auto requirement = original;
				auto& editBuffers = GetRequirementEditBuffers(requirement);
				ImGui::PushID(requirement.key.c_str());
				const bool manualOverride = requirement.automatic && requirement.overridden;
				const auto typeText = Text(manualOverride ? "tracking.manualOverrideLabel" : (requirement.automatic ? "tracking.detected" : "tracking.manualLabel"),
					manualOverride ? "Manual override" : (requirement.automatic ? "Detected" : "Manual"));
				ImGui::TextColored(manualOverride ? ImVec4(1.0f, 0.75f, 0.35f, 1.0f) : (requirement.automatic ? ImVec4(0.55f, 0.8f, 1.0f, 1.0f) : ImVec4(0.65f, 1.0f, 0.65f, 1.0f)), "%s", typeText.c_str());
				ImGui::SameLine();
				ImGui::TextWrapped("%s", requirement.source.empty() ? Text("tracking.unknownSource", "Unspecified source").c_str() : requirement.source.c_str());

				if (ImGui::InputText((Text("tracking.editSource", "Source") + "##EditSource").c_str(), editBuffers.source, sizeof(editBuffers.source))) {
					requirement.source = editBuffers.source;
					tracker::UpdateRequirement(requirement);
				}
				textInputActive = textInputActive || ImGui::IsItemActive();
				if (ImGui::InputText((Text("tracking.editDetail", "Quest or recipe details") + "##EditDetail").c_str(), editBuffers.detail, sizeof(editBuffers.detail))) {
					requirement.detail = editBuffers.detail;
					tracker::UpdateRequirement(requirement);
				}
				textInputActive = textInputActive || ImGui::IsItemActive();
				if (ImGui::InputText((Text("tracking.editIngredient", "Ingredient") + "##EditIngredient").c_str(), editBuffers.ingredient, sizeof(editBuffers.ingredient))) {
					requirement.ingredient = editBuffers.ingredient;
					tracker::UpdateRequirement(requirement);
					recalculate = true;
				}
				textInputActive = textInputActive || ImGui::IsItemActive();
				ImGui::SetNextItemWidth(100.0f);
				if (ImGui::InputInt((Text("tracking.editCount", "Quantity") + "##EditCount").c_str(), &requirement.count, 1, 10)) {
					requirement.count = (std::clamp)(requirement.count, 1, 999);
					tracker::UpdateRequirement(requirement);
					recalculate = true;
				}
				bool completed = requirement.completed;
				if (ImGui::Checkbox((Text("tracking.completed", "Completed") + "##Completed").c_str(), &completed)) {
					requirement.completed = completed;
					requirement.completionOverridden = true;
					tracker::UpdateRequirement(requirement);
					recalculate = true;
				}
				if (!requirement.automatic) {
					ImGui::SameLine();
					if (ImGui::SmallButton((Text("tracking.remove", "Remove") + "##RemoveTracking").c_str())) {
						tracker::RemoveManual(requirement.key);
						recalculate = true;
					}
				}
				if (!requirement.detail.empty()) {
					ImGui::TextWrapped("%s", requirement.detail.c_str());
				}
				ImGui::Separator();
				ImGui::PopID();
			};

			if (kDeveloper.GetValue() == 1) {
				ImGui::SetNextItemOpen(false, ImGuiCond_Once);
				if (ImGui::CollapsingHeader((Text("tracking.detectRequirements", "Detect requirements") + "##DetectRequirements").c_str())) {
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::InputTextWithHint("##TrackingDetectionSearch", Text("tracking.detectionSearchHint", "Filter detected requirements, ingredients, and quests...").c_str(), trackingDetectionSearch, sizeof(trackingDetectionSearch));
				textInputActive = textInputActive || ImGui::IsItemActive();
				const std::string detectionQuery(trackingDetectionSearch);

				ImGui::SetNextItemOpen(false, ImGuiCond_Once);
				if (ImGui::CollapsingHeader((Text("tracking.detectedRequirements", "Detected requirements") + "##DetectedRequirementsList").c_str())) {
					ImGui::TextDisabled("%s", FormatText("tracking.detectedSummary", "{detected} detected rows, {active} unfinished rows currently contribute to protection.",
						{ { "detected", std::to_string(detectedCount) }, { "active", std::to_string(detectedActiveCount) } }).c_str());
					for (const auto& requirement : requirements) {
						if (IsTrackingRequirementVisible(requirement, manualProtectionOnly) && requirement.automatic && RequirementMatchesSearch(requirement, detectionQuery)) {
							drawRequirement(requirement);
						}
					}
				}

				ImGui::SetNextItemOpen(false, ImGuiCond_Once);
				if (ImGui::CollapsingHeader((Text("tracking.manual", "Add manual requirement") + "##ManualRequirement").c_str())) {
					ImGui::SetNextItemWidth(-1.0f);
					if (ImGui::InputText(Text("tracking.source", "Source").c_str(), trackingSource, sizeof(trackingSource))) {
						textInputActive = true;
					}
					textInputActive = textInputActive || ImGui::IsItemActive();
					ImGui::TextDisabled("%s", Text("tracking.sourceDescription", "Quest, activity, station, or any other reason to reserve the ingredient.").c_str());
					ImGui::SetNextItemWidth(-1.0f);
					if (ImGui::InputText(Text("tracking.detail", "Details").c_str(), trackingDetail, sizeof(trackingDetail))) {
						textInputActive = true;
					}
					textInputActive = textInputActive || ImGui::IsItemActive();
					ImGui::SetNextItemWidth(-1.0f);
					if (ImGui::InputText(Text("tracking.ingredient", "Ingredient").c_str(), trackingIngredient, sizeof(trackingIngredient))) {
						textInputActive = true;
					}
					textInputActive = textInputActive || ImGui::IsItemActive();
					ImGui::SetNextItemWidth(100.0f);
					ImGui::InputInt(Text("tracking.count", "Quantity").c_str(), &trackingCount, 1, 10);
					trackingCount = (std::clamp)(trackingCount, 1, 999);
					if (ImGui::Button((Text("tracking.add", "Add requirement") + "##AddTrackingRequirement").c_str()) && trackingIngredient[0] != '\0') {
						tracker::AddManual(trackingSource, trackingDetail, trackingIngredient, trackingCount);
						trackingSource[0] = '\0';
						trackingDetail[0] = '\0';
						trackingIngredient[0] = '\0';
						trackingCount = 1;
						recalculate = true;
					}
				}

				ImGui::SetNextItemOpen(false, ImGuiCond_Once);
				if (ImGui::CollapsingHeader((Text("tracking.requirements", "Tracked requirements") + "##TrackedRequirements").c_str())) {
					ImGui::TextDisabled("%s", FormatText("tracking.summary", "{detected} detected, {manual} manual, {active} protecting",
						{
							{ "detected", std::to_string(detectedCount) },
							{ "manual", std::to_string(requirements.size() - detectedCount) },
							{ "active", std::to_string(activeCount) }
						}).c_str());
					if (requirements.empty()) {
						ImGui::TextDisabled("%s", Text("tracking.none", "No detected or manual requirements are currently tracked.").c_str());
					}

					for (const auto& requirement : requirements) {
						if (IsTrackingRequirementVisible(requirement, manualProtectionOnly) && !requirement.automatic && RequirementMatchesSearch(requirement, detectionQuery)) {
							drawRequirement(requirement);
						}
					}
				}

				ImGui::SetNextItemOpen(false, ImGuiCond_Once);
				if (ImGui::CollapsingHeader((Text("tracking.atronachForge", "Atronach Forge") + "##AtronachForgeInfo").c_str())) {
					ImGui::TextWrapped("%s", Text("tracking.atronachForgeLocation", "Location: The Midden, beneath the College of Winterhold.").c_str());
					ImGui::TextWrapped("%s", Text("tracking.atronachForgeDirections", "In-game, enter the College of Winterhold and use the trapdoor to The Midden, then follow the tunnels to the forge chamber.").c_str());
					ImGui::TextDisabled("%s", Text("tracking.atronachForgeSource", "Recipe detection comes from loaded constructible records whose crafting keyword identifies the Atronach Forge.").c_str());
				}

			const auto quests = tracker::GetQuests();
			std::set<std::uint32_t> detectedQuestIDs;
			for (const auto& requirement : requirements) {
				if (!IsTrackingRequirementVisible(requirement, manualProtectionOnly) || !requirement.automatic || requirement.key.rfind("quest:", 0) != 0) {
					continue;
				}
				const auto end = requirement.key.find(':', 6);
				if (end == std::string::npos) {
					continue;
				}
				try {
					detectedQuestIDs.insert(static_cast<std::uint32_t>(std::stoul(requirement.key.substr(6, end - 6))));
				} catch (...) {}
			}

			const auto drawQuest = [&](const tracker::QuestInfo& quest, std::string_view a_prefix) {
				ImGui::PushID((std::string(a_prefix) + quest.key).c_str());
				const auto title = quest.title.empty() ? Text("tracking.unnamedQuest", "<unnamed quest>") : quest.title;
				ImGui::SetNextItemOpen(false, ImGuiCond_Once);
				if (ImGui::TreeNodeEx((title + "##Quest").c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
					const auto status = QuestStatusLabel(quest);
					const auto running = quest.running ? Text("tracking.questRunning", "Running") : Text("tracking.questStopped", "Stopped");
					ImGui::TextDisabled("%s", FormatText("tracking.questMetadata", "Form ID: {formID} | Editor ID: {editorID} | Status: {status}",
						{
							{ "formID", quest.formID },
							{ "editorID", quest.editorID.empty() ? Text("tracking.noneValue", "none") : quest.editorID },
							{ "status", status }
						}).c_str());
					ImGui::TextDisabled("%s", FormatText("tracking.questDebugMetadata", "Plugin: {plugin} | Type: {type} | Stage: {stage} | Runtime: {runtime}",
						{
							{ "plugin", quest.modName.empty() ? Text("tracking.noneValue", "none") : quest.modName },
							{ "type", QuestTypeLabel(quest) },
							{ "stage", std::to_string(quest.currentStage) },
							{ "runtime", running }
						}).c_str());
					if (ImGui::SmallButton((Text("tracking.selectQuest", "Select for debugging") + "##SelectQuest").c_str())) {
						selectedTrackingQuestFormID = quest.formIDValue;
						CopySettingText(trackingStageInput, std::to_string(quest.currentStage));
					}
					if (quest.objectives.empty()) {
						ImGui::TextDisabled("%s", Text("tracking.noObjectives", "No objectives are present in this quest record.").c_str());
					}
					for (const auto& objective : quest.objectives) {
						ImGui::PushID(static_cast<int>(objective.index));
						const auto objectiveStatus = objective.completed ? Text("tracking.objectiveCompleted", "Completed") :
							objective.dormant ? Text("tracking.objectiveDormant", "Dormant") : Text("tracking.objectiveDisplayed", "Displayed");
						const auto objectiveLabel = FormatText("tracking.objectiveHeader", "Objective {index} ({status})",
							{ { "index", std::to_string(objective.index) }, { "status", objectiveStatus } });
						ImGui::SetNextItemOpen(false, ImGuiCond_Once);
						if (ImGui::TreeNodeEx((objectiveLabel + "##Objective").c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
							ImGui::TextDisabled("%s", Text("tracking.objectiveText", "Full objective text").c_str());
							ImGui::TextWrapped("%s", objective.text.empty() ? Text("tracking.emptyObjective", "(no display text)").c_str() : objective.text.c_str());
							ImGui::TextDisabled("%s", FormatText("tracking.objectiveState", "State: {state}", { { "state", std::to_string(objective.state) } }).c_str());
							ImGui::TreePop();
						}
						ImGui::PopID();
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			};

			auto questMatchesDetection = [&requirements, &detectionQuery, manualProtectionOnly](const tracker::QuestInfo& quest) {
				if (QuestMatchesSearch(quest, detectionQuery)) {
					return true;
				}
				const auto prefix = "quest:" + std::to_string(quest.formIDValue) + ":";
				return std::any_of(requirements.begin(), requirements.end(), [&prefix, &detectionQuery, manualProtectionOnly](const auto& requirement) {
					return IsTrackingRequirementVisible(requirement, manualProtectionOnly) && requirement.automatic && requirement.key.rfind(prefix, 0) == 0 && RequirementMatchesSearch(requirement, detectionQuery);
				});
			};

			ImGui::SetNextItemOpen(false, ImGuiCond_Once);
			if (ImGui::CollapsingHeader((Text("tracking.detectedQuests", "Detected quests") + "##DetectedQuests").c_str())) {
				ImGui::TextDisabled("%s", FormatText("tracking.detectedQuestSummary", "{count} quests have unfinished or overridden ingredient matches.",
					{ { "count", std::to_string(detectedQuestIDs.size()) } }).c_str());
				if (detectedQuestIDs.empty()) {
					ImGui::TextDisabled("%s", Text("tracking.noDetectedQuests", "No quest objectives currently match a loaded ingredient.").c_str());
				} else {
					for (const auto& quest : quests) {
						if (detectedQuestIDs.contains(quest.formIDValue) && questMatchesDetection(quest)) {
							drawQuest(quest, "detected:");
						}
					}
				}
			}
			ImGui::SetNextItemOpen(false, ImGuiCond_Once);
			if (ImGui::CollapsingHeader((Text("tracking.quests", "All loaded quests") + "##AllLoadedQuests").c_str())) {
				ImGui::TextDisabled("%s", FormatText("tracking.questSummary", "{count} quests loaded; informational rows do not protect ingredients by themselves.",
					{ { "count", std::to_string(quests.size()) } }).c_str());
				ImGui::SameLine();
				ImGui::Checkbox(Text("tracking.onlyRunning", "Only running").c_str(), &trackingOnlyRunning);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(140.0f);
				const char* questGroupingItems = "No grouping\0By type\0By mod\0";
				ImGui::Combo("##TrackingQuestGroup", &trackingQuestGroupMode, questGroupingItems);
				std::size_t matchingQuestCount = 0;
				for (const auto& quest : quests) {
					matchingQuestCount += (!trackingOnlyRunning || quest.running) && questMatchesDetection(quest) ? 1 : 0;
				}
				if (!detectionQuery.empty()) {
					ImGui::TextDisabled("%s", FormatText("tracking.questSearchSummary", "Showing {shown} of {count} loaded quests.",
						{ { "shown", std::to_string(matchingQuestCount) }, { "count", std::to_string(quests.size()) } }).c_str());
				}
				if (quests.empty()) {
					ImGui::TextDisabled("%s", Text("tracking.noQuests", "No quest records are currently available from the game.").c_str());
				} else if (matchingQuestCount == 0) {
					ImGui::TextDisabled("%s", Text("tracking.noQuestMatches", "No loaded quests match the search filter.").c_str());
				}
				if (trackingQuestGroupMode == 0) {
					for (const auto& quest : quests) {
						if ((!trackingOnlyRunning || quest.running) && questMatchesDetection(quest)) {
							drawQuest(quest, "all:");
						}
					}
				} else {
					std::vector<std::string> groups;
					for (const auto& quest : quests) {
						if ((!trackingOnlyRunning || quest.running) && questMatchesDetection(quest)) {
							groups.push_back(trackingQuestGroupMode == 1 ? QuestTypeLabel(quest) : quest.modName.empty() ? Text("tracking.noneValue", "none") : quest.modName);
						}
					}
					std::sort(groups.begin(), groups.end());
					groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
					for (const auto& group : groups) {
						ImGui::SetNextItemOpen(false, ImGuiCond_Once);
						if (!ImGui::TreeNodeEx((group + "##QuestGroup").c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
							continue;
						}
						for (const auto& quest : quests) {
							const auto questGroup = trackingQuestGroupMode == 1 ? QuestTypeLabel(quest) : quest.modName.empty() ? Text("tracking.noneValue", "none") : quest.modName;
							if (questGroup == group && (!trackingOnlyRunning || quest.running) && questMatchesDetection(quest)) {
								drawQuest(quest, "group:");
							}
						}
						ImGui::TreePop();
					}
				}
			}
			textInputActive = textInputActive || ImGui::GetIO().WantTextInput;

			ImGui::SetNextItemOpen(false, ImGuiCond_Once);
			if (ImGui::CollapsingHeader((Text("tracking.questEditor", "Quest debugging") + "##QuestDebugging").c_str())) {
				const auto selectedQuest = std::find_if(quests.begin(), quests.end(), [](const auto& quest) {
					return quest.formIDValue == selectedTrackingQuestFormID;
				});
				if (selectedQuest == quests.end()) {
					ImGui::TextDisabled("%s", Text("tracking.noSelectedQuest", "Select a loaded quest above to inspect stages and control objectives.").c_str());
				} else {
				ImGui::TextWrapped("%s", FormatText("tracking.selectedQuest", "Selected: {title} [{formID}] | Current stage: {stage}",
					{ { "title", selectedQuest->title }, { "formID", selectedQuest->formID }, { "stage", std::to_string(selectedQuest->currentStage) } }).c_str());
				if (selectedQuest->stages.empty()) {
					ImGui::TextDisabled("%s", Text("tracking.noStages", "No stage data is available for this quest record.").c_str());
				} else {
					if (ImGui::BeginListBox("##TrackingStageList", ImVec2(230.0f, 140.0f))) {
						for (const auto& stage : selectedQuest->stages) {
							const bool current = stage.index == selectedQuest->currentStage;
							const auto stageLabel = std::to_string(stage.index) + (current ? " (" + Text("tracking.stageCurrent", "current") + ")" : stage.executed ? " (" + Text("tracking.stageExecuted", "executed") + ")" : " (" + Text("tracking.stageWaiting", "waiting") + ")") +
								(stage.startUp ? " [" + Text("tracking.stageStart", "start") + "]" : "") + (stage.shutDown ? " [" + Text("tracking.stageFinish", "finish") + "]" : "");
							if (ImGui::Selectable(stageLabel.c_str(), std::string_view(trackingStageInput) == std::to_string(stage.index))) {
								CopySettingText(trackingStageInput, std::to_string(stage.index));
							}
						}
						ImGui::EndListBox();
					}
				}
				ImGui::SetNextItemWidth(90.0f);
				ImGui::InputText("##TrackingStageInput", trackingStageInput, sizeof(trackingStageInput), ImGuiInputTextFlags_CharsDecimal);
				textInputActive = textInputActive || ImGui::IsItemActive();
				ImGui::SameLine();
				ImGui::Checkbox(Text("tracking.force", "Force").c_str(), &trackingStageForce);
				ImGui::SameLine();
				if (ImGui::Button((Text("tracking.setStage", "Set stage") + "##TrackingSetStage").c_str())) {
					try {
						std::size_t consumed = 0;
						const auto parsedStage = std::stoul(trackingStageInput, &consumed);
						if (parsedStage <= 0xFFFF && trackingStageInput[0] != '\0' && consumed == std::strlen(trackingStageInput)) {
							tracker::RequestQuestStage(selectedQuest->formIDValue, static_cast<std::uint16_t>(parsedStage), trackingStageForce);
							recalculate = true;
						}
					} catch (...) {}
				}
				ImGui::TextDisabled("%s", Text("tracking.stageWarning", "Stage regressions and stopped quests require Force; make a hard save before changing quest state.").c_str());
					ImGui::SetNextItemOpen(false, ImGuiCond_Once);
					if (ImGui::CollapsingHeader((Text("tracking.objectiveActions", "Objective actions") + "##ObjectiveActions").c_str())) {
						if (selectedQuest->objectives.empty()) {
							ImGui::TextDisabled("%s", Text("tracking.noObjectives", "No objectives are present in this quest record.").c_str());
						}
						for (const auto& objective : selectedQuest->objectives) {
					ImGui::PushID(static_cast<int>(objective.index));
					if (ImGui::SmallButton((Text("tracking.showObjective", "Show") + "##ShowObjective").c_str())) {
						tracker::RequestQuestObjective(selectedQuest->formIDValue, static_cast<std::uint16_t>(objective.index), tracker::ObjectiveAction::kShow);
						recalculate = true;
					}
					ImGui::SameLine();
					if (ImGui::SmallButton((Text("tracking.hideObjective", "Hide") + "##HideObjective").c_str())) {
						tracker::RequestQuestObjective(selectedQuest->formIDValue, static_cast<std::uint16_t>(objective.index), tracker::ObjectiveAction::kHide);
						recalculate = true;
					}
					ImGui::SameLine();
					if (ImGui::SmallButton((Text("tracking.completeObjective", "Done") + "##CompleteObjective").c_str())) {
						tracker::RequestQuestObjective(selectedQuest->formIDValue, static_cast<std::uint16_t>(objective.index), tracker::ObjectiveAction::kComplete);
						recalculate = true;
					}
					ImGui::SameLine();
					if (ImGui::SmallButton((Text("tracking.failObjective", "Fail") + "##FailObjective").c_str())) {
						tracker::RequestQuestObjective(selectedQuest->formIDValue, static_cast<std::uint16_t>(objective.index), tracker::ObjectiveAction::kFail);
						recalculate = true;
					}
					ImGui::SameLine();
					ImGui::TextWrapped("%s", objective.text.empty() ? Text("tracking.emptyObjective", "(no display text)").c_str() : objective.text.c_str());
					ImGui::PopID();
						}
					}
				}
				}
			}
			}
			const auto trackerStatus = tracker::GetStatus();
			if (!trackerStatus.empty()) {
				ImGui::TextWrapped("%s", trackerStatus.c_str());
			}

			ImGui::EndChild();
			bool ingredientSearchActive = false;
			if (openProtectedIngredientWindow) {
				bool pickerOpen = true;
				const auto displaySize = ImGui::GetIO().DisplaySize;
				const auto maximumPickerHeight = (std::max)(ImGui::GetFrameHeight() * 4.0f, displaySize.y - ImGui::GetFrameHeight() * 2.0f);
				ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
				ImGui::SetNextWindowSizeConstraints(ImVec2(360.0f, 0.0f), ImVec2(420.0f, maximumPickerHeight));
				const auto pickerTitle = Text("tracking.addProtected", "Add protected ingredient") + "###TrackingProtectedIngredientWindow";
				const auto& io = ImGui::GetIO();
				if (positionProtectedIngredientWindow) {
					const auto parentPosition = ImGui::GetWindowPos();
					const auto parentSize = ImGui::GetWindowSize();
					constexpr float pickerWidth = 420.0f;
					const auto maximumX = (std::max)(0.0f, io.DisplaySize.x - pickerWidth);
					const auto maximumY = (std::max)(0.0f, io.DisplaySize.y - ImGui::GetFrameHeight());
					const auto pickerX = (std::clamp)(parentPosition.x + (parentSize.x - pickerWidth) * 0.5f, 0.0f, maximumX);
					const auto pickerY = (std::clamp)(parentPosition.y + ImGui::GetFrameHeight(), 0.0f, maximumY);
					ImGui::SetNextWindowPos(ImVec2(pickerX, pickerY), ImGuiCond_Always);
				}
				const bool pickerBeginResult = ImGui::Begin(pickerTitle.c_str(), &pickerOpen, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoCollapse);
				const auto* pickerWindow = ImGui::GetCurrentWindow();
				const auto pickerWindowPosition = ImGui::GetWindowPos();
				const auto pickerWindowSize = ImGui::GetWindowSize();
				positionProtectedIngredientWindow = false;
			if (pickerBeginResult) {
				const bool focusRequested = focusProtectedIngredientSearch;
				if (focusRequested) {
					ImGui::SetKeyboardFocusHere();
					focusProtectedIngredientSearch = false;
				}
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::InputTextWithHint("##TrackingProtectedIngredientSearch", Text("tracking.protectedSearchHint", "Type an ingredient to protect...").c_str(), protectedIngredientSearch, sizeof(protectedIngredientSearch));
				ingredientSearchActive = ImGui::IsItemActive();
				const std::string searchQuery(protectedIngredientSearch);
				auto suggestions = GetIngredientNames();
				suggestions.erase(std::remove_if(suggestions.begin(), suggestions.end(), [&searchQuery](const auto& name) {
					return IngredientMatchScore(name, searchQuery) < 0 || std::any_of(protectedIngredients.begin(), protectedIngredients.end(), [&name](const auto& entry) {
						return entry.name == name;
					});
				}), suggestions.end());
				std::sort(suggestions.begin(), suggestions.end(), [&searchQuery](const auto& left, const auto& right) {
					const auto leftScore = IngredientMatchScore(left, searchQuery);
					const auto rightScore = IngredientMatchScore(right, searchQuery);
					return leftScore == rightScore ? left < right : leftScore < rightScore;
				});
				if (suggestions.empty()) {
					ImGui::TextDisabled("%s", Text("tracking.noAvailableIngredients", "No available ingredients match the search.").c_str());
				} else {
					for (const auto& suggestion : suggestions) {
						if (ImGui::Selectable(suggestion.c_str())) {
							AddProtectedIngredient(suggestion);
							protectedIngredientSearch[0] = '\0';
							ingredientSearchActive = false;
							recalculate = true;
							pickerOpen = false;
						}
					}
				}
			}
			ImGui::End();
			openProtectedIngredientWindow = pickerOpen;
			if (pickerOpen) {
				cursorOverWindow.store(true, std::memory_order_release);
				protectedIngredientWindowVisible = true;
				protectedIngredientWindowMin = pickerWindowPosition;
				protectedIngredientWindowMax = ImVec2(
					pickerWindowPosition.x + pickerWindowSize.x,
					pickerWindowPosition.y + pickerWindowSize.y);
			} else {
				protectedIngredientWindowVisible = false;
				protectedIngredientWindowMin = ImVec2(-1.0f, -1.0f);
				protectedIngredientWindowMax = ImVec2(-1.0f, -1.0f);
			}
			if (!pickerOpen) {
				focusProtectedIngredientSearch = false;
				ImGui::ClearActiveID();
			}
			}
			textInputActive = textInputActive || ingredientSearchActive;
			if (!selectedTrackingIngredient.empty()) {
				const bool detailsPopupRequested = openTrackingIngredientDetails;
				const auto detailsPopupLabel = Text("tracking.ingredientDetails", "Ingredient details") + "###TrackingIngredientDetails";
				if (detailsPopupRequested) {
					ImGui::OpenPopup(detailsPopupLabel.c_str());
					openTrackingIngredientDetails = false;
				}
				const bool detailsPopupIsOpen = ImGui::IsPopupOpen(detailsPopupLabel.c_str());
				bool detailsWindowOpen = true;
				if (detailsPopupIsOpen) {
					cursorOverWindow.store(true, std::memory_order_release);
				}
				const auto detailsDisplaySize = ImGui::GetIO().DisplaySize;
				const auto maximumDetailsHeight = (std::max)(ImGui::GetFrameHeight() * 6.0f, detailsDisplaySize.y - ImGui::GetFrameHeight() * 2.0f);
				ImGui::SetNextWindowSizeConstraints(ImVec2(420.0f, 0.0f), ImVec2(560.0f, maximumDetailsHeight));
				ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_FirstUseEver);
				const bool detailsPopupVisible = ImGui::BeginPopupModal(detailsPopupLabel.c_str(), &detailsWindowOpen, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_AlwaysVerticalScrollbar);
				if (detailsPopupVisible) {
					cursorOverWindow.store(true, std::memory_order_release);
					ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), "%s", selectedTrackingIngredient.c_str());
					ImGui::TextWrapped("%s", Text("tracking.ingredientDetailsDescription", "This popup shows every protection source for this ingredient. Changes are saved immediately and reduce the copies available to the potion calculator.").c_str());
					const auto protectedEntry = std::find_if(protectedIngredients.begin(), protectedIngredients.end(), [](const auto& entry) {
						return entry.name == selectedTrackingIngredient;
					});
					ImGui::SeparatorText(Text("tracking.staticProtection", "Static protection").c_str());
					ImGui::TextWrapped("%s", Text("tracking.staticProtectionDescription", "Static protection is a manual reservation for this ingredient. It applies independently of detected quests, recipes, and selected effects.").c_str());
					if (protectedEntry == protectedIngredients.end()) {
						ImGui::TextDisabled("%s", Text("tracking.noStaticProtection", "This ingredient has no static protection exception.").c_str());
						ImGui::TextWrapped("%s", Text("tracking.addStaticProtectionDescription", "Use this button to add a saved reservation before changing its quantity.").c_str());
						if (ImGui::Button((Text("tracking.addStaticProtection", "Protect this ingredient") + "##AddStaticProtection").c_str())) {
							AddProtectedIngredient(selectedTrackingIngredient);
							recalculate = true;
						}
					} else {
						const auto protectedIndex = static_cast<std::size_t>(std::distance(protectedIngredients.begin(), protectedEntry));
						ImGui::PushID("StaticProtection");
						bool protectAll = protectedEntry->count < 0;
						if (ImGui::Checkbox(Text("tracking.protectAllStatic", "Protect all static copies").c_str(), &protectAll)) {
							if (protectAll) {
								protectedEntry->previousCount = (std::max)(1, protectedEntry->count);
								protectedEntry->count = -1;
							} else {
								protectedEntry->count = (std::max)(1, protectedEntry->previousCount);
							}
							SaveProtectedIngredients();
							recalculate = true;
						}
						ImGui::TextWrapped("%s", Text("tracking.protectAllStaticDescription", "When checked, reserve every copy currently available. When unchecked, reserve only the finite quantity entered below.").c_str());
						if (!protectAll) {
							ImGui::SetNextItemWidth(100.0f);
							if (ImGui::InputInt(Text("tracking.staticQuantity", "Static quantity").c_str(), &protectedEntry->count, 1, 10)) {
								protectedEntry->count = (std::max)(1, protectedEntry->count);
								protectedEntry->previousCount = protectedEntry->count;
								SaveProtectedIngredients();
								recalculate = true;
							}
							ImGui::TextWrapped("%s", Text("tracking.staticQuantityDescription", "This is the number of copies reserved by the static protection entry; increasing it leaves fewer copies available for crafting.").c_str());
						}
						ImGui::TextWrapped("%s", Text("tracking.removeStaticProtectionDescription", "Remove the saved static reservation when this ingredient no longer needs a manual hold.").c_str());
						if (ImGui::Button((Text("tracking.removeStaticProtection", "Remove static protection") + "##RemoveStaticProtection").c_str())) {
							protectedIngredients.erase(protectedIngredients.begin() + static_cast<std::ptrdiff_t>(protectedIndex));
							SaveProtectedIngredients();
							recalculate = true;
						}
						ImGui::PopID();
					}

					ImGui::SeparatorText(Text("tracking.detectedSources", "Detected sources").c_str());
					ImGui::TextWrapped("%s", Text("tracking.detectedSourcesDescription", "These rows were found from loaded game records or entered as tracking requirements. Each row contributes its own reservation, and all unfinished rows are combined for this ingredient.").c_str());
					bool foundRequirement = false;
					for (const auto& original : requirements) {
						if (!IsTrackingRequirementVisible(original, manualProtectionOnly) || original.ingredient != selectedTrackingIngredient) {
							continue;
						}
						foundRequirement = true;
						auto requirement = original;
						ImGui::PushID(requirement.key.c_str());
						const bool manualOverride = requirement.automatic && requirement.overridden;
						const auto typeText = Text(manualOverride ? "tracking.manualOverrideLabel" : (requirement.automatic ? "tracking.detected" : "tracking.manualLabel"),
							manualOverride ? "Manual override" : (requirement.automatic ? "Detected" : "Manual"));
						ImGui::TextColored(manualOverride ? ImVec4(1.0f, 0.75f, 0.35f, 1.0f) : (requirement.automatic ? ImVec4(0.55f, 0.8f, 1.0f, 1.0f) : ImVec4(0.65f, 1.0f, 0.65f, 1.0f)), "%s", typeText.c_str());
						ImGui::SameLine();
						ImGui::TextWrapped("%s", requirement.source.empty() ? Text("tracking.unknownSource", "Unspecified source").c_str() : requirement.source.c_str());
						if (!requirement.detail.empty()) {
							ImGui::TextWrapped("%s", requirement.detail.c_str());
						}
						bool protectAll = requirement.count >= 999;
						if (ImGui::Checkbox((Text("tracking.protectAllDetection", "Protect all from this detection") + "##ProtectAllDetection").c_str(), &protectAll)) {
							if (protectAll) {
								requirement.previousCount = requirement.count < kUnlimitedProtectionCount ? (std::max)(1, requirement.count) : GetFiniteProtectionCount(requirement);
								requirement.count = kUnlimitedProtectionCount;
							} else {
								requirement.count = GetFiniteProtectionCount(requirement);
								requirement.previousCount = requirement.count;
							}
							tracker::UpdateRequirement(requirement);
							recalculate = true;
						}
						ImGui::TextWrapped("%s", Text("tracking.protectAllDetectionDescription", "Reserve every available copy for this source. Clear it to use a finite protected quantity instead.").c_str());
						if (!protectAll) {
							ImGui::SetNextItemWidth(100.0f);
							if (ImGui::InputInt((Text("tracking.protectQuantity", "Protected quantity") + "##ProtectQuantity").c_str(), &requirement.count, 1, 10)) {
								requirement.count = (std::clamp)(requirement.count, 1, 998);
								requirement.previousCount = requirement.count;
								tracker::UpdateRequirement(requirement);
								recalculate = true;
							}
							ImGui::TextWrapped("%s", Text("tracking.protectQuantityDescription", "The protected quantity for this source is combined with other active sources for the same ingredient.").c_str());
						}
						bool completed = requirement.completed;
						if (ImGui::Checkbox((Text("tracking.completed", "Completed") + "##IngredientDetectionCompleted").c_str(), &completed)) {
							requirement.completed = completed;
							requirement.completionOverridden = true;
							tracker::UpdateRequirement(requirement);
							recalculate = true;
						}
						ImGui::TextWrapped("%s", Text("tracking.completedDescription", "Mark this requirement completed when it is fulfilled. Completed rows no longer reserve ingredient copies.").c_str());
						if (!requirement.automatic) {
							if (ImGui::SmallButton((Text("tracking.remove", "Remove") + "##IngredientDetectionRemove").c_str())) {
								tracker::RemoveManual(requirement.key);
								recalculate = true;
							}
							ImGui::TextWrapped("%s", Text("tracking.removeDetectionDescription", "Remove deletes this manual tracking row and its reservation; automatically detected rows cannot be removed here.").c_str());
						}
						ImGui::Separator();
						ImGui::PopID();
					}
					if (!foundRequirement) {
						ImGui::TextDisabled("%s", Text("tracking.noIngredientDetections", "No automatic or manual detections currently reference this ingredient.").c_str());
					}
					ImGui::EndPopup();
				}
				if (!detailsWindowOpen) {
					selectedTrackingIngredient.clear();
					openTrackingIngredientDetails = false;
				}
			}
			searchInputFocused.store(textInputActive || ImGui::GetIO().WantTextInput, std::memory_order_release);
			return recalculate;
		}

		void DrawDeveloperSplit()
		{
			const auto contentSize = ImGui::GetContentRegionAvail();
			const auto panelSpacing = ImGui::GetStyle().ItemSpacing.x;
			const auto panelWidth = (std::max)(0.0f, (contentSize.x - panelSpacing) * 0.5f);
			bool leftTextInputActive = false;

			if (ImGui::BeginChild("AlchemyMainPane", ImVec2(panelWidth, 0.0f), true, ImGuiWindowFlags_NoScrollbar)) {
				if (settingsOpen) {
					if (DrawSettings()) {
						menu::RequestRecalculation(true);
					}
				} else {
					DrawRecipes();
				}
				leftTextInputActive = searchInputFocused.load(std::memory_order_acquire);
			}
			ImGui::EndChild();
			if (!developerTestHubOpen) {
				return;
			}

			ImGui::SameLine(0.0f, panelSpacing);
			if (ImGui::BeginChild("DeveloperTestHubPane", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
				const bool hubTextInputActive = devhub::Draw();
				if (!devhub::IsOpen()) {
					developerTestHubOpen = false;
				}
				searchInputFocused.store(leftTextInputActive || hubTextInputActive, std::memory_order_release);
			}
			ImGui::EndChild();
		}
	}

	void SetVisible(bool a_visible)
	{
		isWindowOpen.store(a_visible, std::memory_order_release);
	}

	void SetCursorPosition(float a_x, float a_y)
	{
		skyrimCursorPosition = ImVec2(a_x, a_y);
	}

	void SetLeftMouseButtonDown(bool a_down)
	{
		leftMouseButtonDown.store(a_down, std::memory_order_release);
	}

	void AddMouseWheel(float a_delta)
	{
		mouseWheelDelta.fetch_add(a_delta, std::memory_order_release);
	}

	void AddInputCharacter(std::uint32_t a_codePoint)
	{
		if (a_codePoint == 0 || a_codePoint > 0x10FFFF || (a_codePoint >= 0xD800 && a_codePoint <= 0xDFFF)) {
			return;
		}
		std::scoped_lock lock(pendingInputMutex);
		pendingInput.push_back(PendingInput{
			.type = PendingInputType::kCharacter,
			.value = a_codePoint
		});
	}

	void AddInputKey(std::uint32_t a_keyCode, bool a_pressed)
	{
		std::scoped_lock lock(pendingInputMutex);
		pendingInput.push_back(PendingInput{
			.type = PendingInputType::kKey,
			.value = a_keyCode,
			.pressed = a_pressed
		});
	}

	ImGuiKey DIKToImGuiKey(std::uint32_t a_keyCode)
	{
		switch (a_keyCode) {
		case 0x01: return ImGuiKey_Escape;
		case 0x02: return ImGuiKey_1;
		case 0x03: return ImGuiKey_2;
		case 0x04: return ImGuiKey_3;
		case 0x05: return ImGuiKey_4;
		case 0x06: return ImGuiKey_5;
		case 0x07: return ImGuiKey_6;
		case 0x08: return ImGuiKey_7;
		case 0x09: return ImGuiKey_8;
		case 0x0A: return ImGuiKey_9;
		case 0x0B: return ImGuiKey_0;
		case 0x0C: return ImGuiKey_Minus;
		case 0x0D: return ImGuiKey_Equal;
		case 0x0E: return ImGuiKey_Backspace;
		case 0x0F: return ImGuiKey_Tab;
		case 0x10: return ImGuiKey_Q;
		case 0x11: return ImGuiKey_W;
		case 0x12: return ImGuiKey_E;
		case 0x13: return ImGuiKey_R;
		case 0x14: return ImGuiKey_T;
		case 0x15: return ImGuiKey_Y;
		case 0x16: return ImGuiKey_U;
		case 0x17: return ImGuiKey_I;
		case 0x18: return ImGuiKey_O;
		case 0x19: return ImGuiKey_P;
		case 0x1A: return ImGuiKey_LeftBracket;
		case 0x1B: return ImGuiKey_RightBracket;
		case 0x1C: return ImGuiKey_Enter;
		case 0x1D: return ImGuiKey_LeftCtrl;
		case 0x1E: return ImGuiKey_A;
		case 0x1F: return ImGuiKey_S;
		case 0x20: return ImGuiKey_D;
		case 0x21: return ImGuiKey_F;
		case 0x22: return ImGuiKey_G;
		case 0x23: return ImGuiKey_H;
		case 0x24: return ImGuiKey_J;
		case 0x25: return ImGuiKey_K;
		case 0x26: return ImGuiKey_L;
		case 0x27: return ImGuiKey_Semicolon;
		case 0x28: return ImGuiKey_Apostrophe;
		case 0x29: return ImGuiKey_GraveAccent;
		case 0x2A: return ImGuiKey_LeftShift;
		case 0x2B: return ImGuiKey_Backslash;
		case 0x2C: return ImGuiKey_Z;
		case 0x2D: return ImGuiKey_X;
		case 0x2E: return ImGuiKey_C;
		case 0x2F: return ImGuiKey_V;
		case 0x30: return ImGuiKey_B;
		case 0x31: return ImGuiKey_N;
		case 0x32: return ImGuiKey_M;
		case 0x33: return ImGuiKey_Comma;
		case 0x34: return ImGuiKey_Period;
		case 0x35: return ImGuiKey_Slash;
		case 0x36: return ImGuiKey_RightShift;
		case 0x39: return ImGuiKey_Space;
		case 0x3A: return ImGuiKey_CapsLock;
		case 0x47: return ImGuiKey_Keypad7;
		case 0x48: return ImGuiKey_Keypad8;
		case 0x49: return ImGuiKey_Keypad9;
		case 0x4B: return ImGuiKey_Keypad4;
		case 0x4C: return ImGuiKey_Keypad5;
		case 0x4D: return ImGuiKey_Keypad6;
		case 0x4F: return ImGuiKey_Keypad1;
		case 0x50: return ImGuiKey_Keypad2;
		case 0x51: return ImGuiKey_Keypad3;
		case 0x52: return ImGuiKey_Keypad0;
		case 0x53: return ImGuiKey_KeypadDecimal;
		case 0x9C: return ImGuiKey_KeypadEnter;
		case 0x9D: return ImGuiKey_RightCtrl;
		case 0xC7: return ImGuiKey_Home;
		case 0xC8: return ImGuiKey_UpArrow;
		case 0xC9: return ImGuiKey_PageUp;
		case 0xCB: return ImGuiKey_LeftArrow;
		case 0xCD: return ImGuiKey_RightArrow;
		case 0xCF: return ImGuiKey_End;
		case 0xD0: return ImGuiKey_DownArrow;
		case 0xD1: return ImGuiKey_PageDown;
		case 0xD2: return ImGuiKey_Insert;
		case 0xD3: return ImGuiKey_Delete;
		default: return ImGuiKey_None;
		}
	}

	void UpdateImGuiMouseInput()
	{
		auto& io = ImGui::GetIO();
		if (!IsVisible()) {
			io.AddMousePosEvent(-1.0f, -1.0f);
			io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			mouseWheelDelta.store(0.0f, std::memory_order_release);
			leftMouseButtonDown.store(false, std::memory_order_release);
			return;
		}

		io.AddMousePosEvent(skyrimCursorPosition.x, skyrimCursorPosition.y);
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, leftMouseButtonDown.load(std::memory_order_acquire));
		io.AddMouseWheelEvent(0.0f, mouseWheelDelta.exchange(0.0f, std::memory_order_acq_rel));
	}

	void ResetInputState()
	{
		if (ImGui::GetCurrentContext()) {
			ImGui::ClearActiveID();
		}
		selectedRecipeIngredientDetails.clear();
		selectedTrackingIngredient.clear();
		mouseWheelDelta.store(0.0f, std::memory_order_release);
		protectedIngredientPopupInputCapture.store(false, std::memory_order_release);
		cursorOverWindow.store(false, std::memory_order_release);
		protectedIngredientWindowVisible = false;
		protectedIngredientWindowMin = ImVec2(-1.0f, -1.0f);
		protectedIngredientWindowMax = ImVec2(-1.0f, -1.0f);
		draggingWindow = false;
		resizingWindow = false;
		previousLeftMouseButtonDown = leftMouseButtonDown.load(std::memory_order_acquire);
		searchRectMin = ImVec2(-1.0f, -1.0f);
		searchRectMax = ImVec2(-1.0f, -1.0f);
		searchInputFocused.store(false, std::memory_order_release);
		focusSearch = false;
		{
			std::scoped_lock lock(pendingInputMutex);
			pendingInput.clear();
		}
	}

	void ProcessKeyboardInput()
	{
		const auto gameWindow = render::GetGameWindowHandle();
		if (!gameWindow || ::GetForegroundWindow() != gameWindow) {
			std::scoped_lock lock(pendingInputMutex);
			pendingInput.clear();
			return;
		}

		std::vector<PendingInput> input;
		{
			std::scoped_lock lock(pendingInputMutex);
			input.swap(pendingInput);
		}
		if (!IsVisible()) {
			return;
		}

		auto& io = ImGui::GetIO();
		for (const auto& pending : input) {
			if (pending.type == PendingInputType::kCharacter) {
				io.AddInputCharacter(pending.value);
				continue;
			}

			if (pending.value == 0x2A || pending.value == 0x36) {
				io.AddKeyEvent(ImGuiMod_Shift, pending.pressed);
			} else if (pending.value == 0x1D || pending.value == 0x9D) {
				io.AddKeyEvent(ImGuiMod_Ctrl, pending.pressed);
			} else if (pending.value == 0x38 || pending.value == 0xB8) {
				io.AddKeyEvent(ImGuiMod_Alt, pending.pressed);
			}
			if (const auto key = DIKToImGuiKey(pending.value); key != ImGuiKey_None) {
				io.AddKeyEvent(key, pending.pressed);
			}
		}
	}

	bool IsSearchInputFocused()
	{
		return searchInputFocused.load(std::memory_order_acquire) || (IsVisible() && ImGui::GetIO().WantTextInput);
	}

	void ClearSearchFocus()
	{
		focusSearch = false;
		focusProtectedIngredientSearch = false;
		searchInputFocused.store(false, std::memory_order_release);
		{
			std::scoped_lock lock(pendingInputMutex);
			pendingInput.clear();
		}
		if (ImGui::GetCurrentContext()) {
			ImGui::ClearActiveID();
		}
	}

	bool IsVisible()
	{
		return isWindowOpen.load(std::memory_order_acquire);
	}

	bool IsCursorOverWindow()
	{
		return cursorOverWindow.load(std::memory_order_acquire) || protectedIngredientPopupInputCapture.load(std::memory_order_acquire);
	}

	void DrawCursor()
	{
		if (!cursorOverWindow.load(std::memory_order_acquire)) {
			return;
		}

		const auto position = skyrimCursorPosition;
		auto* drawList = ImGui::GetForegroundDrawList();
		const auto displaySize = ImGui::GetIO().DisplaySize;
		const float cursorScale = (displaySize.y > 0.0f) ? std::clamp(displaySize.y / 1080.0f, 1.0f, 3.0f) : 1.0f;
		const float s = cursorScale;

		const ImVec2 vertices[] = {
			position,
			ImVec2(position.x + 1.0f * s, position.y + 19.0f * s),
			ImVec2(position.x + 5.5f * s, position.y + 14.5f * s),
			ImVec2(position.x + 10.0f * s, position.y + 22.0f * s),
			ImVec2(position.x + 13.5f * s, position.y + 20.0f * s),
			ImVec2(position.x + 9.0f * s, position.y + 12.5f * s),
			ImVec2(position.x + 15.0f * s, position.y + 11.0f * s)
		};

		const float shadowOffset = 1.5f * s;
		const ImVec2 shadowVertices[] = {
			ImVec2(vertices[0].x + shadowOffset, vertices[0].y + shadowOffset),
			ImVec2(vertices[1].x + shadowOffset, vertices[1].y + shadowOffset),
			ImVec2(vertices[2].x + shadowOffset, vertices[2].y + shadowOffset),
			ImVec2(vertices[3].x + shadowOffset, vertices[3].y + shadowOffset),
			ImVec2(vertices[4].x + shadowOffset, vertices[4].y + shadowOffset),
			ImVec2(vertices[5].x + shadowOffset, vertices[5].y + shadowOffset),
			ImVec2(vertices[6].x + shadowOffset, vertices[6].y + shadowOffset)
		};

		// 1. Drop shadow
		drawList->AddConvexPolyFilled(shadowVertices, IM_ARRAYSIZE(shadowVertices), IM_COL32(10, 10, 15, 140));

		// 2. Outer dark casing and metallic antique bronze/gold border
		const float borderWidth = (std::max)(1.5f, 2.0f * s);
		drawList->AddPolyline(vertices, IM_ARRAYSIZE(vertices), IM_COL32(20, 16, 12, 240), ImDrawFlags_Closed, borderWidth + 1.0f * s);
		drawList->AddPolyline(vertices, IM_ARRAYSIZE(vertices), IM_COL32(195, 165, 95, 255), ImDrawFlags_Closed, borderWidth);

		// 3. Polished pewter/silver interior blade fill
		drawList->AddConvexPolyFilled(vertices, IM_ARRAYSIZE(vertices), IM_COL32(235, 235, 240, 255));

		// 4. Subtle inner bevel highlight line along left edge
		drawList->AddLine(
			ImVec2(position.x + 1.5f * s, position.y + 3.0f * s),
			ImVec2(position.x + 2.0f * s, position.y + 16.0f * s),
			IM_COL32(255, 255, 255, 220),
			1.0f * s);
	}

	void DrawWindow()
	{
		if (!IsVisible()) {
			ResetInputState();
			return;
		}

		ApplyTheme();
		const auto displaySize = ImGui::GetIO().DisplaySize;
		if (!windowStateInitialized) {
			const auto maximumWidth = (std::max)(minimumWindowWidth, displaySize.x);
			const auto maximumHeight = (std::max)(minimumWindowHeight, displaySize.y);
			expandedWindowSize = ImVec2(
				(std::min)(maximumWidth, IsValidSavedSize(windowWidth.GetValue()) ? windowWidth.GetValue() : defaultWindowWidth),
				(std::min)(maximumHeight, IsValidSavedSize(windowHeight.GetValue()) ? windowHeight.GetValue() : defaultWindowHeight));
			const auto maximumX = (std::max)(0.0f, displaySize.x - expandedWindowSize.x);
			const auto maximumY = (std::max)(0.0f, displaySize.y - expandedWindowSize.y);
			const auto defaultX = (std::min)(maximumX, (std::max)(0.0f, displaySize.x - expandedWindowSize.x - screenMargin));
			const auto defaultY = (std::min)(maximumY, screenMargin);
			const auto position = ImVec2(
				(std::min)(maximumX, IsValidSavedValue(windowPositionX.GetValue()) ? windowPositionX.GetValue() : defaultX),
				(std::min)(maximumY, IsValidSavedValue(windowPositionY.GetValue()) ? windowPositionY.GetValue() : defaultY));
			ImGui::SetNextWindowPos(position, ImGuiCond_Always);
			ImGui::SetNextWindowSize(expandedWindowSize, ImGuiCond_Always);
			windowStateInitialized = true;
		}
		if (windowCollapsed) {
			ImGui::SetNextWindowSize(ImVec2(expandedWindowSize.x, ImGui::GetFrameHeight()), ImGuiCond_Always);
			windowSizeIsCollapsed = true;
		} else if (windowSizeIsCollapsed) {
			ImGui::SetNextWindowSize(expandedWindowSize, ImGuiCond_Always);
			windowSizeIsCollapsed = false;
		}
		const auto windowTitle = Text("window.title", "Prosperous Alchemist") + "##AlchemistWindow";
		const bool popupInputCapture = !windowCollapsed && trackingOpen &&
			(openProtectedIngredientWindow || !selectedTrackingIngredient.empty());
		protectedIngredientPopupInputCapture.store(popupInputCapture, std::memory_order_release);
		if (!ImGui::Begin(windowTitle.c_str(), nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar)) {
			const auto windowPosition = ImGui::GetWindowPos();
			const auto windowSize = ImGui::GetWindowSize();
			protectedIngredientWindowVisible = false;
			protectedIngredientWindowMin = ImVec2(-1.0f, -1.0f);
			protectedIngredientWindowMax = ImVec2(-1.0f, -1.0f);
			const bool cursorOverMainWindow =
				skyrimCursorPosition.x >= windowPosition.x && skyrimCursorPosition.x <= windowPosition.x + windowSize.x &&
				skyrimCursorPosition.y >= windowPosition.y && skyrimCursorPosition.y <= windowPosition.y + windowSize.y;
			const bool cursorOverProtectedWindow = IsCursorOverProtectedIngredientWindow(skyrimCursorPosition);
			const bool cursorCapture = popupInputCapture || cursorOverMainWindow || cursorOverProtectedWindow;
			cursorOverWindow.store(cursorCapture, std::memory_order_release);
			ImGui::End();
			return;
		}
		const auto windowPosition = ImGui::GetWindowPos();
		const auto windowSize = ImGui::GetWindowSize();
		const auto mousePosition = skyrimCursorPosition;
		const bool cursorOverMainWindow =
			mousePosition.x >= windowPosition.x && mousePosition.x <= windowPosition.x + windowSize.x &&
			mousePosition.y >= windowPosition.y && mousePosition.y <= windowPosition.y + windowSize.y;
		const bool cursorOverProtectedWindow = IsCursorOverProtectedIngredientWindow(mousePosition);
		const bool cursorCapture = popupInputCapture || cursorOverMainWindow || cursorOverProtectedWindow;
		cursorOverWindow.store(cursorCapture, std::memory_order_release);
		const auto leftButtonDown = leftMouseButtonDown.load(std::memory_order_acquire);
		if (settingsOpen || trackingOpen) {
			searchRectMin = ImVec2(-1.0f, -1.0f);
			searchRectMax = ImVec2(-1.0f, -1.0f);
		}
		const auto titleBarHeight = ImGui::GetFrameHeight();
		const auto buttonSize = (std::max)(14.0f, titleBarHeight - 6.0f);
		const auto buttonMargin = (titleBarHeight - buttonSize) * 0.5f;
		const auto toggleButtonMin = ImVec2(windowPosition.x + windowSize.x - buttonSize - buttonMargin, windowPosition.y + buttonMargin);
		const auto toggleButtonMax = ImVec2(toggleButtonMin.x + buttonSize, toggleButtonMin.y + buttonSize);
		const auto cursorOverToggleButton = mousePosition.x >= toggleButtonMin.x && mousePosition.x <= toggleButtonMax.x &&
			mousePosition.y >= toggleButtonMin.y && mousePosition.y <= toggleButtonMax.y;
		constexpr auto resizeGripSize = 18.0f;
		const auto resizeGripMin = ImVec2(windowPosition.x + windowSize.x - resizeGripSize, windowPosition.y + windowSize.y - resizeGripSize);
		const auto cursorOverResizeGrip = !windowCollapsed && mousePosition.x >= resizeGripMin.x && mousePosition.x <= windowPosition.x + windowSize.x &&
			mousePosition.y >= resizeGripMin.y && mousePosition.y <= windowPosition.y + windowSize.y;
		const auto cursorOverTitleBar = !cursorOverToggleButton && mousePosition.x >= windowPosition.x && mousePosition.x <= windowPosition.x + windowSize.x &&
			mousePosition.y >= windowPosition.y && mousePosition.y <= windowPosition.y + ImGui::GetFrameHeight();
		const auto leftButtonPressed = leftButtonDown && !previousLeftMouseButtonDown;
		const auto cursorOverSearch = mousePosition.x >= searchRectMin.x && mousePosition.x <= searchRectMax.x &&
			mousePosition.y >= searchRectMin.y && mousePosition.y <= searchRectMax.y;
		if (leftButtonPressed && cursorOverSearch) {
			focusSearch = true;
			searchInputFocused.store(true, std::memory_order_release);
		} else if (leftButtonPressed && !cursorOverWindow) {
			searchInputFocused.store(false, std::memory_order_release);
			ImGui::ClearActiveID();
		}
		if (leftButtonPressed && cursorOverToggleButton) {
			windowCollapsed = !windowCollapsed;
			draggingWindow = false;
			resizingWindow = false;
		} else if (leftButtonPressed && cursorOverResizeGrip) {
			resizingWindow = true;
			draggingWindow = false;
			resizeStartCursor = mousePosition;
			resizeStartSize = windowSize;
		} else if (!leftButtonDown) {
			draggingWindow = false;
			resizingWindow = false;
		} else if (!draggingWindow && !previousLeftMouseButtonDown && cursorOverTitleBar) {
			draggingWindow = true;
			dragOffset = ImVec2(mousePosition.x - windowPosition.x, mousePosition.y - windowPosition.y);
		}
		if (resizingWindow) {
			const auto maximumWindowWidth = (std::max)(minimumWindowWidth, displaySize.x - windowPosition.x);
			const auto maximumWindowHeight = (std::max)(minimumWindowHeight, displaySize.y - windowPosition.y);
			const auto width = (std::min)(maximumWindowWidth, (std::max)(minimumWindowWidth, resizeStartSize.x + mousePosition.x - resizeStartCursor.x));
			const auto height = (std::min)(maximumWindowHeight, (std::max)(minimumWindowHeight, resizeStartSize.y + mousePosition.y - resizeStartCursor.y));
			ImGui::SetWindowSize(ImVec2(width, height));
			expandedWindowSize = ImVec2(width, height);
		} else if (draggingWindow) {
			ImGui::SetWindowPos(ImVec2(mousePosition.x - dragOffset.x, mousePosition.y - dragOffset.y));
		}
		previousLeftMouseButtonDown = leftButtonDown;

		auto* drawList = ImGui::GetForegroundDrawList();
		const auto toggleColor = cursorOverToggleButton ? IM_COL32(255, 220, 120, 255) : IM_COL32(210, 180, 100, 255);
		drawList->AddRectFilled(toggleButtonMin, toggleButtonMax, IM_COL32(45, 32, 16, 255), 2.0f);
		drawList->AddRect(toggleButtonMin, toggleButtonMax, toggleColor, 2.0f, 0, 1.5f);
		const auto iconPadding = (std::max)(4.0f, buttonSize * 0.28f);
		drawList->AddLine(
			ImVec2(toggleButtonMin.x + iconPadding, toggleButtonMin.y + iconPadding),
			ImVec2(toggleButtonMax.x - iconPadding, toggleButtonMax.y - iconPadding),
			toggleColor, 1.8f);
		drawList->AddLine(
			ImVec2(toggleButtonMax.x - iconPadding, toggleButtonMin.y + iconPadding),
			ImVec2(toggleButtonMin.x + iconPadding, toggleButtonMax.y - iconPadding),
			toggleColor, 1.8f);
		SaveWindowState(!windowCollapsed && !windowSizeIsCollapsed);
		if (!windowCollapsed) {
			const auto resizeGripColor = cursorOverResizeGrip ? IM_COL32(255, 220, 120, 255) : IM_COL32(160, 130, 75, 255);
			const auto resizeGripMax = ImVec2(windowPosition.x + windowSize.x - 4.0f, windowPosition.y + windowSize.y - 4.0f);
			drawList->AddLine(ImVec2(resizeGripMax.x - 3.0f, resizeGripMax.y), ImVec2(resizeGripMax.x, resizeGripMax.y - 3.0f), resizeGripColor, 1.5f);
			drawList->AddLine(ImVec2(resizeGripMax.x - 8.0f, resizeGripMax.y), ImVec2(resizeGripMax.x, resizeGripMax.y - 8.0f), resizeGripColor, 1.5f);
			drawList->AddLine(ImVec2(resizeGripMax.x - 13.0f, resizeGripMax.y), ImVec2(resizeGripMax.x, resizeGripMax.y - 13.0f), resizeGripColor, 1.5f);
		}

		if (windowCollapsed) {
			ImGui::ClearActiveID();
			searchInputFocused.store(false, std::memory_order_release);
			protectedIngredientWindowVisible = false;
			protectedIngredientWindowMin = ImVec2(-1.0f, -1.0f);
			protectedIngredientWindowMax = ImVec2(-1.0f, -1.0f);
			searchRectMin = ImVec2(-1.0f, -1.0f);
			searchRectMax = ImVec2(-1.0f, -1.0f);
			ImGui::End();
			return;
		}

		if (trackingOpen) {
			if (DrawTracking()) {
				menu::RequestRecalculation(true);
			}
			ImGui::End();
			return;
		}

		if (developerTestHubOpen) {
			DrawDeveloperSplit();
			ImGui::End();
			return;
		}

		if (settingsOpen) {
			if (DrawSettings()) {
				menu::RequestRecalculation(true);
			}
			ImGui::End();
			return;
		}

		DrawRecipes();
		ImGui::End();
	}
}
