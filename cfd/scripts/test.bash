#!/usr/bin/env bash

set -euo pipefail

# DIR is the project root directory
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)/.."
PYISORT="${DIR}/testdata/pyisort.py"

# Source LS_COLORS since the tests rely on them:
. "${DIR}/testdata/ls_colors"

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

trap 'echo -e "${RED}# test:${RESET} ${YELLOW}${TESTNAME}${RESET} failed"' ERR

function _test() {
    TESTNAME="$1"
    echo -e "${GREEN}# test:${RESET}" "$1"
}

function _error() {
    echo -e "${YELLOW}error:${RESET}" "$@"
    return 1
}

[[ ! -x "${PYISORT}" ]] && {
    _error "missing pyisort.py: ${PYISORT}"
}

_test 'build'
make address

CFD="${DIR}/cfd"
[[ ! -x "${CFD}" ]] && {
    _error "missing cfd executable: ${CFD}"
}

_test '`fd -uu`'
fd -uu | "${CFD}" >/dev/null

_test '`fd --absolute-path`'
fd --absolute-path | "${CFD}" >/dev/null

_test '`fd -uu --color always`'
fd -uu --color always | "${CFD}" >/dev/null

_test '`fd --color always --absolute-path`'
fd --color always --absolute-path | "${CFD}" >/dev/null

_test '`ls -la`'
ls -la | "${CFD}" >/dev/null


# testdata/fd_sort

FD_TEST="$(cd ./testdata/fd_test && fd --color always | LC_ALL=C sort --random-sort)"

_test 'sort'
diff ./testdata/fd_test/want_sort.out <(echo "${FD_TEST}" | "${CFD}" --sort)

_test 'isort'
diff ./testdata/fd_test/want_isort.out <(echo "${FD_TEST}" | "${CFD}" --isort)

# GOROOT

if command -v go >/dev/null && [[ -d "$(go env GOROOT)" ]]; then
    ALL_GO="$(mktemp)"
    ALL_GO_COLOR="$(mktemp)"
    DIFF_FLAGS=(
        --suppress-common-lines
        --side-by-side
    )
    [ -t 1 ] && {
        DIFF_FLAGS+=(--color=always)
    }
    (
        cd "$(go env GOROOT)"
        fd -uu --color always | sort --random-sort --buffer-size=64M >"${ALL_GO_COLOR}"
        fd -uu | sort --random-sort --buffer-size=64M >"${ALL_GO}"
    )
    trap 'rm "${ALL_GO}" "${ALL_GO_COLOR}"' EXIT

    _test 'GOROOT: no-sort (comp)'
    diff "${DIFF_FLAGS[@]}" \
        <(sed -E 's/\x1b\[.*\.\/\x1b\[0m|\.\///g' "${ALL_GO_COLOR}" | sort) \
        <(cat "${ALL_GO_COLOR}" | "${CFD}" | sort)

    _test 'GOROOT: sort (comp)'
    diff "${DIFF_FLAGS[@]}" \
        <(sed 's/^\.\///g' "${ALL_GO}" | "${PYISORT}") \
        <(cat "${ALL_GO}" | ${CFD} --sort)

    _test 'GOROOT: isort (comp)'
    diff "${DIFF_FLAGS[@]}" \
        <(sed 's/^\.\///g' "${ALL_GO}" | "${PYISORT}" --ignore-case) \
        <(cat "${ALL_GO}" | "${CFD}" --isort)
fi

_test 'passed'
