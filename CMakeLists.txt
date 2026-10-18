cmake_minimum_required(VERSION 3.28)

project(VulkanEngine LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)

if(MSVC)
  set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})
endif()
include(FetchContent)

if (NOT ANDROID)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Disable GLFW examples")
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "Disable GLFW tests")
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "Disable GLFW docs")
  FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 4afa227a056681d2628894b0893527bf69496a41
  )
endif()


set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "Disable tinygltf loader example")
set(TINYGLTF_HEADER_ONLY ON CACHE BOOL "Build header only tinygltf")
FetchContent_Declare(
        tinygltf
        GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
        GIT_TAG        v2.8.21
)

FetchContent_Declare(
        thread-pool
        GIT_REPOSITORY https://github.com/bshoshany/thread-pool.git
        GIT_TAG        cabb3df5876c9a6824b07fcb0ff73d4a0e506ca0
)

file(MAKE_DIRECTORY ${FETCHCONTENT_BASE_DIR}/mikktspace)
file(WRITE ${FETCHCONTENT_BASE_DIR}/mikktspace/CMakeLists.txt
  "cmake_minimum_required(VERSION 3.22)\n"
  "project(mikktspace LANGUAGES C)\n"
  "add_library(mikktspace \"${FETCHCONTENT_BASE_DIR}/mikktspace-src/mikktspace.c\")\n"
  "target_include_directories(mikktspace PUBLIC \"${FETCHCONTENT_BASE_DIR}/mikktspace-src\")\n"
)
FetchContent_Declare(
        mikktspace
        DOWNLOAD_COMMAND git clone https://github.com/mmikk/MikkTSpace.git ${FETCHCONTENT_BASE_DIR}/mikktspace-src &&
                         cd ${FETCHCONTENT_BASE_DIR}/mikktspace-src && git checkout 3e895b49d05ea07e4c2133156cfa94369e19e409 &&
                         ${CMAKE_COMMAND} -E copy ${FETCHCONTENT_BASE_DIR}/mikktspace/CMakeLists.txt ${FETCHCONTENT_BASE_DIR}/mikktspace-src/CMakeLists.txt

)

FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG        1.0.1
)

file(MAKE_DIRECTORY ${FETCHCONTENT_BASE_DIR}/imgui)
file(WRITE ${FETCHCONTENT_BASE_DIR}/imgui/CMakeLists.txt
  "cmake_minimum_required(VERSION 3.22)\n"
  "project(imgui LANGUAGES CXX)\n"
  "add_library(imgui\n"
  "  \"${FETCHCONTENT_BASE_DIR}/imgui-src/imgui.cpp\"\n"
  "  \"${FETCHCONTENT_BASE_DIR}/imgui-src/imgui_draw.cpp\"\n"
  "  \"${FETCHCONTENT_BASE_DIR}/imgui-src/imgui_tables.cpp\"\n"
  "  \"${FETCHCONTENT_BASE_DIR}/imgui-src/imgui_widgets.cpp\"\n"
  "  \"${FETCHCONTENT_BASE_DIR}/imgui-src/imgui_demo.cpp\"\n"
  ")\n"
  "target_include_directories(imgui PUBLIC \"${FETCHCONTENT_BASE_DIR}/imgui-src\")\n"
)

FetchContent_Declare(
  imgui
  DOWNLOAD_COMMAND git clone https://github.com/ocornut/imgui ${FETCHCONTENT_BASE_DIR}/imgui-src &&
                   cd ${FETCHCONTENT_BASE_DIR}/imgui-src && git checkout 70cca1eac07aa3809bdd3717253c3754a5b7cfcc &&
                   ${CMAKE_COMMAND} -E copy ${FETCHCONTENT_BASE_DIR}/imgui/CMakeLists.txt ${FETCHCONTENT_BASE_DIR}/imgui-src/CMakeLists.txt
)

FetchContent_Declare(
        nvtx
        GIT_REPOSITORY https://github.com/NVIDIA/NVTX.git
        GIT_TAG        release-v3
)

set(JSON_BuildTests OFF CACHE BOOL "Disable JSON tests")
FetchContent_Declare(
        json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
)

set(CPP_RTTI_ENABLED ON CACHE BOOL "Enable C++ RTTI")
set(CPP_EXCEPTIONS_ENABLED ON CACHE BOOL "Enable C++ exceptions" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "Disable warnings as errors in Jolt" FORCE)
if (WIN32)
  #hack to force MD (default mode) instead of MT
  set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "Override static MSVC runtime library setting" FORCE)
endif()
FetchContent_Declare(
  jolt
  GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
  GIT_TAG        7a14ab982e279c3e20826f6769bb93d5637242b8
  SOURCE_SUBDIR  Build
)

FetchContent_Declare(
    vk-bootstrap
    GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap
    GIT_TAG        v1.3.302
)

#there is no UNIX one in vulkan/vulkan.h
if (ANDROID)
   set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_ANDROID_KHR)
elseif(WIN32)
   set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_WIN32_KHR)
endif()
FetchContent_Declare(
    volk
    GIT_REPOSITORY https://github.com/zeux/volk
    GIT_TAG e767d0ee782709f2bd2df148927ddab02f2dbfe4
)

FetchContent_Declare(
  vulkan-memory-allocator
  GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
  GIT_TAG v3.2.1
)

set(RENDER_GRAPH_BUILD_TESTS OFF CACHE BOOL "Build the RenderGraph test suite" FORCE)
FetchContent_Declare(
  renderGraph
  GIT_REPOSITORY https://github.com/BykadorovR/RenderGraph.git
  GIT_TAG        master
)

set(vulkanEngineDependencies
  glm
  imgui
  tinygltf
  thread-pool
  mikktspace
  nvtx
  json
  vk-bootstrap
  volk
  vulkan-memory-allocator
  jolt
  renderGraph
)
if (NOT ANDROID)
  list(APPEND vulkanEngineDependencies glfw)
endif()
FetchContent_MakeAvailable(${vulkanEngineDependencies})

# Jolt adds the implementation detail _DEBUG to its public interface. The
# consumer already gets the correct runtime/debug mode from its own build
# configuration, so leaking this macro only contaminates imported C++ modules.
get_target_property(joltInterfaceDefinitions Jolt INTERFACE_COMPILE_DEFINITIONS)
if (joltInterfaceDefinitions)
  list(FILTER joltInterfaceDefinitions EXCLUDE REGEX "(^|:)_DEBUG(>|$)")
  set_property(TARGET Jolt PROPERTY INTERFACE_COMPILE_DEFINITIONS "${joltInterfaceDefinitions}")
endif()

if (ANDROID)
  set_target_properties(vk-bootstrap PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

file(GLOB_RECURSE app_source_graphic CONFIGURE_DEPENDS "src/Graphic/*.c*")
source_group("Graphic\\src" FILES ${app_source_graphic})
file(GLOB_RECURSE app_header_graphic CONFIGURE_DEPENDS "include/Graphic/*.h*")
source_group("Graphic\\include" FILES ${app_header_graphic})
file(GLOB_RECURSE app_source_primitive CONFIGURE_DEPENDS "src/Primitive/*.c*")
source_group("Primitive\\src" FILES ${app_source_primitive})
file(GLOB_RECURSE app_header_primitive CONFIGURE_DEPENDS "include/Primitive/*.h*")
source_group("Primitive\\include" FILES ${app_header_primitive})
file(GLOB_RECURSE app_source_utility CONFIGURE_DEPENDS "src/Utility/*.c*")
source_group("Utility\\src" FILES ${app_source_utility})
file(GLOB_RECURSE app_header_utility CONFIGURE_DEPENDS "include/Utility/*.h*")
source_group("Utility\\include" FILES ${app_header_utility})
file(GLOB_RECURSE app_source_engine CONFIGURE_DEPENDS "src/Engine/*.c*")
source_group("Engine\\src" FILES ${app_source_engine})
file(GLOB_RECURSE app_header_engine CONFIGURE_DEPENDS "include/Engine/*.h*")
source_group("Engine\\include" FILES ${app_header_engine})


###############################
if (WIN32)
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

#include headers to project (so they will be shown in include folder)
add_library(${PROJECT_NAME} SHARED ${app_header_graphic}   ${app_source_graphic}
                                   ${app_header_primitive} ${app_source_primitive}
                                   ${app_header_utility}   ${app_source_utility}
                                   ${app_header_engine}    ${app_source_engine})
if (NOT ANDROID)
  set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/libraries"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/libraries"
  )
endif()
add_library(VulkanEngine::VulkanEngine ALIAS ${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)
target_compile_definitions(${PROJECT_NAME} PRIVATE
  GLM_ENABLE_EXPERIMENTAL
)
target_compile_definitions(${PROJECT_NAME} PUBLIC
  VK_NO_PROTOTYPES
  $<$<PLATFORM_ID:Windows>:NOMINMAX>
)
if (NOT ANDROID)
  target_compile_definitions(${PROJECT_NAME} PRIVATE GLFW_INCLUDE_NONE)
endif()
###############################

#enable multiple cores compilation for VS
if(MSVC)
 target_compile_options(${PROJECT_NAME} PRIVATE "/MP")
endif()

find_package(Vulkan REQUIRED)
target_include_directories(${PROJECT_NAME} PRIVATE ${Vulkan_INCLUDE_DIRS})

# copy includes so they are visible for applications
file(COPY ${imgui_SOURCE_DIR}/imgui.h ${imgui_SOURCE_DIR}/imconfig.h DESTINATION ${CMAKE_BINARY_DIR}/include)
file(COPY ${tinygltf_SOURCE_DIR}/tiny_gltf.h ${tinygltf_SOURCE_DIR}/stb_image.h ${tinygltf_SOURCE_DIR}/stb_image_write.h ${tinygltf_SOURCE_DIR}/json.hpp DESTINATION ${CMAKE_BINARY_DIR}/include)
file(COPY ${thread-pool_SOURCE_DIR}/include/ DESTINATION ${CMAKE_BINARY_DIR}/include)
file(COPY ${mikktspace_SOURCE_DIR}/mikktspace.h DESTINATION ${CMAKE_BINARY_DIR}/include)
file(COPY ${nvtx_SOURCE_DIR}/c/include/ DESTINATION ${CMAKE_BINARY_DIR}/include)
if (NOT ANDROID)
  file(COPY ${glfw_SOURCE_DIR}/include/ DESTINATION ${CMAKE_BINARY_DIR}/include)
endif()
#TODO: how to copy only headers?
file(COPY ${jolt_SOURCE_DIR}/Jolt DESTINATION ${CMAKE_BINARY_DIR}/include)
file(COPY ${vk-bootstrap_SOURCE_DIR}/src/VkBootstrap.h ${vk-bootstrap_SOURCE_DIR}/src/VkBootstrapDispatch.h DESTINATION ${CMAKE_BINARY_DIR}/include)
file(COPY ${volk_SOURCE_DIR}/volk.h DESTINATION ${CMAKE_BINARY_DIR}/include)
file(COPY ${vulkan-memory-allocator_SOURCE_DIR}/include/vk_mem_alloc.h DESTINATION ${CMAKE_BINARY_DIR}/include)


# Dependencies shared by desktop and Android builds.
target_link_libraries(${PROJECT_NAME} PRIVATE
  imgui
  mikktspace
  nlohmann_json::nlohmann_json
)
target_link_libraries(${PROJECT_NAME} PUBLIC
  Jolt
  RenderGraph::RenderGraph
)

if (ANDROID)
  target_include_directories(${PROJECT_NAME} PRIVATE ${ANDROID_NDK}/sources/third_party/vulkan/src/include)
  target_link_libraries(${PROJECT_NAME} PRIVATE android log)
endif()
