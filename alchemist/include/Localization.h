#pragma once

#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace alchemist::localization {
	using FormatArgument = std::pair<std::string_view, std::string_view>;

	void Initialize(std::string_view a_configuredLocale);
	std::string Translate(std::string_view a_key, std::string_view a_fallback);
	std::string Format(
		std::string_view a_key,
		std::string_view a_fallback,
		std::initializer_list<FormatArgument> a_arguments);
	std::string GetLocale();
	std::vector<std::filesystem::path> GetFontFiles();
}
