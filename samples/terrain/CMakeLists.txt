cmake_minimum_required(VERSION 3.28)

project(Terrain LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})
include(FetchContent)

set(JSON_BuildTests OFF CACHE BOOL "Disable JSON tests")
FetchContent_Declare(
        json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
)

FetchContent_MakeAvailable(json)

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
set_target_properties(${PROJECT_NAME} PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
  ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
  ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
)

###############################

#enable multiple cores compilation for VS
if(MSVC)
 target_compile_options(${PROJECT_NAME} PRIVATE "/MP")
endif()

find_package(VulkanEngine REQUIRED)
target_include_directories(${PROJECT_NAME} PRIVATE ${VulkanEngine_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} ${VulkanEngine_LIBRARIES} nlohmann_json::nlohmann_json)

set(SHADER_COMPILE_DEBUG 1)
#recompile after every build just in case, it's fast
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                   COMMAND python ${VulkanEngine_ROOT}/compile.py ${VulkanEngine_ROOT}/shaders ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders ${Vulkan_GLSLC_EXECUTABLE} ${SHADER_COMPILE_DEBUG}
                   COMMENT "Compile shaders")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                   WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                   COMMAND ${CMAKE_COMMAND} -E copy_directory ${VulkanEngine_DLL_PATH} ${CMAKE_BINARY_DIR}
                   COMMENT "Copy engine binaries")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                   WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                   COMMAND ${CMAKE_COMMAND} -E copy_directory ${VulkanEngine_ASSETS} ${CMAKE_CURRENT_SOURCE_DIR}/assets
                   COMMENT "Copy engine assets")
