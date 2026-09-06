#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace RE
{
	class BGSPerk;
	class AlchemyItem;
	class EffectSetting;
	class IngredientItem;
	class SpellItem;
	class TESGlobal;
}

namespace alchemist::caco
{
	// The adapter reads loaded CACO records and settings without invoking Papyrus or mutating the active menu.
	struct SeekerEvaluationState
	{
		const RE::SpellItem* spell = nullptr;
		const RE::BGSPerk* perk = nullptr;
		const RE::TESGlobal* rewardGlobal = nullptr;
		bool spellListed = false;
		bool spellActive = false;
		std::int32_t perkRank = 0;
		float rewardGlobalValue = 0.0f;
		bool rewardGlobalAvailable = false;
		bool nativeContract = false;
	};

	struct AlchemyEvaluationContext
	{
		std::vector<const RE::BGSPerk*> activePerks;
		SeekerEvaluationState seeker;
		std::int32_t alchemistPerkRank = 0;
		float fortifyAlchemyLevel = 0.0f;
		bool hasPhysician = false;
		bool hasBenefactor = false;
		bool hasPoisoner = false;
		bool hasSeekerOfShadows = false;
		bool hasPurity = false;
		bool hasConcentratedPoison = false;
		bool captured = false;
	};

	namespace algorithm
	{
		// These helpers mirror the value-bearing portions of Skyrim/CACO's construction pipeline and are
		// intentionally side-effect free so hypothetical recipes can be evaluated off the active menu.
		struct EffectInput
		{
			float magnitude = 0.0f;
			float duration = 0.0f;
			bool valid = false;
		};

		struct EffectValue
		{
			float magnitude = 0.0f;
			float duration = 0.0f;
			double cost = 0.0;
			std::int32_t goldValue = 0;
			bool valid = false;
		};

		inline constexpr float CalculateAlchemyEffectiveness(
			float a_ingredientInitMultiplier,
			float a_skillFactor,
			float a_alchemyLevel,
			float a_perkMultiplier) noexcept
		{
			return a_ingredientInitMultiplier *
				(1.0f + (a_skillFactor - 1.0f) * a_alchemyLevel / 100.0f) * a_perkMultiplier;
		}

		inline constexpr float CalculateAlchemyActorValueMultiplier(float a_fortifyAlchemyLevel) noexcept
		{
			return 1.0f + a_fortifyAlchemyLevel * 0.01f;
		}

		inline constexpr float CalculateVanillaAlchemyEffectiveness(
			float a_alchemyLevel,
			float a_alchemistMultiplier,
			float a_perkMultiplier = 1.0f) noexcept
		{
			return CalculateAlchemyEffectiveness(4.0f, 1.5f, a_alchemyLevel, a_alchemistMultiplier * a_perkMultiplier);
		}

		inline constexpr float CalculateDurationBasedIngredientPowerFactor(float a_effectiveness) noexcept
		{
			return a_effectiveness * 0.01f;
		}

		inline double CalculateEffectCostPrecise(
			float a_baseCost,
			float a_magnitude,
			float a_duration,
			bool a_noMagnitude,
			bool a_noDuration) noexcept
		{
			if (!std::isfinite(a_baseCost) || a_baseCost <= 0.0f) {
				return 0.0f;
			}

			const double magnitude = !a_noMagnitude && std::isfinite(a_magnitude) ?
				(std::max)(1.0, static_cast<double>(a_magnitude)) : 1.0;
			const double duration = !a_noDuration && std::isfinite(a_duration) && a_duration > 0.0f ?
				static_cast<double>(a_duration) / 10.0 : 1.0;
			const double cost = static_cast<double>(a_baseCost) *
				std::pow(magnitude, 1.1) * std::pow(duration, 1.1);
			return std::isfinite(cost) && cost > 0.0 ? cost : 0.0;
		}

		inline EffectInput CalculateEffectInput(
			float a_sourceMagnitude,
			float a_sourceDuration,
			bool a_noMagnitude,
			bool a_noDuration,
			bool a_powerAffectsMagnitude,
			bool a_powerAffectsDuration,
			float a_magnitudePowerFactor,
			float a_durationPowerFactor) noexcept
		{
			EffectInput result;
			if ((a_powerAffectsMagnitude && !a_noMagnitude &&
				(!std::isfinite(a_magnitudePowerFactor) || a_magnitudePowerFactor <= 0.0f)) ||
				(a_powerAffectsDuration && !a_noDuration &&
					(!std::isfinite(a_durationPowerFactor) || a_durationPowerFactor <= 0.0f))) {
				return result;
			}

			result.magnitude = a_noMagnitude ? 0.0f :
				(std::isfinite(a_sourceMagnitude) ? (std::max)(0.0f, a_sourceMagnitude) :
					std::numeric_limits<float>::quiet_NaN());
			if (a_powerAffectsMagnitude && !a_noMagnitude) {
				result.magnitude = std::round(result.magnitude * a_magnitudePowerFactor);
			}

			result.duration = a_noDuration ? 0.0f :
				(!std::isfinite(a_sourceDuration) ? std::numeric_limits<float>::quiet_NaN() :
					(std::max)(0.0f, a_sourceDuration));
			if (a_powerAffectsDuration && !a_noDuration) {
				result.duration = std::round(result.duration * a_durationPowerFactor);
			}

			if ((!a_noMagnitude && !std::isfinite(result.magnitude)) ||
				(!a_noDuration && !std::isfinite(result.duration))) {
				return result;
			}
			result.valid = true;
			return result;
		}

		inline double CalculateEffectContribution(
			float a_baseCost,
			const EffectInput& a_input,
			bool a_noMagnitude,
			bool a_noDuration) noexcept
		{
			if (!a_input.valid ||
				(!a_noMagnitude && !std::isfinite(a_input.magnitude)) ||
				(!a_noDuration && !std::isfinite(a_input.duration))) {
				return 0.0;
			}
			return CalculateEffectCostPrecise(
				a_baseCost, a_input.magnitude, a_input.duration, a_noMagnitude, a_noDuration);
		}

		inline std::int32_t FloorGoldValue(double a_cost) noexcept
		{
			if (!std::isfinite(a_cost) || a_cost <= 0.0) {
				return 0;
			}
			const double maximum = static_cast<double>((std::numeric_limits<std::int32_t>::max)());
			return static_cast<std::int32_t>((std::min)(std::floor(a_cost), maximum));
		}

		inline constexpr bool ShouldRemoveOpposingAlchemyEffect(
			bool a_potion,
			bool a_beneficial,
			bool a_harmful) noexcept
		{
			return a_potion ? a_harmful : a_beneficial;
		}

		inline constexpr bool ShouldApplyBenefactor(
			bool a_potion,
			bool a_beneficial,
			bool a_includeTypePerks,
			bool a_mixedPotion = false) noexcept
		{
			return a_includeTypePerks && a_potion && !a_mixedPotion && a_beneficial;
		}

		inline constexpr bool ShouldApplyBenefactorFallback(
			bool a_potion,
			bool a_beneficial,
			bool a_includeTypePerks,
			bool a_hasBenefactor,
			bool a_benefactorApplied,
			bool a_benefactorEntryPointFound,
			bool a_mixedPotion = false) noexcept
		{
			return ShouldApplyBenefactor(a_potion, a_beneficial, a_includeTypePerks, a_mixedPotion) &&
				a_hasBenefactor && !a_benefactorApplied && !a_benefactorEntryPointFound;
		}

		inline constexpr bool ShouldApplyPhysicianFallback(
			bool a_cacoActive,
			bool a_hasPhysician,
			bool a_physicianApplied,
			bool a_physicianEffect) noexcept
		{
			return !a_cacoActive && a_hasPhysician && !a_physicianApplied && a_physicianEffect;
		}

		inline constexpr bool ShouldApplyPoisoner(
			bool a_potion,
			bool a_harmful,
			bool a_includeTypePerks) noexcept
		{
			return a_includeTypePerks && !a_potion && a_harmful;
		}

		inline float CalculateEffectCost(
			float a_baseCost,
			float a_magnitude,
			float a_duration,
			bool a_noMagnitude,
			bool a_noDuration) noexcept
		{
			const double cost = CalculateEffectCostPrecise(
				a_baseCost, a_magnitude, a_duration, a_noMagnitude, a_noDuration);
			return std::isfinite(cost) && cost <= static_cast<double>((std::numeric_limits<float>::max)()) ?
				static_cast<float>(cost) : 0.0f;
		}

		inline EffectValue CalculateEffectValue(
			float a_baseCost,
			const EffectInput& a_input,
			bool a_noMagnitude,
			bool a_noDuration) noexcept
		{
			EffectValue result;
			if (!std::isfinite(a_baseCost) || a_baseCost <= 0.0f) {
				return result;
			}

			const double cost = CalculateEffectContribution(a_baseCost, a_input, a_noMagnitude, a_noDuration);
			if (!std::isfinite(cost) || cost <= 0.0) {
				return result;
			}
			result.magnitude = a_input.magnitude;
			result.duration = a_input.duration;
			result.cost = cost;
			result.goldValue = FloorGoldValue(cost);
			result.valid = true;
			return result;
		}

		inline EffectValue CalculateEffectValue(
			float a_baseCost,
			float a_sourceMagnitude,
			float a_sourceDuration,
			bool a_noMagnitude,
			bool a_noDuration,
			bool a_powerAffectsMagnitude,
			bool a_powerAffectsDuration,
			float a_magnitudePowerFactor,
			float a_durationPowerFactor) noexcept
		{
			EffectValue result;
			if (!std::isfinite(a_baseCost) || a_baseCost <= 0.0f) {
				return result;
			}

			const auto input = CalculateEffectInput(
				a_sourceMagnitude,
				a_sourceDuration,
				a_noMagnitude,
				a_noDuration,
				a_powerAffectsMagnitude,
				a_powerAffectsDuration,
				a_magnitudePowerFactor,
				a_durationPowerFactor);
			return CalculateEffectValue(a_baseCost, input, a_noMagnitude, a_noDuration);
		}

		inline EffectValue CalculateEffectValue(
			float a_baseCost,
			float a_sourceMagnitude,
			float a_sourceDuration,
			bool a_noMagnitude,
			bool a_noDuration,
			bool a_powerAffectsMagnitude,
			bool a_powerAffectsDuration,
			float a_powerFactor) noexcept
		{
			return CalculateEffectValue(
				a_baseCost,
				a_sourceMagnitude,
				a_sourceDuration,
				a_noMagnitude,
				a_noDuration,
				a_powerAffectsMagnitude,
				a_powerAffectsDuration,
				a_powerFactor,
				a_powerFactor);
		}

		inline float CalculateEffectCost(
			float a_baseCost,
			float a_magnitude,
			float a_duration) noexcept
		{
			return CalculateEffectCost(a_baseCost, a_magnitude, a_duration, false, false);
		}

		inline constexpr std::int32_t ApplyImpureGold(std::int32_t a_preAdjustmentGold) noexcept
		{
			return a_preAdjustmentGold > 0 ? static_cast<std::int32_t>(a_preAdjustmentGold / 5) : 0;
		}
	}

	static_assert(algorithm::ApplyImpureGold(466) == 93);
	static_assert(algorithm::ApplyImpureGold(4) == 0);
	static_assert(algorithm::CalculateAlchemyEffectiveness(4.0f, 1.5f, 100.0f, 1.0f) > 5.99f);
	static_assert(algorithm::CalculateAlchemyEffectiveness(4.0f, 1.5f, 100.0f, 1.0f) < 6.01f);
	static_assert(algorithm::CalculateAlchemyEffectiveness(4.0f, 1.5f, 15.0f, 1.0f) > 4.29f);
	static_assert(algorithm::CalculateAlchemyEffectiveness(4.0f, 1.5f, 15.0f, 1.0f) < 4.31f);
	static_assert(algorithm::CalculateAlchemyEffectiveness(3.0f, 3.0f, 15.0f, 1.0f) > 3.89f);
	static_assert(algorithm::CalculateAlchemyEffectiveness(3.0f, 3.0f, 15.0f, 1.0f) < 3.91f);
	static_assert(algorithm::CalculateVanillaAlchemyEffectiveness(100.0f, 2.0f) > 11.99f);
	static_assert(algorithm::CalculateVanillaAlchemyEffectiveness(100.0f, 2.0f) < 12.01f);
	static_assert(algorithm::CalculateVanillaAlchemyEffectiveness(100.0f, 2.0f, 1.25f) > 14.99f);
	static_assert(algorithm::CalculateVanillaAlchemyEffectiveness(100.0f, 2.0f, 1.25f) < 15.01f);
	static_assert(algorithm::CalculateAlchemyActorValueMultiplier(15.0f) > 1.14f);
	static_assert(algorithm::CalculateAlchemyActorValueMultiplier(15.0f) < 1.16f);
	static_assert(algorithm::CalculateDurationBasedIngredientPowerFactor(3.9f) > 0.038f);
	static_assert(algorithm::CalculateDurationBasedIngredientPowerFactor(3.9f) < 0.040f);
	static_assert(algorithm::ShouldRemoveOpposingAlchemyEffect(true, true, false) == false);
	static_assert(algorithm::ShouldRemoveOpposingAlchemyEffect(true, false, true) == true);
	static_assert(algorithm::ShouldRemoveOpposingAlchemyEffect(false, true, false) == true);
	static_assert(algorithm::ShouldRemoveOpposingAlchemyEffect(false, false, true) == false);
	static_assert(algorithm::ShouldApplyBenefactor(true, true, true));
	static_assert(!algorithm::ShouldApplyBenefactor(true, true, true, true));
	static_assert(!algorithm::ShouldApplyBenefactor(false, true, true));
	static_assert(!algorithm::ShouldApplyBenefactor(true, false, true));
	static_assert(!algorithm::ShouldApplyBenefactor(true, true, false));
		static_assert(algorithm::ShouldApplyBenefactorFallback(true, true, true, true, false, false));
		static_assert(!algorithm::ShouldApplyBenefactorFallback(true, true, true, true, false, true));
		static_assert(!algorithm::ShouldApplyBenefactorFallback(true, true, true, true, true, false));
	static_assert(!algorithm::ShouldApplyBenefactorFallback(true, true, true, true, false, false, true));
	static_assert(algorithm::ShouldApplyPoisoner(false, true, true));
	static_assert(!algorithm::ShouldApplyPoisoner(true, true, true));
	static_assert(!algorithm::ShouldApplyPoisoner(false, false, true));
	static_assert(!algorithm::ShouldApplyPoisoner(false, true, false));

	// A pure one-effect result can use the authored CACO exemplar instead of a calculated value.
	struct ExemplarResult
	{
		const RE::AlchemyItem* potion = nullptr;
		std::int32_t goldValue = 0;
	};

	class Adapter final
	{
	public:
		static void Initialize() noexcept;
		static void Refresh() noexcept;

		[[nodiscard]] static bool IsDetected() noexcept;
		[[nodiscard]] static bool IsActive() noexcept;
		[[nodiscard]] static std::uint64_t GetCalculationRevision() noexcept;
		[[nodiscard]] static bool IsPotionHandlingEnabled() noexcept;
		[[nodiscard]] static bool IsImpureProcessingEnabled() noexcept;
		[[nodiscard]] static bool IsReweightingEnabled() noexcept;
		[[nodiscard]] static bool IsRenamingEnabled() noexcept;
		[[nodiscard]] static bool TryGetAlchemyEffectivenessMultiplier(
			float a_alchemyLevel,
			float a_fallbackPerkMultiplier,
			const AlchemyEvaluationContext& a_context,
			float& a_multiplier) noexcept;
		[[nodiscard]] static bool TryGetAlchemyEffectivenessMultiplier(
			const RE::EffectSetting* a_effect,
			float a_alchemyLevel,
			float a_fallbackPerkMultiplier,
			bool a_potion,
			bool a_includeTypePerks,
			bool a_mixedPotion,
			const AlchemyEvaluationContext& a_context,
			float& a_multiplier) noexcept;
		[[nodiscard]] static bool TryGetAlchemyEffectivenessMultipliers(
			const RE::EffectSetting* a_effect,
			float a_alchemyLevel,
			float a_fallbackPerkMultiplier,
			bool a_potion,
			bool a_includeTypePerks,
			bool a_mixedPotion,
			const AlchemyEvaluationContext& a_context,
			float& a_magnitudeMultiplier,
			float& a_durationMultiplier) noexcept;
		// Evaluates Skyrim/Alchemy Plus perk entry points without requiring CACO's records or settings.
		[[nodiscard]] static bool TryGetVanillaAlchemyEffectivenessMultipliers(
			const RE::EffectSetting* a_effect,
			float a_alchemyLevel,
			float a_fallbackAlchemistMultiplier,
			bool a_potion,
			bool a_includeTypePerks,
			bool a_mixedPotion,
			const AlchemyEvaluationContext& a_context,
			float& a_magnitudeMultiplier,
			float& a_durationMultiplier) noexcept;

		[[nodiscard]] static bool HasBeneficialKeyword(const RE::EffectSetting* a_effect) noexcept;
		[[nodiscard]] static bool HasHarmfulKeyword(const RE::EffectSetting* a_effect) noexcept;
		[[nodiscard]] static bool IsDurationBased(const RE::EffectSetting* a_effect) noexcept;
		[[nodiscard]] static RE::EffectSetting* ResolveIngredientEffect(
			const RE::IngredientItem* a_ingredient,
			RE::EffectSetting* a_sourceEffect) noexcept;

		[[nodiscard]] static float ApplyImpureMagnitude(float a_value) noexcept;
		[[nodiscard]] static float ApplyImpureDuration(float a_value) noexcept;
		[[nodiscard]] static std::int32_t ApplyImpureGold(std::int32_t a_value) noexcept;
		[[nodiscard]] static float ApplyImpureGold(float a_value) noexcept;

		[[nodiscard]] static bool TryGetCrucibleExemplar(
			const RE::EffectSetting* a_primaryEffect,
			float a_magnitude,
			float a_duration,
			ExemplarResult& a_result) noexcept;

		[[nodiscard]] static bool TryGetPotionName(
			std::size_t a_effectCount,
			const RE::EffectSetting* a_primaryEffect,
			float a_magnitude,
			float a_duration,
			std::string_view a_primaryName,
			std::string_view a_secondaryName,
			bool a_isPoison,
			bool a_impure,
			std::string_view a_potionPrefix,
			std::string_view a_poisonPrefix,
			std::string& a_name) noexcept;

		[[nodiscard]] static bool TryGetPotionWeight(
			std::size_t a_effectCount,
			bool a_hasBeneficial,
			bool a_hasHarmful,
			bool a_hasPurity,
			bool a_hasConcentratedPoison,
			float& a_weight) noexcept;
	};
}
