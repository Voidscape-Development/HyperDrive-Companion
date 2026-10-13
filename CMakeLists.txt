cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

if(NOT ENABLE_FRONTEND_API OR NOT ENABLE_QT)
  message(
    FATAL_ERROR
    "HyperDrive Companion needs the frontend API and Qt: configure with ENABLE_FRONTEND_API and ENABLE_QT on"
  )
endif()

set(HYPERDRIVE_WITH_IXWEBSOCKET ON)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/dependencies.cmake")

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

find_package(obs-frontend-api REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)
set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json ixwebsocket)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
# IXWebSocket's headers bring in windows.h, whose min and max macros break std::min and std::max
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE $<$<PLATFORM_ID:Windows>:NOMINMAX>)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/companion.cpp
    src/companion.hpp
    src/core/address.cpp
    src/core/address.hpp
    src/core/commands.cpp
    src/core/commands.hpp
    src/core/config.cpp
    src/core/config.hpp
    src/core/engine-io.cpp
    src/core/engine-io.hpp
    src/core/layouts.cpp
    src/core/layouts.hpp
    src/core/state.cpp
    src/core/state.hpp
    src/core/triggers.cpp
    src/core/triggers.hpp
    src/hotkeys.cpp
    src/hotkeys.hpp
    src/layout-source.cpp
    src/layout-source.hpp
    src/net/hyperdrive-client.cpp
    src/net/hyperdrive-client.hpp
    src/plugin-main.cpp
    src/text.hpp
    src/ui/dock.cpp
    src/ui/dock.hpp
    src/ui/settings-dialog.cpp
    src/ui/settings-dialog.hpp
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
