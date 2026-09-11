#include "RenderHook.h"

#include "AlchemistWindow.h"
#include "Localization.h"
#include "MenuHandler.h"
#include "RE/R/Renderer.h"

#include <Windows.h>
#include <d3d11.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace alchemist::render {
	namespace {
		using PresentFunction = REX::W32::HRESULT(__stdcall*)(REX::W32::IDXGISwapChain*, unsigned, unsigned);

		PresentFunction originalPresent = nullptr;
		HWND gameWindowHandle = nullptr;
		std::chrono::steady_clock::time_point lastFrameTime;
		bool lastOverlayVisible = false;
		bool overlayStateKnown = false;
		bool imguiInitialized = false;

		void SynchronizeSkyrimCursor(float& a_cursorX, float& a_cursorY)
		{
			const auto displaySize = ImGui::GetIO().DisplaySize;
			menu::CursorSnapshot cursorSnapshot;
			if (menu::GetCursorSnapshot(cursorSnapshot) && displaySize.x > 0.0f && displaySize.y > 0.0f) {
				a_cursorX = cursorSnapshot.x / cursorSnapshot.width * displaySize.x;
				a_cursorY = cursorSnapshot.y / cursorSnapshot.height * displaySize.y;
				ui::SetCursorPosition(
					a_cursorX,
					a_cursorY);
				return;
			}

			if (!gameWindowHandle) {
				return;
			}

			RECT clientRect{};
			POINT cursorPosition{};
			if (displaySize.x <= 0.0f || displaySize.y <= 0.0f ||
				!GetClientRect(gameWindowHandle, &clientRect) ||
				!GetCursorPos(&cursorPosition) || !ScreenToClient(gameWindowHandle, &cursorPosition)) {
				return;
			}
			const auto clientWidth = clientRect.right - clientRect.left;
			const auto clientHeight = clientRect.bottom - clientRect.top;
			if (clientWidth <= 0 || clientHeight <= 0) {
				return;
			}

			a_cursorX = static_cast<float>(cursorPosition.x) / static_cast<float>(clientWidth) * displaySize.x;
			a_cursorY = static_cast<float>(cursorPosition.y) / static_cast<float>(clientHeight) * displaySize.y;
			ui::SetCursorPosition(
				a_cursorX,
				a_cursorY);
		}

		void UpdateImGuiFrameState()
		{
			auto& io = ImGui::GetIO();
			RECT clientRect{};
			if (GetClientRect(gameWindowHandle, &clientRect)) {
				io.DisplaySize = ImVec2(
					static_cast<float>(clientRect.right - clientRect.left),
					static_cast<float>(clientRect.bottom - clientRect.top));
			}

			const auto currentTime = std::chrono::steady_clock::now();
			if (lastFrameTime == std::chrono::steady_clock::time_point{}) {
				io.DeltaTime = 1.0f / 60.0f;
			} else {
				io.DeltaTime = std::chrono::duration<float>(currentTime - lastFrameTime).count();
				if (io.DeltaTime <= 0.0f || io.DeltaTime > 1.0f) {
					io.DeltaTime = 1.0f / 60.0f;
				}
			}
			lastFrameTime = currentTime;
		}

		bool InitializeImGui()
		{
			if (imguiInitialized) {
				return true;
			}

			auto* window = RE::BSGraphics::Renderer::GetCurrentRenderWindow();
			auto* rendererData = RE::BSGraphics::Renderer::GetRendererDataSingleton();
			auto* device = RE::BSGraphics::Renderer::GetDevice();
			if (!window) {
				return false;
			}
			if (!window->hWnd) {
				return false;
			}
			if (!rendererData) {
				return false;
			}
			if (!rendererData->context) {
				return false;
			}
			if (!device) {
				return false;
			}

			const auto hwnd = reinterpret_cast<HWND>(window->hWnd);
			gameWindowHandle = hwnd;
			if (!ImGui::GetCurrentContext()) {
				ImGui::CreateContext();
			}
			auto& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
			io.MouseDrawCursor = false;
			ImGui::StyleColorsDark();

			RECT clientRect{};
			float displayHeight = 1080.0f;
			if (GetClientRect(hwnd, &clientRect)) {
				const auto height = static_cast<float>(clientRect.bottom - clientRect.top);
				if (height > 0.0f) {
					displayHeight = height;
				}
			}
			const float fontScale = std::clamp(displayHeight / 1080.0f, 1.0f, 3.0f);
			const float fontSize = std::round(16.0f * fontScale);

			static ImVector<ImWchar> glyphRanges;
			if (glyphRanges.empty()) {
				ImFontGlyphRangesBuilder builder;
				builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
				builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
				static const ImWchar multilingualRanges[] = {
					0x0100, 0x024F, // Latin Extended
					0x0370, 0x03FF, // Greek and Coptic
					0x0500, 0x052F, // Cyrillic Supplement
					0x0530, 0x058F, // Armenian
					0x0590, 0x05FF, // Hebrew
					0x0600, 0x06FF, // Arabic
					0x0750, 0x077F, // Arabic Supplement
					0x08A0, 0x08FF, // Arabic Extended-A
					0x0900, 0x097F, // Devanagari
					0x0980, 0x09FF, // Bengali
					0x0A00, 0x0A7F, // Gurmukhi
					0x0A80, 0x0AFF, // Gujarati
					0x0B00, 0x0B7F, // Oriya
					0x0B80, 0x0BFF, // Tamil
					0x0C00, 0x0C7F, // Telugu
					0x0C80, 0x0CFF, // Kannada
					0x0D00, 0x0D7F, // Malayalam
					0x0D80, 0x0DFF, // Sinhala
					0x0E00, 0x0E7F, // Thai
					0x0E80, 0x0EFF, // Lao
					0x0F00, 0x0FFF, // Tibetan
					0x1000, 0x109F, // Myanmar
					0x10A0, 0x10FF, // Georgian
					0x1100, 0x11FF, // Hangul Jamo
					0x1200, 0x137F, // Ethiopic
					0x1780, 0x17FF, // Khmer
					0x2000, 0x206F, // General Punctuation
					0x20A0, 0x20CF, // Currency Symbols
					0x2100, 0x214F, // Letterlike Symbols
					0x2190, 0x21FF, // Arrows
					0x2200, 0x22FF, // Mathematical Operators
					0x2300, 0x23FF, // Miscellaneous Technical
					0x2500, 0x257F, // Box Drawing
					0x25A0, 0x25FF, // Geometric Shapes
					0x2600, 0x26FF, // Miscellaneous Symbols
					0x2700, 0x27BF, // Dingbats
					0x2B00, 0x2BFF, // Miscellaneous Symbols and Arrows
					0x3040, 0x30FF, // Hiragana and Katakana
					0x3100, 0x312F, // Bopomofo
					0x3400, 0x4DBF, // CJK Extension A
					0x4E00, 0x9FFF, // CJK Unified Ideographs
					0xAC00, 0xD7AF, // Hangul Syllables
					0xF900, 0xFAFF, // CJK Compatibility Ideographs
					0
				};
				builder.AddRanges(multilingualRanges);
				builder.BuildRanges(&glyphRanges);
			}

			std::vector<std::filesystem::path> fontCandidates = localization::GetFontFiles();
			const auto locale = localization::GetLocale();
			std::array<wchar_t, MAX_PATH> windowsDirectory{};
			const auto windowsDirectoryLength = GetWindowsDirectoryW(windowsDirectory.data(), static_cast<UINT>(windowsDirectory.size()));
			const auto systemFontsDirectory = windowsDirectoryLength > 0 ?
				std::filesystem::path(windowsDirectory.data(), windowsDirectory.data() + windowsDirectoryLength) / "Fonts" :
				std::filesystem::path();
			const auto addSystemFont = [&fontCandidates, &systemFontsDirectory](const wchar_t* a_name) {
				if (!systemFontsDirectory.empty()) {
					const auto path = systemFontsDirectory / a_name;
					if (std::find(fontCandidates.begin(), fontCandidates.end(), path) == fontCandidates.end()) {
						fontCandidates.push_back(path);
					}
				}
			};
			if (locale.rfind("zh", 0) == 0) {
				if (locale.find("tw") != std::string::npos || locale.find("hk") != std::string::npos || locale.find("mo") != std::string::npos) {
					addSystemFont(L"msjh.ttc");
					addSystemFont(L"mingliu.ttc");
				} else {
					addSystemFont(L"msyh.ttc");
					addSystemFont(L"simsun.ttc");
				}
			} else if (locale.rfind("ja", 0) == 0) {
				addSystemFont(L"meiryo.ttc");
				addSystemFont(L"msgothic.ttc");
			} else if (locale.rfind("ko", 0) == 0) {
				addSystemFont(L"malgun.ttf");
				addSystemFont(L"gulim.ttc");
			}
			addSystemFont(L"segoeui.ttf");
			addSystemFont(L"tahoma.ttf");
			addSystemFont(L"arial.ttf");
			addSystemFont(L"seguisym.ttf");

			static std::vector<std::vector<char>> fontData;
			fontData.clear();
			fontData.reserve(fontCandidates.size());
			bool fontLoaded = false;
			for (const auto& fontPath : fontCandidates) {
				std::error_code ec;
				if (std::filesystem::is_regular_file(fontPath, ec)) {
					std::ifstream file(fontPath, std::ios::binary | std::ios::ate);
					if (!file) {
						continue;
					}
					const auto fileSize = file.tellg();
					if (fileSize <= 0) {
						continue;
					}
					fontData.emplace_back(static_cast<std::size_t>(fileSize), '\0');
					file.seekg(0);
					file.read(fontData.back().data(), fileSize);
					if (!file) {
						fontData.pop_back();
						continue;
					}
					ImFontConfig fontConfig;
					fontConfig.OversampleH = 2;
					fontConfig.OversampleV = 2;
					fontConfig.PixelSnapH = true;
					fontConfig.MergeMode = fontLoaded;
					fontConfig.FontDataOwnedByAtlas = false;
					if (io.Fonts->AddFontFromMemoryTTF(fontData.back().data(), static_cast<int>(fontData.back().size()), fontSize, &fontConfig, glyphRanges.Data)) {
						fontLoaded = true;
					} else {
						fontData.pop_back();
					}
				}
			}
			if (!fontLoaded) {
				io.Fonts->AddFontDefault();
			}

			if (!ImGui_ImplDX11_Init(reinterpret_cast<ID3D11Device*>(device), reinterpret_cast<ID3D11DeviceContext*>(rendererData->context))) {
				return false;
			}

			imguiInitialized = true;
			return true;
		}

		// Note for Skyrim VR: DXGI swap chain present hooks intercept desktop window presentation.
		// In Skyrim VR, the ImGui overlay renders to this desktop mirror/companion window (viewable
		// in VR via SteamVR desktop overlay or Desktop+). Direct OpenVR compositor hooking is avoided
		// to ensure stability and eliminate VR render thread crashes.
		REX::W32::HRESULT __stdcall PresentHook(REX::W32::IDXGISwapChain* a_swapChain, unsigned a_syncInterval, unsigned a_flags)
		{
			if (InitializeImGui()) {
				const auto overlayVisible = ui::IsVisible();
				const bool overlayStateChanged = !overlayStateKnown || overlayVisible != lastOverlayVisible;
				lastOverlayVisible = overlayVisible;
				overlayStateKnown = true;
				if (!overlayVisible || overlayStateChanged) {
					ui::ResetInputState();
				}
				UpdateImGuiFrameState();
				ImGui_ImplDX11_NewFrame();
				float cursorX = -1.0f;
				float cursorY = -1.0f;
				SynchronizeSkyrimCursor(cursorX, cursorY);
				ui::UpdateImGuiMouseInput();
				ui::ProcessKeyboardInput();
				ImGui::NewFrame();
				if (overlayVisible) {
					ui::DrawWindow();
					ui::DrawCursor();
				}
				ImGui::SetMouseCursor(ImGuiMouseCursor_None);
				ImGui::GetIO().MouseDrawCursor = false;
				ImGui::Render();

				auto* rendererData = RE::BSGraphics::Renderer::GetRendererDataSingleton();
				auto* window = RE::BSGraphics::Renderer::GetCurrentRenderWindow();
				if (overlayVisible && rendererData && rendererData->context && window && window->renderView) {
					auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
					auto* renderTarget = reinterpret_cast<ID3D11RenderTargetView*>(window->renderView);
					context->OMSetRenderTargets(1, &renderTarget, nullptr);
					ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
				}
			}
			return originalPresent ? originalPresent(a_swapChain, a_syncInterval, a_flags) : static_cast<REX::W32::HRESULT>(0);
		}
	}

	bool Install()
	{
		if (originalPresent) {
			return true;
		}

		auto* window = RE::BSGraphics::Renderer::GetCurrentRenderWindow();
		if (!window || !window->swapChain) {
			return false;
		}

		auto vtable = *reinterpret_cast<std::uintptr_t**>(window->swapChain);
		if (!vtable) {
			return false;
		}
		REL::Relocation<std::uintptr_t> swapChainVtable{ reinterpret_cast<std::uintptr_t>(vtable) };
		const auto previousPresent = reinterpret_cast<PresentFunction>(swapChainVtable.write_vfunc(8, &PresentHook));
		if (!previousPresent) {
			return false;
		}
		originalPresent = previousPresent;
		return true;
	}

	HWND GetGameWindowHandle()
	{
		return gameWindowHandle;
	}
}

