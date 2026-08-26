#include "DeveloperTestHub.h"

#include "AlchemistEngine.h"
#include "main.h"
#include "MenuHandler.h"

#include <RE/A/ActorEquipManager.h>
#include <Windows.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <functional>
#include <iomanip>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <set>
#include <string_view>
#include <utility>

namespace alchemist::devhub {
	namespace {
		constexpr std::array<std::string_view, 5> kManagedPerks = { "Alchemist", "Physician", "Benefactor", "Poisoner", "Purity" };
		constexpr std::string_view kSeekerOfShadows = "Seeker of Shadows";
		constexpr int kMaxPerkApplyAttempts = 4;

		struct State {
			bool open = false;
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
			std::map<RE::TESBoundObject*, int> provisioned;
			std::map<std::string, int> pendingPerkRanks;
			std::set<RE::TESObjectARMO*> managedWornGear;
			std::recursive_mutex mutex;
		};

		State state;
		std::array<int, 18> fixtureQuantities = { 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 };

		void DrawTestHint(const char* a_description)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
			ImGui::TextWrapped("%s", a_description);
			ImGui::PopStyleColor();
		}

		void TextWrappedInCell(const char* a_text)
		{
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + (std::max)(0.0f, ImGui::GetContentRegionAvail().x));
			ImGui::TextWrapped("%s", a_text);
			ImGui::PopTextWrapPos();
		}

		bool SelectableWrappedInCell(const char* a_text, bool a_selected)
		{
			const float width = (std::max)(0.0f, ImGui::GetContentRegionAvail().x);
			const float height = (std::max)(ImGui::GetTextLineHeight(), ImGui::CalcTextSize(a_text, nullptr, false, width).y);
			const ImVec2 textPosition = ImGui::GetCursorScreenPos();
			const bool clicked = ImGui::InvisibleButton("##select", ImVec2(width, height));
			const bool hovered = ImGui::IsItemHovered();
			if (a_selected || hovered) {
				const auto color = ImGui::GetColorU32(a_selected ? ImGuiCol_Header : ImGuiCol_HeaderHovered);
				ImGui::GetWindowDrawList()->AddRectFilled(textPosition, ImVec2(textPosition.x + width, textPosition.y + height), color);
			}
			ImGui::SetCursorScreenPos(textPosition);
			TextWrappedInCell(a_text);
			return clicked;
		}

		std::vector<FixtureDefinition> BuildFixtures()
		{
			return {
				{ "Blue Mountain Flower", "Blue Mountain Flower", 3, -1, "beneficial, mixed-effect, two-ingredient, three-ingredient", "Baseline beneficial ingredient." },
				{ "Wheat", "Wheat", 3, -1, "beneficial, two-ingredient, three-ingredient", "Baseline beneficial ingredient." },
				{ "Deathbell", "Deathbell", 3, -1, "poison, two-ingredient", "Baseline poison ingredient." },
				{ "Imp Stool", "Imp Stool", 3, -1, "poison, mixed-effect, two-ingredient", "Poison and mixed-effect fixture." },
				{ "Giant's Toe", "Giant's Toe", 3, -1, "high-value, three-ingredient", "High-value effect contribution." },
				{ "Snowberries", "Snowberries", 3, -1, "Fortify Enchanting, mixed-effect", "Fortify Enchanting ingredient fixture." },
				{ "Blue Butterfly Wing", "Blue Butterfly Wing", 3, -1, "Fortify Enchanting, mixed-effect", "Fortify Enchanting ingredient fixture." },
				{ "Hagraven Claw", "Hagraven Claw", 3, -1, "Fortify Enchanting, Resist Magic", "Fortify Enchanting ingredient fixture." },
				{ "Spriggan Sap", "Spriggan Sap", 3, -1, "Fortify Enchanting, Fortify Smithing", "Fortify Enchanting ingredient fixture." },
				{ "Salmon Roe", "Salmon Roe", 3, -1, "Waterbreathing, high-value duration multiplier", "Highest base-value ingredient." },
				{ "Garlic", "Garlic", 3, -1, "Regenerate Magicka/Health, high-value combo", "Pairs with Salmon Roe." },
				{ "Nordic Barnacle", "Nordic Barnacle", 3, -1, "Waterbreathing, high-value combo", "Pairs with Salmon Roe/Garlic." },
				{ "Jarrin Root", "Jarrin Root", 3, -1, "Damage Health x200 magnitude", "Extreme poison magnitude fixture." },
				{ "Creep Cluster", "Creep Cluster", 3, -1, "5-effect gold recipe combo", "Standard gold crafting loop fixture." },
				{ "Mora Tapinella", "Mora Tapinella", 3, -1, "5-effect gold recipe combo", "Standard gold crafting loop fixture." },
				{ "Scaly Pholiota", "Scaly Pholiota", 3, -1, "5-effect gold recipe combo", "Standard gold crafting loop fixture." },
				{ "Canis Root", "Canis Root", 3, -1, "Paralysis, Fortify Marksman/One-Handed", "Paralysis poison fixture." },
				{ "Swamp Fungal Pod", "Swamp Fungal Pod", 3, -1, "Paralysis, Lingering Damage Magicka", "Paralysis poison fixture." }
			};
		}

		int PerkRank(RE::PlayerCharacter* a_player, RE::BGSPerk* a_perk)
		{
			if (!a_player || !a_perk) {
				return 0;
			}
			for (const auto* entry : a_player->GetPlayerRuntimeData().addedPerks) {
				if (entry && entry->perk == a_perk) {
					return entry->currentRank;
				}
			}
			return 0;
		}

		std::string FormName(const RE::TESForm* a_form)
		{
			const auto* name = a_form ? a_form->GetName() : nullptr;
			return name ? name : "";
		}

		int InventoryCount(RE::PlayerCharacter* a_player, RE::TESBoundObject* a_form)
		{
			if (!a_player || !a_form) {
				return 0;
			}
			auto inventory = a_player->GetInventory();
			const auto found = inventory.find(a_form);
			return found == inventory.end() ? 0 : found->second.first;
		}

		bool IsFortifyAlchemy(RE::EnchantmentItem* a_enchantment)
		{
			if (!a_enchantment) {
				return false;
			}
			for (const auto* effect : a_enchantment->effects) {
				if (effect::isFortifyAlchemy(effect)) {
					return true;
				}
			}
			return false;
		}

		bool IsFortifyAlchemy(RE::InventoryEntryData* a_entry)
		{
			return a_entry && IsFortifyAlchemy(a_entry->GetEnchantment());
		}

		bool IsFortifyEnchanting(const RE::AlchemyItem* a_item)
		{
			if (!a_item) {
				return false;
			}
			for (const auto* effect : a_item->effects) {
				if (effect && effect->baseEffect &&
					(effect->baseEffect->data.primaryAV == RE::ActorValue::kEnchanting ||
					 effect::getName(effect) == "Fortify Enchanting")) {
					return true;
				}
			}
			return false;
		}

		bool IsFortifyAlchemyPotion(const RE::AlchemyItem* a_item)
		{
			if (!a_item) {
				return false;
			}
			for (const auto* effect : a_item->effects) {
				if (effect && effect::isFortifyAlchemy(effect)) {
					return true;
				}
			}
			return false;
		}

		bool EquipAlchemyGear(RE::PlayerCharacter* a_player, RE::TESObjectARMO* a_armor)
		{
			if (!a_player || !a_armor || a_armor->IsDeleted()) {
				return false;
			}
			auto* equipManager = RE::ActorEquipManager::GetSingleton();
			if (!equipManager) {
				return false;
			}
			equipManager->EquipObject(a_player, a_armor, nullptr, 1, nullptr, true, true, false, true);
			const auto inventory = a_player->GetInventory();
			const auto found = inventory.find(a_armor);
			return found != inventory.end() && found->second.second && found->second.second->IsWorn();
		}

		bool UnequipAlchemyGear(RE::PlayerCharacter* a_player, RE::TESObjectARMO* a_armor)
		{
			if (!a_player || !a_armor || a_armor->IsDeleted()) {
				return false;
			}
			auto* equipManager = RE::ActorEquipManager::GetSingleton();
			return equipManager && equipManager->UnequipObject(a_player, a_armor, nullptr, 1, nullptr, true, true, false, true);
		}

		std::string EditorID(const RE::TESForm* a_form)
		{
			if (!a_form) {
				return "unavailable";
			}
			const auto* editorId = a_form->GetFormEditorID();
			return editorId && *editorId ? editorId : "unavailable";
		}

		template <class T>
		T* FindFormByName(const std::string& a_name)
		{
			auto* dataHandler = RE::TESDataHandler::GetSingleton();
			if (!dataHandler) {
				return nullptr;
			}
			for (auto* form : dataHandler->GetFormArray<T>()) {
				if (form && FormName(form) == a_name) {
					return form;
				}
			}
			return nullptr;
		}

		std::vector<RE::BGSPerk*> FindPerks(const std::string& a_name)
		{
			std::vector<RE::BGSPerk*> result;
			auto* dataHandler = RE::TESDataHandler::GetSingleton();
			if (!dataHandler) {
				return result;
			}
			for (auto* perk : dataHandler->GetFormArray<RE::BGSPerk>()) {
				if (perk && FormName(perk) == a_name) {
					result.push_back(perk);
				}
			}
			return result;
		}

		std::vector<RE::TESObjectARMO*> FindAlchemyGearForms()
		{
			std::vector<RE::TESObjectARMO*> result;
			auto* dataHandler = RE::TESDataHandler::GetSingleton();
			if (!dataHandler) {
				return result;
			}
			for (auto* armor : dataHandler->GetFormArray<RE::TESObjectARMO>()) {
				if (armor && !armor->IsDeleted() && IsFortifyAlchemy(armor->formEnchanting)) {
					result.push_back(armor);
				}
			}
			std::sort(result.begin(), result.end(), [](const auto* left, const auto* right) {
				const auto leftName = FormName(left);
				const auto rightName = FormName(right);
				return leftName == rightName ? left->GetFormID() < right->GetFormID() : leftName < rightName;
			});
			return result;
		}

		std::vector<RE::AlchemyItem*> FindFortifyEnchantingPotionForms()
		{
			std::vector<RE::AlchemyItem*> result;
			auto* dataHandler = RE::TESDataHandler::GetSingleton();
			if (!dataHandler) {
				return result;
			}
			for (auto* item : dataHandler->GetFormArray<RE::AlchemyItem>()) {
				if (item && !item->IsDeleted() && IsFortifyEnchanting(item)) {
					result.push_back(item);
				}
			}
			std::sort(result.begin(), result.end(), [](const auto* left, const auto* right) {
				const auto leftName = FormName(left);
				const auto rightName = FormName(right);
				return leftName == rightName ? left->GetFormID() < right->GetFormID() : leftName < rightName;
			});
			return result;
		}

		std::vector<RE::AlchemyItem*> FindFortifyAlchemyPotionForms()
		{
			std::vector<RE::AlchemyItem*> result;
			auto* dataHandler = RE::TESDataHandler::GetSingleton();
			if (!dataHandler) {
				return result;
			}
			for (auto* item : dataHandler->GetFormArray<RE::AlchemyItem>()) {
				if (item && !item->IsDeleted() && IsFortifyAlchemyPotion(item)) {
					result.push_back(item);
				}
			}
			std::sort(result.begin(), result.end(), [](const auto* left, const auto* right) {
				const auto leftName = FormName(left);
				const auto rightName = FormName(right);
				return leftName == rightName ? left->GetFormID() < right->GetFormID() : leftName < rightName;
			});
			return result;
		}

		bool IsProvisionedAlchemyGear(RE::TESObjectARMO* a_armor)
		{
			return a_armor && state.provisioned.contains(a_armor);
		}

		bool HasSpell(RE::PlayerCharacter* a_player, RE::SpellItem* a_spell)
		{
			if (!a_player || !a_spell) {
				return false;
			}
			return std::find(a_player->GetActorRuntimeData().addedSpells.begin(), a_player->GetActorRuntimeData().addedSpells.end(), a_spell) != a_player->GetActorRuntimeData().addedSpells.end();
		}

		void CaptureActiveState(ActiveState& a_active);
		void RefreshInventoryOnGameThread(bool a_updateMessage);
		void RecalculateAndRefresh();
		std::string FormID(std::uint32_t a_formId);

		bool HasActiveSpell(RE::PlayerCharacter* a_player, RE::SpellItem* a_spell)
		{
			if (!a_player || !a_spell) {
				return false;
			}
			auto* magicTarget = a_player->GetMagicTarget();
			auto* activeEffects = magicTarget ? magicTarget->GetActiveEffectList() : nullptr;
			if (!activeEffects) {
				return false;
			}
			return std::any_of(activeEffects->begin(), activeEffects->end(), [a_spell](const auto* effect) {
				return effect && effect->spell == a_spell && effect->flags.none(RE::ActiveEffect::Flag::kInactive, RE::ActiveEffect::Flag::kDispelled);
			});
		}

		bool ApplyPerkRanks(RE::PlayerCharacter* a_player, const std::vector<RE::BGSPerk*>& a_perks, int a_rank)
		{
			if (!a_player || a_perks.empty() || a_rank < 0) {
				return false;
			}
			for (auto* perk : a_perks) {
				int current = PerkRank(a_player, perk);
				for (int attempts = 0; current > 0 && attempts < 32; ++attempts) {
					a_player->RemovePerk(perk);
					current = PerkRank(a_player, perk);
				}
				if (current > 0) {
					return false;
				}
			}
			if (a_rank > 0) {
				const std::size_t targetIndex = (std::min)(static_cast<std::size_t>(a_rank - 1), a_perks.size() - 1);
				a_player->AddPerk(a_perks[targetIndex], 1);
				a_player->CheckTempModifiers();
				return PerkRank(a_player, a_perks[targetIndex]) > 0;
			}
			a_player->CheckTempModifiers();
			return true;
		}

		bool SetPerkRank(RE::PlayerCharacter* a_player, RE::BGSPerk* a_perk, int a_rank)
		{
			return ApplyPerkRanks(a_player, { a_perk }, a_rank);
		}

		bool DispelSpellEffects(RE::PlayerCharacter* a_player, RE::SpellItem* a_spell)
		{
			if (!a_player || !a_spell || !HasActiveSpell(a_player, a_spell)) {
				return true;
			}
			auto* magicTarget = a_player->GetMagicTarget();
			RE::BSPointerHandle<RE::Actor> caster(a_player);
			return magicTarget && magicTarget->DispelEffect(a_spell, caster);
		}

		bool ApplySeekerState(RE::PlayerCharacter* a_player, RE::SpellItem* a_spell, RE::BGSPerk* a_perk, RE::TESGlobal* a_global,
			bool a_spellListed, bool a_spellActive, int a_perkRank, float a_globalValue)
		{
			if (!a_player || !a_spell || !a_perk || !a_global || a_spell->GetSpellType() != RE::MagicSystem::SpellType::kAbility || a_perkRank < 0) {
				return false;
			}

			bool success = true;
			const bool listedBefore = HasSpell(a_player, a_spell);
			if (a_spellListed) {
				if (!listedBefore) {
					success = a_player->AddSpell(a_spell) && success;
				}
			} else {
				success = DispelSpellEffects(a_player, a_spell) && success;
				if (listedBefore) {
					success = a_player->RemoveSpell(a_spell) && success;
				}
			}
			if (a_spellListed && !a_spellActive) {
				success = DispelSpellEffects(a_player, a_spell) && success;
			}
			success = SetPerkRank(a_player, a_perk, a_perkRank) && success;
			a_global->value = a_globalValue;
			if (a_spellListed && a_spellActive && !HasActiveSpell(a_player, a_spell)) {
				a_player->CastPermanentMagic(false, false, false, true);
			}
			a_player->CheckTempModifiers();

			const bool listedAfter = HasSpell(a_player, a_spell);
			const bool activeAfter = HasActiveSpell(a_player, a_spell);
			const int perkRankAfter = PerkRank(a_player, a_perk);
			const bool globalAfter = a_global->value == a_globalValue;
			return success && listedAfter == a_spellListed && activeAfter == (a_spellListed && a_spellActive) && perkRankAfter == a_perkRank && globalAfter;
		}

		std::vector<RE::TESObjectARMO*> GetAlchemyGear(RE::PlayerCharacter* a_player)
		{
			std::vector<RE::TESObjectARMO*> result;
			if (!a_player) {
				return result;
			}
			for (const auto& [form, entry] : a_player->GetInventory()) {
				auto* armor = form && form->Is(RE::FormType::Armor) ? static_cast<RE::TESObjectARMO*>(form) : nullptr;
				if (armor && entry.first > 0 && entry.second && IsFortifyAlchemy(entry.second.get()) && IsProvisionedAlchemyGear(armor)) {
					result.push_back(armor);
				}
			}
			return result;
		}

		bool ClearAlchemyGear(RE::PlayerCharacter* a_player)
		{
			if (!a_player) {
				return false;
			}
			for (const auto& [form, entry] : a_player->GetInventory()) {
				auto* armor = form && form->Is(RE::FormType::Armor) ? static_cast<RE::TESObjectARMO*>(form) : nullptr;
				if (armor && entry.second && entry.second->IsWorn() && IsFortifyAlchemy(entry.second.get()) && state.managedWornGear.contains(armor)) {
					UnequipAlchemyGear(a_player, armor);
				}
			}
			for (const auto& [form, entry] : a_player->GetInventory()) {
				auto* armor = form && form->Is(RE::FormType::Armor) ? static_cast<RE::TESObjectARMO*>(form) : nullptr;
				if (armor && entry.second && entry.second->IsWorn() && IsFortifyAlchemy(entry.second.get()) && state.managedWornGear.contains(armor)) {
					return false;
				}
			}
			return true;
		}

		std::string Escape(std::string a_value)
		{
			std::string result;
			result.reserve(a_value.size());
			for (const auto character : a_value) {
				switch (character) {
				case '\\': result += "\\\\"; break;
				case '\n': result += "\\n"; break;
				case '\r': result += "\\r"; break;
				case '=': result += "\\="; break;
				case ';': result += "\\;"; break;
				case ',': result += "\\,"; break;
				default: result += character; break;
				}
			}
			return result;
		}

		std::string Join(const std::vector<std::string>& a_values)
		{
			std::ostringstream result;
			for (std::size_t index = 0; index < a_values.size(); ++index) {
				if (index > 0) {
					result << "; ";
				}
				result << Escape(a_values[index]);
			}
			return result.str().empty() ? "none" : result.str();
		}

		std::string FormID(std::uint32_t a_formId)
		{
			if (a_formId == 0) {
				return "unavailable";
			}
			std::ostringstream result;
			result << "0x" << std::uppercase << std::hex << a_formId;
			return result.str();
		}

		std::string OptionalInt(bool a_available, int a_value)
		{
			return a_available ? std::to_string(a_value) : "unavailable";
		}

		std::string OptionalFloat(bool a_available, float a_value)
		{
			if (!a_available) {
				return "unavailable";
			}
			std::ostringstream result;
			result << std::setprecision(9) << a_value;
			return result.str();
		}

		std::string AlgorithmName(EvaluationAlgorithm a_algorithm)
		{
			switch (a_algorithm) {
			case EvaluationAlgorithm::Vanilla: return "vanilla";
			case EvaluationAlgorithm::AlchemyPlus: return "alchemy_plus";
			case EvaluationAlgorithm::CACO: return "caco";
			default: return "automatic";
			}
		}

		void SetMessage(const std::string& a_message, bool a_busy = false)
		{
			std::scoped_lock lock(state.mutex);
			state.message = a_message;
			state.busy = a_busy;
		}

		bool QueueTask(std::function<void()> a_task, const std::string& a_description)
		{
			{
				std::scoped_lock lock(state.mutex);
				if (!state.initialized && a_description != "initialize") {
					state.message = "The player state is not initialized yet.";
					return false;
				}
				if (state.busy) {
					return false;
				}
				state.busy = true;
				state.message = a_description;
			}
			if (const auto* taskInterface = SKSE::GetTaskInterface()) {
				taskInterface->AddTask([task = std::move(a_task), description = a_description]() {
					try {
						task();
					} catch (const std::exception&) {
						SetMessage("Developer test task failed: " + description);
					} catch (...) {
						SetMessage("Developer test task failed: " + description);
					}
					{
						std::scoped_lock lock(state.mutex);
						state.busy = false;
					}
				});
				return true;
			} else {
				SetMessage("Skyrim's task interface is unavailable; no game state was changed.");
				return false;
			}
		}

		void ApplyPerkOnGameThread(const std::string& a_name, int a_rank, int a_attempt)
		{
			std::scoped_lock lock(state.mutex);
			const auto pending = state.pendingPerkRanks.find(a_name);
			if (pending == state.pendingPerkRanks.end() || pending->second != a_rank) {
				return;
			}
			auto clearPending = [&]() {
				const auto pending = state.pendingPerkRanks.find(a_name);
				if (pending != state.pendingPerkRanks.end() && pending->second == a_rank) {
					state.pendingPerkRanks.erase(pending);
				}
			};
			auto perks = FindPerks(a_name);
			if (perks.empty()) {
				state.message = "Perk form unavailable: " + a_name;
				clearPending();
				state.busy = false;
				return;
			}
			auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			if (!playerCharacter) {
				state.message = "Player character unavailable.";
				clearPending();
				state.busy = false;
				return;
			}
			const bool applied = ApplyPerkRanks(playerCharacter, perks, a_rank);
			player.init();
			player.setState();
			if (!applied && a_attempt + 1 < kMaxPerkApplyAttempts) {
				if (const auto* taskInterface = SKSE::GetTaskInterface()) {
					state.message = "Waiting to verify " + a_name + " rank=" + std::to_string(a_rank) + "...";
					taskInterface->AddUITask([a_name, a_rank, a_attempt]() {
						{
							std::scoped_lock lock(state.mutex);
							const auto pending = state.pendingPerkRanks.find(a_name);
							if (pending == state.pendingPerkRanks.end() || pending->second != a_rank) {
								return;
							}
						}
						if (const auto* taskInterface = SKSE::GetTaskInterface()) {
							taskInterface->AddTask([a_name, a_rank, a_attempt]() {
								ApplyPerkOnGameThread(a_name, a_rank, a_attempt + 1);
							});
						} else {
							std::scoped_lock lock(state.mutex);
							const auto pending = state.pendingPerkRanks.find(a_name);
							if (pending != state.pendingPerkRanks.end() && pending->second == a_rank) {
								state.pendingPerkRanks.erase(pending);
								state.message = "Could not verify " + a_name + " rank=" + std::to_string(a_rank) + "; Skyrim's task interface became unavailable.";
								state.busy = false;
							}
						}
					});
					return;
				}
			}
			state.message = applied ? "Applied " + a_name + " rank=" + std::to_string(a_rank) : "Could not apply " + a_name + " rank=" + std::to_string(a_rank) + ".";
			RecalculateAndRefresh();
			CaptureActiveState(state.activeState);
			clearPending();
			state.busy = false;
		}

		void CaptureActiveState(ActiveState& a_active)
		{
			a_active = {};
			auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			if (!playerCharacter) {
				return;
			}
			a_active.available = true;
			const auto& info = playerCharacter->GetInfoRuntimeData();
			if (info.skills && info.skills->data) {
				a_active.alchemySkill = static_cast<int>(info.skills->data->skills[RE::PlayerCharacter::PlayerSkills::Data::Skills::kAlchemy].level);
			}
			if (auto* actorValueOwner = playerCharacter->AsActorValueOwner()) {
				a_active.effectiveAlchemy = actorValueOwner->GetActorValue(RE::ActorValue::kAlchemy);
			}
			player.captureAlchemyEvaluationContext();
			a_active.alchemyEvaluationContext = player.alchemyEvaluationContext;
			for (const auto perkName : kManagedPerks) {
				const auto perks = FindPerks(std::string(perkName));
				int rank = 0;
				for (std::size_t index = 0; index < perks.size(); ++index) {
					if (PerkRank(playerCharacter, perks[index]) > 0) {
						rank = perks.size() > 1 ? static_cast<int>(index + 1) : PerkRank(playerCharacter, perks[index]);
					}
				}
				if (rank > 0) {
					a_active.perks.push_back(std::string(perkName) + (rank > 1 ? " (rank " + std::to_string(rank) + ")" : ""));
				}
			}
			const auto& seekerState = player.alchemyEvaluationContext.seeker;
			a_active.seekerSpellAvailable = seekerState.spell != nullptr;
			a_active.seekerSpellFormId = seekerState.spell ? seekerState.spell->GetFormID() : 0;
			a_active.seekerSpellListed = seekerState.spellListed;
			a_active.seekerSpellActive = seekerState.spellActive;
			a_active.seekerPerkAvailable = seekerState.perk != nullptr;
			a_active.seekerPerkFormId = seekerState.perk ? seekerState.perk->GetFormID() : 0;
			a_active.seekerPerkRank = seekerState.perkRank;
			a_active.seekerRewardGlobalAvailable = seekerState.rewardGlobalAvailable;
			a_active.seekerRewardGlobalFormId = seekerState.rewardGlobal ? seekerState.rewardGlobal->GetFormID() : 0;
			a_active.seekerRewardGlobalValue = seekerState.rewardGlobalValue;
			a_active.seekerNativeContract = seekerState.nativeContract;
			if (seekerState.spellActive) {
				a_active.spells.emplace_back(kSeekerOfShadows);
			}
			for (const auto& [form, entry] : playerCharacter->GetInventory()) {
				if (form && entry.second && entry.second->IsWorn() && IsFortifyAlchemy(entry.second.get())) {
					std::ostringstream description;
					description << FormName(form) << " [" << FormID(form->GetFormID()) << "]";
					if (const auto* enchantment = entry.second->GetEnchantment()) {
						for (const auto* effect : enchantment->effects) {
							if (effect::isFortifyAlchemy(effect)) {
								description << " magnitude=" << std::setprecision(6) << effect::getMagnitude(effect);
							}
						}
					}
					a_active.gear.push_back(description.str());
				}
			}
		}

		std::string Effects(const RE::AlchemyItem* a_item)
		{
			if (!a_item) {
				return "unavailable";
			}
			std::ostringstream result;
			for (const auto* effect : a_item->effects) {
				if (!effect) {
					continue;
				}
				if (result.tellp() > 0) {
					result << "; ";
				}
				result << effect::getName(effect) << " magnitude=" << std::setprecision(6) << effect::getMagnitude(effect) << " duration=" << effect::getDuration(effect);
			}
			return result.tellp() > 0 ? result.str() : "unavailable";
		}

		bool HasCompleteComparisonEvidence(const ComparisonRecord& a_record)
		{
			return a_record.potionFormId != 0 &&
				a_record.hasCurrentInventoryValue &&
				a_record.hasEnteredPreCraftValue &&
				a_record.hasPredictedRawValue && std::isfinite(a_record.predictedRawValue) &&
				a_record.hasPredictedDisplayedValue &&
				a_record.recipeIngredients != "unavailable" &&
				a_record.recipeIngredientDetails != "unavailable" &&
				a_record.predictionDetails != "unavailable" &&
				a_record.creationAlchemySkill >= 0 &&
				std::isfinite(a_record.creationEffectiveAlchemy) && a_record.creationEffectiveAlchemy >= 0.0f;
		}

		void CaptureCreationState(ComparisonRecord& a_record, const ActiveState& a_active)
		{
			if (a_active.alchemySkill >= 0) {
				a_record.creationAlchemySkill = a_active.alchemySkill;
			}
			if (std::isfinite(a_active.effectiveAlchemy) && a_active.effectiveAlchemy >= 0.0f) {
				a_record.creationEffectiveAlchemy = a_active.effectiveAlchemy;
			}
			a_record.creationAlchemyEvaluationContext = a_active.alchemyEvaluationContext;
		}

		void Recompute(ComparisonRecord& a_record);
		void CopyActiveToRecord(ComparisonRecord& a_record, const ActiveState& a_active);

		void RefreshInventoryOnGameThread(bool a_updateMessage = true)
		{
			auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			if (!playerCharacter) {
				state.message = "Player character unavailable; inventory is unavailable.";
				state.busy = false;
				return;
			}
			std::vector<InventoryItem> refreshed;
			for (const auto& [form, entry] : playerCharacter->GetInventory()) {
				auto* alchemyItem = form && form->Is(RE::FormType::AlchemyItem) ? static_cast<RE::AlchemyItem*>(form) : nullptr;
				if (!alchemyItem || !entry.second || entry.first <= 0) {
					continue;
				}
				InventoryItem item;
				item.formId = form->GetFormID();
				item.editorId = EditorID(form);
				item.name = FormName(form);
				item.type = alchemyItem->IsPoison() ? "Poison" : "Potion";
				item.count = entry.first;
				item.value = entry.second->GetValue();
				item.costOverride = alchemyItem->data.costOverride;
				item.effects = Effects(alchemyItem);
				refreshed.push_back(std::move(item));
			}
			std::sort(refreshed.begin(), refreshed.end(), [](const auto& left, const auto& right) {
				return left.name == right.name ? left.formId < right.formId : left.name < right.name;
			});
			state.inventory = std::move(refreshed);
			for (auto& fixture : state.fixtures) {
				auto* ingredient = FindFormByName<RE::IngredientItem>(fixture.formName);
				fixture.inventoryCount = InventoryCount(playerCharacter, ingredient);
			}
			const auto selected = std::find_if(state.inventory.begin(), state.inventory.end(), [](const auto& item) { return item.formId == state.selectedFormId; });
			if (state.selectedFormId != 0 && selected == state.inventory.end()) {
				state.selectionUnavailable = true;
				if (state.hasCurrent) {
					state.current.hasCurrentInventoryValue = false;
					state.current.status = "Unavailable";
				}
			} else if (selected != state.inventory.end()) {
				state.selectionUnavailable = false;
				if (state.hasCurrent) {
					state.current.potionName = selected->name;
					state.current.potionEditorId = selected->editorId;
					state.current.potionType = selected->type;
					state.current.effects = selected->effects;
					state.current.inventoryCount = selected->count;
					state.current.currentInventoryValue = selected->value;
					state.current.inventoryCostOverride = selected->costOverride;
					state.current.hasCurrentInventoryValue = true;
				}
			}
			CaptureActiveState(state.activeState);
			if (state.hasCurrent) {
				CopyActiveToRecord(state.current, state.activeState);
				if (state.current.hasEnteredPreCraftValue && state.current.hasCurrentInventoryValue) {
					state.current.differenceCurrentMinusEntered = state.current.currentInventoryValue - state.current.enteredPreCraftValue;
				}
				if (state.current.hasPredictedDisplayedValue && state.current.hasCurrentInventoryValue) {
					state.current.differenceCurrentMinusPredicted = state.current.currentInventoryValue - state.current.predictedDisplayedValue;
				}
				Recompute(state.current);
			}
			if (a_updateMessage) {
				state.message = "Inventory refreshed.";
			}
			state.busy = false;
		}

		void MarkCurrentDirty();
		Player CreationPlayer(const ComparisonRecord& a_record);

		void RefreshSelectedPredictionAfterRecalculate()
		{
			if (!state.hasCurrent) {
				return;
			}
			if (state.current.recipeIngredients == "unavailable" || state.current.recipeIngredientDetails == "unavailable") {
				state.current.hasPredictedRawValue = false;
				state.current.hasPredictedDisplayedValue = false;
				state.current.predictionDetails = "unavailable";
				Recompute(state.current);
				return;
			}

			const auto recipe = engine::FindRecipeForIngredientDetails(state.current.recipeIngredientDetails);
			if (!recipe) {
				state.current.hasPredictedRawValue = false;
				state.current.hasPredictedDisplayedValue = false;
				state.current.recipeIngredients = "unavailable";
				state.current.recipeIngredientDetails = "unavailable";
				state.current.predictionDetails = "unavailable";
			} else {
				state.current.hasPredictedRawValue = true;
				state.current.predictedRawValue = recipe->calculatedValue;
				state.current.hasPredictedDisplayedValue = true;
				state.current.predictedDisplayedValue = recipe->displayedValue;
				state.current.recipeIngredients = recipe->ingredients;
				state.current.recipeIngredientDetails = recipe->ingredientDetails;
				state.current.predictionDetails = recipe->calculationDetails;
			}
			MarkCurrentDirty();
			Recompute(state.current);
		}

		void RefreshCurrentPredictionAtCreationState()
		{
			if (!state.hasCurrent || state.current.recipeIngredientDetails == "unavailable" ||
				state.current.creationAlchemySkill < 0 || !std::isfinite(state.current.creationEffectiveAlchemy) ||
				state.current.creationEffectiveAlchemy < 0.0f) {
				return;
			}
			const auto recipe = engine::FindRecipeForIngredientDetailsAtState(
				state.current.recipeIngredientDetails, CreationPlayer(state.current));
			if (!recipe) {
				state.current.hasPredictedRawValue = false;
				state.current.hasPredictedDisplayedValue = false;
				state.current.predictionDetails = "unavailable";
				return;
			}
			state.current.hasPredictedRawValue = true;
			state.current.predictedRawValue = recipe->calculatedValue;
			state.current.hasPredictedDisplayedValue = true;
			state.current.predictedDisplayedValue = recipe->displayedValue;
			state.current.recipeIngredients = recipe->ingredients;
			state.current.recipeIngredientDetails = recipe->ingredientDetails;
			state.current.predictionDetails = recipe->calculationDetails;
		}

		void RecalculateAndRefresh()
		{
			engine::Recalculate(true);
			RefreshSelectedPredictionAfterRecalculate();
			RefreshInventoryOnGameThread(false);
			menu::RefreshAlchemyMenu(player.hasPerkPurity);
		}

		void Recompute(ComparisonRecord& a_record)
		{
			if (!a_record.potionFormId || !a_record.hasCurrentInventoryValue) {
				a_record.status = "Unavailable";
				return;
			}
			a_record.differenceCurrentMinusEntered = 0;
			a_record.differenceCurrentMinusPredicted = 0;
			a_record.differencePredictedMinusEntered = 0;
			if (a_record.hasEnteredPreCraftValue) {
				a_record.differenceCurrentMinusEntered = a_record.currentInventoryValue - a_record.enteredPreCraftValue;
			}
			if (a_record.hasPredictedDisplayedValue) {
				a_record.differenceCurrentMinusPredicted = a_record.currentInventoryValue - a_record.predictedDisplayedValue;
			}
			if (a_record.hasPredictedDisplayedValue && a_record.hasEnteredPreCraftValue) {
				a_record.differencePredictedMinusEntered = a_record.predictedDisplayedValue - a_record.enteredPreCraftValue;
			}
			if (!HasCompleteComparisonEvidence(a_record)) {
				a_record.status = "Incomplete";
			} else if (a_record.differenceCurrentMinusEntered == 0 && a_record.differenceCurrentMinusPredicted == 0 && a_record.differencePredictedMinusEntered == 0) {
				a_record.status = "Match";
			} else {
				a_record.status = "Mismatch";
			}
		}

		void MarkCurrentDirty()
		{
			if (!state.hasCurrent) {
				return;
			}
			if (state.current.recordId != 0) {
				for (auto& record : state.records) {
					if (record.recordId == state.current.recordId) {
						record = state.current;
						break;
					}
				}
			}
		}

		void CopyActiveToRecord(ComparisonRecord& a_record, const ActiveState& a_active)
		{
			if (!a_record.creationAlchemyEvaluationContext.captured) {
				CaptureCreationState(a_record, a_active);
			}
			a_record.alchemySkill = a_active.alchemySkill;
			a_record.effectiveAlchemy = a_active.effectiveAlchemy;
			a_record.perks = a_active.perks;
			a_record.spells = a_active.spells;
			a_record.gear = a_active.gear;
			a_record.seekerSpellAvailable = a_active.seekerSpellAvailable;
			a_record.seekerSpellFormId = a_active.seekerSpellFormId;
			a_record.seekerSpellListed = a_active.seekerSpellListed;
			a_record.seekerSpellActive = a_active.seekerSpellActive;
			a_record.seekerPerkAvailable = a_active.seekerPerkAvailable;
			a_record.seekerPerkFormId = a_active.seekerPerkFormId;
			a_record.seekerPerkRank = a_active.seekerPerkRank;
			a_record.seekerRewardGlobalAvailable = a_active.seekerRewardGlobalAvailable;
			a_record.seekerRewardGlobalFormId = a_active.seekerRewardGlobalFormId;
			a_record.seekerRewardGlobalValue = a_active.seekerRewardGlobalValue;
			a_record.seekerNativeContract = a_active.seekerNativeContract;
		}

		Player CreationPlayer(const ComparisonRecord& a_record)
		{
			Player evaluated = player;
			if (a_record.creationAlchemyEvaluationContext.captured) {
				evaluated.alchemyEvaluationContext = a_record.creationAlchemyEvaluationContext;
				evaluated.alchemistPerkLevel = evaluated.alchemyEvaluationContext.alchemistPerkRank;
				evaluated.fortifyAlchemyLevel = evaluated.alchemyEvaluationContext.fortifyAlchemyLevel;
				evaluated.hasPerkPhysician = evaluated.alchemyEvaluationContext.hasPhysician;
				evaluated.hasPerkBenefactor = evaluated.alchemyEvaluationContext.hasBenefactor;
				evaluated.hasPerkPoisoner = evaluated.alchemyEvaluationContext.hasPoisoner;
				evaluated.hasPerkPurity = evaluated.alchemyEvaluationContext.hasPurity;
				evaluated.hasPerkConcentratedPoison = evaluated.alchemyEvaluationContext.hasConcentratedPoison;
				evaluated.hasSeekerOfShadows = evaluated.alchemyEvaluationContext.hasSeekerOfShadows;
			}
			if (a_record.creationAlchemySkill >= 0) {
				evaluated.alchemyLevel = static_cast<float>(a_record.creationAlchemySkill);
			}
			if (std::isfinite(a_record.creationEffectiveAlchemy) && a_record.creationEffectiveAlchemy >= 0.0f) {
				evaluated.alchemyLevel = a_record.creationEffectiveAlchemy;
			}
			return evaluated;
		}

		void InitializeOnGameThread()
		{
			std::scoped_lock lock(state.mutex);
			if (!RE::PlayerCharacter::GetSingleton()) {
				state.message = "Cannot initialize the hub: player character is unavailable.";
				state.busy = false;
				return;
			}
			state.initialized = true;
			RecalculateAndRefresh();
			state.message = "Hub ready. Test ingredients remain available until explicitly removed.";
		}
	}

	void Open()
	{
		{
			std::scoped_lock lock(state.mutex);
			state.open = true;
			if (state.fixtures.empty()) {
				state.fixtures = BuildFixtures();
			}
			if (state.initialized || state.busy) {
				return;
			}
		}
		QueueTask(InitializeOnGameThread, "initialize");
	}

	bool IsOpen()
	{
		std::scoped_lock lock(state.mutex);
		return state.open;
	}

	View GetView()
	{
		std::scoped_lock lock(state.mutex);
		View view;
		view.initialized = state.initialized;
		view.busy = state.busy;
		view.message = state.message;
		view.fixtures = state.fixtures;
		view.inventory = state.inventory;
		view.records = state.records;
		view.current = state.current;
		view.hasCurrent = state.hasCurrent;
		view.hasAlgorithmMatrix = state.hasAlgorithmMatrix;
		view.algorithmMatrix = state.algorithmMatrix;
		view.selectedFormId = state.selectedFormId;
		view.selectionUnavailable = state.selectionUnavailable;
		view.activeState = state.activeState;
		view.pendingPerkRanks = state.pendingPerkRanks;
		return view;
	}

	bool ShouldSuppressInventoryRecalculation()
	{
		std::scoped_lock lock(state.mutex);
		return state.open && state.busy;
	}

	void ProvisionFixture(std::size_t a_index, int a_quantity)
	{
		if (a_quantity <= 0) {
			SetMessage("Fixture quantity must be greater than zero.");
			return;
		}
		QueueTask([a_index, a_quantity]() {
			std::scoped_lock lock(state.mutex);
			if (a_index >= state.fixtures.size()) {
				state.message = "Fixture is unavailable.";
				state.busy = false;
				return;
			}
			auto* ingredient = FindFormByName<RE::IngredientItem>(state.fixtures[a_index].formName);
			if (!ingredient) {
				state.message = "Ingredient form unavailable: " + state.fixtures[a_index].formName;
				state.busy = false;
				return;
			}
			auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			if (!playerCharacter) {
				state.message = "Player character unavailable.";
				state.busy = false;
				return;
			}
			const int before = InventoryCount(playerCharacter, ingredient);
			playerCharacter->AddObjectToContainer(ingredient, nullptr, a_quantity, nullptr);
			const int count = InventoryCount(playerCharacter, ingredient);
			const int added = (std::max)(0, count - before);
			state.provisioned[ingredient] += added;
			state.fixtures[a_index].inventoryCount = count;
			state.message = added == a_quantity ? "Provisioned " + state.fixtures[a_index].displayName + " x" + std::to_string(added) + "; inventory count=" + std::to_string(count) :
				"Provisioned " + state.fixtures[a_index].displayName + " x" + std::to_string(added) + " of " + std::to_string(a_quantity) + "; inventory count=" + std::to_string(count);
			RecalculateAndRefresh();
			CaptureActiveState(state.activeState);
			state.busy = false;
		}, "Provisioning fixture...");
	}

	void ProvisionIndexes(const std::vector<std::size_t>& a_indexes)
	{
		std::vector<std::pair<std::size_t, int>> requests;
		for (const auto index : a_indexes) {
			if (index < fixtureQuantities.size()) {
				requests.emplace_back(index, fixtureQuantities[index]);
			}
		}
		QueueTask([requests = std::move(requests)]() {
			std::scoped_lock lock(state.mutex);
			auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			if (!playerCharacter) {
				state.message = "Player character unavailable.";
				state.busy = false;
				return;
			}
			std::vector<std::string> unavailable;
			for (const auto& [index, quantity] : requests) {
				if (index >= state.fixtures.size()) {
					continue;
				}
				auto* ingredient = FindFormByName<RE::IngredientItem>(state.fixtures[index].formName);
				if (!ingredient) {
					unavailable.push_back(state.fixtures[index].formName);
					continue;
				}
				const int beforeCount = InventoryCount(playerCharacter, ingredient);
				playerCharacter->AddObjectToContainer(ingredient, nullptr, quantity, nullptr);
				const int afterCount = InventoryCount(playerCharacter, ingredient);
				if (afterCount < beforeCount + quantity) {
					unavailable.push_back(state.fixtures[index].formName + " (provision failed)");
				}
				state.provisioned[ingredient] += (std::max)(0, afterCount - beforeCount);
				state.fixtures[index].inventoryCount = afterCount;
			}
			state.message = unavailable.empty() ? "Requested ingredient fixtures provisioned." : "Unavailable ingredient forms: " + Join(unavailable);
			RecalculateAndRefresh();
			CaptureActiveState(state.activeState);
			state.busy = false;
		}, "Provisioning ingredient fixtures...");
	}

	void ProvisionAll() { ProvisionIndexes({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17 }); }
	void ProvisionBeneficial() { ProvisionIndexes({ 0, 1 }); }
	void ProvisionPoison() { ProvisionIndexes({ 2, 3 }); }
	void ProvisionThreeEffect() { ProvisionIndexes({ 0, 1, 4 }); }
	void ProvisionFortifyEnchanting() { ProvisionIndexes({ 5, 6, 7, 8 }); }
	void ProvisionHighValue() { ProvisionIndexes({ 9, 10, 11 }); }
	void ProvisionFiveEffectCombo() { ProvisionIndexes({ 13, 14, 15 }); }
	void ProvisionParalysis() { ProvisionIndexes({ 3, 16, 17 }); }

	void ProvisionFortifyEnchantingPotions(int a_quantity)
	{
		const int quantity = (std::max)(1, a_quantity);
		QueueTask([quantity]() {
			std::scoped_lock lock(state.mutex);
			auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			if (!playerCharacter) {
				state.message = "Player character unavailable.";
				state.busy = false;
				return;
			}
			const auto forms = FindFortifyEnchantingPotionForms();
			if (forms.empty()) {
				state.message = "No potion with Fortify Enchanting was found in loaded data.";
				state.busy = false;
				return;
			}
			std::vector<std::string> added;
			for (auto* potion : forms) {
				const int before = InventoryCount(playerCharacter, potion);
				playerCharacter->AddObjectToContainer(potion, nullptr, quantity, nullptr);
				const int count = InventoryCount(playerCharacter, potion);
				const int diff = (std::max)(0, count - before);
				if (diff > 0) {
					state.provisioned[potion] += diff;
					added.push_back(FormName(potion) + " x" + std::to_string(diff));
				}
			}
			state.message = added.empty() ? "Could not provision Fortify Enchanting potions." : "Provisioned: " + Join(added);
			RecalculateAndRefresh();
			CaptureActiveState(state.activeState);
			state.busy = false;
		}, "Provisioning Fortify Enchanting potions...");
	}

	void ProvisionFortifyAlchemyPotions(int a_quantity)
	{
		const int quantity = (std::max)(1, a_quantity);
		QueueTask([quantity]() {
			std::scoped_lock lock(state.mutex);
			auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			if (!playerCharacter) {
				state.message = "Player character unavailable.";
				state.busy = false;
				return;
			}
			const auto forms = FindFortifyAlchemyPotionForms();
			if (forms.empty()) {
				state.message = "No potion with Fortify Alchemy was found in loaded data.";
				state.busy = false;
				return;
			}
			std::vector<std::string> added;
			for (auto* potion : forms) {
				const int before = InventoryCount(playerCharacter, potion);
				playerCharacter->AddObjectToContainer(potion, nullptr, quantity, nullptr);
				const int count = InventoryCount(playerCharacter, potion);
				const int diff = (std::max)(0, count - before);
				if (diff > 0) {
					state.provisioned[potion] += diff;
					added.push_back(FormName(potion) + " x" + std::to_string(diff));
				}
			}
			state.message = added.empty() ? "Could not provision Fortify Alchemy potions." : "Provisioned: " + Join(added);
			RecalculateAndRefresh();
			CaptureActiveState(state.activeState);
			state.busy = false;
		}, "Provisioning Fortify Alchemy potions...");
	}

	void RemoveTestIngredients()
	{
		QueueTask([]() {
			std::scoped_lock lock(state.mutex);
			auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			if (!playerCharacter) {
				state.message = "Player character unavailable.";
				state.busy = false;
				return;
			}
			int removed = 0;
			const auto inventory = playerCharacter->GetInventory();
			for (const auto& [form, entry] : inventory) {
				if (!form || entry.first <= 0 || !form->Is(RE::FormType::Ingredient)) {
					continue;
				}
				const int before = entry.first;
				playerCharacter->RemoveItem(form, before, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				removed += (std::max)(0, before - InventoryCount(playerCharacter, form));
			}
			for (auto iterator = state.provisioned.begin(); iterator != state.provisioned.end();) {
				if (iterator->first && iterator->first->Is(RE::FormType::Ingredient)) {
					iterator = state.provisioned.erase(iterator);
				} else {
					++iterator;
				}
			}
			state.message = "Removed " + std::to_string(removed) + " ingredient(s) from the player's inventory.";
			RecalculateAndRefresh();
			CaptureActiveState(state.activeState);
			state.busy = false;
		}, "Removing all ingredients...");
	}

	void RemoveAllPotions()
	{
		QueueTask([]() {
			std::scoped_lock lock(state.mutex);
			auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			if (!playerCharacter) {
				state.message = "Player character unavailable.";
				state.busy = false;
				return;
			}
			int removed = 0;
			const auto inventory = playerCharacter->GetInventory();
			for (const auto& [form, entry] : inventory) {
				if (!form || entry.first <= 0 || !form->Is(RE::FormType::AlchemyItem)) {
					continue;
				}
				const int before = entry.first;
				playerCharacter->RemoveItem(form, before, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				removed += (std::max)(0, before - InventoryCount(playerCharacter, form));
			}
			for (auto iterator = state.provisioned.begin(); iterator != state.provisioned.end();) {
				if (iterator->first && iterator->first->Is(RE::FormType::AlchemyItem)) {
					iterator = state.provisioned.erase(iterator);
				} else {
					++iterator;
				}
			}
			state.selectedFormId = 0;
			state.hasCurrent = false;
			state.current = {};
			state.message = "Removed " + std::to_string(removed) + " potion(s) and poison(s) from inventory.";
			RecalculateAndRefresh();
			CaptureActiveState(state.activeState);
			state.busy = false;
		}, "Removing all potions and poisons...");
	}

	void RemoveProvisionedItems()
	{
		QueueTask([]() {
			std::scoped_lock lock(state.mutex);
			auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			if (!playerCharacter) {
				state.message = "Player character unavailable.";
				state.busy = false;
				return;
			}
			int removedCount = 0;
			for (const auto& [form, qty] : state.provisioned) {
				if (!form || qty <= 0) {
					continue;
				}
				const int current = InventoryCount(playerCharacter, form);
				const int toRemove = (std::min)(current, qty);
				if (toRemove > 0) {
					playerCharacter->RemoveItem(form, toRemove, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
					removedCount += toRemove;
				}
			}
			state.provisioned.clear();
			state.managedWornGear.clear();
			state.message = "Removed " + std::to_string(removedCount) + " provisioned test item(s).";
			RecalculateAndRefresh();
			CaptureActiveState(state.activeState);
			state.busy = false;
		}, "Removing provisioned test items...");
	}

	void RemoveAllNonEquippedItems()
	{
		QueueTask([]() {
			std::scoped_lock lock(state.mutex);
			auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			if (!playerCharacter) {
				state.message = "Player character unavailable.";
				state.busy = false;
				return;
			}
			int removed = 0;
			const auto inventory = playerCharacter->GetInventory();
			for (const auto& [form, entry] : inventory) {
				if (!form || entry.first <= 0 || (entry.second && entry.second->IsWorn())) {
					continue;
				}
				const int before = entry.first;
				playerCharacter->RemoveItem(form, before, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				removed += (std::max)(0, before - InventoryCount(playerCharacter, form));
			}
			state.message = "Removed " + std::to_string(removed) + " non-equipped inventory item(s); equipped items were preserved.";
			RecalculateAndRefresh();
			CaptureActiveState(state.activeState);
			state.busy = false;
		}, "Removing non-equipped inventory...");
	}

	void ApplySkill(int a_value)
	{
		QueueTask([a_value]() {
			std::scoped_lock lock(state.mutex);
			auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			if (!playerCharacter) {
				state.message = "Player character unavailable.";
				state.busy = false;
				return;
			}
			auto& info = playerCharacter->GetInfoRuntimeData();
			if (!info.skills || !info.skills->data) {
				state.message = "Alchemy skill data unavailable; skill was not changed.";
				state.busy = false;
				return;
			}
			info.skills->data->skills[RE::PlayerCharacter::PlayerSkills::Data::Skills::kAlchemy].level = (std::clamp)(a_value, 0, 100);
			if (auto* actorValueOwner = playerCharacter->AsActorValueOwner()) {
				actorValueOwner->SetActorValue(RE::ActorValue::kAlchemy, static_cast<float>((std::clamp)(a_value, 0, 100)));
			}
			player.init();
			player.setState();
			state.message = "Applied Alchemy skill=" + std::to_string(static_cast<int>(info.skills->data->skills[RE::PlayerCharacter::PlayerSkills::Data::Skills::kAlchemy].level));
			state.busy = false;
			RecalculateAndRefresh();
			CaptureActiveState(state.activeState);
		}, "Applying Alchemy skill...");
	}

	void SetPerk(const std::string& a_name, int a_rank)
	{
		const int requestedRank = (std::max)(0, a_rank);
		{
			std::scoped_lock lock(state.mutex);
			state.pendingPerkRanks[a_name] = requestedRank;
		}
		if (!QueueTask([a_name, requestedRank]() {
			ApplyPerkOnGameThread(a_name, requestedRank, 0);
		}, "Applying perk...")) {
			std::scoped_lock lock(state.mutex);
			const auto pending = state.pendingPerkRanks.find(a_name);
			if (pending != state.pendingPerkRanks.end() && pending->second == requestedRank) {
				state.pendingPerkRanks.erase(pending);
			}
		}
	}

	void GrantAllPerks()
	{
		QueueTask([]() {
			std::scoped_lock lock(state.mutex);
			auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			if (!playerCharacter) {
				state.message = "Player character unavailable.";
				state.busy = false;
				return;
			}
			std::vector<std::string> unavailable;
			for (const auto perkName : kManagedPerks) {
				auto perks = FindPerks(std::string(perkName));
				if (perks.empty()) {
					unavailable.push_back(std::string(perkName));
					continue;
				}
				const int targetRank = (perkName == "Alchemist") ? static_cast<int>(perks.size()) : 1;
				if (!ApplyPerkRanks(playerCharacter, perks, targetRank)) {
					unavailable.push_back(std::string(perkName) + " (grant failed)");
				}
			}
			playerCharacter->CheckTempModifiers();
			player.init();
			player.setState();
			state.message = unavailable.empty() ? "Granted all available test perks (Alchemist Rank 5, Physician, Benefactor, Poisoner, Purity)." : "Unavailable perk forms: " + Join(unavailable);
			RecalculateAndRefresh();
			CaptureActiveState(state.activeState);
			state.busy = false;
		}, "Granting test perks...");
	}

	void ClearTestPerks()
	{
		QueueTask([]() {
			std::scoped_lock lock(state.mutex);
			auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			for (const auto perkName : kManagedPerks) {
				for (auto* perk : FindPerks(std::string(perkName))) {
					int current = PerkRank(playerCharacter, perk);
					for (int attempts = 0; current > 0 && attempts < 32; ++attempts) {
						playerCharacter->RemovePerk(perk);
						current = PerkRank(playerCharacter, perk);
					}
				}
			}
			playerCharacter->CheckTempModifiers();
			player.init();
			player.setState();
			state.message = "Cleared test perks.";
			RecalculateAndRefresh();
			CaptureActiveState(state.activeState);
			state.busy = false;
		}, "Clearing test perks...");
	}

	void ToggleSeekerOfShadows(bool a_grant)
	{
		QueueTask([a_grant]() {
			std::scoped_lock lock(state.mutex);
			auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			auto* spell = seeker::GetSpell();
			auto* perk = seeker::GetPerk();
			auto* rewardGlobal = seeker::GetRewardGlobal();
			if (!spell || !perk || !rewardGlobal) {
				state.message = "Seeker of Shadows reward records are unavailable in Dragonborn.esm.";
				state.busy = false;
				return;
			}
			const bool updated = a_grant ? ApplySeekerState(playerCharacter, spell, perk, rewardGlobal, true, true, 1, seeker::kShadowsRewardValue) :
				ApplySeekerState(playerCharacter, spell, perk, rewardGlobal, false, false, 0, 0.0f);
			if (!updated) {
				state.message = a_grant ? "Seeker of Shadows reward state could not be applied." : "Seeker of Shadows reward state could not be cleared.";
				state.busy = false;
				RecalculateAndRefresh();
				CaptureActiveState(state.activeState);
				return;
			}
			player.init();
			player.setState();
			state.message = a_grant ? "Granted Seeker of Shadows." : "Cleared Seeker of Shadows.";
			RecalculateAndRefresh();
			CaptureActiveState(state.activeState);
			state.busy = false;
		}, "Updating Seeker of Shadows...");
	}

	void GiveTestGear()
	{
		QueueTask([]() {
			std::scoped_lock lock(state.mutex);
			auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			const auto forms = FindAlchemyGearForms();
			if (forms.empty()) {
				state.message = "No armor form with Fortify Alchemy was found in loaded data.";
				state.busy = false;
				return;
			}
			std::vector<std::string> added;
			for (auto* armor : forms) {
				const int count = InventoryCount(playerCharacter, armor);
				if (count > 0) {
					continue;
				}
				playerCharacter->AddObjectToContainer(armor, nullptr, 1, nullptr);
				const int updatedCount = InventoryCount(playerCharacter, armor);
				if (updatedCount > count) {
					state.provisioned[armor] += updatedCount - count;
					added.push_back(FormName(armor) + " [" + FormID(armor->GetFormID()) + "]");
					break;
				}
			}
			state.message = added.empty() ? "Fortify Alchemy test gear is already present in inventory; use Equip test gear." : "Added test gear: " + Join(added);
			RecalculateAndRefresh();
			CaptureActiveState(state.activeState);
			state.busy = false;
		}, "Checking test gear...");
	}

	void EquipTestGear()
	{
		QueueTask([]() {
			std::scoped_lock lock(state.mutex);
			auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			for (const auto& [form, entry] : playerCharacter->GetInventory()) {
				auto* armor = form && form->Is(RE::FormType::Armor) ? static_cast<RE::TESObjectARMO*>(form) : nullptr;
				if (armor && entry.second && entry.second->IsWorn() && IsFortifyAlchemy(entry.second.get()) &&
					IsProvisionedAlchemyGear(armor)) {
					state.managedWornGear.insert(armor);
				}
			}
			if (!ClearAlchemyGear(playerCharacter)) {
				state.message = "Existing Fortify Alchemy gear could not be unequipped safely.";
				state.busy = false;
				RecalculateAndRefresh();
				CaptureActiveState(state.activeState);
				return;
			}
			const auto gear = GetAlchemyGear(playerCharacter);
			if (gear.empty()) {
				state.message = "No owned Fortify Alchemy gear is available; no equipment was changed.";
				state.busy = false;
				RecalculateAndRefresh();
				CaptureActiveState(state.activeState);
				return;
			}
			for (auto* armor : gear) {
				if (!EquipAlchemyGear(playerCharacter, armor)) {
					state.message = "Could not equip Fortify Alchemy item: " + FormName(armor);
					state.busy = false;
					RecalculateAndRefresh();
					CaptureActiveState(state.activeState);
					return;
				}
				state.managedWornGear.insert(armor);
			}
			state.message = "Equipped provisioned Fortify Alchemy test gear.";
			RecalculateAndRefresh();
			CaptureActiveState(state.activeState);
			state.busy = false;
		}, "Equipping test gear...");
	}

	void UnequipTestGear()
	{
		QueueTask([]() {
			std::scoped_lock lock(state.mutex);
			auto* playerCharacter = RE::PlayerCharacter::GetSingleton();
			for (const auto& [form, entry] : playerCharacter->GetInventory()) {
				auto* armor = form && form->Is(RE::FormType::Armor) ? static_cast<RE::TESObjectARMO*>(form) : nullptr;
				if (armor && entry.second && entry.second->IsWorn() && IsFortifyAlchemy(entry.second.get()) &&
					IsProvisionedAlchemyGear(armor)) {
					state.managedWornGear.insert(armor);
				}
			}
			const bool cleanupComplete = ClearAlchemyGear(playerCharacter);
			state.message = cleanupComplete ? "Unequipped Fortify Alchemy gear." : "Some Fortify Alchemy gear could not be unequipped.";
			RecalculateAndRefresh();
			CaptureActiveState(state.activeState);
			state.busy = false;
		}, "Unequipping test gear...");
	}

	void Close()
	{
		std::scoped_lock lock(state.mutex);
		state.open = false;
	}

	void OnMenuClosed()
	{
		std::scoped_lock lock(state.mutex);
		state.open = false;
	}

	void Shutdown()
	{
		std::scoped_lock lock(state.mutex);
		state.open = false;
		state.initialized = false;
		state.managedWornGear.clear();
		state.provisioned.clear();
		state.pendingPerkRanks.clear();
		state.hasAlgorithmMatrix = false;
		state.algorithmMatrix = {};
	}

	void RefreshInventory()
	{
		QueueTask([]() {
			std::scoped_lock lock(state.mutex);
			RecalculateAndRefresh();
			state.message = "Inventory refreshed.";
		}, "Refreshing potion inventory...");
	}

	void SelectPotion(std::uint32_t a_formId)
	{
		std::scoped_lock lock(state.mutex);
		const auto found = std::find_if(state.inventory.begin(), state.inventory.end(), [a_formId](const auto& item) { return item.formId == a_formId; });
		if (found == state.inventory.end()) {
			state.selectedFormId = a_formId;
			state.selectionUnavailable = true;
			state.message = "Selected potion is no longer in inventory; refresh and reselect it.";
			return;
		}
		state.selectedFormId = a_formId;
		state.selectionUnavailable = false;
		const auto previous = std::find_if(state.records.begin(), state.records.end(), [a_formId](const auto& record) { return record.potionFormId == a_formId; });
		if (previous != state.records.end()) {
			state.current = *previous;
		} else {
			state.current = {};
			state.current.recordId = 0;
			state.current.potionFormId = found->formId;
			state.current.potionName = found->name;
			state.current.potionEditorId = found->editorId;
			state.current.potionType = found->type;
			state.current.effects = found->effects;
			state.current.inventoryCount = found->count;
			state.current.currentInventoryValue = found->value;
			state.current.inventoryCostOverride = found->costOverride;
			state.current.hasCurrentInventoryValue = true;
			state.current.recipeIngredients = "unavailable";
			state.current.status = "Incomplete";
		}
		state.current.potionFormId = found->formId;
		state.current.potionName = found->name;
		state.current.potionEditorId = found->editorId;
		state.current.potionType = found->type;
		state.current.effects = found->effects;
		state.current.inventoryCount = found->count;
		state.current.currentInventoryValue = found->value;
		state.current.inventoryCostOverride = found->costOverride;
		state.current.hasCurrentInventoryValue = true;
		Recompute(state.current);
		state.hasCurrent = true;
	}

	void SetEnteredValue(int a_value)
	{
		std::scoped_lock lock(state.mutex);
		if (!state.hasCurrent) {
			return;
		}
		state.current.hasEnteredPreCraftValue = true;
		state.current.enteredPreCraftValue = a_value;
		MarkCurrentDirty();
		Recompute(state.current);
	}

	void ClearEnteredValue()
	{
		std::scoped_lock lock(state.mutex);
		if (!state.hasCurrent) {
			return;
		}
		state.current.hasEnteredPreCraftValue = false;
		MarkCurrentDirty();
		Recompute(state.current);
	}

	void SelectPrediction(std::size_t a_recipeIndex)
	{
		std::scoped_lock lock(state.mutex);
		if (!state.hasCurrent) {
			return;
		}
		const auto recipes = engine::GetCachedRecipes();
		if (a_recipeIndex >= recipes.size()) {
			state.current.hasPredictedRawValue = false;
			state.current.hasPredictedDisplayedValue = false;
			state.current.recipeIngredients = "unavailable";
			state.current.recipeIngredientDetails = "unavailable";
			state.current.predictionDetails = "unavailable";
		} else {
			state.current.hasPredictedRawValue = true;
			state.current.predictedRawValue = recipes[a_recipeIndex].calculatedValue;
			state.current.hasPredictedDisplayedValue = true;
			state.current.predictedDisplayedValue = recipes[a_recipeIndex].displayedValue;
			state.current.recipeIngredients = recipes[a_recipeIndex].ingredients;
			state.current.recipeIngredientDetails = recipes[a_recipeIndex].ingredientDetails;
			state.current.predictionDetails = recipes[a_recipeIndex].calculationDetails;
		}
		MarkCurrentDirty();
		Recompute(state.current);
	}

	void AddRecord()
	{
		std::scoped_lock lock(state.mutex);
		if (!state.hasCurrent || !state.current.potionFormId) {
			state.message = "Select a potion before adding a record.";
			return;
		}
		CopyActiveToRecord(state.current, state.activeState);
		Recompute(state.current);
		if (!HasCompleteComparisonEvidence(state.current)) {
			state.message = "Select an exact cached recipe and enter both values before adding a record.";
			return;
		}
		if (state.current.recordId == 0) {
			state.current.recordId = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
		}
		const auto found = std::find_if(state.records.begin(), state.records.end(), [](const auto& record) { return record.recordId == state.current.recordId; });
		if (found == state.records.end()) {
			state.records.push_back(state.current);
		} else {
			*found = state.current;
		}
		state.message = "Record added in memory.";
	}

	void RemoveRecord(std::size_t a_index)
	{
		std::scoped_lock lock(state.mutex);
		if (a_index >= state.records.size()) {
			return;
		}
		const auto id = state.records[a_index].recordId;
		state.records.erase(state.records.begin() + static_cast<std::ptrdiff_t>(a_index));
		if (state.hasCurrent && state.current.recordId == id) {
			state.current.recordId = 0;
		}
	}

	void RunAlgorithmMatrix()
	{
		QueueTask([]() {
			const auto matrix = engine::RunAlgorithmMatrix();
			std::scoped_lock lock(state.mutex);
			state.algorithmMatrix = matrix;
			state.hasAlgorithmMatrix = matrix.completed;
			if (!matrix.completed) {
				state.message = "The exhaustive algorithm test could not access loaded ingredient data.";
			} else {
				state.message = "Exhaustive algorithm test completed for " + std::to_string(matrix.ingredientCount) + " ingredient forms.";
			}
			state.busy = false;
		}, "Testing all ingredient combinations...");
	}

	bool Draw()
	{
		if (kDeveloper.GetValue() != 1) {
			Close();
			return false;
		}
		bool textInputActive = false;
		const auto view = GetView();
		ImGui::Separator();
		ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), "Developer test hub");

		DrawTestHint("Add known ingredients or potions for controlled recipe, inventory, and cleanup tests.");
		if (ImGui::CollapsingHeader("Fixture & potion provisioning")) {
			if (ImGui::Button("Provision all ingredient fixtures")) ProvisionAll();
			DrawTestHint("All 18 fixtures should be added using their configured quantities; inventory counts and recipes should update.");
			if (ImGui::Button("Provision beneficial ingredients")) ProvisionBeneficial();
			DrawTestHint("Blue Mountain Flower and Wheat should be added; beneficial potion recipes should become available.");
			if (ImGui::Button("Provision poison ingredients")) ProvisionPoison();
			DrawTestHint("Deathbell and Imp Stool should be added; poison and mixed-effect recipes should become available.");
			if (ImGui::Button("Provision three-effect/three-ingredient fixtures")) ProvisionThreeEffect();
			DrawTestHint("Blue Mountain Flower, Wheat, and Giant's Toe should support three-ingredient/three-effect recipes.");
			if (ImGui::Button("Provision Fortify Enchanting ingredients")) ProvisionFortifyEnchanting();
			DrawTestHint("Snowberries, Blue Butterfly Wing, Hagraven Claw, and Spriggan Sap should be added for Fortify Enchanting recipe coverage.");
			if (ImGui::Button("Provision high-value ingredients (Salmon Roe)")) ProvisionHighValue();
			DrawTestHint("Salmon Roe, Garlic, and Nordic Barnacle should be added for high-value Waterbreathing recipes.");
			if (ImGui::Button("Provision 5-effect combo ingredients")) ProvisionFiveEffectCombo();
			DrawTestHint("Creep Cluster, Mora Tapinella, and Scaly Pholiota should be added for standard gold crafting loop recipes.");
			if (ImGui::Button("Provision paralysis ingredients")) ProvisionParalysis();
			DrawTestHint("Imp Stool, Canis Root, and Swamp Fungal Pod should be added for Paralysis poisons.");

			ImGui::SeparatorText("Potions provisioning");
			if (ImGui::Button("Provision Fortify Enchanting potions")) ProvisionFortifyEnchantingPotions(1);
			DrawTestHint("All available Fortify Enchanting potion forms in loaded data should be added to inventory.");
			if (ImGui::Button("Provision Fortify Alchemy potions")) ProvisionFortifyAlchemyPotions(1);
			DrawTestHint("All available Fortify Alchemy potion forms in loaded data should be added to inventory.");

			ImGui::SeparatorText("Inventory cleanup");
			if (ImGui::Button("Remove all ingredients")) RemoveTestIngredients();
			DrawTestHint("Remove every ingredient from the player's inventory.");
			if (ImGui::Button("Remove all potions & poisons")) RemoveAllPotions();
			DrawTestHint("Remove all potions and poisons from inventory; ingredients and gear remain.");
			if (ImGui::Button("Remove provisioned test items")) RemoveProvisionedItems();
			DrawTestHint("Remove only the test items provisioned by the hub (fixtures, test potions, and test gear).");
			if (ImGui::Button("Remove all non-equipped items")) RemoveAllNonEquippedItems();
			DrawTestHint("Remove every non-equipped inventory item; equipped items remain.");

			ImGui::SeparatorText("Ingredient fixtures");
			if (ImGui::BeginTable("FixtureTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
				ImGui::TableSetupColumn("Ingredient");
				ImGui::TableSetupColumn("Quantity");
				ImGui::TableSetupColumn("Give");
				ImGui::TableSetupColumn("Expected result");
				ImGui::TableSetupColumn("Inventory count");
				ImGui::TableSetupColumn("Coverage");
				ImGui::TableSetupColumn("Notes");
				ImGui::TableHeadersRow();
				for (std::size_t index = 0; index < view.fixtures.size(); ++index) {
					ImGui::PushID(static_cast<int>(index));
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(view.fixtures[index].displayName.c_str());
					ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(75.0f); ImGui::InputInt("##quantity", &fixtureQuantities[index], 1, 3); fixtureQuantities[index] = (std::max)(1, fixtureQuantities[index]);
					ImGui::TableSetColumnIndex(2); if (ImGui::Button("Give##fixture-give")) ProvisionFixture(index, fixtureQuantities[index]);
					ImGui::TableSetColumnIndex(3); ImGui::TextWrapped("Inventory count should increase by the requested amount; verify %s.", view.fixtures[index].coverage.c_str());
					ImGui::TableSetColumnIndex(4); ImGui::Text("%s", view.fixtures[index].inventoryCount < 0 ? "unavailable" : std::to_string(view.fixtures[index].inventoryCount).c_str());
					ImGui::TableSetColumnIndex(5); ImGui::TextWrapped("%s", view.fixtures[index].coverage.c_str());
					ImGui::TableSetColumnIndex(6); ImGui::TextWrapped("%s", view.fixtures[index].notes.c_str());
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
		}

		DrawTestHint("Verify recipe completeness by evaluating every loaded ingredient pair and triple through each supported algorithm. This does not change inventory or player state.");
		if (ImGui::CollapsingHeader("Algorithm completeness")) {
			ImGui::BeginDisabled(view.busy);
			if (ImGui::Button("Test all ingredients with every algorithm")) RunAlgorithmMatrix();
			ImGui::EndDisabled();
			DrawTestHint("Runs Vanilla, Alchemy Plus, CACO, and Automatic (the active combined path); unavailable compatibility mods use safe fallback behavior while still exercising each algorithm path.");
			if (!view.hasAlgorithmMatrix) {
				DrawTestHint("No exhaustive algorithm result is available yet.");
			} else {
				ImGui::Text("Ingredient forms tested: %zu", view.algorithmMatrix.ingredientCount);
				ImGui::Text("Cross-algorithm totals: %s", view.algorithmMatrix.totalsAgree ? "match" : "differ");
				ImGui::Text("Alchemy Plus: %s; CACO: %s; combined: %s",
					view.algorithmMatrix.alchemyPlusActive ? "active" : "inactive",
					view.algorithmMatrix.cacoActive ? "active" : "inactive",
					view.algorithmMatrix.combinedActive ? "active" : "inactive");
				if (ImGui::BeginTable("AlgorithmMatrix", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
					ImGui::TableSetupColumn("Algorithm");
					ImGui::TableSetupColumn("Compatibility records");
					ImGui::TableSetupColumn("Craftable pairs");
					ImGui::TableSetupColumn("Craftable triples");
					ImGui::TableSetupColumn("Total craftable");
					ImGui::TableSetupColumn("BMF + Wheat");
					ImGui::TableSetupColumn("BMF + Canis + Jarrin");
					ImGui::TableHeadersRow();
					for (const auto& algorithm : view.algorithmMatrix.algorithms) {
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(AlgorithmName(algorithm.algorithm).c_str());
						ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(algorithm.liveCompatibilityRecords ? "live" : "fallback");
						ImGui::TableSetColumnIndex(2); ImGui::Text("%zu / %zu", algorithm.craftablePairs, algorithm.pairCandidates);
						ImGui::TableSetColumnIndex(3); ImGui::Text("%zu / %zu", algorithm.craftableTriples, algorithm.tripleCandidates);
						ImGui::TableSetColumnIndex(4); ImGui::Text("%zu", algorithm.craftablePairs + algorithm.craftableTriples);
						ImGui::TableSetColumnIndex(5); ImGui::TextUnformatted(algorithm.blueMountainFlowerWheat ? "yes" : "no");
						ImGui::TableSetColumnIndex(6);
						if (algorithm.blueMountainFlowerCanisJarrin) {
							ImGui::Text("%.0f", algorithm.blueMountainFlowerCanisJarrinValue);
						} else {
							ImGui::TextUnformatted("unavailable");
						}
					}
					ImGui::EndTable();
				}
			}
		}

		DrawTestHint("Change skill, perks, Seeker of Shadows, or Fortify Alchemy gear to test calculation inputs.");
		if (ImGui::CollapsingHeader("Player modifiers")) {
			static int skill = 15;
			ImGui::SetNextItemWidth(100.0f);
			ImGui::InputInt("Alchemy skill", &skill);
			if (ImGui::IsItemDeactivatedAfterEdit()) ApplySkill(skill);
			for (const int quick : { 15, 50, 100 }) { ImGui::SameLine(); if (ImGui::SmallButton(std::to_string(quick).c_str())) { skill = quick; ApplySkill(skill); } }
			DrawTestHint("Edit the skill or choose a 15/50/100 preset; changes apply automatically and Active state/predictions should update.");

			bool usePlayerStats = kIgnorePlayer.GetValue() == 0;
			if (ImGui::Checkbox("Use player's Alchemy stats (vs base level 15)", &usePlayerStats)) {
				kIgnorePlayer.SetValue(usePlayerStats ? 0 : 1);
				REX::INI::SettingStore::GetSingleton()->Save();
				RecalculateAndRefresh();
			}
			DrawTestHint("Uncheck to test with base level 15 stats (IgnorePlayer=1); check to use the player character's actual level, perks, and gear.");

			ImGui::SeparatorText("Perks");
			// The five Alchemist forms represent ranks 1-5; zero removes every managed Alchemist form.
			int alchemistRank = 0;
			for (const auto& entry : view.activeState.perks) {
				if (entry.find("Alchemist") == 0) {
					const auto pos = entry.find("rank ");
					if (pos != std::string::npos) {
						alchemistRank = std::atoi(entry.c_str() + pos + 5);
					} else {
						alchemistRank = 1;
					}
				}
			}
			if (const auto pending = view.pendingPerkRanks.find("Alchemist"); pending != view.pendingPerkRanks.end()) {
				alchemistRank = pending->second;
			}
			ImGui::Text("Alchemist perk (rank 0-5):");
			ImGui::SameLine();
			for (int r = 0; r <= 5; ++r) {
				if (r > 0) ImGui::SameLine();
				const bool isSelected = (alchemistRank == r);
				if (isSelected) {
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.4f, 0.15f, 1.0f));
				}
				ImGui::BeginDisabled(view.busy);
				if (ImGui::SmallButton((std::to_string(r) + "##alchemist-rank").c_str())) {
					SetPerk("Alchemist", r);
				}
				ImGui::EndDisabled();
				if (isSelected) {
					ImGui::PopStyleColor();
				}
			}
			ImGui::SameLine();
			ImGui::TextDisabled(alchemistRank == 0 ? "(0%% bonus)" : "(+%d%% bonus)", alchemistRank * 20);
			DrawTestHint("Alchemist increases potion and poison magnitudes/durations by +20% per rank (up to +100% at Rank 5).");

			for (const auto perk : { "Physician", "Benefactor", "Poisoner", "Purity" }) {
				const auto active = std::any_of(view.activeState.perks.begin(), view.activeState.perks.end(), [perk](const auto& value) { return value.find(perk) == 0; });
				bool enabled = active;
				if (const auto pending = view.pendingPerkRanks.find(std::string(perk)); pending != view.pendingPerkRanks.end()) {
					enabled = pending->second > 0;
				}
				ImGui::BeginDisabled(view.busy);
				const bool changed = ImGui::Checkbox(std::string(perk).c_str(), &enabled);
				ImGui::EndDisabled();
				if (changed) SetPerk(std::string(perk), enabled ? 1 : 0);
				const char* description = "Active state and applicable predicted values should change.";
				if (std::string_view(perk) == "Physician") {
					description = "Enable it and Restore Health, Magicka, and Stamina predictions should increase; other effects should be unchanged.";
				} else if (std::string_view(perk) == "Benefactor") {
					description = "Enable it and beneficial potion predictions should increase; poison predictions should not gain this bonus.";
				} else if (std::string_view(perk) == "Poisoner") {
					description = "Enable it and poison predictions should increase; beneficial potion predictions should not gain this bonus.";
				} else if (std::string_view(perk) == "Purity") {
					description = "With it enabled, a mixed-effect potion should remove harmful effects while retaining beneficial effects and their value; disable it to verify the harmful effects and value return. For a mixed-effect poison, beneficial effects should be removed.";
				}
				DrawTestHint(description);
			}

			if (ImGui::Button("Grant all test perks")) GrantAllPerks();
			DrawTestHint("Grants Alchemist Rank 5, Physician, Benefactor, Poisoner, and Purity.");
			if (ImGui::Button("Clear all test perks")) ClearTestPerks();
			DrawTestHint("Removes all managed alchemy perks.");

			ImGui::SeparatorText("Powers & equipment");
			const bool seekerActive = view.activeState.seekerNativeContract;
			if (ImGui::Button(seekerActive ? "Clear Seeker of Shadows" : "Grant Seeker of Shadows")) ToggleSeekerOfShadows(!seekerActive);
			DrawTestHint(seekerActive ? "The spell should be removed/inactive, the shared perk rank should be 0, and the reward global should be 0." : "The spell should be listed and active, the shared perk rank should be 1, and the reward global should be 3.");
			if (ImGui::Button("Give test Fortify Alchemy gear")) GiveTestGear();
			DrawTestHint("One representative Fortify Alchemy armor should be added to inventory but should not be worn or change Effective Alchemy yet.");
			if (ImGui::Button("Equip test gear")) EquipTestGear();
			DrawTestHint("Provisioned Fortify Alchemy test gear should be worn; unrelated owned gear should be preserved.");
			if (ImGui::Button("Unequip test gear")) UnequipTestGear();
			DrawTestHint("Provisioned Fortify Alchemy test gear should no longer be worn and Effective Alchemy/predictions should decrease.");

		}

		DrawTestHint("Inspect the currently applied skill, perks, Seeker values, and worn Fortify Alchemy gear.");
		if (ImGui::CollapsingHeader("Active state")) {
			ImGui::Text("Alchemy skill: %s", view.activeState.alchemySkill < 0 ? "unavailable" : std::to_string(view.activeState.alchemySkill).c_str());
			ImGui::Text("Effective Alchemy value: %s", std::isfinite(view.activeState.effectiveAlchemy) && view.activeState.effectiveAlchemy >= 0.0f ? OptionalFloat(true, view.activeState.effectiveAlchemy).c_str() : "unavailable");
			ImGui::Text("Perks: %s", Join(view.activeState.perks).c_str());
			ImGui::Text("Spells: %s", Join(view.activeState.spells).c_str());
			ImGui::Text("Seeker spell: %s (form=%s, listed=%d, active=%d)", view.activeState.seekerSpellAvailable ? "available" : "unavailable", FormID(view.activeState.seekerSpellFormId).c_str(), view.activeState.seekerSpellListed ? 1 : 0, view.activeState.seekerSpellActive ? 1 : 0);
			ImGui::Text("Shared Seeker perk: %s (form=%s, rank=%d)", view.activeState.seekerPerkAvailable ? "available" : "unavailable", FormID(view.activeState.seekerPerkFormId).c_str(), view.activeState.seekerPerkRank);
			ImGui::Text("Seeker reward global: %s (form=%s, value=%s)", view.activeState.seekerRewardGlobalAvailable ? "available" : "unavailable", FormID(view.activeState.seekerRewardGlobalFormId).c_str(), OptionalFloat(view.activeState.seekerRewardGlobalAvailable, view.activeState.seekerRewardGlobalValue).c_str());
			ImGui::Text("Seeker native reward contract: %s", view.activeState.seekerNativeContract ? "complete" : "incomplete");
			ImGui::Text("Worn Fortify Alchemy gear: %s", Join(view.activeState.gear).c_str());
		}

		DrawTestHint("Refresh and select crafted results, then compare their in-game values with an exact cached plugin prediction.");
		if (ImGui::CollapsingHeader("Potion inventory and comparison")) {
			if (ImGui::Button("Refresh inventory")) RefreshInventory();
			DrawTestHint("The potion/poison list, fixture counts, selected value, and Active state should be refreshed from Skyrim.");
			if (ImGui::BeginTable("PotionInventory", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 180.0f))) {
				ImGui::TableSetupColumn("Potion/poison"); ImGui::TableSetupColumn("Type"); ImGui::TableSetupColumn("Count"); ImGui::TableSetupColumn("Value"); ImGui::TableSetupColumn("Form ID"); ImGui::TableSetupColumn("Editor ID"); ImGui::TableSetupColumn("Effects"); ImGui::TableHeadersRow();
				for (const auto& item : view.inventory) {
					ImGui::PushID(item.formId);
					ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
					const bool selected = item.formId == view.selectedFormId;
					if (SelectableWrappedInCell(item.name.c_str(), selected)) SelectPotion(item.formId);
					ImGui::TableSetColumnIndex(1); TextWrappedInCell(item.type.c_str());
					ImGui::TableSetColumnIndex(2); ImGui::Text("%d", item.count);
					ImGui::TableSetColumnIndex(3); ImGui::Text("%d", item.value);
					ImGui::TableSetColumnIndex(4); ImGui::Text("%s", FormID(item.formId).c_str());
					ImGui::TableSetColumnIndex(5); TextWrappedInCell(item.editorId.c_str());
					ImGui::TableSetColumnIndex(6); TextWrappedInCell(item.effects.c_str());
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
			if (view.selectionUnavailable) DrawTestHint("Selected potion is unavailable after refresh; reselect the same form.");
			if (view.hasCurrent) {
				ImGui::SeparatorText("Comparison");
				ImGui::Text("Selected: %s [%s]", view.current.potionName.c_str(), FormID(view.current.potionFormId).c_str());
				ImGui::TextWrapped("Effects: %s", view.current.effects.c_str());
				int entered = view.current.hasEnteredPreCraftValue ? view.current.enteredPreCraftValue : 0;
				ImGui::SetNextItemWidth(180.0f); if (ImGui::InputInt("Entered pre-craft plugin value", &entered)) SetEnteredValue(entered); textInputActive = ImGui::IsItemActive();
				DrawTestHint("Enter the value shown before crafting; the comparison differences and status should update immediately.");
				if (ImGui::Button("Clear entered value")) ClearEnteredValue();
				DrawTestHint("The entered value should clear and Comparison status should return to Incomplete until another value is entered.");
				ImGui::Text("Current inventory potion value: %s", view.current.hasCurrentInventoryValue ? std::to_string(view.current.currentInventoryValue).c_str() : "unavailable");
				ImGui::Text("Plugin predicted raw value: %s", OptionalFloat(view.current.hasPredictedRawValue, view.current.predictedRawValue).c_str());
				ImGui::Text("Plugin predicted displayed value: %s", OptionalInt(view.current.hasPredictedDisplayedValue, view.current.predictedDisplayedValue).c_str());
				ImGui::Text("Difference current - entered: %s", view.current.hasEnteredPreCraftValue && view.current.hasCurrentInventoryValue ? std::to_string(view.current.differenceCurrentMinusEntered).c_str() : "unavailable");
				ImGui::Text("Difference current - predicted: %s", view.current.hasPredictedDisplayedValue && view.current.hasCurrentInventoryValue ? std::to_string(view.current.differenceCurrentMinusPredicted).c_str() : "unavailable");
				ImGui::Text("Difference predicted - entered: %s", view.current.hasPredictedDisplayedValue && view.current.hasEnteredPreCraftValue ? std::to_string(view.current.differencePredictedMinusEntered).c_str() : "unavailable");
				ImGui::Text("Comparison status: %s", view.current.status.c_str());
				const auto recipes = engine::GetCachedRecipes();
				if (ImGui::BeginCombo("Prediction recipe", view.current.recipeIngredients.c_str())) {
					for (std::size_t index = 0; index < recipes.size(); ++index) {
						const bool selectedRecipe = recipes[index].ingredientDetails == view.current.recipeIngredientDetails;
						if (ImGui::Selectable((recipes[index].name + " | " + recipes[index].ingredients + " | " + recipes[index].ingredientDetails).c_str(), selectedRecipe)) SelectPrediction(index);
					}
					ImGui::EndCombo();
				}
				ImGui::TextWrapped("Selected recipe ingredients: %s", view.current.recipeIngredientDetails.c_str());
				DrawTestHint("Choose the cached recipe whose ingredient form IDs and counts exactly match the craft; predicted values, details, and comparison differences should change to that recipe.");
				if (recipes.empty()) {
					DrawTestHint("Plugin prediction: unavailable; no cached recipe result is available.");
				}
				if (ImGui::Button("Add record")) AddRecord();
				DrawTestHint("A record requires the exact recipe, ingredient form IDs/counts, entered pre-craft value, current value, and both plugin predictions.");
			}
		}

		DrawTestHint("Review comparison records in memory, and remove records that are no longer needed.");
		if (ImGui::CollapsingHeader("Comparison records")) {
			if (view.records.empty()) {
				DrawTestHint("No records have been added.");
			} else if (ImGui::BeginTable("ComparisonRecords", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
				ImGui::TableSetupColumn("Potion"); ImGui::TableSetupColumn("Status"); ImGui::TableSetupColumn("Entered"); ImGui::TableSetupColumn("Current"); ImGui::TableSetupColumn("Action"); ImGui::TableSetupColumn("Expected result"); ImGui::TableHeadersRow();
				for (std::size_t index = 0; index < view.records.size(); ++index) {
					const auto& record = view.records[index]; ImGui::PushID(static_cast<int>(index)); ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(record.potionName.c_str());
					ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(record.status.c_str());
					ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(OptionalInt(record.hasEnteredPreCraftValue, record.enteredPreCraftValue).c_str());
					ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(OptionalInt(record.hasCurrentInventoryValue, record.currentInventoryValue).c_str());
					ImGui::TableSetColumnIndex(4); if (ImGui::SmallButton("Remove")) RemoveRecord(index);
					ImGui::TableSetColumnIndex(5); ImGui::TextWrapped("The record should disappear; if it was current, the comparison should no longer reference it.");
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
		}
		ImGui::SeparatorText("Output");
		if (!view.message.empty()) {
			ImGui::TextWrapped("Status: %s", view.message.c_str());
		}
		if (view.busy) {
			DrawTestHint("Waiting for Skyrim's game thread...");
		}
		return textInputActive;
	}
}
