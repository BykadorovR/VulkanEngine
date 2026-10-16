cmake_minimum_required(VERSION 3.28)

project(AndroidApplication)
set(CMAKE_CXX_STANDARD 23)
include(FetchContent)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -DUSE_DEBUG_EXTENTIONS -DVK_USE_PLATFORM_ANDROID_KHR")
FetchContent_Declare(
        VulkanEngine        
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/../../../../../../
)
FetchContent_MakeAvailable(VulkanEngine)

# Integrate GameActivity, refer to
#     https://d.android.com/games/agdk/integrate-game-activity
# for the detailed instructions.
find_package(game-activity REQUIRED CONFIG)

include_directories("include")
file(GLOB_RECURSE app_source "src/*.c*")
source_group("src" FILES ${app_source})
file(GLOB_RECURSE app_header "include/*.h*")
source_group("include" FILES ${app_header})

add_library(${PROJECT_NAME} SHARED ${app_source} ${app_header})
target_compile_definitions(${PROJECT_NAME} PRIVATE GLM_ENABLE_EXPERIMENTAL)

set(LAYER_SRC_DIR ${ANDROID_NDK}/sources/third_party/vulkan/src)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR}/include 
                                                   ${LAYER_SRC_DIR}/include 
                                                   ${VulkanEngine_SOURCE_DIR}/include)
target_link_libraries(${PROJECT_NAME} VulkanEngine game-activity::game-activity log android)

# debug shaders crash on Android with enabled shadows
set(SHADER_COMPILE_DEBUG 0)
#recompile after every build just in case, it's fast
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                   COMMAND python ${VulkanEngine_SOURCE_DIR}/compile.py ${VulkanEngine_SOURCE_DIR}/shaders ${CMAKE_SOURCE_DIR}/../assets/shaders ${Vulkan_GLSLC_EXECUTABLE} ${SHADER_COMPILE_DEBUG}
                   COMMENT "Compile shaders")
