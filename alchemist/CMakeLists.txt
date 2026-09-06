cmake_minimum_required(VERSION 3.21)

# This consumer project builds the plugin from the alchemist directory and expects the
# CommonLibSSE-NG submodule plus a static vcpkg installation to be available locally.
project(
	alchemist
	LANGUAGES CXX
	VERSION 1.1.0.0
)

set(COMMONLIBSSE_DIR "../alandtse-CommonLibSSE-NG" CACHE PATH "Path to the CommonLibSSE-NG checkout")
get_filename_component(COMMONLIBSSE_PATH "${COMMONLIBSSE_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
if(NOT EXISTS "${COMMONLIBSSE_PATH}/CMakeLists.txt")
	message(FATAL_ERROR "COMMONLIBSSE_DIR must point to a CommonLibSSE-NG checkout: ${COMMONLIBSSE_PATH}")
endif()

# CommonLibSSE-NG requires the static package configuration so the shipped DLL does not
# depend on dynamic vcpkg or MSVC runtime libraries beside the plugin.
set(VCPKG_STATIC_DIR "" CACHE PATH "Path to the static vcpkg installed directory")
if(NOT EXISTS "${VCPKG_STATIC_DIR}/share/spdlog/spdlogConfig.cmake")
	message(FATAL_ERROR "VCPKG_STATIC_DIR must point to a static vcpkg installed directory: ${VCPKG_STATIC_DIR}")
endif()
set(CMAKE_PREFIX_PATH "${VCPKG_STATIC_DIR}" CACHE STRING "Static vcpkg package prefix" FORCE)
find_package(nlohmann_json CONFIG REQUIRED)
# The wrapper and release packaging support Release only; keep multi-config generators
# from silently producing an unsupported Debug or RelWithDebInfo plugin.
set(CMAKE_CONFIGURATION_TYPES "Release" CACHE STRING "Supported build configurations" FORCE)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" CACHE STRING "Use the static MSVC runtime" FORCE)

set(BUILD_TESTS OFF CACHE BOOL "Disable CommonLibSSE-NG tests for the plugin build" FORCE)
set(REX_OPTION_INI ON CACHE BOOL "Enable REX INI support" FORCE)
set(ENABLE_SKYRIM_SE ON CACHE BOOL "Enable Skyrim SE support" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "Enable Skyrim AE support" FORCE)
set(ENABLE_SKYRIM_VR ON CACHE BOOL "Enable Skyrim VR support" FORCE)

add_subdirectory("${COMMONLIBSSE_PATH}" "${CMAKE_BINARY_DIR}/CommonLibSSE")
include("${COMMONLIBSSE_PATH}/cmake/CommonLibSSE.cmake")

include(FetchContent)
# Dear ImGui is fetched during the first configure unless it is already present in the
# CMake FetchContent cache; subsequent builds reuse the populated source tree.
FetchContent_Declare(
	imgui
	GIT_REPOSITORY https://github.com/ocornut/imgui.git
	GIT_TAG v1.91.9b
)
FetchContent_MakeAvailable(imgui)

add_library(alchemist_imgui STATIC
	${imgui_SOURCE_DIR}/imgui.cpp
	${imgui_SOURCE_DIR}/imgui_draw.cpp
	${imgui_SOURCE_DIR}/imgui_tables.cpp
	${imgui_SOURCE_DIR}/imgui_widgets.cpp
	${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)
target_include_directories(alchemist_imgui PUBLIC
	${imgui_SOURCE_DIR}
	${imgui_SOURCE_DIR}/backends
)

add_commonlibsse_plugin(
	alchemist
	NAME "alchemist"
	AUTHOR "FruitsBerriesMelons123"
	VERSION "1.1.0.0"
	USE_ADDRESS_LIBRARY
	SOURCES
		AlchemyPlus/AlchemyPlus.cpp
		AlchemyPlus/AlchemyPlus.h
		CACO/CACO.cpp
		CACO/CACO.h
		src/main.cpp
		src/AlchemistEngine.cpp
		src/AlchemistWindow.cpp
		src/DeveloperTestHub.cpp
		src/MenuHandler.cpp
		src/RenderHook.cpp
		version.rc
)

target_include_directories(alchemist PRIVATE include ${CMAKE_CURRENT_SOURCE_DIR} ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_features(alchemist PRIVATE cxx_std_23)
target_precompile_headers(alchemist PRIVATE include/AlchemistPCH.h)
target_link_libraries(alchemist PRIVATE alchemist_imgui nlohmann_json::nlohmann_json d3d11 dxgi user32)

set(ALCHEMIST_DEPLOY_DIR "" CACHE PATH "Optional directory for the post-build plugin deployment")
# Deployment is intentionally opt-in so a configure/build does not overwrite an unrelated
# Skyrim profile. build.py supplies this directory from the local DLL_DEPLOY setting.
if(ALCHEMIST_DEPLOY_DIR)
	add_custom_command(
		TARGET alchemist
		POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${ALCHEMIST_DEPLOY_DIR}"
		COMMAND ${CMAKE_COMMAND} -E copy
			"$<TARGET_FILE:alchemist>"
			"${ALCHEMIST_DEPLOY_DIR}/$<TARGET_FILE_NAME:alchemist>"
		VERBATIM
	)
endif()
