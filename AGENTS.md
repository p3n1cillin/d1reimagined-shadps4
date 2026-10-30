<!--
SPDX-FileCopyrightText: 2026 D1 Reimagined contributors
SPDX-License-Identifier: GPL-2.0-or-later
-->

# AGENTS.md

This is the D1 Reimagined fork of shadPS4: upstream v0.19.0 plus a short series of commits on `d1` that
Destiny (PS4, build 56578) needs. Read [d1/README.md](d1/README.md) first. The rules agents break:

- **One fix per commit and per pull request**, based on `d1`, with a message that says what was wrong in
  the game and what changed. The series must stay easy to rebase onto a newer upstream.
- **Mark fork-only code** with a `D1` comment. Put behaviour that could change other games behind a
  `D1_*` environment variable, and add it to the table in `d1/README.md`.
- **Fix the general case.** Don't special-case one title ID, address or shader hash unless the commit
  says why there is no general fix.
- **No game material.** Never add game files, dumps, system modules, firmware, keys or captures.
- **Don't run the emulator unasked.** It boots a real game from the person's own dump.
- **Upstream rules for upstream PRs.** A fix sent to shadps4-emu/shadPS4 follows its
  [CONTRIBUTING.md](CONTRIBUTING.md), including its A.I. rules: disclose AI use, and the description and
  comments are written by a person.
- Style is upstream's: `clang-format` with the repository's `.clang-format`, 100-column lines, and
  upstream's naming rules.

Disclose AI use in the pull request (the template's `AI-assisted` line).
