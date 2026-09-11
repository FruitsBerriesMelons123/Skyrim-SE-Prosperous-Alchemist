#pragma once

#include <cstdint>

namespace alchemist::ui {
	void SetVisible(bool a_visible);
	void SetCursorPosition(float a_x, float a_y);
	void SetLeftMouseButtonDown(bool a_down);
	void AddMouseWheel(float a_delta);
	void AddInputCharacter(std::uint32_t a_codePoint);
	void UpdateImGuiMouseInput();
	void ProcessKeyboardInput();
	void ResetInputState();
	bool IsSearchInputFocused();
	void ClearSearchFocus();
	bool IsVisible();
	bool IsCursorOverWindow();
	void DrawCursor();
	void DrawWindow();
}
