#include "RenderHook.h"

#include "AlchemistWindow.h"
#include "MenuHandler.h"
#include "RE/R/Renderer.h"

#include <Windows.h>
#include <d3d11.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>

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
				static const ImWchar latinExtended[] = {
					0x0100, 0x024F, // Latin Extended-A and Latin Extended-B
					0
				};
				builder.AddRanges(latinExtended);
				static const ImWchar arrowsRange[] = {
					0x2190, 0x21FF, // Arrows (including ↑ U+2191 and ↓ U+2193)
					0
				};
				builder.AddRanges(arrowsRange);
				builder.BuildRanges(&glyphRanges);
			}

			const char* fontCandidates[] = {
				"C:\\Windows\\Fonts\\segoeui.ttf",
				"C:\\Windows\\Fonts\\arial.ttf"
			};
			bool fontLoaded = false;
			for (const auto* fontPath : fontCandidates) {
				std::error_code ec;
				if (std::filesystem::exists(fontPath, ec)) {
					ImFontConfig fontConfig;
					fontConfig.OversampleH = 2;
					fontConfig.OversampleV = 2;
					fontConfig.PixelSnapH = true;
					if (io.Fonts->AddFontFromFileTTF(fontPath, fontSize, &fontConfig, glyphRanges.Data)) {
						fontLoaded = true;
						break;
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
				ui::SetLeftMouseButtonDown((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
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

