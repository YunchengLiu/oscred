file(GLOB sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.cpp")
target_sources(osvault-tests PRIVATE ${sources})
unset(sources)

# Keep direct helper coverage without exporting private shared-library symbols
if(BUILD_SHARED_LIBS)
    target_sources(osvault-tests PRIVATE "${PROJECT_SOURCE_DIR}/osvault/windows/hex_codec.cpp")
    # Place the test executable beside the DLL for discovery and execution
    set_target_properties(osvault-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:osvault>")
endif()

target_link_libraries(osvault-tests PRIVATE Advapi32)

# Serialize access to the user's credential store and always schedule final cleanup
set(native_properties LABELS native RESOURCE_LOCK windows_credentials)
catch_discover_tests(
    osvault-tests
    TEST_SPEC "[native]"
    PROPERTIES ${native_properties} FIXTURES_REQUIRED credentials
)

find_program(powershell NAMES pwsh powershell REQUIRED)
add_test(NAME windows.cleanup COMMAND "${powershell}" -NoLogo -NoProfile -NonInteractive -File
                                      "${CMAKE_CURRENT_LIST_DIR}/cleanup.ps1"
)
set_tests_properties(windows.cleanup PROPERTIES FIXTURES_CLEANUP credentials)
set_tests_properties(vault.persistence.write vault.persistence.read windows.cleanup PROPERTIES ${native_properties})
unset(native_properties)
