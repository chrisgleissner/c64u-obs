cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

# Determine authoritative version from git tag first
execute_process(
  COMMAND git describe --tags --always --dirty
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_VERSION_TAG
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)

# Extract clean version number from git tag for project version
if(GIT_VERSION_TAG AND GIT_VERSION_TAG MATCHES "^v?([0-9]+\\.[0-9]+\\.[0-9]+)")
  # Extract version number from tag (e.g., "v1.2.3" -> "1.2.3", "1.2.3-dirty" -> "1.2.3") 
  string(REGEX REPLACE "^v?([0-9]+\\.[0-9]+\\.[0-9]+).*" "\\1" AUTHORITATIVE_VERSION "${GIT_VERSION_TAG}")
  message(STATUS "Using git tag version: ${AUTHORITATIVE_VERSION}")
else()
  # Fallback to buildspec.json version if no proper git tag is available
  set(AUTHORITATIVE_VERSION "${_version}")
  message(STATUS "No git tag found, using buildspec.json version: ${AUTHORITATIVE_VERSION}")
endif()

project(${_name} VERSION ${AUTHORITATIVE_VERSION})

# Set dummy VirtualCam GUID to prevent Windows build warning
set(VIRTUALCAM_GUID "{00000000-0000-0000-0000-000000000000}")

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)
option(ENABLE_TESTS "Build tests" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

# Update buildspec.json with authoritative version if it came from git tag
if(GIT_VERSION_TAG AND GIT_VERSION_TAG MATCHES "^v?([0-9]+\\.[0-9]+\\.[0-9]+)")
  # Update buildspec.json with the git tag version to keep it in sync
  # Use sed to update only line 41 (the main project version, not dependency versions)
  execute_process(
    COMMAND sed -i "41s/\"version\": \"[^\"]*\"/\"version\": \"${AUTHORITATIVE_VERSION}\"/" "${CMAKE_SOURCE_DIR}/buildspec.json"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE sed_result
  )
  
  if(sed_result EQUAL 0)
    message(STATUS "Updated buildspec.json version to: ${AUTHORITATIVE_VERSION}")
  else()
    message(WARNING "Failed to update buildspec.json version")
  endif()
endif()

# Use git tag for display version (includes dirty/commit info), authoritative version for project
set(DISPLAY_VERSION "${GIT_VERSION_TAG}")

execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NOT GIT_HASH)
  set(GIT_HASH "unknown")
endif()

string(TIMESTAMP BUILD_TIME "%Y-%m-%d %H:%M:%S" UTC)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
  C64U_VERSION_TAG="${DISPLAY_VERSION}"
  C64U_GIT_HASH="${GIT_HASH}"
  C64U_BUILD_TIME="${BUILD_TIME}"
)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.c
    src/c64u-network.c
    src/c64u-protocol.c
    src/c64u-video.c
    src/c64u-color.c
    src/c64u-audio.c
    src/c64u-source.c
    src/c64u-record.c
    src/c64u-version.c
    src/c64u-properties.c
)

# Link resolver library for DNS functionality on Unix platforms
if(UNIX)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE resolv)
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# Add tests if enabled
if(ENABLE_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
