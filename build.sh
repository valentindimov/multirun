#!/bin/bash
set -eo pipefail
cd "$(dirname "$0")"
mkdir -p build
musl-gcc -fanalyzer -O2 -Wall -Wextra -Werror -pedantic-errors -s -static multirun.c -o build/multirun
