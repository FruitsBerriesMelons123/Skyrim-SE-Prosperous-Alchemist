#include "AlchemistWindow.h"
#include "AlchemistEngine.h"
#include "DeveloperTestHub.h"
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
#include <mutex>
#include <string_view>
#include <vector>

namespace alchemist::ui {
	namespace {
		constexpr auto defaultWindowWidth = 620.0f;
		constexpr auto defaultWindowHeight = 600.0f;
		constexpr auto minimumWindowWidth = 360.0f;
		constexpr auto minimumWindowHeight = 220.0f;
		constexpr auto screenMargin = 40.0f;

		REX::INI::F32<> windowPositionX("Window", "PositionX", -1.0f);
		REX::INI::F32<> windowPositionY("Window", "PositionY", -1.0f);
		REX::INI::F32<> windowWidth("Window", "Width", -1.0f);
		REX::INI::F32<> windowHeight("Window", "Height", -1.0f);

		std::atomic_bool isWindowOpen = false;
		std::atomic_bool leftMouseButtonDown = false;
		std::atomic<float> mouseWheelDelta = 0.0f;
		std::array<bool, 256> previousKeyboardState{};
		std::chrono::steady_clock::time_point backspaceRepeatAt;
		std::atomic_bool cursorOverWindow = false;
		std::mutex pendingTextInputMutex;
		std::vector<std::uint32_t> pendingTextInput;
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
		std::vector<ProtectedIngredientEntry> protectedIngredients;
		char protectedIngredientSearch[512]{};

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

	ImVec2 skyrimCursorPosition(-1.0f, -1.0f);

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

		void LoadProtectedIngredients();

		void LoadSettingsBuffers()
		{
			LoadProtectedIngredients();

			const auto prefixes = getPotionPrefixes();
			CopySettingText(potionPrefix, prefixes.potion);
			CopySettingText(poisonPrefix, prefixes.poison);
			settingsBuffersInitialized = true;
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
			const float settingsWidth = ImGui::CalcTextSize(settingsText.c_str()).x + style.FramePadding.x * 2.0f;
			const float sortWidth = ImGui::CalcTextSize(currentSortLabel.c_str()).x + style.FramePadding.x * 2.0f + ImGui::GetFrameHeight();
			const float effectsCheckboxWidth = ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(effectsText.c_str()).x;
			float searchWidth = ImGui::GetContentRegionAvail().x - settingsWidth - sortWidth - effectsCheckboxWidth - style.ItemSpacing.x * 3.0f;
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
			if (ImGui::Button((settingsText + "##Settings").c_str())) {
				settingsOpen = true;
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
						"Recalculation complete! - 100%",
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
				kProtectIngredients.SetValue(kProtectIngredients.GetValueDefault());
				kSinglethreaded.SetValue(kSinglethreaded.GetValueDefault());
				kProtectedIngredients.SetValue(kDefaultProtectedIngredients);
				kPotionPoison.SetValue(kPotionPoison.GetValueDefault());
				kCacheDurationSeconds.SetValue(kCacheDurationSeconds.GetValueDefault());
				kStaleRecalculateThresholdMs.SetValue(kStaleRecalculateThresholdMs.GetValueDefault());
				kCraftDebounceMs.SetValue(kCraftDebounceMs.GetValueDefault());
				kFilterPotionsBySelectedIngredients.SetValue(kFilterPotionsBySelectedIngredients.GetValueDefault());
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

			ImGui::SeparatorText(Text("settings.ingredientProtection", "Ingredient protection").c_str());
			bool protectIngredients = kProtectIngredients.GetValue() != 0;
			if (ImGui::Checkbox(Text("settings.protectIngredients", "Protect ingredients").c_str(), &protectIngredients)) {
				kProtectIngredients.SetValue(protectIngredients ? 1 : 0);
				SaveSettings();
				recalculate = true;
			}
			ImGui::TextDisabled("%s", Text("settings.protectIngredientsDescription", "Ingredients are protected only while this option is checked.").c_str());
			if (ImGui::Button((Text("settings.addProtected", "Add protected ingredient") + "##AddProtectedIngredient").c_str())) {
				protectedIngredientSearch[0] = '\0';
				focusProtectedIngredientSearch = true;
				const auto buttonRectMin = ImGui::GetItemRectMin();
				const auto buttonRectMax = ImGui::GetItemRectMax();
				const auto popupWidth = (std::max)(360.0f, buttonRectMax.x - buttonRectMin.x);
				const auto popupHeight = ImGui::GetTextLineHeightWithSpacing() * 9.0f + ImGui::GetStyle().WindowPadding.y * 2.0f;
				ImGui::SetNextWindowPos(ImVec2(buttonRectMin.x, buttonRectMax.y), ImGuiCond_Always);
				ImGui::SetNextWindowSize(ImVec2(popupWidth, popupHeight), ImGuiCond_Always);
				ImGui::OpenPopup("ProtectedIngredientSuggestions");
			}
			bool ingredientSearchActive = false;
			if (ImGui::BeginPopup("ProtectedIngredientSuggestions")) {
				if (focusProtectedIngredientSearch) {
					ImGui::SetKeyboardFocusHere();
					focusProtectedIngredientSearch = false;
				}
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::InputTextWithHint("##ProtectedIngredientSearch", Text("settings.protectedSearchHint", "Type an ingredient to protect...").c_str(), protectedIngredientSearch, sizeof(protectedIngredientSearch));
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
					ImGui::TextDisabled("%s", Text("settings.noAvailableIngredients", "No available ingredients match the search.").c_str());
				} else {
					for (const auto& suggestion : suggestions) {
						if (ImGui::Selectable(suggestion.c_str())) {
							AddProtectedIngredient(suggestion);
							protectedIngredientSearch[0] = '\0';
							ingredientSearchActive = false;
							recalculate = true;
							ImGui::CloseCurrentPopup();
						}
					}
				}
				ImGui::EndPopup();
			}
			textInputActive = textInputActive || ingredientSearchActive;

			if (ImGui::Button((Text("settings.clearProtected", "Clear all protected ingredients") + "##ClearProtected").c_str())) {
				protectedIngredients.clear();
				SaveProtectedIngredients();
				recalculate = true;
			}
			ImGui::SameLine();
			if (ImGui::Button((Text("settings.restoreProtected", "Restore default protected ingredients") + "##RestoreProtected").c_str())) {
				kProtectedIngredients.SetValue(kDefaultProtectedIngredients);
				LoadProtectedIngredients();
				SaveSettings();
				recalculate = true;
			}
			if (protectedIngredients.empty()) {
				ImGui::TextDisabled("%s", Text("settings.noProtected", "No protected ingredients added.").c_str());
			}
			for (std::size_t index = 0; index < protectedIngredients.size();) {
				auto& entry = protectedIngredients[index];
				ImGui::PushID(static_cast<int>(index));
				ImGui::TextUnformatted(entry.name.c_str());
				ImGui::SameLine(300.0f);
				bool protectAll = entry.count < 0;
				const auto protectionLabel = Text(protectAll ? "settings.protectingAll" : "settings.protecting", protectAll ? "Protecting all" : "Protecting");
				if (ImGui::Checkbox((protectionLabel + "##ProtectionMode").c_str(), &protectAll)) {
					if (protectAll) {
						entry.previousCount = (std::max)(1, entry.count);
						entry.count = -1;
					} else {
						entry.count = (std::max)(1, entry.previousCount);
					}
					SaveProtectedIngredients();
					recalculate = true;
				}
				if (!protectAll) {
					ImGui::SameLine();
					ImGui::SetNextItemWidth(90.0f);
					if (ImGui::InputInt("##ProtectedCount", &entry.count, 1, 10)) {
						entry.count = (std::max)(1, entry.count);
						entry.previousCount = entry.count;
						SaveProtectedIngredients();
						recalculate = true;
					}
				}
				ImGui::SameLine();
				if (ImGui::SmallButton((Text("settings.remove", "Remove") + "##RemoveProtected").c_str())) {
					protectedIngredients.erase(protectedIngredients.begin() + static_cast<std::ptrdiff_t>(index));
					SaveProtectedIngredients();
					recalculate = true;
					ImGui::PopID();
					continue;
				}
				ImGui::PopID();
				++index;
			}

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
		std::scoped_lock lock(pendingTextInputMutex);
		pendingTextInput.push_back(a_codePoint);
	}

	void UpdateImGuiMouseInput()
	{
		auto& io = ImGui::GetIO();
		if (!IsVisible()) {
			io.AddMousePosEvent(-1.0f, -1.0f);
			io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			mouseWheelDelta.store(0.0f, std::memory_order_release);
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
		mouseWheelDelta.store(0.0f, std::memory_order_release);
		cursorOverWindow.store(false, std::memory_order_release);
		draggingWindow = false;
		resizingWindow = false;
		previousLeftMouseButtonDown = leftMouseButtonDown.load(std::memory_order_acquire);
		previousKeyboardState.fill(false);
		backspaceRepeatAt = std::chrono::steady_clock::time_point{};
		searchRectMin = ImVec2(-1.0f, -1.0f);
		searchRectMax = ImVec2(-1.0f, -1.0f);
		searchInputFocused.store(false, std::memory_order_release);
		focusSearch = false;
		std::scoped_lock lock(pendingTextInputMutex);
		pendingTextInput.clear();
	}

	void ProcessKeyboardInput()
	{
		const auto gameWindow = render::GetGameWindowHandle();
		if (!gameWindow || ::GetForegroundWindow() != gameWindow) {
			previousKeyboardState.fill(false);
			return;
		}

		std::array<bool, 256> keyboardState{};
		for (int key = 0; key < 256; ++key) {
			keyboardState[key] = (GetAsyncKeyState(key) & 0x8000) != 0;
		}

		const bool hasTextInputFocus = searchInputFocused.load(std::memory_order_acquire) || ImGui::GetIO().WantTextInput;
		if (IsVisible() && hasTextInputFocus) {
			auto& io = ImGui::GetIO();
			std::vector<std::uint32_t> textInput;
			{
				std::scoped_lock lock(pendingTextInputMutex);
				textInput.swap(pendingTextInput);
			}
			for (const auto codePoint : textInput) {
				io.AddInputCharacter(codePoint);
			}
			const auto currentTime = std::chrono::steady_clock::now();
			const auto backspaceDown = keyboardState[VK_BACK];
			if (backspaceDown) {
				if (!previousKeyboardState[VK_BACK]) {
					io.AddKeyEvent(ImGuiKey_Backspace, true);
					io.AddKeyEvent(ImGuiKey_Backspace, false);
					backspaceRepeatAt = currentTime + std::chrono::milliseconds(400);
				} else if (backspaceRepeatAt != std::chrono::steady_clock::time_point{} && currentTime >= backspaceRepeatAt) {
					io.AddKeyEvent(ImGuiKey_Backspace, true);
					io.AddKeyEvent(ImGuiKey_Backspace, false);
					backspaceRepeatAt = currentTime + std::chrono::milliseconds(50);
				}
			} else {
				backspaceRepeatAt = std::chrono::steady_clock::time_point{};
			}
			for (int key = 0; key < 256; ++key) {
				if (key == VK_BACK || !keyboardState[key] || previousKeyboardState[key]) {
					continue;
				}
				if (key == VK_BACK) {
					io.AddKeyEvent(ImGuiKey_Backspace, true);
					io.AddKeyEvent(ImGuiKey_Backspace, false);
					continue;
				}
				if (key == VK_RETURN) {
					io.AddKeyEvent(ImGuiKey_Enter, true);
					io.AddKeyEvent(ImGuiKey_Enter, false);
					io.AddKeyEvent(ImGuiKey_KeypadEnter, true);
					io.AddKeyEvent(ImGuiKey_KeypadEnter, false);
					continue;
				}
				if (key == VK_DELETE) {
					io.AddKeyEvent(ImGuiKey_Delete, true);
					io.AddKeyEvent(ImGuiKey_Delete, false);
					continue;
				}
			}
		}
		previousKeyboardState = keyboardState;
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
			std::scoped_lock lock(pendingTextInputMutex);
			pendingTextInput.clear();
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
		return cursorOverWindow.load(std::memory_order_acquire);
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
		if (!ImGui::Begin(windowTitle.c_str(), nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar)) {
			const auto windowPosition = ImGui::GetWindowPos();
			const auto windowSize = ImGui::GetWindowSize();
			cursorOverWindow.store(
				skyrimCursorPosition.x >= windowPosition.x && skyrimCursorPosition.x <= windowPosition.x + windowSize.x &&
					skyrimCursorPosition.y >= windowPosition.y && skyrimCursorPosition.y <= windowPosition.y + windowSize.y,
				std::memory_order_release);
			ImGui::End();
			return;
		}
		const auto windowPosition = ImGui::GetWindowPos();
		const auto windowSize = ImGui::GetWindowSize();
		const auto mousePosition = skyrimCursorPosition;
		cursorOverWindow.store(
			mousePosition.x >= windowPosition.x && mousePosition.x <= windowPosition.x + windowSize.x &&
				mousePosition.y >= windowPosition.y && mousePosition.y <= windowPosition.y + windowSize.y,
			std::memory_order_release);
		const auto leftButtonDown = leftMouseButtonDown.load(std::memory_order_acquire);
		if (settingsOpen) {
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
			searchRectMin = ImVec2(-1.0f, -1.0f);
			searchRectMax = ImVec2(-1.0f, -1.0f);
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
