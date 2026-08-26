#include "AlchemyPlus.h"

#include <Windows.h>

#include <RE/B/BSResourceNiBinaryStream.h>
#include <RE/E/EffectSetting.h>
#include <RE/T/TESDataHandler.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace alchemist::alchemyplus {
	namespace {
		constexpr char kConfigurationPath[] = "SKSE/Plugins/AlchemyPlus.json";
		constexpr std::uint32_t kMaximumConfigurationSize = 1024 * 1024;
		constexpr float kDefaultMagnitudeThreshold = 9999.0f;
		constexpr float kDefaultMagnitudeMultiple = 0.01f;
		constexpr float kDefaultDurationThreshold = 86313600.0f;
		constexpr float kDefaultDurationMultiple = 1.0f;

		struct RoundingSetting {
			float threshold;
			float multiple;
		};

		struct RoundingOverride {
			std::optional<float> threshold;
			std::optional<float> multiple;
		};

		struct RoundingState {
			bool enabled = false;
			RoundingSetting magnitude{ kDefaultMagnitudeThreshold, kDefaultMagnitudeMultiple };
			RoundingSetting duration{ kDefaultDurationThreshold, kDefaultDurationMultiple };
			std::map<const RE::EffectSetting*, RoundingOverride> magnitudeOverrides;
			std::map<const RE::EffectSetting*, RoundingOverride> durationOverrides;
			std::size_t ignoredOverrides = 0;
		};

		bool g_detected = false;
		bool g_active = false;
		bool g_configurationLoaded = false;
		bool g_impureCostFixEnabled = false;
		nlohmann::json g_configuration;
		RoundingState g_rounding;

		bool ReadConfiguration(RE::BSResourceNiBinaryStream& a_fileStream, std::string& a_contents)
		{
			if (!a_fileStream.good() || !a_fileStream.stream) {
				return false;
			}

			const auto size = a_fileStream.stream->totalSize;
			if (size == 0 || size > kMaximumConfigurationSize) {
				return false;
			}

			a_contents.resize(size);
			std::uint32_t totalRead = 0;
			while (totalRead < size) {
				std::uint64_t bytesRead = 0;
				const auto error = a_fileStream.stream->DoRead(a_contents.data() + totalRead, size - totalRead, bytesRead);
				if (error != RE::BSResource::ErrorCode::kNone || bytesRead == 0 || bytesRead > size - totalRead) {
					return false;
				}
				totalRead += static_cast<std::uint32_t>(bytesRead);
			}
			return true;
		}

		bool ReadEnabled(const nlohmann::json& a_object)
		{
			const auto iterator = a_object.find("enabled");
			return iterator != a_object.end() && iterator->is_boolean() && iterator->get<bool>();
		}

		bool ReadFeatureEnabled(const nlohmann::json& a_root, std::string_view a_name)
		{
			const auto iterator = a_root.find(a_name);
			return iterator != a_root.end() && iterator->is_object() && ReadEnabled(*iterator);
		}

		bool ReadFiniteNumber(const nlohmann::json& a_object, std::string_view a_name, float& a_value)
		{
			const auto iterator = a_object.find(a_name);
			if (iterator == a_object.end() || !iterator->is_number()) {
				return false;
			}

			try {
				const auto value = iterator->get<float>();
				if (!std::isfinite(value)) {
					return false;
				}
				a_value = value;
				return true;
			} catch (...) {
				return false;
			}
		}

		bool ParseFormIdentifier(std::string_view a_identifier, std::string& a_plugin, RE::FormID& a_formID)
		{
			const auto delimiter = a_identifier.find('|');
			if (delimiter == std::string_view::npos || delimiter == 0 || delimiter + 1 >= a_identifier.size() ||
				a_identifier.find('|', delimiter + 1) != std::string_view::npos) {
				return false;
			}

			a_plugin.assign(a_identifier.substr(0, delimiter));
			a_identifier.remove_prefix(delimiter + 1);
			if (a_identifier.starts_with("0x") || a_identifier.starts_with("0X")) {
				a_identifier.remove_prefix(2);
			}
			if (a_identifier.empty()) {
				return false;
			}

			std::uint64_t value = 0;
			constexpr auto maximum = (std::numeric_limits<RE::FormID>::max)();
			for (const auto character : a_identifier) {
				std::uint64_t digit = 0;
				if (character >= '0' && character <= '9') {
					digit = static_cast<std::uint64_t>(character - '0');
				} else if (character >= 'a' && character <= 'f') {
					digit = static_cast<std::uint64_t>(character - 'a' + 10);
				} else if (character >= 'A' && character <= 'F') {
					digit = static_cast<std::uint64_t>(character - 'A' + 10);
				} else {
					return false;
				}
				if (value > (maximum - digit) / 16) {
					return false;
				}
				value = value * 16 + digit;
			}

			a_formID = static_cast<RE::FormID>(value);
			return true;
		}

		RE::EffectSetting* ResolveEffectSetting(std::string_view a_identifier)
		{
			std::string plugin;
			RE::FormID formID = 0;
			if (!ParseFormIdentifier(a_identifier, plugin, formID)) {
				return nullptr;
			}

			try {
				auto* dataHandler = RE::TESDataHandler::GetSingleton();
				if (!dataHandler) {
					return nullptr;
				}
				auto* form = dataHandler->LookupForm(formID, plugin);
				return form ? form->As<RE::EffectSetting>() : nullptr;
			} catch (...) {
				return nullptr;
			}
		}

		void ReadRoundingOverride(
			const nlohmann::json& a_object,
			RoundingOverride& a_override,
			bool& a_hasValue,
			bool a_allowThreshold,
			bool a_allowMultiple)
		{
			float value = 0.0f;
			if (a_allowThreshold && ReadFiniteNumber(a_object, "magnitudeThreshold", value)) {
				a_override.threshold = value;
				a_hasValue = true;
			}
			if (a_allowMultiple && ReadFiniteNumber(a_object, "magnitudeMult", value) && value > 0.0f) {
				a_override.multiple = value;
				a_hasValue = true;
			}
		}

		void ReadDurationRoundingOverride(
			const nlohmann::json& a_object,
			RoundingOverride& a_override,
			bool& a_hasValue)
		{
			float value = 0.0f;
			if (ReadFiniteNumber(a_object, "durationThreshold", value)) {
				a_override.threshold = value;
				a_hasValue = true;
			}
			if (ReadFiniteNumber(a_object, "durationMult", value) && value > 0.0f) {
				a_override.multiple = value;
				a_hasValue = true;
			}
		}

		bool ReadRoundingConfiguration(const nlohmann::json& a_root, RoundingState& a_state)
		{
			const auto iterator = a_root.find("roundedPotency");
			if (iterator == a_root.end() || !iterator->is_object() || !ReadEnabled(*iterator)) {
				return false;
			}

			RoundingState state;
			state.enabled = true;
			if (!ReadFiniteNumber(*iterator, "magnitudeThreshold", state.magnitude.threshold) ||
				!ReadFiniteNumber(*iterator, "magnitudeMult", state.magnitude.multiple) ||
				!ReadFiniteNumber(*iterator, "durationThreshold", state.duration.threshold) ||
				!ReadFiniteNumber(*iterator, "durationMult", state.duration.multiple) ||
				state.magnitude.multiple <= 0.0f || state.duration.multiple <= 0.0f) {
				return false;
			}

			const auto overrides = iterator->find("overrides");
			if (overrides != iterator->end() && !overrides->is_object()) {
				state.ignoredOverrides = 1;
			} else if (overrides != iterator->end()) {
				for (const auto& [identifier, value] : overrides->items()) {
					if (identifier.empty() || identifier.front() == '$' || !value.is_object()) {
						++state.ignoredOverrides;
						continue;
					}

					const auto* baseEffect = ResolveEffectSetting(identifier);
					if (!baseEffect) {
						++state.ignoredOverrides;
						continue;
					}

					RoundingOverride magnitudeOverride;
					bool hasMagnitudeValue = false;
					ReadRoundingOverride(value, magnitudeOverride, hasMagnitudeValue, true, true);
					if (hasMagnitudeValue) {
						state.magnitudeOverrides[baseEffect] = magnitudeOverride;
					}

					RoundingOverride durationOverride;
					bool hasDurationValue = false;
					ReadDurationRoundingOverride(value, durationOverride, hasDurationValue);
					if (hasDurationValue) {
						state.durationOverrides[baseEffect] = durationOverride;
					}
				}
			}

			a_state = std::move(state);
			return true;
		}
	}
	void Adapter::Initialize() noexcept
	{
		g_detected = false;
		g_active = false;
		g_configurationLoaded = false;
		g_impureCostFixEnabled = false;
		g_configuration.clear();
		g_rounding = {};

		try {
			g_detected = ::GetModuleHandleW(L"AlchemyPlus.dll") != nullptr;
			if (!g_detected) {
				return;
			}

			RE::BSResourceNiBinaryStream fileStream{ kConfigurationPath };
			if (!fileStream.good()) {
				return;
			}

			std::string contents;
			if (!ReadConfiguration(fileStream, contents)) {
				return;
			}

			auto configuration = nlohmann::json::parse(contents);
			if (!configuration.is_object()) {
				return;
			}

			g_configuration = std::move(configuration);
			g_configurationLoaded = true;
			g_active = ReadRoundingConfiguration(g_configuration, g_rounding);
			g_impureCostFixEnabled = ReadFeatureEnabled(g_configuration, "impureCostFix");
			g_active = g_active || g_impureCostFixEnabled;
		} catch (...) {
			g_active = false;
			g_configurationLoaded = false;
			g_impureCostFixEnabled = false;
			g_configuration.clear();
			g_rounding = {};
		}
	}

	bool Adapter::IsDetected() noexcept
	{
		return g_detected;
	}

	bool Adapter::IsActive() noexcept
	{
		return g_detected && g_active;
	}

	bool Adapter::IsRoundingEnabled() noexcept
	{
		return g_detected && g_configurationLoaded && g_rounding.enabled;
	}

	bool Adapter::IsImpureCostFixEnabled() noexcept
	{
		return g_detected && g_configurationLoaded && g_impureCostFixEnabled;
	}

	bool Adapter::GetMagnitudeRounding(const RE::EffectSetting* a_baseEffect, float& a_threshold, float& a_multiple) noexcept
	{
		if (!IsRoundingEnabled() || !a_baseEffect) {
			return false;
		}

		RoundingSetting setting = g_rounding.magnitude;
		if (const auto iterator = g_rounding.magnitudeOverrides.find(a_baseEffect); iterator != g_rounding.magnitudeOverrides.end()) {
			if (iterator->second.threshold.has_value()) {
				setting.threshold = iterator->second.threshold.value();
			}
			if (iterator->second.multiple.has_value()) {
				setting.multiple = iterator->second.multiple.value();
			}
		}
		a_threshold = setting.threshold;
		a_multiple = setting.multiple;
		return true;
	}

	bool Adapter::GetDurationRounding(const RE::EffectSetting* a_baseEffect, float& a_threshold, float& a_multiple) noexcept
	{
		if (!IsRoundingEnabled() || !a_baseEffect) {
			return false;
		}

		RoundingSetting setting = g_rounding.duration;
		if (const auto iterator = g_rounding.durationOverrides.find(a_baseEffect); iterator != g_rounding.durationOverrides.end()) {
			if (iterator->second.threshold.has_value()) {
				setting.threshold = iterator->second.threshold.value();
			}
			if (iterator->second.multiple.has_value()) {
				setting.multiple = iterator->second.multiple.value();
			}
		}
		a_threshold = setting.threshold;
		a_multiple = setting.multiple;
		return true;
	}

	float Adapter::ApplyMagnitudeRounding(const RE::EffectSetting* a_baseEffect, float a_value) noexcept
	{
		float threshold = 0.0f;
		float multiple = 0.0f;
		if (!GetMagnitudeRounding(a_baseEffect, threshold, multiple) || !std::isfinite(a_value) ||
			!std::isfinite(threshold) || !std::isfinite(multiple) || multiple <= 0.0f || a_value <= threshold) {
			return a_value;
		}

		float result = a_value;
		result += multiple * 0.5f;
		if (!std::isfinite(result)) {
			return a_value;
		}
		result -= std::remainderf(result, multiple);
		return std::isfinite(result) ? result : a_value;
	}

	float Adapter::ApplyDurationRounding(const RE::EffectSetting* a_baseEffect, float a_value) noexcept
	{
		float threshold = 0.0f;
		float multiple = 0.0f;
		if (!GetDurationRounding(a_baseEffect, threshold, multiple) || !std::isfinite(a_value) ||
			!std::isfinite(threshold) || !std::isfinite(multiple) || multiple <= 0.0f || a_value <= threshold) {
			return a_value;
		}

		float result = a_value;
		result += multiple * 0.5f;
		if (!std::isfinite(result)) {
			return a_value;
		}
		result -= std::remainderf(result, multiple);
		if (!std::isfinite(result) || result < -2147483648.0f || result > 2147483520.0f) {
			return a_value;
		}
		return static_cast<float>(static_cast<std::int32_t>(result));
	}

	float Adapter::AdjustImpureEffectCost(float a_effectCost, bool a_isPoison, bool a_isHostile, bool& a_impure) noexcept
	{
		if (!IsImpureCostFixEnabled() || a_isPoison == a_isHostile) {
			return a_effectCost;
		}

		a_impure = true;
		return -a_effectCost;
	}

	float Adapter::FinalizeImpureCost(float a_cost) noexcept
	{
		if (!std::isfinite(a_cost) || a_cost <= 0.0f) {
			return 0.0f;
		}

		constexpr float maximum = 2147483520.0f;
		return static_cast<float>(static_cast<std::int32_t>((std::min)(a_cost, maximum)));
	}

	const nlohmann::json* Adapter::GetConfiguration() noexcept
	{
		return g_detected && g_configurationLoaded ? &g_configuration : nullptr;
	}
}
