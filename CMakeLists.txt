cmake_minimum_required(VERSION 3.24)
project(TitanExt VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler definitions and flags
if (WIN32)
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

if (MSVC)
    add_compile_options(/W4 /permissive- /Zc:preprocessor)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-variable -Wno-unused-but-set-variable)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-Wno-packed-not-aligned)
    endif()
    if (WIN32)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
    endif()
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Standalone TitanExt Core Library
add_library(titanext_core INTERFACE)
target_include_directories(titanext_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Test Suite Executable
add_executable(titanext_tests tests/test_ext4.cpp)
target_link_libraries(titanext_tests PRIVATE titanext_core)

# CLI Formatting Tool
add_executable(titanext_format src/tools/titanext_format.cpp)
target_link_libraries(titanext_format PRIVATE titanext_core)

# CLI Inspection & Scrubber Tool
add_executable(titanext_check src/tools/titanext_check.cpp)
target_link_libraries(titanext_check PRIVATE titanext_core)

# Background Service Daemon
add_executable(titanext_svc src/service/titanext_svc.cpp)
target_link_libraries(titanext_svc PRIVATE titanext_core)
if (WIN32)
    target_link_libraries(titanext_svc PRIVATE advapi32)
endif()

if (WIN32)
    # WinFsp Mount Driver Bridge
    add_executable(titanext_mount src/mount/winfsp_mount.cpp)
    target_link_libraries(titanext_mount PRIVATE titanext_core advapi32)

    # Standalone Installer Packager
    add_executable(titanext_packager src/tools/titanext_packager.cpp)
    target_link_libraries(titanext_packager PRIVATE titanext_core)

    # Standalone 1-Click GUI Installer
    add_executable(titanext_setup WIN32 src/installer/titanext_setup.cpp)
    target_link_libraries(titanext_setup PRIVATE titanext_core user32 gdi32 gdiplus comctl32 dwmapi uxtheme ole32 oleaut32 shell32 advapi32)
    add_dependencies(titanext_setup titanext_format titanext_check titanext_mount titanext_svc titanext_packager)

    set(WINFSP_MSI "${CMAKE_CURRENT_SOURCE_DIR}/redist/winfsp-2.1.25156.msi")
    set(PACKAGER_EXTRA_ARGS "")
    if (EXISTS "${WINFSP_MSI}")
        set(PACKAGER_EXTRA_ARGS "${WINFSP_MSI}")
    endif()

    add_custom_command(
        TARGET titanext_setup POST_BUILD
        COMMAND titanext_packager $<TARGET_FILE:titanext_setup>
                $<TARGET_FILE:titanext_format>
                $<TARGET_FILE:titanext_check>
                $<TARGET_FILE:titanext_mount>
                $<TARGET_FILE:titanext_svc>
                ${CMAKE_CURRENT_SOURCE_DIR}/assets/titanext_hero.jpg
                ${PACKAGER_EXTRA_ARGS}
        COMMENT "Embedding all binaries, WinFsp MSI, and assets into standalone titanext_setup.exe..."
    )
endif()

# ============================================================================
# CPack Packaging Configuration
# ============================================================================
set(CPACK_PACKAGE_NAME "TitanExt")
set(CPACK_PACKAGE_VENDOR "MicaNT-Kernel")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Linux Ext4/3/2 File System Storage Engine and WinFsp Mount Bridge for Windows NT")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

install(TARGETS titanext_tests titanext_format titanext_check titanext_svc RUNTIME DESTINATION bin)
if (WIN32)
    install(TARGETS titanext_mount titanext_setup RUNTIME DESTINATION bin)
    install(DIRECTORY assets/ DESTINATION assets)
    install(DIRECTORY redist/ DESTINATION redist OPTIONAL)
endif()

set(CPACK_GENERATOR "ZIP")
include(CPack)

# CTest registration
enable_testing()
add_test(NAME TitanExtCoreTests COMMAND titanext_tests)
