cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0091 NEW)
cmake_policy(SET CMP0042 NEW)

project(node-qnn-llm)

set(CMAKE_CXX_STANDARD 17)

if (DEFINED ENV{QNN_SDK_ROOT} AND NOT QNN_SDK_ROOT)
  set(QNN_SDK_ROOT $ENV{QNN_SDK_ROOT})
endif()

if (NOT QNN_SDK_ROOT)
  message(FATAL_ERROR "QNN_SDK_ROOT is not set")
endif()

add_definitions(-DNAPI_VERSION=7)

include_directories(
  ${CMAKE_JS_INC}
  ${CMAKE_SOURCE_DIR}/src
  ${QNN_SDK_ROOT}/include/Genie
  ${QNN_SDK_ROOT}/include/QNN
)

file(GLOB SOURCE_FILES
  "src/addons.cc"
  "src/utils.c"
  "src/EmbeddingsHolder.cpp"
  "src/Embedding.cpp"
  "src/EmbeddingQueryWorker.cpp"
  "src/ContextHolder.cpp"
  "src/Context.cpp"
  "src/LoadWorker.cpp"
  "src/QueryWorker.cpp"
  "src/ProcessWorker.cpp"
  "src/SaveSessionWorker.cpp"
  "src/RestoreSessionWorker.cpp"
  "src/ReleaseWorker.cpp"
  "src/UnpackWorker.cpp"
  "src/unpack.cpp"
)

set(QNN_LIBS "Genie")

string(REGEX MATCH "ARM64|aarch64|arm64" IS_ARM64 ${NODE_ARCH})
string(REGEX MATCH "x64|AMD64|amd64|x86_64" IS_X64 ${NODE_ARCH})

# Determine platform-specific package directory
if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND IS_ARM64)
  set(PACKAGE_NAME "node-qnn-llm-linux-arm64")
elseif (CMAKE_SYSTEM_NAME STREQUAL "Windows" AND IS_ARM64)
  set(PACKAGE_NAME "node-qnn-llm-win32-arm64")
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND IS_X64)
  set(PACKAGE_NAME "node-qnn-llm-linux-x64")
elseif (CMAKE_SYSTEM_NAME STREQUAL "Windows" AND IS_X64)
  set(PACKAGE_NAME "node-qnn-llm-win32-x64")
endif()
set(PLATFORM_PACKAGE_DIR "packages/${PACKAGE_NAME}")

if (CMAKE_SYSTEM_NAME STREQUAL "Windows" AND IS_ARM64 AND MSVC)
  set(QNN_ARCH "aarch64-windows-msvc")
  set(SHARED_PREFIX "")
  set(SHARED_EXT ".dll")
elseif (CMAKE_SYSTEM_NAME STREQUAL "Windows" AND IS_X64 AND MSVC)
  set(QNN_ARCH "x86_64-windows-msvc")
  set(SHARED_PREFIX "")
  set(SHARED_EXT ".dll")
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND IS_ARM64 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(QNN_ARCH "aarch64-oe-linux-gcc11.2")
  set(SHARED_PREFIX "lib")
  set(SHARED_EXT ".so")
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND IS_X64 AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set(QNN_ARCH "x86_64-linux-clang")
  set(SHARED_PREFIX "lib")
  set(SHARED_EXT ".so")
else()
  message(FATAL_ERROR "Unsupported system: ${CMAKE_SYSTEM_NAME} ${NODE_ARCH} with compiler: ${CMAKE_CXX_COMPILER_ID}")
endif()

set(QNN_LIB_DIR ${QNN_SDK_ROOT}/lib)
set(QNN_PLAT_LIB_DIR ${QNN_LIB_DIR}/${QNN_ARCH})

foreach(LIB IN LISTS QNN_LIBS)
  add_library(${LIB} SHARED IMPORTED)
  set_target_properties(${LIB} PROPERTIES IMPORTED_LOCATION ${QNN_PLAT_LIB_DIR}/${SHARED_PREFIX}${LIB}${SHARED_EXT})
  if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set_target_properties(${LIB} PROPERTIES IMPORTED_IMPLIB ${QNN_PLAT_LIB_DIR}/${SHARED_PREFIX}${LIB}.lib)
  endif()
endforeach()

include(FetchContent)

# zlib
set(ZLIB_BUILD_STATIC ON)
set(ZLIB_BUILD_SHARED OFF)
set(ZLIB_BUILD_EXAMPLES OFF)
set(ZLIB_BUILD_TESTING OFF)
set(ZLIB_INSTALL OFF)

FetchContent_Declare(
  zlib
  GIT_REPOSITORY https://github.com/madler/zlib.git
  GIT_TAG        v1.3.1
)
FetchContent_MakeAvailable(zlib)

# zstd
set(ZSTD_BUILD_STATIC ON)
set(ZSTD_BUILD_SHARED OFF)
set(ZSTD_BUILD_EXAMPLES OFF)
set(ZSTD_BUILD_TESTS OFF)

FetchContent_Declare(
  zstd
  GIT_REPOSITORY https://github.com/facebook/zstd.git
  GIT_TAG        v1.5.7
)
FetchContent_MakeAvailable(zstd)
add_subdirectory(${zstd_SOURCE_DIR}/build/cmake)

add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES} ${CMAKE_JS_SRC})
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "" SUFFIX ".node")
target_link_libraries(
  ${PROJECT_NAME}
  ${CMAKE_JS_LIB}
  Genie
  zlibstatic
  libzstd_static
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # node.js: node_modules/${PACKAGE_NAME}
  # electron: resources/node_modules/${PACKAGE_NAME}
  set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "-Wl,-rpath,node_modules/${PACKAGE_NAME} -Wl,-rpath,resources/node_modules/${PACKAGE_NAME}")
endif()

if(MSVC AND CMAKE_JS_NODELIB_DEF AND CMAKE_JS_NODELIB_TARGET)
  # Generate node.lib
  execute_process(COMMAND ${CMAKE_AR} /def:${CMAKE_JS_NODELIB_DEF} /out:${CMAKE_JS_NODELIB_TARGET} ${CMAKE_STATIC_LINKER_FLAGS})
endif()

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> ${CMAKE_SOURCE_DIR}/${PLATFORM_PACKAGE_DIR}/index.node
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:Genie> ${CMAKE_SOURCE_DIR}/${PLATFORM_PACKAGE_DIR}/${SHARED_PREFIX}Genie${SHARED_EXT}
)
