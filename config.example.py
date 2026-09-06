"""Machine-specific paths used by repository Python scripts."""

from pathlib import Path
from typing import Final


CMAKE_DIR: Final = Path(r"D:\Apps\CMake\bin")
NINJA_DIR: Final = Path(r"D:\Apps\ninja")
MO2_PROFILE: Final = Path(r"E:\Projects\games\skyrim\1.6.1170")
SKSE_SOURCE: Final = Path(r"E:\Projects\games\skyrim\skse\skse64_2_03_01\src")
SKYUI_SOURCE: Final = Path(r"E:\Projects\games\skyrim\skyui\SkyUI-Community")
SSEEDIT: Final = Path(r"E:\Projects\games\skyrim\SSEEdit")
SKYRIM_LOGS: Final = Path(r"D:\Documents\My Games\Skyrim Special Edition\SKSE")
VCPKG_STATIC_DIR: Final = Path(r"E:\Projects\games\skyrim\prosperous-alchemist\pa-ng\vcpkg-packages\x64-windows-static")
DLL_DEPLOY: Final = Path(
	r"E:\Projects\games\skyrim\1.6.1170\mods\Prosperous Alchemist NG\SKSE\Plugins\alchemist.dll"
)


PATHS: Final = {
	"cmake_dir": CMAKE_DIR,
	"ninja_dir": NINJA_DIR,
	"mo2_profile": MO2_PROFILE,
	"skse_source": SKSE_SOURCE,
	"skyui_source": SKYUI_SOURCE,
	"sseedit": SSEEDIT,
	"skyrim_logs": SKYRIM_LOGS,
	"vcpkg_static_dir": VCPKG_STATIC_DIR,
	"dll_deploy": DLL_DEPLOY,
}

__all__ = [
	"CMAKE_DIR",
	"DLL_DEPLOY",
	"MO2_PROFILE",
	"NINJA_DIR",
	"PATHS",
	"SKSE_SOURCE",
	"SKYRIM_LOGS",
	"SKYUI_SOURCE",
	"SSEEDIT",
	"VCPKG_STATIC_DIR",
]
