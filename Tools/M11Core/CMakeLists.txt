cmake_minimum_required(VERSION 3.25)

project(ABTSM11CoreStandalone LANGUAGES CXX)

include(CTest)

set(ABTS_M11_TOOL_BUILD_VERSION "M11-A-v2.1-tool.2")
set(ABTS_M11_SOURCE_HASH_SCHEMA
	"ABTS.M11Core.SourceContent.RelativePathLF.SHA256")
set(ABTS_M11_SOURCE_HASH_SCHEMA_VERSION 1)

if(NOT WIN32 OR NOT MSVC)
	message(FATAL_ERROR
		"M11Core standalone conformance is intentionally pinned to "
		"Windows/MSVC so it can match the Unreal Editor toolchain.")
endif()
if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
	message(FATAL_ERROR
		"M11Core standalone conformance requires the pinned Ninja "
		"generator; active generator is '${CMAKE_GENERATOR}'.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
	message(FATAL_ERROR
		"M11Core standalone conformance requires a 64-bit compiler.")
endif()
string(TOLOWER "$ENV{VSCMD_ARG_TGT_ARCH}" ABTS_M11_ACTIVE_TARGET_ARCH)
if(NOT ABTS_M11_ACTIVE_TARGET_ARCH STREQUAL "x64")
	message(FATAL_ERROR
		"M11Core standalone conformance requires an x64 VsDevCmd "
		"environment; VSCMD_ARG_TGT_ARCH='${ABTS_M11_ACTIVE_TARGET_ARCH}'.")
endif()

set(ABTS_M11_REQUIRED_VC_TOOLS_VERSION "14.44.35207")
string(STRIP "$ENV{VCToolsVersion}" ABTS_M11_ACTIVE_VC_TOOLS_VERSION)
string(REGEX REPLACE "[/\\\\]+$" "" ABTS_M11_ACTIVE_VC_TOOLS_VERSION
	"${ABTS_M11_ACTIVE_VC_TOOLS_VERSION}")
if(NOT ABTS_M11_ACTIVE_VC_TOOLS_VERSION STREQUAL
		ABTS_M11_REQUIRED_VC_TOOLS_VERSION)
	message(FATAL_ERROR
		"M11Core requires MSVC tools ${ABTS_M11_REQUIRED_VC_TOOLS_VERSION}, "
		"but the active VsDevCmd environment reports "
		"'${ABTS_M11_ACTIVE_VC_TOOLS_VERSION}'. Run "
		"BuildAndRunPortableConformance.ps1.")
endif()

get_filename_component(ABTS_REPOSITORY_ROOT
	"${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(ABTS_RUNTIME_ROOT "${ABTS_REPOSITORY_ROOT}/Source/ABTSRuntime")
set(ABTS_RUNTIME_PUBLIC_ROOT "${ABTS_RUNTIME_ROOT}/Public")
set(ABTS_RUNTIME_PRIVATE_ROOT "${ABTS_RUNTIME_ROOT}/Private")
set(ABTS_M11_CORE_SOURCE_ROOT "${ABTS_RUNTIME_PRIVATE_ROOT}/M11Core")
set(ABTS_M11_SEARCH_PUBLIC_ROOT "${ABTS_RUNTIME_PUBLIC_ROOT}/M11Search")
set(ABTS_M11_SEARCH_SOURCE_ROOT "${ABTS_RUNTIME_PRIVATE_ROOT}/M11Search")

set(ABTS_M11_CORE_SOURCES)
set(ABTS_M11_SOURCE_ORIGINS)

function(abts_m11_require_core_source source_name)
	set(core_candidate "${ABTS_M11_CORE_SOURCE_ROOT}/${source_name}")

	if(EXISTS "${core_candidate}")
		list(APPEND ABTS_M11_CORE_SOURCES "${core_candidate}")
		list(APPEND ABTS_M11_SOURCE_ORIGINS
			"Private/M11Core/${source_name}")
	else()
		message(FATAL_ERROR
			"Required portable core source is missing: ${core_candidate}. "
			"M11-A v2.1 must not compile the retired Private/World solver.")
	endif()

	set(ABTS_M11_CORE_SOURCES "${ABTS_M11_CORE_SOURCES}" PARENT_SCOPE)
	set(ABTS_M11_SOURCE_ORIGINS "${ABTS_M11_SOURCE_ORIGINS}" PARENT_SCOPE)
endfunction()

abts_m11_require_core_source("ABTSM11CoreConformance.cpp")
abts_m11_require_core_source("ABTSM11CoreTypes.cpp")
abts_m11_require_core_source("ABTSM11CoreHash.cpp")
abts_m11_require_core_source("ABTSM11CoreNumerics.cpp")
abts_m11_require_core_source("ABTSM11CoreEncounter.cpp")
abts_m11_require_core_source("ABTSM11CoreSolveSupport.cpp")
abts_m11_require_core_source("ABTSM11CoreSolver.cpp")

set(ABTS_M11_PRODUCTION_CORE_SOURCES ${ABTS_M11_CORE_SOURCES})
list(FILTER ABTS_M11_PRODUCTION_CORE_SOURCES EXCLUDE REGEX
	"ABTSM11CoreConformance\\.cpp$")

file(GLOB ABTS_M11_ALL_DIRECT_SOURCES CONFIGURE_DEPENDS
	"${ABTS_M11_CORE_SOURCE_ROOT}/*.cpp")
set(ABTS_M11_CLASSIFIED_DIRECT_SOURCES ${ABTS_M11_CORE_SOURCES})
list(SORT ABTS_M11_ALL_DIRECT_SOURCES)
list(SORT ABTS_M11_CLASSIFIED_DIRECT_SOURCES)
if(NOT ABTS_M11_ALL_DIRECT_SOURCES STREQUAL
		ABTS_M11_CLASSIFIED_DIRECT_SOURCES)
	message(FATAL_ERROR
		"An M11Core source is not classified for compilation and source "
		"identity. All='${ABTS_M11_ALL_DIRECT_SOURCES}' Classified='"
		"${ABTS_M11_CLASSIFIED_DIRECT_SOURCES}'.")
endif()

set(ABTS_M11_SEARCH_SOURCES
	"${ABTS_M11_SEARCH_SOURCE_ROOT}/ABTSM11SearchTypes.cpp"
	"${ABTS_M11_SEARCH_SOURCE_ROOT}/ABTSM11CandidateSearch.cpp")
file(GLOB ABTS_M11_ALL_SEARCH_SOURCES CONFIGURE_DEPENDS
	"${ABTS_M11_SEARCH_SOURCE_ROOT}/*.cpp")
set(ABTS_M11_CLASSIFIED_SEARCH_SOURCES ${ABTS_M11_SEARCH_SOURCES})
list(SORT ABTS_M11_ALL_SEARCH_SOURCES)
list(SORT ABTS_M11_CLASSIFIED_SEARCH_SOURCES)
if(NOT ABTS_M11_ALL_SEARCH_SOURCES STREQUAL
		ABTS_M11_CLASSIFIED_SEARCH_SOURCES)
	message(FATAL_ERROR
		"An M11Search source is not classified for compilation and source "
		"identity. All='${ABTS_M11_ALL_SEARCH_SOURCES}' Classified='"
		"${ABTS_M11_CLASSIFIED_SEARCH_SOURCES}'.")
endif()
set(ABTS_M11_SEARCH_HEADERS
	"${ABTS_M11_SEARCH_PUBLIC_ROOT}/ABTSM11SearchTypes.h"
	"${ABTS_M11_SEARCH_PUBLIC_ROOT}/ABTSM11CandidateSearch.h")
file(GLOB ABTS_M11_ALL_SEARCH_HEADERS CONFIGURE_DEPENDS
	"${ABTS_M11_SEARCH_PUBLIC_ROOT}/*.h")
set(ABTS_M11_CLASSIFIED_SEARCH_HEADERS ${ABTS_M11_SEARCH_HEADERS})
list(SORT ABTS_M11_ALL_SEARCH_HEADERS)
list(SORT ABTS_M11_CLASSIFIED_SEARCH_HEADERS)
if(NOT ABTS_M11_ALL_SEARCH_HEADERS STREQUAL
		ABTS_M11_CLASSIFIED_SEARCH_HEADERS)
	message(FATAL_ERROR
		"An M11Search header is not classified for source identity. All='"
		"${ABTS_M11_ALL_SEARCH_HEADERS}' Classified='"
		"${ABTS_M11_CLASSIFIED_SEARCH_HEADERS}'.")
endif()

file(GLOB ABTS_M11_CORE_PUBLIC_HEADERS CONFIGURE_DEPENDS
	"${ABTS_RUNTIME_PUBLIC_ROOT}/M11Core/*.h")
file(GLOB ABTS_M11_CORE_PRIVATE_HEADERS CONFIGURE_DEPENDS
	"${ABTS_RUNTIME_PRIVATE_ROOT}/M11Core/*.h")

# Production identity deliberately excludes conformance fixtures and tooling:
# corpus edits must not invalidate an already selected production candidate.
set(ABTS_M11_PRODUCTION_CORE_IDENTITY_FILES
	"${ABTS_RUNTIME_PUBLIC_ROOT}/M11Core/ABTSM11CoreMath.h"
	"${ABTS_RUNTIME_PUBLIC_ROOT}/M11Core/ABTSM11CoreSolver.h"
	"${ABTS_RUNTIME_PUBLIC_ROOT}/M11Core/ABTSM11CoreTypes.h"
	"${ABTS_RUNTIME_PRIVATE_ROOT}/M11Core/ABTSM11CoreInternal.h"
	"${ABTS_M11_CORE_SOURCE_ROOT}/ABTSM11CoreTypes.cpp"
	"${ABTS_M11_CORE_SOURCE_ROOT}/ABTSM11CoreHash.cpp"
	"${ABTS_M11_CORE_SOURCE_ROOT}/ABTSM11CoreNumerics.cpp"
	"${ABTS_M11_CORE_SOURCE_ROOT}/ABTSM11CoreEncounter.cpp"
	"${ABTS_M11_CORE_SOURCE_ROOT}/ABTSM11CoreSolveSupport.cpp"
	"${ABTS_M11_CORE_SOURCE_ROOT}/ABTSM11CoreSolver.cpp")
set(ABTS_M11_CONFORMANCE_TOOL_IDENTITY_FILES
	"${ABTS_RUNTIME_PUBLIC_ROOT}/M11Core/ABTSM11CoreConformance.h"
	"${ABTS_M11_CORE_SOURCE_ROOT}/ABTSM11CoreConformance.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/BuildAndRunPortableConformance.ps1"
	"${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt"
	"${CMAKE_CURRENT_LIST_DIR}/CMakePresets.json"
	"${CMAKE_CURRENT_LIST_DIR}/M11CoreConformanceMain.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/TestNoUnrealDependencies.ps1")
set(ABTS_M11_SEARCH_TOOL_IDENTITY_FILES
	${ABTS_M11_SEARCH_HEADERS}
	${ABTS_M11_SEARCH_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/M11SearchMain.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/Python/m11_search.py"
	"${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt"
	"${CMAKE_CURRENT_LIST_DIR}/CMakePresets.json")

foreach(required_identity_file IN LISTS
		ABTS_M11_PRODUCTION_CORE_IDENTITY_FILES
		ABTS_M11_CONFORMANCE_TOOL_IDENTITY_FILES
		ABTS_M11_SEARCH_TOOL_IDENTITY_FILES)
	if(NOT EXISTS "${required_identity_file}")
		message(FATAL_ERROR
			"Identity input is missing: ${required_identity_file}")
	endif()
endforeach()

# Every direct M11Core header must be classified into exactly one identity.
set(ABTS_M11_CLASSIFIED_DIRECT_HEADERS
	"${ABTS_RUNTIME_PUBLIC_ROOT}/M11Core/ABTSM11CoreMath.h"
	"${ABTS_RUNTIME_PUBLIC_ROOT}/M11Core/ABTSM11CoreSolver.h"
	"${ABTS_RUNTIME_PUBLIC_ROOT}/M11Core/ABTSM11CoreTypes.h"
	"${ABTS_RUNTIME_PRIVATE_ROOT}/M11Core/ABTSM11CoreInternal.h"
	"${ABTS_RUNTIME_PUBLIC_ROOT}/M11Core/ABTSM11CoreConformance.h")
list(SORT ABTS_M11_CLASSIFIED_DIRECT_HEADERS)
set(ABTS_M11_ALL_DIRECT_HEADERS
	${ABTS_M11_CORE_PUBLIC_HEADERS}
	${ABTS_M11_CORE_PRIVATE_HEADERS})
list(SORT ABTS_M11_ALL_DIRECT_HEADERS)
if(NOT ABTS_M11_ALL_DIRECT_HEADERS STREQUAL
		ABTS_M11_CLASSIFIED_DIRECT_HEADERS)
	message(FATAL_ERROR
		"An M11Core public/private header is missing from the production "
		"or conformance identity classification. All='"
		"${ABTS_M11_ALL_DIRECT_HEADERS}' Classified='"
		"${ABTS_M11_CLASSIFIED_DIRECT_HEADERS}'.")
endif()

function(abts_m11_build_content_identity
		output_hash
		output_count
		output_manifest_json)
	set(identity_files ${ARGN})
	list(REMOVE_DUPLICATES identity_files)
	set(identity_relative_paths)
	foreach(identity_file IN LISTS identity_files)
		file(RELATIVE_PATH identity_relative_path
			"${ABTS_REPOSITORY_ROOT}"
			"${identity_file}")
		cmake_path(CONVERT "${identity_relative_path}" TO_CMAKE_PATH_LIST
			identity_relative_path NORMALIZE)
		list(APPEND identity_relative_paths "${identity_relative_path}")
	endforeach()
	list(SORT identity_relative_paths)
	set(identity_material "")
	set(identity_manifest_json "")
	set(identity_manifest_separator "")
	foreach(identity_relative_path IN LISTS identity_relative_paths)
		set(identity_file
			"${ABTS_REPOSITORY_ROOT}/${identity_relative_path}")
		file(READ "${identity_file}" identity_file_content)
		string(REPLACE "\r\n" "\n"
			identity_file_content "${identity_file_content}")
		string(REPLACE "\r" "\n"
			identity_file_content "${identity_file_content}")
		string(SHA256 identity_file_sha256
			"${identity_file_content}")
		string(APPEND identity_material
			"${identity_relative_path}\t${identity_file_sha256}\n")
		string(APPEND identity_manifest_json
			"${identity_manifest_separator}"
			"    {\"path\":\"${identity_relative_path}\","
			"\"sha256\":\"${identity_file_sha256}\"}")
		set(identity_manifest_separator ",\n")
	endforeach()
	string(SHA256 identity_hash "${identity_material}")
	list(LENGTH identity_relative_paths identity_count)
	set(${output_hash} "${identity_hash}" PARENT_SCOPE)
	set(${output_count} "${identity_count}" PARENT_SCOPE)
	set(${output_manifest_json}
		"${identity_manifest_json}" PARENT_SCOPE)
endfunction()

abts_m11_build_content_identity(
	ABTS_M11_PRODUCTION_CORE_SOURCE_HASH_SHA256
	ABTS_M11_PRODUCTION_CORE_SOURCE_FILE_COUNT
	ABTS_M11_PRODUCTION_CORE_FILE_MANIFEST_JSON
	${ABTS_M11_PRODUCTION_CORE_IDENTITY_FILES})
abts_m11_build_content_identity(
	ABTS_M11_CONFORMANCE_TOOL_SOURCE_HASH_SHA256
	ABTS_M11_CONFORMANCE_TOOL_SOURCE_FILE_COUNT
	ABTS_M11_CONFORMANCE_TOOL_FILE_MANIFEST_JSON
	${ABTS_M11_CONFORMANCE_TOOL_IDENTITY_FILES})
abts_m11_build_content_identity(
	ABTS_M11_SEARCH_TOOL_SOURCE_HASH_SHA256
	ABTS_M11_SEARCH_TOOL_SOURCE_FILE_COUNT
	ABTS_M11_SEARCH_TOOL_FILE_MANIFEST_JSON
	${ABTS_M11_SEARCH_TOOL_IDENTITY_FILES})

set(ABTS_M11_COMPILER_IDENTITY
	"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}|VCTools-${ABTS_M11_ACTIVE_VC_TOOLS_VERSION}|CXX20|x64|fp-precise")
set(ABTS_M11_NUMERICAL_COMPILE_CONTRACT
	"C++20,/fp:precise,/Ox,/Ot,/Oi,/Ob2,/GR-,/Zp8,/MD")
set(ABTS_M11_CXX_STANDARD "C++20")
set(ABTS_M11_FLOATING_POINT_MODE "/fp:precise")
set(ABTS_M11_GENERATED_INCLUDE_DIRECTORY
	"${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${ABTS_M11_GENERATED_INCLUDE_DIRECTORY}")
file(WRITE
	"${ABTS_M11_GENERATED_INCLUDE_DIRECTORY}/ABTSM11CoreToolBuildIdentity.generated.h"
	"// Generated by Tools/M11Core/CMakeLists.txt. Do not edit.\n"
	"#pragma once\n\n"
	"#include <cstddef>\n\n"
	"#if !defined(_M_X64)\n"
	"#error M11Core standalone conformance requires the x64 MSVC target.\n"
	"#endif\n\n"
	"static_assert(sizeof(void*) == 8, \"M11Core requires 64-bit pointers.\");\n\n"
	"namespace ABTS::M11Core::ToolIdentity\n"
	"{\n"
	"\tinline constexpr char ToolBuildVersion[] = \"${ABTS_M11_TOOL_BUILD_VERSION}\";\n"
	"\tinline constexpr char SourceHashSchema[] = \"${ABTS_M11_SOURCE_HASH_SCHEMA}\";\n"
	"\tinline constexpr int SourceHashSchemaVersion = ${ABTS_M11_SOURCE_HASH_SCHEMA_VERSION};\n"
	"\tinline constexpr char CompilerId[] = \"${CMAKE_CXX_COMPILER_ID}\";\n"
	"\tinline constexpr char CompilerVersion[] = \"${CMAKE_CXX_COMPILER_VERSION}\";\n"
	"\tinline constexpr char VCToolsVersion[] = \"${ABTS_M11_ACTIVE_VC_TOOLS_VERSION}\";\n"
	"\tinline constexpr char CompilerIdentity[] = \"${ABTS_M11_COMPILER_IDENTITY}\";\n"
	"\tinline constexpr char Architecture[] = \"x64\";\n"
	"\tinline constexpr char CxxStandard[] = \"${ABTS_M11_CXX_STANDARD}\";\n"
	"\tinline constexpr char FloatingPointMode[] = \"${ABTS_M11_FLOATING_POINT_MODE}\";\n"
	"\tinline constexpr char NumericalCompileContract[] = \"${ABTS_M11_NUMERICAL_COMPILE_CONTRACT}\";\n"
	"\tinline constexpr char ProductionCoreSourceHashSha256[] = \"${ABTS_M11_PRODUCTION_CORE_SOURCE_HASH_SHA256}\";\n"
	"\tinline constexpr char ConformanceToolSourceHashSha256[] = \"${ABTS_M11_CONFORMANCE_TOOL_SOURCE_HASH_SHA256}\";\n"
	"\tinline constexpr char SearchSourceHashSha256[] = \"${ABTS_M11_SEARCH_TOOL_SOURCE_HASH_SHA256}\";\n"
	"\tinline constexpr std::size_t ProductionCoreSourceFileCount = ${ABTS_M11_PRODUCTION_CORE_SOURCE_FILE_COUNT};\n"
	"\tinline constexpr std::size_t ConformanceToolSourceFileCount = ${ABTS_M11_CONFORMANCE_TOOL_SOURCE_FILE_COUNT};\n"
	"\tinline constexpr std::size_t SearchSourceFileCount = ${ABTS_M11_SEARCH_TOOL_SOURCE_FILE_COUNT};\n"
	"}\n")

add_executable(ABTSM11CoreConformance
	${ABTS_M11_CORE_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/M11CoreConformanceMain.cpp")

target_include_directories(ABTSM11CoreConformance PRIVATE
	"${ABTS_M11_GENERATED_INCLUDE_DIRECTORY}"
	"${ABTS_RUNTIME_PUBLIC_ROOT}"
	"${ABTS_RUNTIME_PRIVATE_ROOT}")

target_compile_features(ABTSM11CoreConformance PRIVATE cxx_std_20)
set_target_properties(ABTSM11CoreConformance PROPERTIES
	CXX_EXTENSIONS OFF
	MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"
	RUNTIME_OUTPUT_DIRECTORY
		"${ABTS_REPOSITORY_ROOT}/Intermediate/M11CoreStandalone/bin")

# UBT's Development Editor build uses this numerical/compiler contract. Keep
# the standalone executable pinned to it so cross-front-end hashes are useful.
target_compile_definitions(ABTSM11CoreConformance PRIVATE
	WIN32_LEAN_AND_MEAN
	NOMINMAX)
target_compile_options(ABTSM11CoreConformance PRIVATE
	/permissive-
	/Zc:__cplusplus
	/Zc:preprocessor
	/Zc:inline
	/utf-8
	/fp:precise
	/Ox
	/Ot
	/Oi
	/Ob2
	/GR-
	/Zp8)
target_link_options(ABTSM11CoreConformance PRIVATE
	/INCREMENTAL:NO
	/OPT:REF
	/OPT:ICF)

add_executable(ABTSM11SearchCLI
	${ABTS_M11_PRODUCTION_CORE_SOURCES}
	${ABTS_M11_SEARCH_SOURCES}
	"${CMAKE_CURRENT_LIST_DIR}/M11SearchMain.cpp")

target_include_directories(ABTSM11SearchCLI PRIVATE
	"${ABTS_M11_GENERATED_INCLUDE_DIRECTORY}"
	"${ABTS_RUNTIME_PUBLIC_ROOT}"
	"${ABTS_RUNTIME_PRIVATE_ROOT}")

target_compile_features(ABTSM11SearchCLI PRIVATE cxx_std_20)
set_target_properties(ABTSM11SearchCLI PROPERTIES
	CXX_EXTENSIONS OFF
	MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"
	RUNTIME_OUTPUT_DIRECTORY
		"${ABTS_REPOSITORY_ROOT}/Intermediate/M11CoreStandalone/bin")
target_compile_definitions(ABTSM11SearchCLI PRIVATE
	WIN32_LEAN_AND_MEAN
	NOMINMAX)
target_compile_options(ABTSM11SearchCLI PRIVATE
	/permissive-
	/Zc:__cplusplus
	/Zc:preprocessor
	/Zc:inline
	/utf-8
	/fp:precise
	/Ox
	/Ot
	/Oi
	/Ob2
	/GR-
	/Zp8)
target_link_options(ABTSM11SearchCLI PRIVATE
	/INCREMENTAL:NO
	/OPT:REF
	/OPT:ICF)

# Remove CMake's conflicting defaults. The target options above remain the
# single source of truth for optimization and RTTI.
foreach(flag_variable CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_RELEASE)
	string(REGEX REPLACE "(^|[ \t])/GR([ \t]|$)" " "
		"${flag_variable}" "${${flag_variable}}")
	string(REGEX REPLACE "(^|[ \t])/O[012dx]([ \t]|$)" " "
		"${flag_variable}" "${${flag_variable}}")
	string(REGEX REPLACE "(^|[ \t])/Ob[0-3]([ \t]|$)" " "
		"${flag_variable}" "${${flag_variable}}")
	set("${flag_variable}" "${${flag_variable}}" CACHE STRING
		"M11Core normalized MSVC flags" FORCE)
endforeach()

string(JOIN "\",\n    \"" ABTS_M11_SOURCE_ORIGIN_JSON
	${ABTS_M11_SOURCE_ORIGINS})
file(WRITE "${CMAKE_BINARY_DIR}/m11_core_source_manifest.json"
	"{\n"
	"  \"schema\": \"abts.m11_core.source_manifest.v2\",\n"
	"  \"toolBuildVersion\": \"${ABTS_M11_TOOL_BUILD_VERSION}\",\n"
	"  \"sourceHashSchema\": \"${ABTS_M11_SOURCE_HASH_SCHEMA}\",\n"
	"  \"sourceHashSchemaVersion\": ${ABTS_M11_SOURCE_HASH_SCHEMA_VERSION},\n"
	"  \"productionCoreSourceHashSha256\": \"${ABTS_M11_PRODUCTION_CORE_SOURCE_HASH_SHA256}\",\n"
	"  \"productionCoreSourceFileCount\": ${ABTS_M11_PRODUCTION_CORE_SOURCE_FILE_COUNT},\n"
	"  \"conformanceToolSourceHashSha256\": \"${ABTS_M11_CONFORMANCE_TOOL_SOURCE_HASH_SHA256}\",\n"
	"  \"conformanceToolSourceFileCount\": ${ABTS_M11_CONFORMANCE_TOOL_SOURCE_FILE_COUNT},\n"
	"  \"searchSourceHashSha256\": \"${ABTS_M11_SEARCH_TOOL_SOURCE_HASH_SHA256}\",\n"
	"  \"searchSourceFileCount\": ${ABTS_M11_SEARCH_TOOL_SOURCE_FILE_COUNT},\n"
	"  \"compilerIdentity\": \"${ABTS_M11_COMPILER_IDENTITY}\",\n"
	"  \"architecture\": \"x64\",\n"
	"  \"cxxStandard\": \"${ABTS_M11_CXX_STANDARD}\",\n"
	"  \"floatingPointMode\": \"${ABTS_M11_FLOATING_POINT_MODE}\",\n"
	"  \"numericalCompileContract\": \"${ABTS_M11_NUMERICAL_COMPILE_CONTRACT}\",\n"
	"  \"vcToolsVersion\": \"${ABTS_M11_ACTIVE_VC_TOOLS_VERSION}\",\n"
	"  \"buildSurface\": \"EngineFreeStandaloneConformance\",\n"
	"  \"unrealIncludeDirectories\": [],\n"
	"  \"unrealLinkLibraries\": [],\n"
	"  \"sources\": [\n"
	"    \"${ABTS_M11_SOURCE_ORIGIN_JSON}\"\n"
	"  ],\n"
	"  \"productionCoreFiles\": [\n"
	"${ABTS_M11_PRODUCTION_CORE_FILE_MANIFEST_JSON}\n"
	"  ],\n"
	"  \"conformanceToolFiles\": [\n"
	"${ABTS_M11_CONFORMANCE_TOOL_FILE_MANIFEST_JSON}\n"
	"  ],\n"
	"  \"searchToolFiles\": [\n"
	"${ABTS_M11_SEARCH_TOOL_FILE_MANIFEST_JSON}\n"
	"  ]\n"
	"}\n")

if(BUILD_TESTING)
	find_program(POWERSHELL_EXECUTABLE
		NAMES pwsh powershell
		REQUIRED)

	add_test(NAME M11Core.PortableConformance
		COMMAND ABTSM11CoreConformance --json)
	set_tests_properties(M11Core.PortableConformance PROPERTIES
		PASS_REGULAR_EXPRESSION "\"passed\":true"
		LABELS "M11Core;Portable;Conformance")

	add_test(NAME M11Core.NoUnrealEditorDependencies
		COMMAND "${POWERSHELL_EXECUTABLE}"
			-NoLogo
			-NoProfile
			-ExecutionPolicy Bypass
			-File
			"${CMAKE_CURRENT_LIST_DIR}/TestNoUnrealDependencies.ps1"
			-Executable
			"$<TARGET_FILE:ABTSM11CoreConformance>")
	set_tests_properties(M11Core.NoUnrealEditorDependencies PROPERTIES
		PASS_REGULAR_EXPRESSION "\"passed\":true"
		LABELS "M11Core;Portable;Dependencies")

	add_test(NAME M11Search.DeterministicSmoke
		COMMAND ABTSM11SearchCLI --self-test --json)
	set_tests_properties(M11Search.DeterministicSmoke PROPERTIES
		PASS_REGULAR_EXPRESSION "\"passed\":true"
		LABELS "M11Search;Portable;Determinism"
		TIMEOUT 600)

	add_test(NAME M11Search.NoUnrealEditorDependencies
		COMMAND "${POWERSHELL_EXECUTABLE}"
			-NoLogo
			-NoProfile
			-ExecutionPolicy Bypass
			-File
			"${CMAKE_CURRENT_LIST_DIR}/TestNoUnrealDependencies.ps1"
			-Executable
			"$<TARGET_FILE:ABTSM11SearchCLI>")
	set_tests_properties(M11Search.NoUnrealEditorDependencies PROPERTIES
		PASS_REGULAR_EXPRESSION "\"passed\":true"
		LABELS "M11Search;Portable;Dependencies")
endif()

message(STATUS
	"[ABTS][M11-A-v2.1] Portable target configured with MSVC tools "
	"${ABTS_M11_ACTIVE_VC_TOOLS_VERSION}; ProductionCoreHash="
	"${ABTS_M11_PRODUCTION_CORE_SOURCE_HASH_SHA256}; "
	"ConformanceToolHash="
	"${ABTS_M11_CONFORMANCE_TOOL_SOURCE_HASH_SHA256}; SearchToolHash="
	"${ABTS_M11_SEARCH_TOOL_SOURCE_HASH_SHA256}; ToolVersion="
	"${ABTS_M11_TOOL_BUILD_VERSION}; manifest="
	"${CMAKE_BINARY_DIR}/m11_core_source_manifest.json")
