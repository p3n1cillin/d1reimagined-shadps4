#!/bin/bash -ex

# SPDX-FileCopyrightText: 2023 Citra Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

# D1: check only the lines the d1 series (or this pull request) changes. Upstream v0.19.0 itself has
# trailing whitespace and unformatted lines in files we don't touch, so whole-tree checks fail on
# every push. Bump D1_UPSTREAM when integrating a newer upstream baseline.
D1_UPSTREAM=50c5c1b4e86e275b2e9df9c33606a934b037684c
if [ "$GITHUB_EVENT_NAME" = "pull_request" ]; then
    base="${COMMIT_RANGE%%..*}"
else
    base="$D1_UPSTREAM"
fi

if git diff -U0 "$base" HEAD -- src '*.yml' '*.md' '*.txt' CMakeLists.txt | grep -n '^+[^+].*\s$'; then
    echo Trailing whitespace found in changed lines, aborting
    exit 1
fi

# Default clang-format points to default 3.5 version one
CLANG_FORMAT=clang-format-23
$CLANG_FORMAT --version

out="$(git-clang-format-23 --binary "$CLANG_FORMAT" --diff --extensions cpp,h "$base" -- src || true)"
case "$out" in
    "" | *"no modified files to format"* | *"did not modify any files"*) ;;
    *)
        echo "!!! changed lines not compliant to coding style, here is the fix:"
        echo "$out"
        exit 1
        ;;
esac
