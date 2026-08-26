#pragma once

namespace alchemist::ui {
	void SetVisible(bool a_visible);
	void SetCursorPosition(float a_x, float a_y);
	void SetLeftMouseButtonDown(bool a_down);
	void AddMouseWheel(float a_delta);
	void UpdateImGuiMouseInput();
	void ProcessKeyboardInput();
	void ResetInputState();
	bool IsSearchInputFocused();
	bool IsVisible();
	bool IsCursorOverWindow();
	void DrawCursor();
	void DrawWindow();
}
