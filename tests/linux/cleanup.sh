#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$(realpath "$0")")/session.sh"
recover=false
if [[ ${1:-} == --recover ]]; then recover=true; shift; fi
load_session "${1:?Pass the exact session directory}"
owner=$(jq -er '.pid' "$root/session.json")
start=$(jq -er '.start' "$root/session.json")
if $recover; then
    if [[ $(process_start "$owner" || true) == "$start" ]]; then
        fail 'The owning CTest is still alive; stop it before recovery'
    fi
else
    [[ $PPID == "$owner" && $(process_start "$PPID") == "$start" ]] ||
        fail 'Only the owning CTest may clean an active session; use --recover after it ends'
fi
trap 'exit 130' INT
trap 'exit 143' TERM
remove_session
