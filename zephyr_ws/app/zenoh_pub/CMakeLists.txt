cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zenoh_pub)

file(GLOB app_sources src/*.c)
set(common_sources ${CMAKE_CURRENT_SOURCE_DIR}/../common/src/atomic_shim.c
                   ${CMAKE_CURRENT_SOURCE_DIR}/../common/src/net_utils.c)
target_sources(app PRIVATE ${app_sources} ${common_sources})
target_include_directories(
  app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common/include)

if(DEFINED ENV{ZENOH_LOCATOR} AND NOT "$ENV{ZENOH_LOCATOR}" STREQUAL "")
  target_compile_definitions(app PRIVATE ZENOH_LOCATOR=\"$ENV{ZENOH_LOCATOR}\")
  message(STATUS "zenoh_pub: ZENOH_LOCATOR is set from environment")
endif()
