# FindVulkanEngine
# -------
#
# Finds the VulkanEngine library
#
# This will define the following variables:
#
#   VulkanEngine_FOUND        -- True if the system has the VulkanEngine library
#   VulkanEngine_ROOT         -- Path to root folder
#   VulkanEngine_INCLUDE_DIRS -- The include directories for VulkanEngine
#   VulkanEngine_LIBRARIES    -- Libraries to link against
#   VulkanEngine_DLL_PATH     -- DLLs required by VulkanEngine, valid on Windows platform only
#   VulkanEngine_SHADERS      -- Shaders folder
#   VulkanEngine_ASSETS       -- Folder with mandatory images and models
# and the following target:
#   VulkanEngine::VulkanEngine

include(FindPackageHandleStandardArgs)

if(DEFINED ENV{VulkanEngine_INSTALL_PREFIX})
  message(STATUS "ENVIRONMENT")
  set(VulkanEngine_INSTALL_PREFIX $ENV{VulkanEngine_INSTALL_PREFIX})
else()
  # Assume we are in <install-prefix>/cmake/VulkanEngineConfig.cmake
  get_filename_component(CMAKE_CURRENT_LIST_DIR "${CMAKE_CURRENT_LIST_FILE}" PATH)
  get_filename_component(VulkanEngine_INSTALL_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../" ABSOLUTE)
endif()

set(VulkanEngine_ROOT ${VulkanEngine_INSTALL_PREFIX})
if(NOT VulkanEngineBuildDir)
  set(VulkanEngineBuildDir "${CMAKE_CURRENT_BINARY_DIR}/vulkanEngine")
endif()

# Include shaders.
set(VulkanEngine_SHADERS ${VulkanEngine_INSTALL_PREFIX}/shaders/)

find_package(Vulkan REQUIRED)

# VulkanEngine exposes RenderGraph C++ modules in its public headers. Build the
# engine in the consumer's CMake graph so CMake can compile and propagate the
# required module interfaces for the active compiler and configuration.
if(NOT TARGET VulkanEngine::VulkanEngine)
  if(NOT EXISTS "${VulkanEngine_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "VulkanEngine source tree is required to build its C++ modules")
  endif()
  add_subdirectory("${VulkanEngine_ROOT}" "${VulkanEngineBuildDir}")
endif()

# Include directories.
set(VulkanEngine_INCLUDE_DIRS ${VulkanEngine_INSTALL_PREFIX}/include/
                              ${CMAKE_BINARY_DIR}/include/
                              ${Vulkan_INCLUDE_DIRS})
set(VulkanEngine_LIBRARIES VulkanEngine::VulkanEngine)
# VulkanEngine dll path
set(VulkanEngine_DLL_PATH ${CMAKE_BINARY_DIR}/bin/)
set(VulkanEngine_ASSETS ${VulkanEngine_INSTALL_PREFIX}/assets)

find_package_handle_standard_args(VulkanEngine DEFAULT_MSG VulkanEngine_LIBRARIES
                                                           VulkanEngine_INCLUDE_DIRS
                                                           VulkanEngine_DLL_PATH
                                                           VulkanEngine_SHADERS
                                                           VulkanEngine_ASSETS)
