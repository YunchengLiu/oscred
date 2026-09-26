#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/session.sh"
recover=false
if [[ ${1:-} == --recover ]]; then recover=true; shift; fi
session_path "${1:?Pass the exact session directory}"
[[ -e $root ]] || exit 0
load_session "$root"
if $recover; then
    [[ $(process_start "$owner" || true) != "$start" ]] || fail 'The owning CTest is still alive; stop it before recovery'
else
    [[ $PPID == "$owner" && $(process_start "$PPID") == "$start" ]] ||
        fail 'Only the owning CTest may clean an active session; use --recover after it ends'
fi
require_idle
remove_session "${2:?Pass the Keychain helper}"
