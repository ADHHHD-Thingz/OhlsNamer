#!/bin/bash
# RAR writer self-test on third-party RAR4/RAR5 sample archives (for example
# libarchive's libarchive/test/*.rar.uu). Renames EVERY file entry, then checks
# the result with unrar and rar and compares extracted bytes with the original.
# Usage: tests/rar_format_corpus.sh DIR_WITH_RAR_FILES WORKDIR
set -u
export LC_ALL=C.UTF-8
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=$1
WORK=$2
CLI="$ROOT/archive_cli"
mkdir -p "$WORK"
pass=0 refused=0 failed=0
for f in "$SRC"/*.rar "$SRC"/*.exe; do
  [ -f "$f" ] || continue
  b=$(basename "$f")
  # Only archives that the reference tools consider valid are meaningful inputs.
  if ! unrar t -idq -p- "$f" >/dev/null 2>&1; then
    enc=0; unrar lt -p- "$f" 2>/dev/null | grep -q '^\s*Flags: encrypted' && enc=1
    [ $enc -eq 0 ] && continue
  fi
  for mode in longer unicode; do
    case $b in *.exe) out="$WORK/$mode-${b%.exe}.rar";; *) out="$WORK/$mode-$b";; esac
    cp "$f" "$out"
    extra=""; [ $mode = unicode ] && extra="--unicode"
    log=$("$CLI" testrename "$out" $extra 2>&1)
    if echo "$log" | grep -q "testrename: OK"; then
      ok=1
      unrar t -idq -p- "$out" >/dev/null 2>&1 || { unrar t -idq -p- "$f" >/dev/null 2>&1 && ok=0; }
      rar t -idq -p- "$out" >/dev/null 2>&1 || { rar t -idq -p- "$f" >/dev/null 2>&1 && ok=0; }
      # Same bytes per entry: compare extracted content hashes (unencrypted only).
      rm -rf "$WORK/a" "$WORK/b"; mkdir -p "$WORK/a" "$WORK/b"
      if unrar x -idq -y -p- "$f" "$WORK/a/" >/dev/null 2>&1; then
        unrar x -idq -y -p- "$out" "$WORK/b/" >/dev/null 2>&1
        ha=$(cd "$WORK/a" && find . -type f -exec sha256sum {} + | awk '{print $1}' | sort | sha256sum)
        hb=$(cd "$WORK/b" && find . -type f -exec sha256sum {} + | awk '{print $1}' | sort | sha256sum)
        [ "$ha" = "$hb" ] || ok=0
      fi
      # Names really changed (listing shows the new prefix).
      unrar lb -p- "$out" 2>/dev/null | grep -qE "Renamed_|$(printf '\xc3\x9cn\xc3\xaf_')" || ok=0
      if [ $ok -eq 1 ]; then pass=$((pass + 1)); echo "ok      $mode $b $(echo "$log" | grep -o 'method=[a-z ]*') $(echo "$log" | grep -o 'Recovery record removed\|Quick-open data removed' | tr '\n' ' ')";
      else failed=$((failed + 1)); echo "FAILED  $mode $b (validation)"; fi
    else
      reason=$(echo "$log" | sed -n 's/.*testrename: FAILED method= *renamed=[0-9]* //p;s/.*error: //p' | head -1)
      refused=$((refused + 1)); echo "refused $mode $b: $reason"
    fi
  done
done
echo "RAR corpus: passed=$pass refused=$refused failed=$failed"
[ $failed -eq 0 ]
