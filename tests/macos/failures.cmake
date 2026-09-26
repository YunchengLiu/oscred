set(command "${bash}" "${CMAKE_CURRENT_LIST_DIR}/run.sh" "${root}" home "${executable}")
file(MAKE_DIRECTORY "${root}/missing-home/Library/Preferences")
execute_process(
    COMMAND "${bash}" "${CMAKE_CURRENT_LIST_DIR}/run.sh" "${root}" missing-home "${executable}" --expect-missing
    RESULT_VARIABLE status
    TIMEOUT 60
)
if(NOT status STREQUAL "0")
    message(FATAL_ERROR "Missing-Keychain behavior failed: ${status}")
endif()
execute_process(
    COMMAND "${bash}" "${CMAKE_CURRENT_LIST_DIR}/cleanup.sh" --recover "${root}" "${helper}"
    RESULT_VARIABLE status
    TIMEOUT 60
)
if(NOT status STREQUAL "1")
    message(FATAL_ERROR "Recovery accepted an active CTest session: ${status}")
endif()
execute_process(
    COMMAND "${bash}" -c [=[source "$1"; load_session "$2"; remove_session /usr/bin/false]=] _
            "${CMAKE_CURRENT_LIST_DIR}/session.sh" "${root}"
    RESULT_VARIABLE status
    TIMEOUT 60
)
if(NOT status STREQUAL "1" OR NOT EXISTS "${root}/session.txt")
    message(FATAL_ERROR "Failed cleanup did not retain the session: ${status}")
endif()
# Each child writes a record first; verification independently deletes it after the child has exited
foreach(mode IN ITEMS fail crash hold)
    set(timeout 60)
    if(mode STREQUAL "hold")
        set(timeout 2)
    endif()
    execute_process(
        COMMAND ${command} --failure-child "${mode}"
        RESULT_VARIABLE status
        TIMEOUT "${timeout}"
    )
    if((mode STREQUAL "fail" AND NOT status STREQUAL "23")
       OR (mode STREQUAL "crash" AND NOT status STREQUAL "Subprocess killed")
       OR (mode STREQUAL "hold" AND NOT status STREQUAL "Process terminated due to timeout")
    )
        message(FATAL_ERROR "Unexpected ${mode} child status: ${status}")
    endif()
    execute_process(
        COMMAND ${command} --verify-failure "${mode}"
        RESULT_VARIABLE status
        TIMEOUT 60
    )
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "Cannot independently verify and clean ${mode} child record: ${status}")
    endif()
endforeach()
