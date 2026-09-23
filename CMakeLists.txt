cmake_minimum_required(VERSION 3.28)

# Select test dependencies before the vcpkg toolchain processes the manifest
option(BUILD_TESTING "Build the testing tree." ON)
if(BUILD_TESTING)
    list(APPEND VCPKG_MANIFEST_FEATURES tests)
endif()

project(
    osvault
    VERSION 0.1.0
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

function(osvault_setup_target target)
    # Use the Windows 10 API baseline for supported Windows 10/11 desktop builds
    target_compile_definitions(
        ${target} PRIVATE "$<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN;NOMINMAX>"
                          "$<$<PLATFORM_ID:Windows>:WINVER=0x0A00;_WIN32_WINNT=0x0A00;NTDDI_VERSION=0x0A000000>"
    )
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options(${target} PRIVATE /utf-8 /W4)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(osvault)
add_library(osvault::osvault ALIAS osvault)

file(GLOB sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/osvault/*.cpp")

if(WIN32)
    file(GLOB win_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/osvault/windows/*.cpp"
         "${CMAKE_CURRENT_SOURCE_DIR}/osvault/windows/*.h"
    )
    list(APPEND sources ${win_sources})
    unset(win_sources)
endif()

target_sources(
    osvault
    PRIVATE ${sources}
    PUBLIC FILE_SET HEADERS BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}" FILES osvault/osvault.h
)

unset(sources)

target_compile_features(osvault PUBLIC cxx_std_23)
target_link_libraries(osvault PRIVATE "$<$<PLATFORM_ID:Windows>:Advapi32>")
osvault_setup_target(osvault)

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# install and package configuration

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/osvault-config.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/osvault-config.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/osvault"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/osvault-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    TARGETS osvault
    EXPORT osvault-targets
    FILE_SET HEADERS
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(
    EXPORT osvault-targets
    FILE osvault-targets.cmake
    NAMESPACE osvault::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/osvault"
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/osvault-config.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/osvault-config-version.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/osvault"
)

# Keep the IDE copy local to development builds that generate a compile database
if(PROJECT_IS_TOP_LEVEL
   AND CMAKE_EXPORT_COMPILE_COMMANDS
   AND CMAKE_GENERATOR MATCHES "Ninja|Make"
)
    add_custom_target(
        copy-compile-database ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/compile_commands.json"
                "${CMAKE_CURRENT_SOURCE_DIR}/build/compile_commands.json"
        DEPENDS ${CMAKE_BINARY_DIR}/compile_commands.json
        COMMENT "copying compile_commands.json to the build directory"
        VERBATIM
    )
endif()
