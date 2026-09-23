file(GLOB sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.cpp")
target_sources(osvault-tests PRIVATE ${sources})
unset(sources)

target_link_libraries(osvault-tests PRIVATE Advapi32)

# Serialize access to the user's credential store and always schedule final cleanup
set(native_properties LABELS native RESOURCE_LOCK windows_credentials)
catch_discover_tests(
    osvault-tests
    TEST_SPEC "[native]"
    PROPERTIES ${native_properties} FIXTURES_REQUIRED credentials
)

add_test(NAME vault.persistence.write COMMAND osvault-tests --write-test)
add_test(NAME vault.persistence.read COMMAND osvault-tests --read-test)
set_tests_properties(vault.persistence.write PROPERTIES FIXTURES_SETUP persistence FIXTURES_REQUIRED credentials)
set_tests_properties(vault.persistence.read PROPERTIES FIXTURES_REQUIRED "credentials;persistence")

find_program(powershell NAMES pwsh powershell REQUIRED)
add_test(NAME windows.cleanup COMMAND "${powershell}" -NoLogo -NoProfile -NonInteractive -File
                                      "${CMAKE_CURRENT_LIST_DIR}/cleanup.ps1"
)
set_tests_properties(windows.cleanup PROPERTIES FIXTURES_CLEANUP credentials)
set_tests_properties(vault.persistence.write vault.persistence.read windows.cleanup PROPERTIES ${native_properties})
unset(native_properties)
