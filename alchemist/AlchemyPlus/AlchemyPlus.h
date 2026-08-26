#pragma once

#include <nlohmann/json_fwd.hpp>

namespace RE {
	class EffectSetting;
}

namespace alchemist::alchemyplus {
	// Detection is independent from activation: the DLL may be present while its JSON features are absent or disabled.
	class Adapter final {
	public:
		static void Initialize() noexcept;
		[[nodiscard]] static bool IsDetected() noexcept;
		[[nodiscard]] static bool IsActive() noexcept;
		[[nodiscard]] static bool IsRoundingEnabled() noexcept;
		[[nodiscard]] static bool IsImpureCostFixEnabled() noexcept;
		[[nodiscard]] static bool GetMagnitudeRounding(const RE::EffectSetting* a_baseEffect, float& a_threshold, float& a_multiple) noexcept;
		[[nodiscard]] static bool GetDurationRounding(const RE::EffectSetting* a_baseEffect, float& a_threshold, float& a_multiple) noexcept;
		[[nodiscard]] static float ApplyMagnitudeRounding(const RE::EffectSetting* a_baseEffect, float a_value) noexcept;
		[[nodiscard]] static float ApplyDurationRounding(const RE::EffectSetting* a_baseEffect, float a_value) noexcept;
		[[nodiscard]] static float AdjustImpureEffectCost(float a_effectCost, bool a_isPoison, bool a_isHostile, bool& a_impure) noexcept;
		[[nodiscard]] static float FinalizeImpureCost(float a_cost) noexcept;
		[[nodiscard]] static const nlohmann::json* GetConfiguration() noexcept;
	};
}
