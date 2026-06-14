cmake_minimum_required(VERSION 3.21)
project(dopygame)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(X_VCPKG_APPLOCAL_DEPS_INSTALL ON)

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)

if(APPLE)
    set(CMAKE_INSTALL_RPATH "@executable_path/lib")
elseif(UNIX)
    set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")
endif()

# --------------------------------------------------
# Global build type default
# --------------------------------------------------

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No build type set; defaulting to Release")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(DOPYGAME_ENABLE_LTO "Enable link-time optimization for Release builds" ON)
option(DOPYGAME_ENABLE_TRACY "Enable Tracy profiler" OFF)

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)

if(ipo_supported AND DOPYGAME_ENABLE_LTO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    message(STATUS "LTO enabled")
endif()

if(DOPYGAME_ENABLE_LTO AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options($<$<CONFIG:Release>:-flto=thin>)
    add_link_options($<$<CONFIG:Release>:-flto=thin>)
endif()

# --------------------------------------------------
# Project optimization profile (applies only to our targets)
# --------------------------------------------------

set(_gnu_clang "$<CXX_COMPILER_ID:GNU,Clang>")
set(_c_or_cxx  "$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>")

add_library(dopygame_project_optimizations INTERFACE)

target_compile_options(dopygame_project_optimizations INTERFACE
    $<$<AND:${_c_or_cxx},${_gnu_clang}>:-fomit-frame-pointer>
    $<$<AND:${_c_or_cxx},${_gnu_clang},$<CONFIG:Release>>:-O3>
    $<$<AND:${_c_or_cxx},${_gnu_clang},$<CONFIG:Release>>:-ffunction-sections>
    $<$<AND:${_c_or_cxx},${_gnu_clang},$<CONFIG:Release>>:-fdata-sections>
    $<$<AND:${_c_or_cxx},${_gnu_clang},$<CONFIG:RelWithDebInfo>>:-O2>
    $<$<AND:${_c_or_cxx},${_gnu_clang},$<CONFIG:MinSizeRel>>:-Os>
)

target_link_options(dopygame_project_optimizations INTERFACE
    $<$<AND:${_c_or_cxx},$<PLATFORM_ID:Linux>,${_gnu_clang},$<CONFIG:Release>>:-Wl,--gc-sections>
    $<$<AND:${_c_or_cxx},$<PLATFORM_ID:Darwin>,${_gnu_clang},$<CONFIG:Release>>:-Wl,-dead_strip>
)

# --------------------------------------------------
# Dependencies
# --------------------------------------------------

find_package(SDL3 REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(libjpeg-turbo CONFIG REQUIRED)

find_package(SDL3_shadercross CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)

if(DOPYGAME_ENABLE_TRACY)
    find_package(Tracy CONFIG REQUIRED)
endif()

# --------------------------------------------------
# Application library + main executable
# --------------------------------------------------

add_subdirectory(src/Application)

add_executable(DOPYGAME MACOSX_BUNDLE src/main.cpp)

target_link_libraries(DOPYGAME PRIVATE dopy_game)
target_link_libraries(DOPYGAME PRIVATE SDL3_shadercross::SDL3_shadercross)
target_link_libraries(DOPYGAME PRIVATE glm::glm)
target_link_libraries(DOPYGAME PRIVATE EnTT::EnTT)

if(WIN32)
    target_link_libraries(DOPYGAME PRIVATE ws2_32 comctl32 setupapi version)
    target_link_options(DOPYGAME PRIVATE -static-libgcc -static-libstdc++)
endif()

# --------------------------------------------------
# Post-build
# --------------------------------------------------

set(VCPKG_LIB_DIR "${CMAKE_CURRENT_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/lib")

add_custom_command(TARGET DOPYGAME POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/Data"
    "$<TARGET_FILE_DIR:DOPYGAME>/Data"
    COMMENT "Copying Data folder to build directory"
)

# --------------------------------------------------
# Install rules (consumed by CPack)
# --------------------------------------------------

include(GNUInstallDirs)

set(DOPYGAME_INSTALL_BINDIR ".")

if(APPLE)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Data/" DESTINATION "DOPYGAME.app/Contents/MacOS")
else()
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Data/" DESTINATION "./Data")
endif()

# --------------------------------------------------
# CPack
# --------------------------------------------------

set(CPACK_PACKAGE_NAME                "DOPYGAME")
set(CPACK_PACKAGE_VENDOR              "DoPySOFT")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "The DoPy Game")
set(CPACK_PACKAGE_VERSION_MAJOR       "0")
set(CPACK_PACKAGE_VERSION_MINOR       "1")
set(CPACK_PACKAGE_VERSION_PATCH       "0")
set(CPACK_PACKAGE_VERSION
    "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY   "DOPYGAME")
set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_STRIP_FILES                 TRUE)
set(CPACK_RESOURCE_FILE_LICENSE       "${CMAKE_CURRENT_SOURCE_DIR}/License.txt")

if(WIN32)
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_NSIS_DISPLAY_NAME                     "DOPYGAME")
    set(CPACK_NSIS_PACKAGE_NAME                     "DOPYGAME")
    set(CPACK_NSIS_INSTALL_ROOT                     "$PROGRAMFILES64")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL  ON)
    set(CPACK_NSIS_MODIFY_PATH                      OFF)
    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\DOPYGAME.lnk' '$INSTDIR\\\\DOPYGAME.exe'")
    set(CPACK_NSIS_DELETE_ICONS_EXTRA
        "Delete '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\DOPYGAME.lnk'")
elseif(APPLE)
    set(CPACK_GENERATOR       "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "DOPYGAME")
    set(CPACK_DMG_FORMAT      "UDZO")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)

message(STATUS "Build type       : ${CMAKE_BUILD_TYPE}")
message(STATUS "LTO enabled      : ${DOPYGAME_ENABLE_LTO}")
message(STATUS "Tracy enabled    : ${DOPYGAME_ENABLE_TRACY}")
message(STATUS "Compiler         : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "CPack generators : ${CPACK_GENERATOR}")
