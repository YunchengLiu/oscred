#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/session.sh"
load_session "${1:?Pass the exact session directory}"
require_idle
home_name=${2:?Pass home or missing-home}
[[ $home_name == home || $home_name == missing-home ]] || fail 'Invalid private HOME'
shift 2
export OSVAULT_TEST_ROOT=$root HOME=$root/$home_name TMPDIR=$root/tmp
unset CFFIXED_USER_HOME
ulimit -c 0
# exec preserves the recorded PID for cleanup's live-worker check
printf '%s\n' "$$" "$(process_start "$$")" > "$root/worker"
exec /usr/bin/sandbox-exec -f "$root/sandbox.sb" "$@"
