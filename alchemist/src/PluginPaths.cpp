#include "PluginPaths.h"

#include <Windows.h>

#include <string>

namespace alchemist::paths {
	namespace {
		std::filesystem::path GetModulePath()
		{
			HMODULE module = nullptr;
			if (!GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&GetModulePath),
				&module)) {
				return {};
			}

			std::wstring buffer(260, L'\0');
			for (;;) {
				const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
				if (length == 0) {
					return {};
				}
				if (length < buffer.size() - 1) {
					buffer.resize(length);
					return std::filesystem::path(buffer);
				}
				buffer.resize(buffer.size() * 2);
			}
		}
	}

	std::filesystem::path GetPluginDirectory()
	{
		return GetModulePath().parent_path();
	}
}
