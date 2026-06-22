cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zenoh_pub)

file(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})

if(DEFINED ENV{ZENOH_LOCATOR} AND NOT "$ENV{ZENOH_LOCATOR}" STREQUAL "")
  target_compile_definitions(app PRIVATE ZENOH_LOCATOR=\"$ENV{ZENOH_LOCATOR}\")
  message(STATUS "zenoh_pub: ZENOH_LOCATOR is set from environment")
endif()
