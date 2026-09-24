#!/usr/bin/env bash
# Private service lifecycle shared by the CTest fixtures
set -euo pipefail
umask 077

fail() { printf '%s\n' "$*" >&2; exit 1; }

process_start() {
    local stat
    local -a fields
    [[ -r /proc/$1/stat ]] || return 1
    stat=$(cat "/proc/$1/stat") || return 1
    # comm can contain spaces; starttime is field 22, index 19 after removing pid and comm
    read -ra fields <<< "${stat##*) }"
    printf '%s\n' "${fields[19]}"
}

session_path() {
    root=$(realpath -m -- "${1:?Pass the exact session directory}")
    [[ $root == "$1" && ! -L $root && $(basename "$(dirname "$root")") == native-tests &&
       $(basename "$root") == session-* ]] || fail 'Refusing a path outside native-tests/session-*'
    # procps --env treats commas as separators between selectors
    [[ $root != *,* ]] || fail 'The session path cannot contain a comma'
}

load_session() {
    session_path "$1"
    [[ -d $root && -O $root ]] || fail 'The session directory is missing or belongs to another user'
    jq -e --arg root "$root" --argjson uid "$UID" \
        '.format == "osvault-ctest-session-1" and .root == $root and .uid == $uid' \
        "$root/session.json" >/dev/null || fail 'Invalid session marker'
    export OSVAULT_TEST_ROOT=$root
    export OSVAULT_TEST_BUS
    OSVAULT_TEST_BUS=$(jq -er '.address' "$root/session.json") || fail 'Missing private bus address'
    [[ $OSVAULT_TEST_BUS == unix:abstract=osvault-* ]] || fail 'Invalid private bus address'
    use_environment "$root" "$OSVAULT_TEST_BUS"
}

use_environment() {
    export HOME=$1/home XDG_DATA_HOME=$1/data XDG_CONFIG_HOME=$1/config
    export XDG_CACHE_HOME=$1/cache XDG_RUNTIME_DIR=$1/run
    export OSVAULT_TEST_SCOPE=$1 OSVAULT_TEST_BUS=$2 DBUS_SESSION_BUS_ADDRESS=$2
    unset DISPLAY WAYLAND_DISPLAY DBUS_SESSION_BUS_PID DBUS_STARTER_ADDRESS DBUS_STARTER_BUS_TYPE GNOME_KEYRING_CONTROL
}

call() {
    busctl --address="$OSVAULT_TEST_BUS" --timeout=5 --json=short call org.freedesktop.secrets "$@"
}

start_bus() {
    local scope=$OSVAULT_TEST_SCOPE i
    mkdir -p -- "$HOME" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR"
    # No activation paths, and no filesystem socket outside the selected output area
    cat > "$scope/bus.conf" <<EOF
<busconfig>
  <type>session</type><listen>$OSVAULT_TEST_BUS</listen><auth>EXTERNAL</auth>
  <policy context="default"><allow send_destination="*"/><allow eavesdrop="true"/><allow own="*"/></policy>
</busconfig>
EOF
    dbus-daemon --fork --config-file="$scope/bus.conf" >"$scope/bus.log" 2>&1
    dbus-monitor --session "type='method_call',interface='org.freedesktop.Secret.Prompt'" \
        >"$scope/prompts.log" 2>&1 </dev/null &
    local monitor=$!
    for ((i=0; i<100; ++i)); do
        kill -0 "$monitor" 2>/dev/null || fail 'Prompt monitor exited during startup'
        if grep -q 'member=NameLost' "$scope/prompts.log"; then return; fi
        sleep 0.05
    done
    fail 'Prompt monitor startup timed out'
}

start_keyring() {
    local mode=$1 daemon i
    local -a command=(gnome-keyring-daemon --foreground --components=secrets
                      "--control-directory=$XDG_RUNTIME_DIR")
    if [[ $mode == unlocked ]]; then
        printf 'osvault-isolated-test\n' | "${command[@]}" --unlock >"$OSVAULT_TEST_SCOPE/daemon.log" 2>&1 &
    else
        "${command[@]}" >"$OSVAULT_TEST_SCOPE/daemon.log" 2>&1 </dev/null &
    fi
    daemon=$!
    for ((i=0; i<100; ++i)); do
        kill -0 "$daemon" 2>/dev/null || fail 'Private keyring exited during startup'
        if busctl --address="$OSVAULT_TEST_BUS" --timeout=1 --json=short call org.freedesktop.DBus \
            /org/freedesktop/DBus org.freedesktop.DBus NameHasOwner s org.freedesktop.secrets |
            jq -e '.data[0] == true' >/dev/null; then return; fi
        sleep 0.05
    done
    fail 'Private keyring startup timed out'
}

stop_processes() {
    local signal status i
    # Match this user and exact inherited session/scope markers, never a process name or saved PID
    # procps uses pidfd_send_signal where available; re-query live matches before each escalation
    local marker=${1:?Pass the exact process marker}
    local -a selector=(-A -u "$UID" --env "$marker")
    for signal in TERM KILL; do
        status=0
        pkill --signal "$signal" "${selector[@]}" || status=$?
        ((status <= 1)) || return "$status"
        for ((i=0; i<50; ++i)); do
            status=0
            pgrep "${selector[@]}" >/dev/null || status=$?
            if ((status == 1)); then return; fi
            ((status == 0)) || return "$status"
            sleep 0.1
        done
    done
    fail 'Isolated processes remain; session data has been retained'
}

remove_session() {
    local log failed=0
    stop_processes "OSVAULT_TEST_ROOT=$root" || return 1
    # Retain only the latest service diagnostics beside the disposable session
    : > "$root.log" || return 1
    for log in "$root"/*.log "$root"/f-*/*.log; do
        [[ -f $log ]] || continue
        if [[ $log == *prompts.log ]] && grep -q 'member=Prompt' "$log"; then
            printf 'An interactive Prompt call was observed: %s\n' "$log" >&2
            failed=1
        fi
        printf '\n%s\n' "${log#"$root"/}" >> "$root.log" || return 1
        cat -- "$log" >> "$root.log" || return 1
    done
    # Revalidate the exact marked directory immediately before recursive removal
    load_session "$root"
    rm -rf -- "$root" || return 1
    printf 'Isolated processes stopped and temporary keyring removed; log: %s.log\n' "$root"
    return "$failed"
}
