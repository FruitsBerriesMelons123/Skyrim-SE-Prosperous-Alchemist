"""EXAMPLE ONLY Machine-specific paths used by repository Python scripts."""

from pathlib import Path
from typing import Final


MO2_PROFILE: Final = Path(r"<path-to>\_steam_latest")
SKSE_SOURCE: Final = Path(r"<path-to>\src")
SKYUI_SOURCE: Final = Path(r"<path-to>\SkyUI-Community")
SSEEDIT: Final = Path(r"<path-to>\SSEEdit")
SKYRIM_LOGS: Final = Path(r"<path-to>\Documents\My Games\Skyrim Special Edition\SKSE")
VCPKG_STATIC_DIR: Final = Path(r"<path-to>\prosperous-alchemist-vcpkg\x64-windows-static")
DLL_DEPLOY: Final = Path(
	r"<path-to>\_steam_latest\mods\Prosperous Alchemist NG\SKSE\Plugins\alchemist.dll"
)


PATHS: Final = {
	"mo2_profile": MO2_PROFILE,
	"skse_source": SKSE_SOURCE,
	"skyui_source": SKYUI_SOURCE,
	"sseedit": SSEEDIT,
	"skyrim_logs": SKYRIM_LOGS,
	"vcpkg_static_dir": VCPKG_STATIC_DIR,
	"dll_deploy": DLL_DEPLOY,
}

__all__ = [
	"DLL_DEPLOY",
	"MO2_PROFILE",
	"PATHS",
	"SKSE_SOURCE",
	"SKYRIM_LOGS",
	"SKYUI_SOURCE",
	"SSEEDIT",
	"VCPKG_STATIC_DIR",
]
