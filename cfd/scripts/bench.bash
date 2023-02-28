#!/usr/bin/env bash

set -euo pipefail

# DIR is the project root directory
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)/.."
cd "$DIR"

if [ -t 1 ]; then
    RED=$'\E[00;31m'
    GREEN=$'\E[00;32m'
    YELLOW=$'\E[00;33m'
    RESET=$'\E[0m'
else
    RED=''
    GREEN=''
    YELLOW=''
    RESET=''
fi
TESTNAME=''

function _bench() {
    TESTNAME="$1"
    echo -e "${GREEN}# bench:${RESET}" "$1"
}

trap 'echo -e "${RED}# test:${RESET} ${YELLOW}${TESTNAME}${RESET} failed"' ERR

make clean
make release

if command -v realpath >/dev/null; then
    CFD="$(realpath "${DIR}/cfd")"
else
    CFD="${DIR}/cfd"
fi
[[ ! -x "${CFD}" ]] && {
    _error "missing cfd executable: ${CFD}"
}

TMP="$(mktemp -d)"
trap 'rm -r "${TMP}"' EXIT

LINUX_COLOR="${TMP}/linux_color.txt"
LINUX_NO_COLOR="${TMP}/linux_no_color.txt"
unzstd -q -o "${LINUX_COLOR}" ./testdata/bench/linux_color.txt.zst
unzstd -q -o "${LINUX_NO_COLOR}" ./testdata/bench/linux_no_color.txt.zst

_bench 'no-sort (no-color)'
hyperfine --warmup 2 "'${CFD}' --bench 5 --benchfile '${LINUX_NO_COLOR}'"

_bench 'sort (no-color)'
hyperfine --warmup 2 "'${CFD}' --bench 5 --benchfile '${LINUX_NO_COLOR}' --sort"

_bench 'isort (no-color)'
hyperfine --warmup 2 "'${CFD}' --bench 5 --benchfile '${LINUX_NO_COLOR}' --isort"

_bench 'no-sort (color)'
hyperfine --warmup 2 "'${CFD}' --bench 5 --benchfile '${LINUX_COLOR}'"

_bench 'sort (color)'
hyperfine --warmup 2 "'${CFD}' --bench 5 --benchfile '${LINUX_COLOR}' --sort"

_bench 'isort (color)'
hyperfine --warmup 2 "'${CFD}' --bench 5 --benchfile '${LINUX_COLOR}' --isort"
