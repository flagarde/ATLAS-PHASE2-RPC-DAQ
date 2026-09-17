cmake_minimum_required(VERSION 3.25)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GetCMakeMM)
cmmm(VERSION 1.1 NO_CHANGELOG)

cmmm_modules_list(URL "gh:cmake-tools/cmcm")

# default versions
set(CPM_TAG "v0.43.1")
set(CPM_REPOSITORY "https://github.com/cpm-cmake/CPM.cmake.git")

project(RPCDAQ VERSION 1.0)
include(GNUInstallDirs)

get_property(GENERATOR_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT GENERATOR_IS_MULTI_CONFIG)
  if(NOT DEFINED CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE "Debug")
  endif()
  message(STATUS "Compiling type : ${CMAKE_BUILD_TYPE}")
else()
  message("Single-configuration generator")
endif()

add_subdirectory(src)
