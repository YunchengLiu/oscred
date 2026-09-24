#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$(realpath "$0")")/session.sh"
for tool in busctl jq dbus-daemon dbus-monitor gnome-keyring-daemon pkill pgrep; do
    command -v "$tool" >/dev/null || fail "Linux native tests require $tool"
done
for tool in pkill pgrep; do
    [[ $("$tool" --help) == *--env* ]] || fail "Linux native tests require $tool with --env support"
done
session_path "${1:?Pass the exact session directory}"
address=${2:?Pass the private bus address}
mkdir -p -- "$(dirname "$root")"
mkdir -- "$root" || fail 'A previous session exists; recover it with cleanup.sh --recover before retrying'
jq -n --arg root "$root" --arg address "$address" --argjson uid "$UID" --argjson pid "$PPID" \
    --arg start "$(process_start "$PPID")" \
    '{format:"osvault-ctest-session-1",root:$root,address:$address,uid:$uid,pid:$pid,start:$start}' > "$root/session.json"
export OSVAULT_TEST_ROOT=$root
use_environment "$root" "$address"
# CTest schedules cleanup even if this setup fails; INT/TERM also attempt immediate rollback
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'status=$?; if ((status)); then remove_session || true; fi' EXIT
start_bus
start_keyring unlocked
printf 'Isolated CTest session: %s\n' "$root"
