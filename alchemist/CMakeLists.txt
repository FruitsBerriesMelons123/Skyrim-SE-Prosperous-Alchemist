cmake_minimum_required(VERSION 3.21)

project(
	alchemist
	LANGUAGES CXX
	VERSION 1.0.0.0
)

set(COMMONLIBSSE_DIR "../alandtse-CommonLibSSE-NG" CACHE PATH "Path to the CommonLibSSE-NG checkout")
get_filename_component(COMMONLIBSSE_PATH "${COMMONLIBSSE_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
if(NOT EXISTS "${COMMONLIBSSE_PATH}/CMakeLists.txt")
	message(FATAL_ERROR "COMMONLIBSSE_DIR must point to a CommonLibSSE-NG checkout: ${COMMONLIBSSE_PATH}")
endif()

set(VCPKG_STATIC_DIR "" CACHE PATH "Path to the static vcpkg installed directory")
if(NOT EXISTS "${VCPKG_STATIC_DIR}/share/spdlog/spdlogConfig.cmake")
	message(FATAL_ERROR "VCPKG_STATIC_DIR must point to a static vcpkg installed directory: ${VCPKG_STATIC_DIR}")
endif()
set(CMAKE_PREFIX_PATH "${VCPKG_STATIC_DIR}" CACHE STRING "Static vcpkg package prefix" FORCE)
set(CMAKE_CONFIGURATION_TYPES "Release" CACHE STRING "Supported build configurations" FORCE)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" CACHE STRING "Use the static MSVC runtime" FORCE)

set(BUILD_TESTS OFF CACHE BOOL "Disable CommonLibSSE-NG tests for the plugin build" FORCE)
set(REX_OPTION_INI ON CACHE BOOL "Enable REX INI support" FORCE)
set(ENABLE_SKYRIM_SE ON CACHE BOOL "Enable Skyrim SE support" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "Enable Skyrim AE support" FORCE)
set(ENABLE_SKYRIM_VR ON CACHE BOOL "Enable Skyrim VR support" FORCE)

add_subdirectory("${COMMONLIBSSE_PATH}" "${CMAKE_BINARY_DIR}/CommonLibSSE")
include("${COMMONLIBSSE_PATH}/cmake/CommonLibSSE.cmake")

add_commonlibsse_plugin(
	alchemist
	NAME "alchemist"
	AUTHOR "FruitsBerriesMelons123"
	VERSION "1.0.0.0"
	USE_ADDRESS_LIBRARY
	SOURCES
		src/main.cpp
		version.rc
)

target_include_directories(alchemist PRIVATE include)
target_compile_features(alchemist PRIVATE cxx_std_23)
target_precompile_headers(alchemist PRIVATE include/AlchemistPCH.h)

set(ALCHEMIST_DEPLOY_DIR "" CACHE PATH "Optional directory for the post-build plugin deployment")
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
