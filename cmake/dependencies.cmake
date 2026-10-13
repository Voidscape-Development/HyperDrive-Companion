# Third-party libraries, fetched at configure time at pinned versions:
# nlohmann/json for the state, IXWebSocket for the WebSocket and HTTP client.

include_guard(GLOBAL)

include(FetchContent)

set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SYSTEM
)
FetchContent_MakeAvailable(nlohmann_json)

if(HYPERDRIVE_WITH_IXWEBSOCKET)
  # Plain HTTP and WebSocket to HyperDrive on the local network: no TLS or zlib
  set(USE_TLS OFF CACHE BOOL "" FORCE)
  set(USE_ZLIB OFF CACHE BOOL "" FORCE)
  set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
  set(_hd_build_shared_libs ${BUILD_SHARED_LIBS})
  set(BUILD_SHARED_LIBS OFF)
  FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG
      2efe037c9cc96fd536774f17bdb5215161ee5087 # v11.4.6
    SYSTEM
  )
  FetchContent_MakeAvailable(ixwebsocket)
  set(BUILD_SHARED_LIBS ${_hd_build_shared_libs})

  # Its warnings are not ours to fix: don't fail the build on them
  set_target_properties(
    ixwebsocket
    PROPERTIES
      POSITION_INDEPENDENT_CODE ON
      COMPILE_WARNING_AS_ERROR OFF
      XCODE_ATTRIBUTE_GCC_TREAT_WARNINGS_AS_ERRORS NO
      FOLDER deps
  )
endif()
