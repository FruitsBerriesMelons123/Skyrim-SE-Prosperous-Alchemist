#pragma once

namespace alchemist::menu {
	struct CursorSnapshot {
		float x = -1.0f;
		float y = -1.0f;
		float width = 0.0f;
		float height = 0.0f;
	};

	void Register();
	void RequestRecalculation();
	void RefreshAlchemyMenu(bool a_hasPurityPerk);
	bool GetCursorSnapshot(CursorSnapshot& a_snapshot);
}
