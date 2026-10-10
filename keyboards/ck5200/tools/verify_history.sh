#!/usr/bin/env bash
# Verify every commit on main builds standalone.
# Usage: bash tools/verify_history.sh [base_ref]   (default: c1d9e7c)
set -uo pipefail

MAIN_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BASE="${1:-c1d9e7c}"
WT_ROOT="$MAIN_ROOT/.history_check"
WT="$WT_ROOT/tree"

git -C "$MAIN_ROOT" worktree remove "$WT" >/dev/null 2>&1 || true
git -C "$MAIN_ROOT" worktree prune
find "$WT_ROOT" -mindepth 1 -maxdepth 1 -exec rm -rf {} + 2>/dev/null || true
mkdir -p "$WT_ROOT"
git -C "$MAIN_ROOT" worktree add --detach "$WT" HEAD >/dev/null

# Gitignored dependencies are shared from the main checkout. The
# RISC-V toolchain is exported on PATH directly because build.sh's
# `find` does not descend through symlinked directories.
for dep in keyboards/ck5200/external keyboards/ck5200/.venv keyboards/ck5200/.stock; do
    if [[ -e "$MAIN_ROOT/$dep" && ! -e "$WT/$dep" ]]; then
        mkdir -p "$(dirname "$WT/$dep")"
        ln -s "$MAIN_ROOT/$dep" "$WT/$dep"
    fi
done
GCC="$(find "$MAIN_ROOT/keyboards/ck5200/.toolchains" -type f -name riscv-none-elf-gcc -print -quit 2>/dev/null || true)"
if [[ -n "$GCC" ]]; then
    export PATH="$(dirname "$GCC"):$PATH"
fi

mapfile -t COMMITS < <(git -C "$MAIN_ROOT" rev-list --reverse "$BASE"..HEAD)
COMMITS=("$BASE" "${COMMITS[@]}")

pass=0; fail=0
for commit in "${COMMITS[@]}"; do
    short=$(git -C "$MAIN_ROOT" rev-parse --short "$commit")
    subject=$(git -C "$MAIN_ROOT" log -1 --format=%s "$commit")
    # Discard any stray worktree-local changes before moving on.
    git -C "$WT" checkout -q -- . 2>/dev/null || true
    git -C "$WT" checkout -q --detach "$commit" || { echo "BUILD-FAIL $short $subject"; fail=$((fail+1)); continue; }
    rm -rf "$WT/keyboards/ck5200/build"
    if (cd "$WT/keyboards/ck5200" && bash scripts/build.sh >/dev/null 2>&1); then
        echo "BUILD-OK   $short $subject"
        pass=$((pass+1))
    else
        echo "BUILD-FAIL $short $subject"
        fail=$((fail+1))
    fi
done

git -C "$MAIN_ROOT" worktree remove "$WT" >/dev/null 2>&1 || true
git -C "$MAIN_ROOT" worktree prune
find "$WT_ROOT" -mindepth 1 -maxdepth 1 -exec rm -rf {} + 2>/dev/null || true
echo "----"
echo "$pass built, $fail failed of ${#COMMITS[@]} commits"
[[ $fail -eq 0 ]]
