"""Build and deploy the Release plugin without cleaning CommonLibSSE-NG artifacts.

The wrapper is intentionally Windows/x64-focused: it configures the standalone
``alchemist`` CMake consumer with Ninja, validates a fresh DLL, and copies that
DLL to the locally configured deployment path.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import queue
import shutil
import subprocess
import sys
import threading
import time
import zipfile
from datetime import datetime
from pathlib import Path


build_log = None
status_file = None

BUILD_TYPE = "Release"
GENERATOR = "Ninja"
TARGET = "alchemist"
PLUGIN_CLEAN_RULES = (
	"CXX_SCAN__alchemist_Release",
	"CXX_DYNDEP__alchemist_Release",
	"CXX_COMPILER__alchemist_scanned_Release",
	"CXX_COMPILER__alchemist_unscanned_Release",
	"RC_COMPILER__alchemist_unscanned_Release",
	"CXX_SHARED_LIBRARY_LINKER__alchemist_Release",
)
# These rules remove only plugin outputs. CommonLibSSE-NG and its fetched dependencies
# are deliberately retained so routine plugin rebuilds do not trigger a full rebuild.


def timestamp() -> str:
	return datetime.now().astimezone().isoformat(timespec="seconds")


def report(message: str) -> None:
	line = f"[{timestamp()}] {message}"
	print(line, flush=True)
	if build_log is not None:
		build_log.write(line + "\n")
		build_log.flush()


def x64_msvc_environment() -> dict[str, str]:
	environment = os.environ.copy()
	if (
		environment.get("VSCMD_ARG_TGT_ARCH") == "x64"
		and environment.get("VSCMD_ARG_HOST_ARCH") == "x64"
		and any(
			(Path(include_path) / "string_view").is_file()
			for include_path in environment.get("INCLUDE", "").split(os.pathsep)
			if include_path
		)
	):
		return environment

	vsdevcmd = None
	vc_install_dir = environment.get("VCINSTALLDIR")
	if vc_install_dir:
		candidate = Path(vc_install_dir).resolve().parent / "Common7" / "Tools" / "VsDevCmd.bat"
		if candidate.is_file():
			vsdevcmd = candidate

	if vsdevcmd is None:
		vswhere_paths = [
			Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe",
			Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe",
		]
		for vswhere in vswhere_paths:
			if vswhere.is_file():
				try:
					res = subprocess.run(
						[
							str(vswhere),
							"-latest",
							"-products",
							"*",
							"-requires",
							"Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
							"-property",
							"installationPath",
						],
						capture_output=True,
						text=True,
						check=True,
					)
					install_path = res.stdout.strip()
					if install_path:
						candidate = Path(install_path) / "Common7" / "Tools" / "VsDevCmd.bat"
						if candidate.is_file():
							vsdevcmd = candidate
							break
				except Exception:
					pass

	if not vsdevcmd or not vsdevcmd.is_file():
		raise RuntimeError("VsDevCmd.bat not found; ensure Visual Studio C++ tools are installed or run from an MSVC developer shell")

	stale_toolchain_names = {
		"VCINSTALLDIR",
		"VCToolsInstallDir",
		"VCToolsVersion",
		"DevEnvDir",
		"INCLUDE",
		"LIB",
		"LIBPATH",
		"UniversalCRTSdkDir",
		"UCRTVersion",
		"VSINSTALLDIR",
		"WindowsSdkDir",
	}
	toolchain_environment = {
		key: value
		for key, value in environment.items()
		if not key.upper().startswith("VSCMD_") and key.upper() not in stale_toolchain_names
	}
	command = f'call "{vsdevcmd}" -arch=x64 -host_arch=x64 >nul && set'
	result = subprocess.run(
		f"cmd.exe /d /c {command}",
		stdout=subprocess.PIPE,
		stderr=subprocess.STDOUT,
		text=True,
		encoding="utf-8",
		errors="replace",
		env=toolchain_environment,
	)
	if result.returncode != 0:
		raise RuntimeError(f"x64 MSVC setup failed with exit={result.returncode}: {result.stdout.strip()}")
	environment = toolchain_environment
	for line in result.stdout.splitlines():
		key, separator, value = line.partition("=")
		if separator and key:
			environment_key = next(
				(existing_key for existing_key in environment if existing_key.lower() == key.lower()),
				key,
			)
			environment[environment_key] = value
	report("Configured the MSVC environment for x64.")
	return environment


def run_process(
	command: list[str],
	repo_root: Path,
	environment: dict[str, str] | None = None,
) -> int:
	resolved_command = list(command)
	if environment and "PATH" in environment and not Path(resolved_command[0]).is_file():
		found_path = shutil.which(resolved_command[0], path=environment.get("PATH"))
		if found_path:
			resolved_command[0] = found_path
	report("Running: " + " ".join(resolved_command))
	process = subprocess.Popen(
		resolved_command,
		cwd=repo_root,
		env=environment,
		stdout=subprocess.PIPE,
		stderr=subprocess.STDOUT,
		text=True,
		encoding="utf-8",
		errors="replace",
		bufsize=1,
	)
	output: queue.Queue[str | None] = queue.Queue()

	def read_output() -> None:
		assert process.stdout is not None
		for line in process.stdout:
			output.put(line.rstrip())
		output.put(None)

	reader = threading.Thread(target=read_output, daemon=True)
	reader.start()
	output_closed = False
	while not output_closed or process.poll() is None:
		try:
			line = output.get(timeout=10)
		except queue.Empty:
			report("Build still running; waiting for compiler output...")
			continue
		if line is None:
			output_closed = True
			continue
		if line:
			report("[cmake] " + line)

	return process.wait()


def cache_value(path: Path, key: str) -> str | None:
	try:
		lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
	except FileNotFoundError:
		return None
	for line in lines:
		prefix = f"{key}:"
		if line.startswith(prefix):
			return line.partition("=")[2]
	return None


def artifact_details(path: Path) -> tuple[int, int, str, str] | None:
	try:
		stat = path.stat()
	except FileNotFoundError:
		return None
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		for chunk in iter(lambda: stream.read(1024 * 1024), b""):
			digest.update(chunk)
	return (
		stat.st_mtime_ns,
		stat.st_size,
		datetime.fromtimestamp(stat.st_mtime).astimezone().isoformat(timespec="seconds"),
		digest.hexdigest().upper(),
	)


def configure_build(
	cmake: str,
	source_dir: Path,
	build_dir: Path,
	vcpkg_static_dir: Path,
	deploy_dir: Path,
	repo_root: Path,
	environment: dict[str, str],
) -> int:
	build_dir.mkdir(parents=True, exist_ok=True)
	cache = build_dir / "CMakeCache.txt"
	if cache.exists():
		configured_generator = cache_value(cache, "CMAKE_GENERATOR")
		configured_pointer_size = cache_value(cache, "CMAKE_SIZEOF_VOID_P")
		configured_compiler = cache_value(cache, "CMAKE_CXX_COMPILER")
		configured_x86_compiler = configured_compiler and "hostx86/x86" in configured_compiler.lower().replace("\\", "/")
		if (
			(configured_generator and configured_generator != GENERATOR)
			or (configured_pointer_size and configured_pointer_size != "8")
			or configured_x86_compiler
		):
			report(
				f"Replacing the existing build tree configured for {configured_generator or 'an unknown generator'} "
				f"and {configured_pointer_size or 'an unknown'}-byte pointers."
			)
			shutil.rmtree(build_dir)
			build_dir.mkdir(parents=True, exist_ok=True)

	return run_process(
		[
			cmake,
			"-S",
			str(source_dir),
			"-B",
			str(build_dir),
			"-G",
			GENERATOR,
			f"-DCMAKE_BUILD_TYPE={BUILD_TYPE}",
			f"-DVCPKG_STATIC_DIR={vcpkg_static_dir}",
			f"-DALCHEMIST_DEPLOY_DIR={deploy_dir}",
		],
		repo_root,
		environment,
	)


def clean_plugin_outputs(
	ninja: str,
	build_dir: Path,
	repo_root: Path,
	environment: dict[str, str],
) -> int:
	report("Cleaning alchemist outputs while preserving CommonLibSSE-NG and cached third-party targets.")
	return run_process(
		[ninja, "-C", str(build_dir), "-t", "clean", "-r", *PLUGIN_CLEAN_RULES],
		repo_root,
		environment,
	)


def build_once(
	cmake: str,
	build_dir: Path,
	artifact: Path,
	repo_root: Path,
	environment: dict[str, str],
) -> tuple[int, int]:
	# Capture the timestamp immediately before the one allowed build attempt so a stale
	# incremental artifact can be detected and retried at most once by main().
	build_start_ns = time.time_ns()
	build_start = datetime.now().astimezone().isoformat(timespec="seconds")
	start_file = Path(os.environ.get("TEMP", repo_root)) / "prosperous-alchemist-build-start.txt"
	start_file.write_text(build_start + "\n", encoding="utf-8")
	report(f"Build start: {build_start}")
	exit_code = run_process(
		[cmake, "--build", str(build_dir), "--target", TARGET],
		repo_root,
		environment,
	)
	result = artifact_details(artifact)
	if result is None:
		report(f"Build exit={exit_code}; artifact missing: {artifact}")
	else:
		artifact_ns, _, artifact_timestamp, _ = result
		freshness = "newer" if artifact_ns > build_start_ns else "not newer"
		report(f"Build exit={exit_code}; artifact={artifact_timestamp}; artifact is {freshness} than build start")
	return exit_code, build_start_ns


def deploy_artifact(artifact: Path, deployed: Path) -> bool:
	# Preserve the built timestamp; verify_deployment() separately checks timestamp and hash.
	try:
		deployed.parent.mkdir(parents=True, exist_ok=True)
		shutil.copyfile(artifact, deployed)
		artifact_stat = artifact.stat()
		os.utime(deployed, ns=(artifact_stat.st_atime_ns, artifact_stat.st_mtime_ns))
	except OSError as error:
		report(f"Unable to deploy {artifact} to {deployed}: {error}")
		return False
	report(f"Deployed {artifact} to {deployed} with the built file timestamp.")
	return True


def verify_deployment(artifact: Path, deployed: Path) -> bool:
	built = artifact_details(artifact)
	deployed_details = artifact_details(deployed)
	if built is None:
		report(f"Built artifact is missing: {artifact}")
		return False
	if deployed_details is None:
		report(f"Deployed artifact is missing: {deployed}")
		return False

	report(
		f"Built DLL: timestamp={built[2]}; size={built[1]}; SHA-256={built[3]}"
	)
	report(
		f"Deployed DLL: timestamp={deployed_details[2]}; size={deployed_details[1]}; "
		f"SHA-256={deployed_details[3]}"
	)
	if built[0] != deployed_details[0] or built[3] != deployed_details[3]:
		report("Built and deployed DLLs differ in timestamp or contents.")
		return False
	report("Built and deployed DLLs match.")
	return True


def package_release(repo_root: Path, artifact: Path, package_path: Path | None = None) -> Path:
	if package_path is None:
		package_path = repo_root / "dist" / "Prosperous-Alchemist-NG-v1.1.X.zip"
	package_path.parent.mkdir(parents=True, exist_ok=True)

	ini_path = repo_root / "alchemist.ini"

	report(f"Packaging release distribution archive: {package_path}")
	with zipfile.ZipFile(package_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
		zf.write(artifact, "SKSE/Plugins/alchemist.dll")
		if ini_path.is_file():
			zf.write(ini_path, "SKSE/Plugins/alchemist.ini")
		if (repo_root / "COPYING").is_file():
			zf.write(repo_root / "COPYING", "COPYING")
		if (repo_root / "EXCEPTIONS.md").is_file():
			zf.write(repo_root / "EXCEPTIONS.md", "EXCEPTIONS.md")
		if (repo_root / "docs" / "USER_README.md").is_file():
			zf.write(repo_root / "docs" / "USER_README.md", "docs/USER_README.md")
		licenses_dir = repo_root / "licenses"
		if licenses_dir.is_dir():
			for lic_file in sorted(licenses_dir.glob("*")):
				if lic_file.is_file():
					zf.write(lic_file, f"licenses/{lic_file.name}")

	report(f"Package created successfully: size={package_path.stat().st_size} bytes")
	return package_path


def main() -> int:
	global status_file

	parser = argparse.ArgumentParser(description="Build the Prosperous Alchemist plugin with live progress output.")
	parser.add_argument("--build-dir", default="build-alchemist")
	parser.add_argument("--cmake", default="cmake")
	parser.add_argument("--package", action="store_true", help="Create a release distribution zip archive.")
	args = parser.parse_args()

	repo_root = Path(__file__).resolve().parent
	# The standalone consumer is the canonical CMake source root. Visual Studio settings
	# may point at this directory only to avoid an IDE warning; build.py remains the
	# supported entry point and controls the Release/Ninja/deployment contract.
	source_dir = repo_root / "alchemist"
	build_dir = (repo_root / args.build_dir).resolve()
	status_file = build_dir / ".build-status.txt"
	try:
		from config import CMAKE_DIR, DLL_DEPLOY, NINJA_DIR, VCPKG_STATIC_DIR
	except ImportError as error:
		report(f"Missing config.py; copy example-config.py and set local paths: {error}")
		return 2
	if args.cmake == "cmake" and Path(CMAKE_DIR).is_dir():
		args.cmake = str(Path(CMAKE_DIR) / "cmake.exe")
	os.environ["PATH"] = str(NINJA_DIR) + os.pathsep + os.environ.get("PATH", "")
	vcpkg_static_dir = Path(VCPKG_STATIC_DIR).resolve()
	deployed = Path(DLL_DEPLOY).resolve()
	artifact = build_dir / f"{TARGET}.dll"
	if not source_dir.is_dir():
		report(f"CMake source directory does not exist: {source_dir}")
		return 2
	if not vcpkg_static_dir.is_dir():
		report(f"Static vcpkg directory does not exist: {vcpkg_static_dir}")
		return 2
	try:
		environment = x64_msvc_environment()
	except RuntimeError as error:
		report(f"Unable to configure the x64 MSVC environment: {error}")
		return 2
	ninja = shutil.which("ninja", path=environment.get("PATH"))
	if not ninja:
		report("Ninja executable was not found on PATH")
		return 2
	report(f"Build paths: source={source_dir}; build={build_dir}; deploy={deployed}")

	report(f"Configuring {source_dir} with {GENERATOR} and {BUILD_TYPE}.")
	configure_exit = configure_build(
		args.cmake,
		source_dir,
		build_dir,
		vcpkg_static_dir,
		deployed.parent,
		repo_root,
		environment,
	)
	if configure_exit != 0:
		report(f"Configure failed with exit={configure_exit}")
		return configure_exit

	clean_exit = clean_plugin_outputs(
		ninja,
		build_dir,
		repo_root,
		environment,
	)
	if clean_exit != 0:
		report(f"Plugin clean failed with exit={clean_exit}")
		return clean_exit

	exit_code, build_start_ns = build_once(
		args.cmake,
		build_dir,
		artifact,
		repo_root,
		environment,
	)

	if exit_code != 0:
		return exit_code
	artifact_result = artifact_details(artifact)
	if artifact_result is None or artifact_result[0] <= build_start_ns:
		report("Build artifact failed freshness validation; performing one clean rebuild.")
		retry_clean_exit = clean_plugin_outputs(
			ninja,
			build_dir,
			repo_root,
			environment,
		)
		if retry_clean_exit != 0:
			report(f"Retry plugin clean failed with exit={retry_clean_exit}")
			return retry_clean_exit
		exit_code, build_start_ns = build_once(
			args.cmake,
			build_dir,
			artifact,
			repo_root,
			environment,
		)
		if exit_code != 0:
			return exit_code
		artifact_result = artifact_details(artifact)
		if artifact_result is None or artifact_result[0] <= build_start_ns:
			report("Build failed freshness validation after the allowed rebuild.")
			return 3
	if not deploy_artifact(artifact, deployed):
		return 4
	if not verify_deployment(artifact, deployed):
		return 4
	if args.package:
		package_release(repo_root, artifact)
	report("Build completed with a fresh artifact.")
	return 0


if __name__ == "__main__":
	build_log = (Path(__file__).resolve().parent / "build.log").open("w", encoding="utf-8", buffering=1)
	exit_code = 1
	try:
		exit_code = main()
	except Exception as error:
		report(f"Build wrapper failed: {error}")
		exit_code = 1
	finally:
		build_log.close()
		if status_file is not None:
			status_file.parent.mkdir(parents=True, exist_ok=True)
			status_file.write_text(f"exit_code={exit_code}\ntimestamp={timestamp()}\n", encoding="utf-8")
	sys.exit(exit_code)
