#!/usr/bin/env bash

set -euo pipefail

# change to parent directory
cd "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)/.."
echo "PWD:" `pwd`

MAKE_CMD="$(type -p make)"
exec watchman-make -p '**/*.c' '**/*.h' --make "${MAKE_CMD} -j8" --target main
