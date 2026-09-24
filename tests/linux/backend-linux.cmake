file(GLOB sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/test_*.cpp")
target_sources(osvault-tests PRIVATE ${sources})
unset(sources)

# Keep direct helper coverage without exporting private shared-library symbols
if(BUILD_SHARED_LIBS)
    target_sources(osvault-tests PRIVATE "${PROJECT_SOURCE_DIR}/osvault/linux/error.cpp")
endif()

target_link_libraries(osvault-tests PRIVATE PkgConfig::libsecret)
find_program(bash NAMES bash REQUIRED)

# CTest owns the private session, test ordering and independent final cleanup
string(SHA256 session_id "${CMAKE_BINARY_DIR}")
string(SUBSTRING "${session_id}" 0 12 session_id)
set(session_root "${CMAKE_BINARY_DIR}/../native-tests/session-${session_id}")
cmake_path(NORMAL_PATH session_root)
set(session_bus "unix:abstract=osvault-${session_id}")
set(native_environment
    "OSVAULT_TEST_ROOT=${session_root}"
    "OSVAULT_TEST_BUS=${session_bus}"
    "DBUS_SESSION_BUS_ADDRESS=${session_bus}"
    "HOME=${session_root}/home"
    "XDG_DATA_HOME=${session_root}/data"
    "XDG_CONFIG_HOME=${session_root}/config"
    "XDG_CACHE_HOME=${session_root}/cache"
    "XDG_RUNTIME_DIR=${session_root}/run"
    "DISPLAY="
    "WAYLAND_DISPLAY="
    "DBUS_STARTER_ADDRESS="
    "DBUS_STARTER_BUS_TYPE="
    "GNOME_KEYRING_CONTROL="
    "DBUS_SESSION_BUS_PID="
)
set(native_properties LABELS native RESOURCE_LOCK linux_secret_service TIMEOUT 30)
catch_discover_tests(
    osvault-tests
    TEST_SPEC "[native]" TEST_LIST osvault_linux_native_tests
    PROPERTIES ${native_properties} FIXTURES_REQUIRED credentials
)
# Apply list-valued properties after Catch has discovered the native test names
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/linux-environment.cmake" CONTENT
     [=[set_tests_properties(${osvault_linux_native_tests} PROPERTIES ENVIRONMENT [==[@native_environment@]==])
]=] @ONLY
)
set_property(
    DIRECTORY
    APPEND
    PROPERTY TEST_INCLUDE_FILES "${CMAKE_CURRENT_BINARY_DIR}/linux-environment.cmake"
)
add_test(NAME linux.setup COMMAND "${bash}" "${CMAKE_CURRENT_LIST_DIR}/setup.sh" "${session_root}" "${session_bus}")
set_tests_properties(linux.setup PROPERTIES FIXTURES_SETUP credentials)
add_test(NAME linux.failures COMMAND "${bash}" "${CMAKE_CURRENT_LIST_DIR}/failures.sh" "$<TARGET_FILE:osvault-tests>")
set_tests_properties(linux.failures PROPERTIES FIXTURES_REQUIRED credentials)
add_test(NAME linux.cleanup COMMAND "${bash}" "${CMAKE_CURRENT_LIST_DIR}/cleanup.sh" "${session_root}")
set_tests_properties(linux.cleanup PROPERTIES FIXTURES_CLEANUP credentials)
set_tests_properties(
    linux.setup vault.persistence.write vault.persistence.read linux.failures linux.cleanup
    PROPERTIES ${native_properties} ENVIRONMENT "${native_environment}"
)
unset(native_properties)
unset(native_environment)
unset(session_bus)
unset(session_root)
unset(session_id)
