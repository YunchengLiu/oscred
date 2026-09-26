#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/session.sh"
session_path "${1:?Pass the exact session directory}"
helper=${2:?Pass the Keychain helper}
[[ $(basename "$(/bin/ps -p "$PPID" -o comm=)") == ctest ]] || fail 'Session setup must be owned by CTest'
mkdir -p -- "$(dirname "$root")"
mkdir -- "$root" || fail 'A previous session exists; recover it with cleanup.sh --recover before retrying'
printf '%s\n' "$root" "$UID" "$PPID" "$(process_start "$PPID")" > "$root/session.txt"
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'status=$?; if ((status)); then remove_session "$helper" || true; fi' EXIT
mkdir -p -- "$root/home/Library/Preferences" "$root/tmp"

quote() {
    local value=${1//\\/\\\\}
    value=${value//\"/\\\"}
    value=${value//$'\n'/\\n}
    printf '"%s"' "$value"
}
real_home=$(cd "$HOME" && pwd -P)
cat > "$root/sandbox.sb" <<EOF
(version 1)
(allow default)
(deny file-write*)
(allow file-write* (subpath $(quote "$root")) (literal "/dev/null"))
(deny file-read* (subpath $(quote "$real_home/Library/Keychains"))
 (literal $(quote "$real_home/Library/Preferences/com.apple.security.plist"))
 (literal $(quote "$real_home/Library/Preferences/com.apple.security.common.plist")))
EOF
bash "$(dirname "$0")/run.sh" "$root" home "$helper" setup
