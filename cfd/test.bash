#!/usr/bin/env bash

set -euo pipefail

make address

echo '# test: `fd -uu`'
fd -uu | ./cfd >/dev/null

echo '# test: `fd --absolute-path`'
fd --absolute-path | ./cfd >/dev/null

echo '# test: `fd -uu --color always`'
fd -uu --color always | ./cfd >/dev/null

echo '# test: `fd --color always --absolute-path`'
fd --color always --absolute-path | ./cfd >/dev/null

echo '# test: `ls -la`'
ls -la | ./cfd >/dev/null

echo '# test: LONG'
LONG="$(echo {a..z}{a..z}{a..z})"
printf '%s\n\%s\n%s\n\%s\n' "${LONG}" "${LONG}" "${LONG}" "${LONG}" | ./cfd >/dev/null
