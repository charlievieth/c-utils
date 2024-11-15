#!/usr/bin/env bash

set -euo pipefail

# DIR is the project root directory
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)/.."
PYISORT="${DIR}/testdata/pyisort.py"

# Source LS_COLORS since the tests rely on them:
# shellcheck source=../testdata/ls_colors
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
EXIT_CODE=0
TESTNAME=''

trap 'echo "${RED}# test:${RESET} ${YELLOW}${TESTNAME}${RESET} failed"' ERR

function _test() {
    TESTNAME="$1"
    echo "${GREEN}# test:${RESET}" "$1"
}

function _error() {
    echo "${YELLOW}error:${RESET}" "$@"
    ((EXIT_CODE++))
}

function _fatal() {
    echo "${YELLOW}error:${RESET}" "$@"
    return 1
}

[[ ! -x "${PYISORT}" ]] && {
    _fatal "missing pyisort.py: ${PYISORT}"
}

if ! command -v fd >/dev/null; then
    _fatal 'missing required command: fd'
fi

_test 'build'
make address

if command -v realpath >/dev/null; then
    CFD="$(realpath "${DIR}/cfd")"
else
    CFD="${DIR}/cfd"
fi
[[ ! -x "${CFD}" ]] && {
    _fatal "missing cfd executable: ${CFD}"
}

# shellcheck disable=SC2016
_test '`fd -uu`'
fd -uu | "${CFD}" >/dev/null

# shellcheck disable=SC2016
_test '`fd --absolute-path`'
fd --absolute-path | "${CFD}" >/dev/null

# shellcheck disable=SC2016
_test '`fd -uu --color always`'
fd -uu --color always | "${CFD}" >/dev/null

# shellcheck disable=SC2016
_test '`fd --color always --absolute-path`'
fd --color always --absolute-path | "${CFD}" >/dev/null

# shellcheck disable=SC2016
_test '`ls -la`'
# shellcheck disable=SC2012
ls -la | "${CFD}" >/dev/null

# testdata/fd_sort

FD_TEST="$(cd "${DIR}/testdata/fd_test" && fd --color always |
    LC_ALL=C sort --random-sort)"

_test 'sort'
diff "${DIR}/testdata/fd_test/want_sort.out" <(echo "${FD_TEST}" |
    "${CFD}" --sort) || _error "failed: ${TESTNAME}"

_test 'isort'
diff "${DIR}/testdata/fd_test/want_isort.out" <(echo "${FD_TEST}" |
    "${CFD}" --isort) || _error "failed: ${TESTNAME}"

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
    if ! diff "${DIFF_FLAGS[@]}" \
        <(sed -E 's/\x1b\[.*\.\/\x1b\[0m|\.\///g' "${ALL_GO_COLOR}" | sort) \
        <("${CFD}" <"${ALL_GO_COLOR}" | sort); then
        _error 'FAIl'
    fi

    _test 'GOROOT: sort (comp)'
    diff "${DIFF_FLAGS[@]}" \
        <(sed 's/^\.\///g' "${ALL_GO}" | "${PYISORT}") \
        <(${CFD} --sort <"${ALL_GO}")

    _test 'GOROOT: isort (comp)'
    diff "${DIFF_FLAGS[@]}" \
        <(sed 's/^\.\///g' "${ALL_GO}" | "${PYISORT}" --ignore-case) \
        <("${CFD}" --isort <"${ALL_GO}")
else
    echo "${YELLOW}skip:${RESET} skipping GOROOT test: go not installed"
fi

if ((EXIT_CODE == 0)); then
    _test 'PASS'
else
    _error "FAIL: ${EXIT_CODE} tests failed"
fi
exit $EXIT_CODE
