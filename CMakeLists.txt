cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0091 NEW)
cmake_policy(SET CMP0042 NEW)

project(node-qnn-llm)

set(CMAKE_CXX_STANDARD 14)

if (DEFINED ENV{QNN_SDK_ROOT} AND NOT QNN_SDK_ROOT)
  set(QNN_SDK_ROOT $ENV{QNN_SDK_ROOT})
endif()

if (NOT QNN_SDK_ROOT)
  message(FATAL_ERROR "QNN_SDK_ROOT is not set")
endif()

add_definitions(-DNAPI_VERSION=7)

include_directories(
  ${CMAKE_JS_INC}
  ${QNN_SDK_ROOT}/include/Genie
  ${QNN_SDK_ROOT}/include/QNN
)

file(GLOB SOURCE_FILES "src/addons.cc" "src/Context.cpp" "src/utils.cpp")

set(QNN_LIB_DIR ${QNN_SDK_ROOT}/lib)
if (CMAKE_SYSTEM_NAME STREQUAL "Windows" AND CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")
  set(QNN_PLAT_LIB_DIR ${QNN_LIB_DIR}/aarch64-windows-msvc)
else()
  message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")
endif()

add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES} ${CMAKE_JS_SRC})
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "" SUFFIX ".node")
target_link_libraries(${PROJECT_NAME} ${CMAKE_JS_LIB} ${QNN_PLAT_LIB_DIR}/Genie.lib)

if(MSVC AND CMAKE_JS_NODELIB_DEF AND CMAKE_JS_NODELIB_TARGET)
  # Generate node.lib
  execute_process(COMMAND ${CMAKE_AR} /def:${CMAKE_JS_NODELIB_DEF} /out:${CMAKE_JS_NODELIB_TARGET} ${CMAKE_STATIC_LINKER_FLAGS})
endif()

install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_BINARY_DIR})
install(FILES ${QNN_PLAT_LIB_DIR}/Genie.dll DESTINATION ${CMAKE_BINARY_DIR})

