#pragma once

#include <RE/Skyrim.h>

#include <string_view>

namespace alchemist::seeker
{
	inline constexpr std::string_view kDragonborn = "Dragonborn.esm";
	inline constexpr RE::FormID kSpellLocalFormID = 0x034838;
	inline constexpr RE::FormID kPerkLocalFormID = 0x03399F;
	inline constexpr RE::FormID kRewardGlobalLocalFormID = 0x020E9A;
	inline constexpr float kShadowsRewardValue = 3.0f;

	inline RE::SpellItem* GetSpell()
	{
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		return dataHandler ? dataHandler->LookupForm<RE::SpellItem>(kSpellLocalFormID, kDragonborn) : nullptr;
	}

	inline RE::BGSPerk* GetPerk()
	{
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		return dataHandler ? dataHandler->LookupForm<RE::BGSPerk>(kPerkLocalFormID, kDragonborn) : nullptr;
	}

	inline RE::TESGlobal* GetRewardGlobal()
	{
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		return dataHandler ? dataHandler->LookupForm<RE::TESGlobal>(kRewardGlobalLocalFormID, kDragonborn) : nullptr;
	}
}