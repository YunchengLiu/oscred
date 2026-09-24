#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$(realpath "$0")")/session.sh"
load_session "${OSVAULT_TEST_ROOT:?Run tests through CTest}"
executable=${1:?Pass the test executable}
address=$OSVAULT_TEST_BUS
scope=
finish() {
    local status=$?
    trap - EXIT INT TERM
    if [[ -n $scope ]]; then stop_processes "OSVAULT_TEST_SCOPE=$scope" || status=1; fi
    exit "$status"
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
for kind in unavailable missing locked; do
    scope=$root/f-$kind
    mkdir -m 700 -- "$scope"
    use_environment "$scope" "$address-$kind"
    start_bus
    if [[ $kind == missing ]]; then
        start_keyring empty
        [[ $(call /org/freedesktop/secrets org.freedesktop.Secret.Service ReadAlias s default | jq -er '.data[0]') == / ]]
    elif [[ $kind == locked ]]; then
        start_keyring unlocked
        collection=$(call /org/freedesktop/secrets org.freedesktop.Secret.Service ReadAlias s default | jq -er '.data[0]')
        [[ $collection != / ]]
        call /org/freedesktop/secrets org.freedesktop.Secret.Service Lock ao 1 "$collection" |
            jq -e --arg path "$collection" '.data[1] == "/" and (.data[0] | index($path) != null)' >/dev/null
    fi
    "$executable" --expect-error "$kind"
    stop_processes "OSVAULT_TEST_SCOPE=$scope"
    scope=
done
