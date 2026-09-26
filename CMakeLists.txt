cmake_minimum_required(VERSION 3.28)

project(
    osvault
    VERSION 1.0.0
    LANGUAGES CXX
)

option(OSVAULT_BUILD_TESTING "Build osvault tests." ${PROJECT_IS_TOP_LEVEL})

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

# Keep Debug installations distinct; the major version identifies the shared-library ABI
set_target_properties(
    osvault
    PROPERTIES CXX_VISIBILITY_PRESET hidden
               VISIBILITY_INLINES_HIDDEN YES
               DEBUG_POSTFIX d
               VERSION ${PROJECT_VERSION}
               SOVERSION ${PROJECT_VERSION_MAJOR}
)
include(GenerateExportHeader)
generate_export_header(
    osvault EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/include/osvault/export.h" STATIC_DEFINE OSVAULT_STATIC
)
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(osvault PUBLIC OSVAULT_STATIC)
endif()

if(WIN32)
    set(backend windows)
elseif(APPLE)
    set(backend macos)
    target_link_libraries(osvault PRIVATE "$<LINK_LIBRARY:FRAMEWORK,Security,CoreFoundation>")
elseif(LINUX)
    set(backend linux)
    find_package(PkgConfig REQUIRED)
    # The object APIs are stable since libsecret 0.18; G_IO_ERROR_MESSAGE_TOO_LARGE requires GIO 2.48
    pkg_check_modules(libsecret REQUIRED IMPORTED_TARGET libsecret-1>=0.18 gio-2.0>=2.48)
    target_link_libraries(osvault PRIVATE PkgConfig::libsecret)
    if(BUILD_SHARED_LIBS)
        # Keep symbols from static dependencies out of the shared library's exported interface
        target_link_options(osvault PRIVATE "LINKER:--exclude-libs,ALL")
    endif()
else()
    message(
        FATAL_ERROR
            "osvault has no native backend for ${CMAKE_SYSTEM_NAME}; available targets are Windows, macOS and Linux"
    )
endif()

file(GLOB sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/osvault/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/osvault/${backend}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/osvault/${backend}/*.h"
)
unset(backend)

target_sources(
    osvault
    PRIVATE ${sources} osvault/backend.h
    PUBLIC FILE_SET HEADERS BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}" FILES osvault/osvault.h
    PUBLIC FILE_SET
           generated
           TYPE
           HEADERS
           BASE_DIRS
           "${CMAKE_CURRENT_BINARY_DIR}/include"
           FILES
           "${CMAKE_CURRENT_BINARY_DIR}/include/osvault/export.h"
)

unset(sources)

target_compile_features(osvault PUBLIC cxx_std_23)
target_link_libraries(osvault PRIVATE "$<$<PLATFORM_ID:Windows>:Advapi32>")
osvault_setup_target(osvault)

if(OSVAULT_BUILD_TESTING)
    enable_testing()
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
    FILE_SET generated
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

install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DOCDIR}")

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
