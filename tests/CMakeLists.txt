option(OSVAULT_USE_EXTERNAL_DOCTEST "Use an installed doctest package." OFF)

if(OSVAULT_USE_EXTERNAL_DOCTEST)
    find_package(doctest CONFIG REQUIRED)
else()
    include(FetchContent)
    FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.5.3
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(doctest)
    list(APPEND CMAKE_MODULE_PATH "${doctest_SOURCE_DIR}/scripts/cmake")
endif()

add_executable(osvault-tests)

file(GLOB sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")

target_sources(osvault-tests PRIVATE ${sources} error.h)
unset(sources)

target_link_libraries(osvault-tests PRIVATE osvault::osvault doctest::doctest)
target_compile_definitions(osvault-tests PRIVATE DOCTEST_CONFIG_VOID_CAST_EXPRESSIONS)
osvault_setup_target(osvault-tests)

include(doctest)
# TEST_SPEC starts with the test-name filter, followed by the suite filter
doctest_discover_tests(
    osvault-tests
    TEST_SPEC "*" "--test-suite-exclude=native"
    ADD_LABELS 1
)

if(NOT APPLE)
    add_test(NAME vault.persistence.write COMMAND osvault-tests --write-test)
    add_test(NAME vault.persistence.read COMMAND osvault-tests --read-test)
endif()

if(WIN32)
    include(windows/backend-win.cmake)
elseif(LINUX)
    include(linux/backend-linux.cmake)
elseif(APPLE)
    include(macos/backend-macos.cmake)
endif()

set_tests_properties(vault.persistence.write PROPERTIES FIXTURES_SETUP persistence FIXTURES_REQUIRED credentials)
set_tests_properties(vault.persistence.read PROPERTIES FIXTURES_REQUIRED "credentials;persistence")
