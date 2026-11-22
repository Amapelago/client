#!/usr/bin/env bash
# Copyright (c) 2026 Axertin
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

#
# Format all tracked C/C++ sources with clang-format. With no arguments it formats in place; arguments go straight to clang-format
# (for example: bash format.sh --dry-run --Werror). git ls-files leaves out build/ (untracked).
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

if ! command -v clang-format >/dev/null 2>&1; then
    echo "error: clang-format not found on PATH." >&2
    echo "  Linux:   sudo apt install clang-format   (or: pipx install clang-format==21.1.8)" >&2
    echo "  macOS:   brew install clang-format" >&2
    echo "  Windows: winget install LLVM.LLVM" >&2
    exit 1
fi

[ "$#" -gt 0 ] || set -- -i
git ls-files -z '*.cpp' '*.hpp' '*.h' | xargs -0 clang-format "$@"
