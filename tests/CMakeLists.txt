find_package(Catch2 3.16 CONFIG REQUIRED)

add_executable(osvault-tests)

file(GLOB sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")

target_sources(osvault-tests PRIVATE ${sources})
unset(sources)

target_link_libraries(osvault-tests PRIVATE osvault::osvault Catch2::Catch2)
osvault_setup_target(osvault-tests)

include(Catch)
catch_discover_tests(osvault-tests TEST_SPEC "~[native]")

if(WIN32)
    include(windows/backend-win.cmake)
endif()
