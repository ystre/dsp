#!/usr/bin/env bash
#
# split-debug-info.sh — split debug info out of built ELF binaries
#
# Usage: split-debug-info.sh <build-dir>
#
# Walks the build output directory, finds executables and shared
# libraries (skips static .a archives, object files, scripts, etc.),
# and splits debug info into a sibling .debug file linked via
# --add-gnu-debuglink. Idempotent — skips files already processed.

set -euo pipefail

TARGET_DIR="${1:?Usage: $0 <build-dir>}"
OBJCOPY="${OBJCOPY:-objcopy}"

if ! command -v "$OBJCOPY" >/dev/null 2>&1; then
    >&2 echo "error: $OBJCOPY not found"
    exit 1
fi

is_elf_binary_or_shared_lib() {
    local f="$1"
    [[ -f "$f" && -s "$f" ]] || return 1

    local filetype
    filetype=$(file -b "$f")

    [[ "$filetype" == *"ELF"* ]] || return 1
    [[ "$filetype" == *"executable"* || "$filetype" == *"shared object"* ]] || return 1

    return 0
}

already_split() {
    local f="$1"
    "$OBJCOPY" --dump-section .gnu_debuglink=/dev/stdout "$f" >/dev/null 2>&1
}

split_one() {
    local f="$1"
    local debug_file="${f}.debug"

    echo "Extracting debug info: $f"
    "$OBJCOPY" --only-keep-debug "$f" "$debug_file"
    "$OBJCOPY" --strip-debug "$f"
    "$OBJCOPY" --add-gnu-debuglink="$debug_file" "$f"
}

find "$TARGET_DIR" -type f | while read -r f; do
    if is_elf_binary_or_shared_lib "$f"; then
        split_one "$f"
    fi
done
