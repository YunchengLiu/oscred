cmake_minimum_required(VERSION 3.28)

project(
    osvault
    VERSION 0.1.0
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(osvault)
add_library(osvault::osvault ALIAS osvault)

file(GLOB osvault_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/osvault/*.cpp")

target_sources(
    osvault
    PRIVATE ${osvault_sources}
    PUBLIC FILE_SET HEADERS BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}" FILES osvault/osvault.h
)

unset(osvault_sources)

target_compile_features(osvault PUBLIC cxx_std_23)
set_target_properties(osvault PROPERTIES CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(osvault PRIVATE /utf-8 /W4)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
    target_compile_options(osvault PRIVATE -Wall -Wextra -Wpedantic)
endif()

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
