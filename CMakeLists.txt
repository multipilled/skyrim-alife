cmake_minimum_required(VERSION 3.22)
project(ALife VERSION 2.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "" FORCE)

# CommonLibSSE-NG: alandtse/CommonLibSSE-NG v11.0.0 (94faaed0c60e, 2026-10-04) — AE 1.7.x / Address Library v5.
# SE+AE runtime support, VR and tests off.
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SKSE_SUPPORT_XBYAK OFF CACHE BOOL "" FORCE)

if (DEFINED ENV{ALIFE_CLIB} AND EXISTS "$ENV{ALIFE_CLIB}/CMakeLists.txt")
  set(ALIFE_CLIB_DIR "$ENV{ALIFE_CLIB}")
else()
  message(FATAL_ERROR "Set ALIFE_CLIB to the alandtse/CommonLibSSE-NG v11.0.0 checkout")
endif()
message(STATUS "Using CommonLibSSE-NG at ${ALIFE_CLIB_DIR}")
add_subdirectory(${ALIFE_CLIB_DIR} _commonlibsse EXCLUDE_FROM_ALL)

file(GLOB ALIFE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp")
add_library(${PROJECT_NAME} SHARED ${ALIFE_SOURCES})
target_link_libraries(${PROJECT_NAME} PRIVATE CommonLibSSE::CommonLibSSE)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_precompile_headers(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/ALife/PCH.h)
target_compile_definitions(${PROJECT_NAME} PRIVATE
  ALIFE_VERSION_MAJOR=2 ALIFE_VERSION_MINOR=0 ALIFE_VERSION_PATCH=0
  _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN
)
target_compile_options(${PROJECT_NAME} PRIVATE /permissive- /Zc:preprocessor /W3 /utf-8 /EHsc)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "ALife")
