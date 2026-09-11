#include "MenuHandler.h"

#include "AlchemistWindow.h"
#include "AlchemistEngine.h"
#include "DeveloperTestHub.h"
#include "main.h"

#include "RE/Skyrim.h"
#include "RE/B/BSInputDeviceManager.h"
#include "RE/B/ButtonEvent.h"
#include "RE/B/BSWin32MouseDevice.h"
#include "RE/M/MenuCursor.h"

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace alchemist::menu {
	namespace {
		void QueueRecalculation(bool a_force = false);
		std::atomic_uint64_t menuGeneration = 0;
		std::atomic_bool nativeAlchemyOpen = false;
		std::atomic<float> nativeCursorX = -1.0f;
		std::atomic<float> nativeCursorY = -1.0f;
		std::atomic<float> nativeCursorWidth = 0.0f;
		std::atomic<float> nativeCursorHeight = 0.0f;
		std::atomic_bool nativeCursorValid = false;

		void CaptureNativeCursor()
		{
			auto* menuCursor = RE::MenuCursor::GetSingleton();
			if (!menuCursor) {
				nativeCursorValid.store(false, std::memory_order_release);
				return;
			}

			const auto& cursorData = menuCursor->GetRuntimeData();
			if (!std::isfinite(cursorData.cursorPosX) || !std::isfinite(cursorData.cursorPosY) ||
				!std::isfinite(cursorData.screenWidthX) || !std::isfinite(cursorData.screenWidthY) ||
				cursorData.screenWidthX <= 0.0f || cursorData.screenWidthY <= 0.0f ||
				cursorData.cursorPosX < 0.0f || cursorData.cursorPosY < 0.0f ||
				cursorData.cursorPosX > cursorData.screenWidthX || cursorData.cursorPosY > cursorData.screenWidthY) {
				nativeCursorValid.store(false, std::memory_order_release);
				return;
			}

			nativeCursorX.store(cursorData.cursorPosX, std::memory_order_relaxed);
			nativeCursorY.store(cursorData.cursorPosY, std::memory_order_relaxed);
			nativeCursorWidth.store(cursorData.screenWidthX, std::memory_order_relaxed);
			nativeCursorHeight.store(cursorData.screenWidthY, std::memory_order_relaxed);
			nativeCursorValid.store(true, std::memory_order_release);
		}

		void ResetNativeCursor()
		{
			nativeCursorValid.store(false, std::memory_order_release);
		}

		bool IsAlchemySubMenu(const RE::CraftingSubMenus::CraftingSubMenu* a_submenu)
		{
			if (!a_submenu) {
				return false;
			}
			const auto vtable = *reinterpret_cast<const std::uintptr_t*>(a_submenu);
			for (const auto& vtableAddress : RE::CraftingSubMenus::CraftingSubMenus::AlchemyMenu::VTABLE) {
				if (vtable == vtableAddress.address()) {
					return true;
				}
			}
			return false;
		}

		class InputHandler final : public RE::BSTEventSink<RE::InputEvent*> {
		public:
			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event,
				RE::BSTEventSource<RE::InputEvent*>*) override
			{
				if (!a_event || !*a_event || !ui::IsVisible()) {
					return RE::BSEventNotifyControl::kContinue;
				}

				auto* event = *a_event;
				if (event->GetEventType() == RE::INPUT_EVENT_TYPE::kMouseMove ||
					event->GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
					CaptureNativeCursor();
				}
				if (event->GetEventType() == RE::INPUT_EVENT_TYPE::kChar) {
					if (!ui::IsSearchInputFocused()) {
						return RE::BSEventNotifyControl::kContinue;
					}
					if (const auto* charEvent = event->AsCharEvent()) {
						ui::AddInputCharacter(charEvent->keyCode);
					}
					return RE::BSEventNotifyControl::kStop;
				}
				if (event->GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
					auto* buttonEvent = event->AsButtonEvent();
					if (buttonEvent && buttonEvent->GetDevice() == RE::INPUT_DEVICE::kMouse) {
						if (buttonEvent->IsPressed()) {
							switch (buttonEvent->GetIDCode()) {
							case RE::BSWin32MouseDevice::Key::kWheelUp:
								ui::AddMouseWheel(1.0f);
								break;
							case RE::BSWin32MouseDevice::Key::kWheelDown:
								ui::AddMouseWheel(-1.0f);
								break;
							default:
								break;
							}
						}
						return ui::IsCursorOverWindow() ? RE::BSEventNotifyControl::kStop : RE::BSEventNotifyControl::kContinue;
					}
					if (!ui::IsSearchInputFocused()) {
						return RE::BSEventNotifyControl::kContinue;
					}
					return RE::BSEventNotifyControl::kStop;
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		class MenuOpenCloseHandler final : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
				RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (!a_event || a_event->menuName != RE::CraftingMenu::MENU_NAME) {
					return RE::BSEventNotifyControl::kContinue;
				}
				if (!a_event->opening) {
					nativeAlchemyOpen.store(false, std::memory_order_release);
					ResetNativeCursor();
					menuGeneration.fetch_add(1, std::memory_order_acq_rel);
					devhub::OnMenuClosed();
					ui::SetVisible(false);
					engine::NotifyAlchemyMenuClosed();
					return RE::BSEventNotifyControl::kContinue;
				}

				auto* uiInterface = RE::UI::GetSingleton();
				if (!uiInterface) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto craftingMenu = uiInterface->GetMenu<RE::CraftingMenu>();
				if (!craftingMenu) {
					return RE::BSEventNotifyControl::kContinue;
				}

				auto* submenu = craftingMenu->GetCraftingSubMenu();
				if (!submenu) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const bool isAlchemyMenu = IsAlchemySubMenu(submenu);
				if (isAlchemyMenu) {
					nativeAlchemyOpen.store(true, std::memory_order_release);
					CaptureNativeCursor();
					menuGeneration.fetch_add(1, std::memory_order_acq_rel);
					ui::SetVisible(true);
					engine::NotifyAlchemyMenuOpened();
					QueueRecalculation();
				} else {
					nativeAlchemyOpen.store(false, std::memory_order_release);
					menuGeneration.fetch_add(1, std::memory_order_acq_rel);
					ui::SetVisible(false);
					engine::NotifyAlchemyMenuClosed();
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		class InventoryChangeHandler final : public RE::BSTEventSink<RE::TESContainerChangedEvent> {
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent* a_event,
				RE::BSTEventSource<RE::TESContainerChangedEvent>*) override
			{
				if (!a_event || !nativeAlchemyOpen.load(std::memory_order_acquire)) {
					return RE::BSEventNotifyControl::kContinue;
				}

				auto* player = RE::PlayerCharacter::GetSingleton();
				if (!player || (a_event->oldContainer != player->GetFormID() && a_event->newContainer != player->GetFormID())) {
					return RE::BSEventNotifyControl::kContinue;
				}

				auto* form = RE::TESForm::LookupByID(a_event->baseObj);
				if (!form || (!form->Is(RE::FormType::Ingredient) && !form->Is(RE::FormType::AlchemyItem) && !form->Is(RE::FormType::Armor))) {
					return RE::BSEventNotifyControl::kContinue;
				}
				QueueRecalculation();
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		class EquipChangeHandler final : public RE::BSTEventSink<RE::TESEquipEvent> {
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* a_event,
				RE::BSTEventSource<RE::TESEquipEvent>*) override
			{
				if (!a_event || !nativeAlchemyOpen.load(std::memory_order_acquire) || !a_event->actor) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (!player || a_event->actor.get() != player) {
					return RE::BSEventNotifyControl::kContinue;
				}
				QueueRecalculation();
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		MenuOpenCloseHandler handler;
		InventoryChangeHandler inventoryChangeHandler;
		EquipChangeHandler equipChangeHandler;
		InputHandler inputHandler;
		std::atomic_bool recalculationQueued = false;
		std::atomic_bool recalculationForceQueued = false;
		bool menuOpenCloseHandlerRegistered = false;
		bool inventoryChangeHandlerRegistered = false;
		bool equipChangeHandlerRegistered = false;
		bool inputHandlerRegistered = false;

		void QueueRecalculation(bool a_force)
		{
			if (devhub::ShouldSuppressInventoryRecalculation()) {
				return;
			}
			if (a_force) {
				recalculationForceQueued.store(true, std::memory_order_release);
			}
			bool expected = false;
			if (!recalculationQueued.compare_exchange_strong(expected, true)) {
				return;
			}
			const auto generation = menuGeneration.load(std::memory_order_acquire);

			auto* taskInterface = SKSE::GetTaskInterface();
			if (!taskInterface) {
				recalculationQueued.store(false);
				recalculationForceQueued.store(false);
				return;
			}
			taskInterface->AddTask([generation]() {
				recalculationQueued.store(false);
				const bool force = recalculationForceQueued.exchange(false, std::memory_order_acq_rel);
				const bool currentMenu = nativeAlchemyOpen.load(std::memory_order_acquire) && generation == menuGeneration.load(std::memory_order_acquire);
				if (!ui::IsVisible() || !currentMenu) {
					return;
				}
				engine::RecalculateAsync([generation]() {
					auto* task = SKSE::GetTaskInterface();
					if (!task) {
						return;
					}
					task->AddTask([generation]() {
						const bool active = nativeAlchemyOpen.load(std::memory_order_acquire) && generation == menuGeneration.load(std::memory_order_acquire);
						if (!active) {
							return;
						}
						RefreshAlchemyMenu(player.hasPerkPurity);
					});
				}, force);
			});
		}
	}

	void Register()
	{
		if (!menuOpenCloseHandlerRegistered) {
			if (auto* uiInterface = RE::UI::GetSingleton()) {
				uiInterface->AddEventSink<RE::MenuOpenCloseEvent>(&handler);
				menuOpenCloseHandlerRegistered = true;
			}
		}
		if (!inventoryChangeHandlerRegistered) {
			if (auto* scriptEventSourceHolder = RE::ScriptEventSourceHolder::GetSingleton()) {
				scriptEventSourceHolder->AddEventSink<RE::TESContainerChangedEvent>(&inventoryChangeHandler);
				inventoryChangeHandlerRegistered = true;
			}
		}
		if (!equipChangeHandlerRegistered) {
			if (auto* scriptEventSourceHolder = RE::ScriptEventSourceHolder::GetSingleton()) {
				scriptEventSourceHolder->AddEventSink<RE::TESEquipEvent>(&equipChangeHandler);
				equipChangeHandlerRegistered = true;
			}
		}
		if (!inputHandlerRegistered) {
			if (auto* inputDeviceManager = RE::BSInputDeviceManager::GetSingleton()) {
				inputDeviceManager->PrependEventSink<RE::InputEvent*>(&inputHandler);
				inputHandlerRegistered = true;
			}
		}
	}

	void RequestRecalculation(bool a_force)
	{
		QueueRecalculation(a_force);
	}

	void RefreshAlchemyMenu(bool a_hasPurityPerk)
	{
		if (!nativeAlchemyOpen.load(std::memory_order_acquire)) {
			return;
		}
		auto* uiInterface = RE::UI::GetSingleton();
		if (!uiInterface) {
			return;
		}
		auto craftingMenu = uiInterface->GetMenu<RE::CraftingMenu>();
		if (!craftingMenu) {
			return;
		}
		auto* submenu = craftingMenu->GetCraftingSubMenu();
		if (!IsAlchemySubMenu(submenu)) {
			return;
		}
		auto* alchemyMenu = static_cast<RE::CraftingSubMenus::CraftingSubMenus::AlchemyMenu*>(submenu);
		alchemyMenu->playerHasPurityPerk = a_hasPurityPerk;
		alchemyMenu->UpdateCraftingInfo(RE::ActorValue::kAlchemy);
		alchemyMenu->playerHasPurityPerk = a_hasPurityPerk;
	}

	bool GetCursorSnapshot(CursorSnapshot& a_snapshot)
	{
		if (!nativeCursorValid.load(std::memory_order_acquire)) {
			return false;
		}
		a_snapshot.x = nativeCursorX.load(std::memory_order_relaxed);
		a_snapshot.y = nativeCursorY.load(std::memory_order_relaxed);
		a_snapshot.width = nativeCursorWidth.load(std::memory_order_relaxed);
		a_snapshot.height = nativeCursorHeight.load(std::memory_order_relaxed);
		return true;
	}

	std::vector<std::uint32_t> GetSelectedIngredientFormIDs()
	{
		std::vector<std::uint32_t> selectedFormIDs;
		if (!nativeAlchemyOpen.load(std::memory_order_acquire)) {
			return selectedFormIDs;
		}

		auto* uiInterface = RE::UI::GetSingleton();
		if (!uiInterface) {
			return selectedFormIDs;
		}
		auto craftingMenu = uiInterface->GetMenu<RE::CraftingMenu>();
		if (!craftingMenu) {
			return selectedFormIDs;
		}
		auto* submenu = craftingMenu->GetCraftingSubMenu();
		if (!IsAlchemySubMenu(submenu)) {
			return selectedFormIDs;
		}

		auto* alchemyMenu = static_cast<RE::CraftingSubMenus::CraftingSubMenus::AlchemyMenu*>(submenu);
		for (const auto selectedIndex : alchemyMenu->selectedIndexes) {
			if (selectedIndex >= alchemyMenu->ingredientEntries.size()) {
				continue;
			}
			const auto& entry = alchemyMenu->ingredientEntries[selectedIndex];
			if (!entry.ingredient || !entry.ingredient->object) {
				continue;
			}
			selectedFormIDs.push_back(entry.ingredient->object->GetFormID());
		}

		std::sort(selectedFormIDs.begin(), selectedFormIDs.end());
		selectedFormIDs.erase(std::unique(selectedFormIDs.begin(), selectedFormIDs.end()), selectedFormIDs.end());
		return selectedFormIDs;
	}
}
