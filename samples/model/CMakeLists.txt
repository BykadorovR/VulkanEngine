cmake_minimum_required(VERSION 3.16)

project(Model LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

#need for mismatch resolving
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR})
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})
include(${CMAKE_ROOT}/Modules/ExternalProject.cmake)

#affects all projects, add folder "include" to include section in VS
include_directories("include")

file(GLOB_RECURSE app_source "src/*.c*")
source_group("src" FILES ${app_source})
file(GLOB_RECURSE app_header "include/*.h*")
source_group("include" FILES ${app_header})

###############################
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

#include headers to project (so they will be shown in include folder)
add_executable(${PROJECT_NAME} ${app_header} ${app_source})

###############################

#enable multiple cores compilation for VS
if(MSVC)
 target_compile_options(${PROJECT_NAME} PRIVATE "/MP")
endif()

find_package(VulkanEngine REQUIRED)
target_include_directories(${PROJECT_NAME} PRIVATE ${VulkanEngine_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} ${VulkanEngine_LIBRARIES})

set(SHADER_COMPILE_DEBUG 1)
#recompile after every build just in case, it's fast
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                   COMMAND python ${VulkanEngine_ROOT}/compile.py ${VulkanEngine_ROOT}/shaders shaders ${Vulkan_GLSLC_EXECUTABLE} ${SHADER_COMPILE_DEBUG}
                   COMMENT "Compile shaders")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                   WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                   COMMAND ${CMAKE_COMMAND} -E copy_directory ${VulkanEngine_DLL_PATH} ${CMAKE_BINARY_DIR}
                   COMMENT "Copy engine binaries")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                   WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                   COMMAND ${CMAKE_COMMAND} -E copy_directory ${VulkanEngine_ASSETS} ${CMAKE_BINARY_DIR}/assets
                   COMMENT "Copy engine assets")