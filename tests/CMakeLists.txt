include(FetchContent)

# Match Catch2's feature detection to the C++23 test consumers
set(CMAKE_CXX_STANDARD 23)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.0
    GIT_SHALLOW TRUE
    SYSTEM
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(Catch2)

add_executable(osvault-tests)

file(GLOB sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

if(WIN32)
    file(GLOB win_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/windows/*.cpp")
    list(APPEND sources ${win_sources})
    unset(win_sources)
endif()

target_sources(osvault-tests PRIVATE ${sources})
unset(sources)

target_link_libraries(osvault-tests PRIVATE osvault::osvault Catch2::Catch2WithMain)
osvault_setup_target(osvault-tests)

include("${catch2_SOURCE_DIR}/extras/Catch.cmake")
catch_discover_tests(osvault-tests)
