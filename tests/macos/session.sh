#!/usr/bin/env bash
# Private Keychain lifecycle shared by the CTest fixtures
set -euo pipefail
umask 077

fail() { printf '%s\n' "$*" >&2; exit 1; }

process_start() {
    local state started
    read -r state started < <(LC_ALL=C /bin/ps -p "$1" -o stat= -o lstart=) || return 1
    # Zombies cannot access the session
    [[ $state != Z* ]] || return 1
    printf '%s\n' "$started"
}

session_path() {
    root=${1:?Pass the exact session directory}
    local parent base
    parent=$(dirname "$root")
    base=$(dirname "$parent")
    [[ $root == /* && $root != *$'\n'* && $(basename "$parent") == native-tests &&
       $(basename "$root") =~ ^session-[0-9a-f]{12}$ && ! -L $root && ! -L $parent &&
       $(cd "$base" && pwd -P) == "$base" ]] || fail 'Expected the exact CMake session path without symlinks'
}

load_session() {
    session_path "$1"
    process_start "$$" >/dev/null || fail 'Cannot inspect session processes'
    [[ -d $root && -O $root && -f $root/session.txt && ! -L $root/session.txt ]] ||
        fail 'The session directory or marker is missing or belongs to another user'
    local recorded_root uid
    {
        IFS= read -r recorded_root
        IFS= read -r uid
        IFS= read -r owner
        IFS= read -r start
    } < "$root/session.txt"
    [[ $recorded_root == "$root" && $uid == "$UID" && $owner =~ ^[0-9]+$ && -n $start ]] ||
        fail 'Session identity mismatch'
}

require_idle() {
    local pid started
    if [[ -f $root/worker ]]; then
        { IFS= read -r pid; IFS= read -r started; } < "$root/worker"
        [[ $pid =~ ^[0-9]+$ && -n $started ]] || fail 'Invalid worker marker'
        [[ $(process_start "$pid" || true) != "$started" ]] || fail 'A session worker is still alive'
    fi
}

remove_session() {
    # Native deletion must succeed before removing the recovery marker and private preferences
    bash "$(dirname "${BASH_SOURCE[0]}")/run.sh" "$root" home "$1" cleanup || return 1
    load_session "$root"
    require_idle
    rm -rf -- "$root"
}
