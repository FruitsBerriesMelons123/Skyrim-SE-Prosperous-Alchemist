#include "AlchemistWindow.h"
#include "AlchemistEngine.h"
#include "DeveloperTestHub.h"
#include "MenuHandler.h"
#include "RenderHook.h"
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
#include <string_view>

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
		char searchText[128]{};
		int sortMode = 0;
		bool settingsOpen = false;
		bool developerTestHubOpen = false;
		bool settingsBuffersInitialized = false;
		bool focusProtectedIngredientSearch = false;
		char potionPrefix[128]{};
		char poisonPrefix[128]{};
		struct ProtectedIngredientEntry {
			std::string name;
			int count = -1;
			int previousCount = 1;
		};
		std::vector<ProtectedIngredientEntry> protectedIngredients;
		char protectedIngredientSearch[128]{};

		bool ContainsInsensitive(std::string_view value, std::string_view query)
		{
			if (query.empty()) {
				return true;
			}
			if (query.size() > value.size()) {
				return false;
			}

			for (std::size_t i = 0; i <= value.size() - query.size(); ++i) {
				bool match = true;
				for (std::size_t j = 0; j < query.size(); ++j) {
					const auto left = static_cast<unsigned char>(value[i + j]);
					const auto right = static_cast<unsigned char>(query[j]);
					if (std::tolower(left) != std::tolower(right)) {
						match = false;
						break;
					}
				}
				if (match) {
					return true;
				}
			}
			return false;
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
			std::strncpy(a_buffer, a_value.c_str(), Size - 1);
			a_buffer[Size - 1] = '\0';
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
			std::string name(a_name);
			std::string query(a_query);
			std::transform(name.begin(), name.end(), name.begin(), [](unsigned char character) {
				return static_cast<char>(std::tolower(character));
			});
			std::transform(query.begin(), query.end(), query.begin(), [](unsigned char character) {
				return static_cast<char>(std::tolower(character));
			});
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
			if (ImGui::Button("Test")) {
				ToggleDeveloperTestHub();
			}
		}

		void DrawRecipes()
		{
			const bool developerEnabled = kDeveloper.GetValue() == 1;
			ImGui::SetNextItemWidth(-1.0f);
			if (focusSearch) {
				ImGui::SetKeyboardFocusHere();
				focusSearch = false;
			}
			ImGui::InputTextWithHint("##RecipeSearch", "Search recipes or ingredients", searchText, sizeof(searchText));
			searchInputFocused.store(ImGui::IsItemActive(), std::memory_order_release);
			searchRectMin = ImGui::GetItemRectMin();
			searchRectMax = ImGui::GetItemRectMax();
			if (developerEnabled) {
				DrawDeveloperToggle();
				ImGui::SameLine();
			}
			if (ImGui::Button("Settings")) {
				settingsOpen = true;
				settingsBuffersInitialized = false;
				searchInputFocused.store(false, std::memory_order_release);
				ImGui::ClearActiveID();
			}
			if (developerTestHubOpen) {
				ImGui::NewLine();
			} else {
				ImGui::SameLine();
			}
			ImGui::SetNextItemWidth(developerTestHubOpen ? -1.0f : 145.0f);
			ImGui::Combo("##RecipeSort", &sortMode, "Value (highest)\0Name (A-Z)\0");

			auto recipes = engine::GetCachedRecipes();
			const std::string_view query(searchText);
			recipes.erase(std::remove_if(recipes.begin(), recipes.end(), [query](const engine::RecipeResult& recipe) {
				return !ContainsInsensitive(recipe.name, query) && !ContainsInsensitive(recipe.ingredients, query) &&
					!ContainsInsensitive(recipe.effects, query);
			}), recipes.end());
			if (sortMode == 1) {
				std::sort(recipes.begin(), recipes.end(), [](const auto& left, const auto& right) {
					if (left.name != right.name) {
						return left.name < right.name;
					}
					if (left.calculatedValue != right.calculatedValue) {
						return left.calculatedValue > right.calculatedValue;
					}
					return left.ingredients < right.ingredients;
				});
			} else {
				std::sort(recipes.begin(), recipes.end(), [](const auto& left, const auto& right) {
					if (left.calculatedValue != right.calculatedValue) {
						return left.calculatedValue > right.calculatedValue;
					}
					if (left.name != right.name) {
						return left.name < right.name;
					}
					return left.ingredients < right.ingredients;
				});
			}

			if (ImGui::BeginTable("RecipeTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
				ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()))) {
				ImGui::TableSetupColumn("Recipe", ImGuiTableColumnFlags_WidthFixed, 170.0f);
				ImGui::TableSetupColumn("Ingredients", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Value (Gold)", ImGuiTableColumnFlags_WidthFixed, 90.0f);
				ImGui::TableSetupColumn("Effects", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();

				for (std::size_t index = 0; index < recipes.size(); ++index) {
					const auto& recipe = recipes[index];
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					if (recipe.isBest) {
						TextColoredWrappedInCell(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), recipe.name.c_str());
					} else {
						TextWrappedInCell(recipe.name.c_str());
					}
					ImGui::TableSetColumnIndex(1);
					TextWrappedInCell(recipe.ingredients.c_str());
					ImGui::TableSetColumnIndex(2);
					ImGui::Text("%d", recipe.displayedValue);
					ImGui::TableSetColumnIndex(3);
					TextWrappedInCell(recipe.effects.c_str());
				}
				ImGui::EndTable();
			}
		}

		bool DrawSettings()
		{
			if (!settingsBuffersInitialized) {
				LoadSettingsBuffers();
			}

			bool recalculate = false;
			bool textInputActive = false;
			if (ImGui::Button("< Back to recipes")) {
				settingsOpen = false;
				settingsBuffersInitialized = false;
				searchInputFocused.store(false, std::memory_order_release);
				ImGui::ClearActiveID();
			}
			ImGui::SameLine();
			if (ImGui::Button("Reset all settings")) {
				kIgnorePlayer.SetValue(kIgnorePlayer.GetValueDefault());
				kProtectIngredients.SetValue(kProtectIngredients.GetValueDefault());
				kSinglethreaded.SetValue(kSinglethreaded.GetValueDefault());
				kNumberOfIngredientsToStressTest.SetValue(kNumberOfIngredientsToStressTest.GetValueDefault());
				kProtectedIngredients.SetValue(kDefaultProtectedIngredients);
				kPotionPoison.SetValue(kPotionPoison.GetValueDefault());
				LoadSettingsBuffers();
				SaveSettings();
				recalculate = true;
			}

			ImGui::Separator();
			ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), "Settings");
			ImGui::TextDisabled("Changes are saved to alchemist.ini automatically.");
			ImGui::BeginChild("SettingsScroll", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
			ImGui::SeparatorText("Calculation");

			bool usePlayerStats = kIgnorePlayer.GetValue() == 0;
			if (ImGui::Checkbox("Use player's Alchemy stats", &usePlayerStats)) {
				kIgnorePlayer.SetValue(usePlayerStats ? 0 : 1);
				SaveSettings();
				recalculate = true;
			}
			ImGui::TextDisabled("Includes Alchemy skill, perks, and worn Fortify Alchemy equipment.");

			bool useSingleThreadedCalculation = kSinglethreaded.GetValue() != 0;
			if (ImGui::Checkbox("Use single-threaded calculation", &useSingleThreadedCalculation)) {
				kSinglethreaded.SetValue(useSingleThreadedCalculation ? 1 : 0);
				SaveSettings();
				recalculate = true;
			}
			ImGui::TextDisabled("Disable this option to use the multithreaded calculation path.");

			ImGui::SeparatorText("Ingredient protection");
			bool protectIngredients = kProtectIngredients.GetValue() != 0;
			if (ImGui::Checkbox("Protect ingredients", &protectIngredients)) {
				kProtectIngredients.SetValue(protectIngredients ? 1 : 0);
				SaveSettings();
				recalculate = true;
			}
			ImGui::TextDisabled("Ingredients are protected only while this option is checked.");
			if (ImGui::Button("Add protected ingredient")) {
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
				ImGui::InputTextWithHint("##ProtectedIngredientSearch", "Type an ingredient to protect...", protectedIngredientSearch, sizeof(protectedIngredientSearch));
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
					ImGui::TextDisabled("No available ingredients match the search.");
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

			if (ImGui::Button("Clear all protected ingredients")) {
				protectedIngredients.clear();
				SaveProtectedIngredients();
				recalculate = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Restore default protected ingredients")) {
				kProtectedIngredients.SetValue(kDefaultProtectedIngredients);
				LoadProtectedIngredients();
				SaveSettings();
				recalculate = true;
			}
			if (protectedIngredients.empty()) {
				ImGui::TextDisabled("No protected ingredients added.");
			}
			for (std::size_t index = 0; index < protectedIngredients.size();) {
				auto& entry = protectedIngredients[index];
				ImGui::PushID(static_cast<int>(index));
				ImGui::TextUnformatted(entry.name.c_str());
				ImGui::SameLine(300.0f);
				bool protectAll = entry.count < 0;
				const char* protectionLabel = protectAll ? "Protecting all" : "Protecting";
				if (ImGui::Checkbox(protectionLabel, &protectAll)) {
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
				if (ImGui::SmallButton("Remove")) {
					protectedIngredients.erase(protectedIngredients.begin() + static_cast<std::ptrdiff_t>(index));
					SaveProtectedIngredients();
					recalculate = true;
					ImGui::PopID();
					continue;
				}
				ImGui::PopID();
				++index;
			}

			ImGui::SeparatorText("Naming");
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::InputText("Potion prefix", potionPrefix, sizeof(potionPrefix))) {
				kPotionPoison.SetValue(std::string(potionPrefix) + "," + poisonPrefix);
				SaveSettings();
			}
			textInputActive = textInputActive || ImGui::IsItemActive();
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				recalculate = true;
			}

			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::InputText("Poison prefix", poisonPrefix, sizeof(poisonPrefix))) {
				kPotionPoison.SetValue(std::string(potionPrefix) + "," + poisonPrefix);
				SaveSettings();
			}
			textInputActive = textInputActive || ImGui::IsItemActive();
			ImGui::TextDisabled("Prefixes are applied to beneficial potion and harmful poison names.");
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				recalculate = true;
			}

			ImGui::SeparatorText("Advanced diagnostics");
			int stressTestCount = kNumberOfIngredientsToStressTest.GetValue();
			if (ImGui::InputInt("Stress-test ingredient count", &stressTestCount)) {
				kNumberOfIngredientsToStressTest.SetValue(stressTestCount);
				SaveSettings();
				recalculate = true;
			}
			ImGui::TextDisabled("0 disables diagnostics, a positive value runs a stress test, and negative values behave like 0.");

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

			if (ImGui::BeginChild("AlchemyMainPane", ImVec2(panelWidth, 0.0f), true)) {
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
			BYTE nativeKeyboardState[256]{};
			GetKeyboardState(nativeKeyboardState);
			for (const auto key : { VK_SHIFT, VK_LSHIFT, VK_RSHIFT, VK_CONTROL, VK_LCONTROL, VK_RCONTROL, VK_MENU, VK_LMENU, VK_RMENU }) {
				nativeKeyboardState[key] = keyboardState[key] ? 0x80 : 0;
			}
			const auto keyboardLayout = GetKeyboardLayout(0);
			auto& io = ImGui::GetIO();
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
				if (key == VK_DELETE) {
					io.AddKeyEvent(ImGuiKey_Delete, true);
					io.AddKeyEvent(ImGuiKey_Delete, false);
					continue;
				}
				const auto scanCode = MapVirtualKeyW(static_cast<UINT>(key), MAPVK_VK_TO_VSC);
				WCHAR characters[4]{};
				const auto characterCount = ToUnicodeEx(static_cast<UINT>(key), scanCode, nativeKeyboardState,
					characters, static_cast<int>(std::size(characters)), 0, keyboardLayout);
				if (characterCount > 0) {
					for (int index = 0; index < characterCount; ++index) {
						io.AddInputCharacter(static_cast<unsigned int>(characters[index]));
					}
				}
			}
		}
		previousKeyboardState = keyboardState;
	}

	bool IsSearchInputFocused()
	{
		return searchInputFocused.load(std::memory_order_acquire) || (IsVisible() && ImGui::GetIO().WantTextInput);
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
		if (!ImGui::Begin("Prosperous Alchemist", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
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
