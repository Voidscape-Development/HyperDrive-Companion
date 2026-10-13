# The unit tests of the plugin's core (src/core), which needs neither OBS nor
# Qt. Built on their own:
#
#   cmake -S tests -B build_tests && cmake --build build_tests && ctest --test-dir build_tests

cmake_minimum_required(VERSION 3.28...3.30)

project(hyperdrive-companion-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

set(HYPERDRIVE_WITH_IXWEBSOCKET OFF)
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/dependencies.cmake")

set(_core "${CMAKE_CURRENT_SOURCE_DIR}/../src/core")
add_executable(
  core-tests
  test-main.cpp
  test-address.cpp
  test-engine-io.cpp
  test-state.cpp
  test-commands.cpp
  test-triggers.cpp
  test-layouts.cpp
  test-config.cpp
  ${_core}/address.cpp
  ${_core}/commands.cpp
  ${_core}/config.cpp
  ${_core}/engine-io.cpp
  ${_core}/layouts.cpp
  ${_core}/state.cpp
  ${_core}/triggers.cpp
)
target_include_directories(core-tests PRIVATE ${_core})
target_link_libraries(core-tests PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(core-tests PRIVATE TESTS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
if(MSVC)
  target_compile_options(core-tests PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(core-tests PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()
add_test(NAME core-tests COMMAND core-tests)
