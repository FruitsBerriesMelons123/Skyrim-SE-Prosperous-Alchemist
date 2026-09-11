#include "Localization.h"

#include "PluginPaths.h"

#include <Windows.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>
#include <unordered_map>

namespace alchemist::localization {
	namespace {
		using json = nlohmann::json;

		std::unordered_map<std::string, std::string> translations;
		std::vector<std::filesystem::path> fontFiles;
		std::string selectedLocale = "en";

		std::string NormalizeLocale(std::string_view a_locale)
		{
			std::string locale;
			locale.reserve(a_locale.size());
			for (const auto character : a_locale) {
				if (character == '_') {
					locale.push_back('-');
				} else if (!std::isspace(static_cast<unsigned char>(character))) {
					locale.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
				}
			}
			return locale;
		}

		std::string DetectLocale()
		{
			WCHAR localeName[LOCALE_NAME_MAX_LENGTH]{};
			if (GetUserDefaultLocaleName(localeName, LOCALE_NAME_MAX_LENGTH) <= 0) {
				return "en";
			}
			int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, localeName, -1, nullptr, 0, nullptr, nullptr);
			if (length <= 1) {
				return "en";
			}
			std::string locale(static_cast<std::size_t>(length), '\0');
			if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, localeName, -1, locale.data(), length, nullptr, nullptr) <= 0) {
				return "en";
			}
			locale.resize(static_cast<std::size_t>(length - 1));
			return NormalizeLocale(locale);
		}

		bool IsValidUtf8(std::string_view a_text)
		{
			std::size_t index = 0;
			while (index < a_text.size()) {
				const auto byte = static_cast<unsigned char>(a_text[index]);
				std::size_t sequenceLength = 0;
				if (byte <= 0x7F) {
					sequenceLength = 1;
				} else if ((byte & 0xE0) == 0xC0) {
					sequenceLength = 2;
					if (byte < 0xC2) {
						return false;
					}
				} else if ((byte & 0xF0) == 0xE0) {
					sequenceLength = 3;
				} else if ((byte & 0xF8) == 0xF0) {
					sequenceLength = 4;
					if (byte > 0xF4) {
						return false;
					}
				} else {
					return false;
				}
				if (index + sequenceLength > a_text.size()) {
					return false;
				}
				for (std::size_t continuation = 1; continuation < sequenceLength; ++continuation) {
					if ((static_cast<unsigned char>(a_text[index + continuation]) & 0xC0) != 0x80) {
						return false;
					}
				}
				if (sequenceLength == 3) {
					const auto second = static_cast<unsigned char>(a_text[index + 1]);
					if ((byte == 0xE0 && second < 0xA0) || (byte == 0xED && second >= 0xA0)) {
						return false;
					}
				}
				if (sequenceLength == 4) {
					const auto second = static_cast<unsigned char>(a_text[index + 1]);
					if ((byte == 0xF0 && second < 0x90) || (byte == 0xF4 && second >= 0x90)) {
						return false;
					}
				}
				index += sequenceLength;
			}
			return true;
		}

		std::vector<std::string> LocaleCandidates(const std::string& a_locale)
		{
			std::vector<std::string> candidates{ "en" };
			const auto separator = a_locale.find('-');
			const auto baseLocale = separator == std::string::npos ? a_locale : a_locale.substr(0, separator);
			if (baseLocale != "en") {
				candidates.push_back(baseLocale);
			}
			if (a_locale != "en" && a_locale != baseLocale) {
				candidates.push_back(a_locale);
			}
			return candidates;
		}

		bool IsSafeFontPath(const std::filesystem::path& a_path)
		{
			if (a_path.empty() || a_path.is_absolute()) {
				return false;
			}
			for (const auto& part : a_path) {
				if (part == "..") {
					return false;
				}
			}
			return true;
		}

		void AddFontFile(const std::filesystem::path& a_fontsDirectory, const std::string& a_fontName)
		{
			const auto relativePath = std::filesystem::path(a_fontName);
			if (!IsSafeFontPath(relativePath)) {
				return;
			}
			const auto fontPath = a_fontsDirectory / relativePath;
			std::error_code ec;
			if (!std::filesystem::is_regular_file(fontPath, ec)) {
				return;
			}
			if (std::find(fontFiles.begin(), fontFiles.end(), fontPath) == fontFiles.end()) {
				fontFiles.push_back(fontPath);
			}
		}

		void LoadResource(const std::filesystem::path& a_localeDirectory, const std::filesystem::path& a_fontsDirectory, const std::string& a_locale)
		{
			const auto filePath = a_localeDirectory / ("alchemist." + a_locale + ".json");
			std::ifstream file(filePath, std::ios::binary);
			if (!file) {
				return;
			}

			try {
				const auto document = json::parse(file);
				if (!document.is_object()) {
					return;
				}
				if (document.contains("strings") && document.at("strings").is_object()) {
					for (const auto& [key, value] : document.at("strings").items()) {
						if (!value.is_string()) {
							continue;
						}
						const auto translated = value.get<std::string>();
						if (IsValidUtf8(key) && IsValidUtf8(translated)) {
							translations[key] = translated;
						}
					}
				}
				if (document.contains("fonts") && document.at("fonts").is_array()) {
					for (const auto& font : document.at("fonts")) {
						if (font.is_string()) {
							AddFontFile(a_fontsDirectory, font.get<std::string>());
						}
					}
				}
			} catch (const json::exception&) {
				return;
			}
		}
	}

	void Initialize(std::string_view a_configuredLocale)
	{
		translations.clear();
		fontFiles.clear();
		selectedLocale = NormalizeLocale(a_configuredLocale);
		if (selectedLocale.empty() || selectedLocale == "auto") {
			selectedLocale = DetectLocale();
		}
		if (selectedLocale.empty()) {
			selectedLocale = "en";
		}

		const auto pluginDirectory = paths::GetPluginDirectory();
		if (pluginDirectory.empty()) {
			return;
		}
		const auto localeDirectory = pluginDirectory / "locales";
		const auto fontsDirectory = pluginDirectory / "fonts";
		for (const auto& locale : LocaleCandidates(selectedLocale)) {
			LoadResource(localeDirectory, fontsDirectory, locale);
		}
	}

	std::string Translate(std::string_view a_key, std::string_view a_fallback)
	{
		const auto found = translations.find(std::string(a_key));
		return found == translations.end() ? std::string(a_fallback) : found->second;
	}

	std::string Format(
		std::string_view a_key,
		std::string_view a_fallback,
		std::initializer_list<FormatArgument> a_arguments)
	{
		auto result = Translate(a_key, a_fallback);
		for (const auto& [name, value] : a_arguments) {
			const std::string token = "{" + std::string(name) + "}";
			std::size_t position = 0;
			while ((position = result.find(token, position)) != std::string::npos) {
				result.replace(position, token.size(), value);
				position += value.size();
			}
		}
		return result;
	}

	std::string GetLocale()
	{
		return selectedLocale;
	}

	std::vector<std::filesystem::path> GetFontFiles()
	{
		return fontFiles;
	}
}
