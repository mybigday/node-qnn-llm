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
  "src/ContextHolder.cpp"
  "src/Context.cpp"
  "src/LoadWorker.cpp"
  "src/QueryWorker.cpp"
  "src/SaveSessionWorker.cpp"
  "src/RestoreSessionWorker.cpp"
  "src/ReleaseWorker.cpp"
  "src/UnpackWorker.cpp"
  "src/unpack.cpp"
)

set(QNN_LIBS "Genie")

set(QNN_LIB_DIR ${QNN_SDK_ROOT}/lib)
set(QNN_PLAT_LIB_DIR ${QNN_LIB_DIR}/aarch64-windows-msvc)

foreach(LIB IN LISTS QNN_LIBS)
  add_library(${LIB} SHARED IMPORTED)
  set_target_properties(${LIB} PROPERTIES IMPORTED_LOCATION ${QNN_PLAT_LIB_DIR}/${LIB}.dll)
  set_target_properties(${LIB} PROPERTIES IMPORTED_IMPLIB ${QNN_PLAT_LIB_DIR}/${LIB}.lib)
endforeach()

include(FetchContent)

# zlib
FetchContent_Declare(
  zlib
  GIT_REPOSITORY https://github.com/madler/zlib.git
  GIT_TAG        v1.3.1
)
FetchContent_MakeAvailable(zlib)

# zstd
FetchContent_Declare(
  zstd
  GIT_REPOSITORY https://github.com/facebook/zstd.git
  GIT_TAG        v1.5.7
)
FetchContent_MakeAvailable(zstd)

add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES} ${CMAKE_JS_SRC})
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "" SUFFIX ".node")
target_link_libraries(
  ${PROJECT_NAME}
  ${CMAKE_JS_LIB}
  Genie
  ZLIB::ZLIB
  ZSTD::libzstd_static
)

if(MSVC AND CMAKE_JS_NODELIB_DEF AND CMAKE_JS_NODELIB_TARGET)
  # Generate node.lib
  execute_process(COMMAND ${CMAKE_AR} /def:${CMAKE_JS_NODELIB_DEF} /out:${CMAKE_JS_NODELIB_TARGET} ${CMAKE_STATIC_LINKER_FLAGS})
endif()

set(TARGET_DIR ${CMAKE_SOURCE_DIR}/dist)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> ${TARGET_DIR}/${PROJECT_NAME}.node
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:Genie> ${TARGET_DIR}/Genie.dll
)
