file(GLOB sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/test_*.cpp")
target_sources(osvault-tests PRIVATE ${sources})
unset(sources)
if(BUILD_SHARED_LIBS)
    target_sources(osvault-tests PRIVATE "${PROJECT_SOURCE_DIR}/osvault/macos/error.cpp")
endif()
target_link_libraries(osvault-tests PRIVATE "$<LINK_LIBRARY:FRAMEWORK,Security,CoreFoundation>")

# Native cleanup remains independent of the library under test
add_executable(osvault-test-keychain "${CMAKE_CURRENT_LIST_DIR}/keychain.cpp")
target_compile_features(osvault-test-keychain PRIVATE cxx_std_23)
target_link_libraries(osvault-test-keychain PRIVATE "$<LINK_LIBRARY:FRAMEWORK,Security,CoreFoundation>")
osvault_setup_target(osvault-test-keychain)
find_program(bash NAMES bash REQUIRED)

string(SHA256 session_id "${CMAKE_BINARY_DIR}")
string(SUBSTRING "${session_id}" 0 12 session_id)
set(session_root "${CMAKE_BINARY_DIR}/../native-tests/session-${session_id}")
cmake_path(NORMAL_PATH session_root)
set(native_command "${bash}" "${CMAKE_CURRENT_LIST_DIR}/run.sh" "${session_root}" home "$<TARGET_FILE:osvault-tests>")
add_test(NAME macos.setup COMMAND "${bash}" "${CMAKE_CURRENT_LIST_DIR}/setup.sh" "${session_root}"
                                  "$<TARGET_FILE:osvault-test-keychain>"
)
add_test(NAME macos.native COMMAND ${native_command} "--test-suite=native")
add_test(NAME vault.persistence.write COMMAND ${native_command} --write-test)
add_test(NAME vault.persistence.read COMMAND ${native_command} --read-test)
add_test(
    NAME macos.failures
    COMMAND "${CMAKE_COMMAND}" "-Dbash=${bash}" "-Droot=${session_root}" "-Dexecutable=$<TARGET_FILE:osvault-tests>"
            "-Dhelper=$<TARGET_FILE:osvault-test-keychain>" -P "${CMAKE_CURRENT_LIST_DIR}/failures.cmake"
)
add_test(NAME macos.cleanup COMMAND "${bash}" "${CMAKE_CURRENT_LIST_DIR}/cleanup.sh" "${session_root}"
                                    "$<TARGET_FILE:osvault-test-keychain>"
)
set_tests_properties(macos.setup PROPERTIES FIXTURES_SETUP credentials)
set_tests_properties(macos.cleanup PROPERTIES FIXTURES_CLEANUP credentials)
set_tests_properties(macos.native macos.failures PROPERTIES FIXTURES_REQUIRED credentials)
set_tests_properties(
    macos.setup macos.native vault.persistence.write vault.persistence.read macos.failures macos.cleanup
    PROPERTIES LABELS native RESOURCE_LOCK macos_keychain TIMEOUT 90
)
unset(native_command)
unset(session_root)
unset(session_id)
