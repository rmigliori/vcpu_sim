#!/bin/sh
# tools/fingerprint.sh -- what a RENAME must not change.
#
# A .vx is a TEXT file in three parts: .text (the encoded instructions),
# .data (the initialised image) and .symmap (the symbol table, which is the
# only place a symbol's NAME survives into the executable).
#
# So a pure rename has an exact signature, and it is stronger than "the tests
# still pass":
#
#   .text and .data      must be IDENTICAL, byte for byte
#   .symmap              must have the same number of entries, with the same
#                        values in the same order -- only the names differ
#
# That is what this script prints, one line per program: a hash of the part
# that must not move, and a hash of the symbol table WITH THE NAMES STRIPPED.
# Both must be unchanged. Run it before the rename, run it after, diff the two.
#
#   tools/fingerprint.sh > /tmp/before.txt
#   ... rename ...
#   cmake --build build && tools/fingerprint.sh | diff /tmp/before.txt -
#
# An empty diff is a PROOF, not an argument: it says the linked image is the
# same program with different labels on it. It is the method of §3.24 (the .vx
# compared byte for byte at every commit) narrowed to the one thing a rename is
# allowed to touch.
#
# --- IT SEES WHAT ctest CANNOT, AND THAT IS MEASURED ---
# Verified on 13/09/2026, because a net nobody tested is not a net. Two runs:
#
#   a real rename (coda_peek -> queue_peek, 7 files)  ctest 32/32, diff EMPTY
#   ONE extra instruction in scheduler.vasm           ctest 32/32, diff on TEN
#                                                     of the fifteen programs
#
# The second is the one that matters. That instruction sat on a branch that is
# never executed, so every assertion still held and the suite stayed green --
# and the fingerprint moved anyway, because the image is not the same image.
#
# So this does NOT replace ctest and ctest does not replace this: they fail on
# different things. Run both.

set -eu

BUILD="${1:-build}"

if [ ! -d "$BUILD/vasm" ]; then
  echo "fingerprint: no '$BUILD/vasm' -- build first" >&2
  exit 1
fi

# 'sha256sum' on Linux, 'shasum -a 256' elsewhere; take the first field either way.
hash_stdin()
{
  if command -v sha256sum >/dev/null 2>&1; then sha256sum
  else shasum -a 256
  fi | cut -d' ' -f1
}

printf '%-24s %-16s %-16s %s\n' PROGRAM TEXT+DATA SYMMAP-VALUES ENTRIES

for vx in "$BUILD"/vasm/*.vx; do
  [ -e "$vx" ] || continue
  name=$(basename "$vx")

  # Everything up to (not including) .symmap: the program itself.
  body=$(sed '/^\.symmap/,$d' "$vx" | hash_stdin)

  # The symbol table with the NAME column removed. Keeping the order matters:
  # a rename that reordered the table would change addresses elsewhere, and
  # this is what would catch it.
  syms=$(sed -n '/^\.symmap/,$p' "$vx" | sed '1d' | awk 'NF { $1=""; print }' | hash_stdin)
  n=$(sed -n '/^\.symmap/,$p' "$vx" | sed '1d' | awk 'NF' | wc -l | tr -d ' ')

  printf '%-24s %-16.16s %-16.16s %s\n' "$name" "$body" "$syms" "$n"
done
