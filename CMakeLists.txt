cmake_minimum_required(VERSION 3.12)
project(dots-explorer LANGUAGES CXX)
set(TARGET_NAME dots-explorer)

# includes
include(FetchContent)

# CMake options
option(DOTS_EXPLORER_USE_VULKAN_BACKEND "Use Vulkan backend instead of OpenGL/DirectX 11" OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    # use default build type if none was specified
    set(DEFAULT_BUILD_TYPE "Release")
    message(STATUS "Setting build type to '${DEFAULT_BUILD_TYPE}' as none was specified.")
    set(CMAKE_BUILD_TYPE "${DEFAULT_BUILD_TYPE}" CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()
set(Boost_USE_STATIC_LIBS OFF)
if (MSVC)
    # use standard C++ exception model and assume no exceptions for external C functions
    # note that this needs to be set globally to include external targets because MSVC is disadvising
    # from mixing exceptions modes
    add_compile_options(/EHsc)
    # replace default warning level that CMake sets when using MSVC. the override code is based
    # on an example given by Kitware in [1]. note that this issue should already have been fixed since
    # CMake 3.15 (see [2]), but for some reason still occurs with the CMake 3.19.20122902-MSVC_2 version
    # used by Visual Studio 16.9.3
    #
    # References:
    #
    #  - [1] https://gitlab.kitware.com/cmake/community/wikis/FAQ#dynamic-replace
    #
    #  - [2] https://gitlab.kitware.com/cmake/cmake/-/issues/19084
    foreach (flag_var
             CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
             CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
        string(REPLACE "/W3" "/W4" ${flag_var} "${${flag_var}}")
    endforeach()
endif()

# dependencies
if (DOTS_EXPLORER_USE_VULKAN_BACKEND)
    message("Using Vulkan backend.")
    find_package(Vulkan REQUIRED)
else()
    if (WIN32)
        message("Using DirectX 11 backend.")
        find_library(DX11_LIBRARY d3d11.lib)
        if (${DX11_LIBRARY} MATCHES DX11_LIBRARY-NOTFOUND)
            message(FATAL_ERROR "Could not find DirectX 11 library.")
        endif()
        set(DOTS_EXPLORER_USE_DX11_BACKEND TRUE)
    else()
        message("Using OpenGL backend.")
        find_package(OpenGL REQUIRED)
        set(DOTS_EXPLORER_USE_OPENGL_BACKEND TRUE)
    endif()
endif()

option(DOTS_BUILD_EXAMPLES "Build the examples" OFF)
option(DOTS_BUILD_UNIT_TESTS "Build the unit tests" OFF)
FetchContent_Declare(
    dots-cpp
    GIT_REPOSITORY https://github.com/pnxs/dots-cpp.git
    GIT_TAG         02f07982ae237bb510302d45595b78ea21595ca1
    #GIT_TAG        4f6f8f0ca83b718f495ce9bc799d39cb5653af0e
#    GIT_TAG        aeb2881dd7258c425dfc8672c25bf39427a3d359
)

#FetchContent_Declare(
#    fmt
#    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
#    GIT_TAG        8.0.1
#)

option(GLFW_BUILD_EXAMPLES "Build the GLFW example programs" OFF)
option(GLFW_BUILD_TESTS "Build the GLFW test programs" OFF)
option(GLFW_BUILD_DOCS "Build the GLFW documentation" OFF)
option(GLFW_INSTALL "Generate installation target" OFF)
option(GLFW_DOCUMENT_INTERNALS "Include internals in documentation" OFF)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        master
)

FetchContent_Declare(
    re2
    GIT_REPOSITORY https://github.com/google/re2.git
    GIT_TAG        2022-04-01
)
FetchContent_MakeAvailable(dots-cpp glfw re2)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.90.9
)
# Check if population has already been performed
FetchContent_GetProperties(imgui)
if (NOT imgui_POPULATED)
  FetchContent_Populate(imgui)
endif()


find_package(Boost CONFIG PATHS ${BOOST_ROOT} REQUIRED COMPONENTS filesystem)

# target
add_executable(${TARGET_NAME})

# properties
target_dots_model(${TARGET_NAME}
    src/common/Settings.dots
    src/common/Version.dots
)
target_sources(${TARGET_NAME}
    PRIVATE
        src/main.cpp
        $<$<BOOL:${DOTS_EXPLORER_USE_VULKAN_BACKEND}>:src/backends/GlfwVulkanBackend.cpp>
        $<$<BOOL:${DOTS_EXPLORER_USE_OPENGL_BACKEND}>:src/backends/GlfwOpenGl3Backend.cpp>
        $<$<BOOL:${DOTS_EXPLORER_USE_DX11_BACKEND}>:src/backends/Win32DX11Backend.cpp>
        src/widgets/views/HostView.cpp
        src/widgets/MainWindow.cpp
        src/common/ImGuiExt.cpp
        src/common/Settings.cpp
        src/common/Version.cpp
        src/common/FilterMatcher.cpp
        src/models/EventModel.cpp
        src/models/PropertyModel.cpp
        src/models/PropertyDescriptorModel.cpp
        src/models/PublisherModel.cpp
        src/models/MetadataModel.cpp
        src/models/StructModel.cpp
        src/models/StructDescriptorModel.cpp
        src/models/TransceiverModel.cpp
        src/dots_ext/FileInChannel.cpp
        src/dots_ext/FileOutChannel.cpp
        src/dots_ext/struct_ops.cpp
        src/widgets/views/TraceView.cpp
        src/widgets/views/TraceItem.cpp
        src/widgets/dialogs/FileDialog.cpp
        src/widgets/dialogs/FilterSettingsEdit.cpp
        src/widgets/views/StructList.cpp
        src/widgets/dialogs/HostSettingsEdit.cpp
        src/widgets/dialogs/HelpDialog.cpp
        src/widgets/dialogs/PublishDialog.cpp
        src/widgets/views/StructItem.cpp
        src/widgets/views/CacheView.cpp
        src/widgets/dialogs/PropertyEdit.cpp
        src/widgets/input/HostEndpointEdit.cpp
        src/widgets/input/FilterExpressionEdit.cpp
        $<$<OR:$<BOOL:${DOTS_EXPLORER_USE_OPENGL_BACKEND}>,$<BOOL:${DOTS_EXPLORER_USE_VULKAN_BACKEND}>>:${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp>
        $<$<BOOL:${DOTS_EXPLORER_USE_VULKAN_BACKEND}>:${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp>
        $<$<BOOL:${DOTS_EXPLORER_USE_OPENGL_BACKEND}>:${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp>
        $<$<BOOL:${DOTS_EXPLORER_USE_DX11_BACKEND}>:${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp>
        $<$<BOOL:${DOTS_EXPLORER_USE_DX11_BACKEND}>:${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp>
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)
target_include_directories(${TARGET_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
)
target_compile_options(${TARGET_NAME}
    PRIVATE
        $<$<AND:$<CXX_COMPILER_ID:GNU>,$<NOT:$<BOOL:${CMAKE_CXX_FLAGS}>>>:-Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
target_compile_definitions(${TARGET_NAME}
    PRIVATE
        # suppress warning for usage of unsafe C runtime functions (e.g. getenv)
        $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
        $<$<BOOL:${Vulkan_FOUND}>:ImTextureID=ImU64>
        $<$<BOOL:${Vulkan_FOUND}>:VK_PROTOTYPES>
        IMGUI_DISABLE_OBSOLETE_FUNCTIONS
)
target_compile_features(${TARGET_NAME}
    PRIVATE
        cxx_std_20
)
target_link_options(${TARGET_NAME}
    PRIVATE
        $<$<AND:$<CONFIG:Release>,$<PLATFORM_ID:Windows>>:/SUBSYSTEM:windows /ENTRY:mainCRTStartup>
)
target_link_libraries(${TARGET_NAME}
    PRIVATE
        DOTS::DOTS
        fmt::fmt
        glfw
        $<$<BOOL:${DOTS_EXPLORER_USE_VULKAN_BACKEND}>:Vulkan::Vulkan>
        "$<$<BOOL:${DOTS_EXPLORER_USE_OPENGL_BACKEND}>:${OPENGL_LIBRARIES}>"
        Boost::filesystem
        re2::re2
        $<$<BOOL:${DOTS_EXPLORER_USE_DX11_BACKEND}>:d3d11>
)

# install
install (TARGETS ${TARGET_NAME} DESTINATION sbin)
