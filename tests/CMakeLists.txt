option(OSVAULT_USE_EXTERNAL_CATCH2 "Use an installed Catch2 package." OFF)

if(OSVAULT_USE_EXTERNAL_CATCH2)
    find_package(Catch2 3 CONFIG REQUIRED)
else()
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.16.0
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(Catch2)
endif()

add_executable(osvault-tests)

file(GLOB sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")

target_sources(osvault-tests PRIVATE ${sources})
unset(sources)

target_link_libraries(osvault-tests PRIVATE osvault::osvault Catch2::Catch2)
osvault_setup_target(osvault-tests)

include(Catch)
catch_discover_tests(osvault-tests TEST_SPEC "~[native]")

add_test(NAME vault.persistence.write COMMAND osvault-tests --write-test)
add_test(NAME vault.persistence.read COMMAND osvault-tests --read-test)
set_tests_properties(vault.persistence.write PROPERTIES FIXTURES_SETUP persistence FIXTURES_REQUIRED credentials)
set_tests_properties(vault.persistence.read PROPERTIES FIXTURES_REQUIRED "credentials;persistence")

if(WIN32)
    include(windows/backend-win.cmake)
elseif(LINUX)
    include(linux/backend-linux.cmake)
endif()
