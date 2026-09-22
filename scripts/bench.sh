#!/usr/bin/env bash
# Compare cum against system compressors on given files and verify round-trip.
# Usage: CUM=path/to/cum scripts/bench.sh file...
# Competitors are used only if installed; they are never linked into cum.
set -euo pipefail

CUM=${CUM:-build/release/cum}
[ $# -gt 0 ] || { echo "usage: CUM=path/to/cum $0 file..." >&2; exit 2; }
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

size() { wc -c < "$1" | tr -d ' '; }
ext() { command -v "$1" >/dev/null && "$@" | wc -c | tr -d ' ' || echo "-"; }

printf "%-20s %10s %10s %10s | %10s %10s %10s %10s\n" \
    file original cum-5 cum-9 gzip-9 bzip2-9 xz-9e zstd-22
status=0
for f in "$@"; do
    row=""
    for level in 5 9; do
        "$CUM" compress "$f" -o "$TMP/c" -l "$level" -f -q
        "$CUM" decompress "$TMP/c" -o "$TMP/d" -f -q
        if cmp -s "$f" "$TMP/d"; then row="$row $(size "$TMP/c")"; else row="$row MISMATCH"; status=1; fi
    done
    printf "%-20s %10s %10s %10s | %10s %10s %10s %10s\n" "$(basename "$f")" "$(size "$f")" $row \
        "$(ext gzip -9c "$f")" "$(ext bzip2 -9c "$f")" "$(ext xz -9e -c "$f")" \
        "$(ext zstd --ultra -22 -q -c "$f")"
done
exit $status
