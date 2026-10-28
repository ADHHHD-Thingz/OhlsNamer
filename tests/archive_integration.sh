#!/bin/bash
# Archive integration test: builds ZIP and RAR archives from the OLS fixtures
# with real tools, checks that in-archive analysis equals plain-file analysis,
# renames inside every archive, then validates the results with independent
# tools (unzip, 7z, Python zipfile, unrar, rar) and compares extracted bytes.
# Needs: zip unzip 7z python3 rar unrar, ./archive_cli and ./core_cli built.
set -u
export LC_ALL=C.UTF-8
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${1:-$(mktemp -d)}
CLI="$ROOT/archive_cli"
CORE="$ROOT/core_cli"
[ -x "$CLI" ] && [ -x "$CORE" ] || { echo "build archive_cli and core_cli first"; exit 1; }
fails=0
fail() { echo "FAIL: $*"; fails=$((fails + 1)); }

mkdir -p "$WORK/src/Sub Folder" "$WORK/arc"
cp "$ROOT"/olstest/*.ols "$WORK/src/"
cp "$ROOT"/*.ols "$WORK/src/Sub Folder/" 2>/dev/null
echo "not a project" > "$WORK/src/readme.txt"
(cd "$WORK/src" && find . -type f | sort | while read -r f; do sha256sum "$f"; done) > "$WORK/orig.sha"

# Plain-file reference: defined-map count per base name.
for f in "$WORK"/src/*.ols "$WORK"/src/Sub\ Folder/*.ols; do
  printf '%s\t%s\n' "$(basename "$f")" "$("$CORE" "$f" | sed -n 's/.* defined=\([0-9]*\) .*/\1/p')"
done | sort > "$WORK/plain_counts.tsv"

cd "$WORK/src"
zip -qr -X "$WORK/arc/deflate.zip" .
zip -qr -0 -X "$WORK/arc/stored.zip" .
7z a -tzip -bso0 -bsp0 "$WORK/arc/sevenzip.zip" . >/dev/null
python3 -I - "$WORK/arc" <<'EOF'
import os, sys, zipfile, io
arc = sys.argv[1]
files = sorted(os.path.relpath(os.path.join(d, f), '.') for d, _, fs in os.walk('.') for f in fs)
with zipfile.ZipFile(os.path.join(arc, 'zip64.zip'), 'w', zipfile.ZIP_DEFLATED) as z:
    for f in files:
        with open(f, 'rb') as src, z.open(f, 'w', force_zip64=True) as dst:
            dst.write(src.read())
class Unseekable(io.RawIOBase):
    def __init__(self, f): self.f = f
    def writable(self): return True
    def write(self, b): return self.f.write(b)
with open(os.path.join(arc, 'descriptor.zip'), 'wb') as raw:
    with zipfile.ZipFile(Unseekable(raw), 'w', zipfile.ZIP_DEFLATED) as z:  # data descriptors
        for f in files:
            z.write(f)
EOF
HAVE_RAR=0; command -v rar >/dev/null && command -v unrar >/dev/null && HAVE_RAR=1
[ $HAVE_RAR -eq 1 ] || echo "skip RAR archives: rar/unrar not installed"
[ $HAVE_RAR -eq 1 ] && {
rar a -ma5 -idq -r "$WORK/arc/rar5.rar" .
rar a -ma5 -idq -r -s "$WORK/arc/rar5_solid.rar" .
rar a -ma5 -idq -r -rr5% -qo+ "$WORK/arc/rar5_rr_qo.rar" .
# RAR 7 can no longer create RAR4 archives; RAR4 is covered by
# tests/rar_format_corpus.sh with third-party samples. Use them if rar can.
rar a -ma4 -idq -r "$WORK/arc/rar4.rar" . 2>/dev/null
rar a -ma4 -idq -r -s -rr3% "$WORK/arc/rar4_solid_rr.rar" . 2>/dev/null
}
rar a -ma5 -idq -r -psecret "$WORK/arc/rar5_encdata.rar" .
rar a -ma5 -idq -r -hpsecret "$WORK/arc/rar5_enchead.rar" .
cd "$WORK/arc"

check_scan() {  # archive: in-archive counts must equal plain-file counts
  local a=$1
  "$CLI" scan "$a" > "$a.scan" 2>&1
  grep -E '^\s+\[[0-9]+\] .* maps=' "$a.scan" | sed -E 's/^\s+\[[0-9]+\] (.*)  maps=[0-9]+ defined=([0-9]+) .*/\1\t\2/' |
    awk -F'\t' '{n=split($1,p,"[/\\\\]"); print p[n]"\t"$2}' | sort > "$a.counts"
  if ! diff -q "$WORK/plain_counts.tsv" "$a.counts" >/dev/null; then
    fail "$a: in-archive map counts differ from plain files"; diff "$WORK/plain_counts.tsv" "$a.counts" | head -5
  else
    echo "ok   scan  $a ($(wc -l < "$a.counts") projects match plain-file counts)"
  fi
}

validate_extract() {  # archive tool: extract and compare bytes with originals
  local a=$1 tool=$2 out="$WORK/x_$a"
  rm -rf "$out"; mkdir -p "$out"
  case $tool in
    unzip) unzip -qq "$a" -d "$out" ;;
    unrar) unrar x -idq -y "$a" "$out/" >/dev/null ;;
  esac
  (cd "$out" && find . -type f | sort | while read -r f; do sha256sum "$f"; done) | awk '{print $1}' | sort > "$out.sha"
  awk '{print $1}' "$WORK/orig.sha" | sort > "$out.orig"
  if ! diff -q "$out.sha" "$out.orig" >/dev/null; then fail "$a: extracted bytes differ from originals"; else echo "ok   bytes $a (all extracted contents identical)"; fi
}

for a in deflate.zip stored.zip sevenzip.zip zip64.zip descriptor.zip; do
  check_scan "$a"
  for mode in inplace copy; do
    cp "$a" "$mode-$a"
    if [ $mode = copy ]; then "$CLI" rename "$mode-$a" --copy > "$mode-$a.log" 2>&1; else "$CLI" rename "$mode-$a" > "$mode-$a.log" 2>&1; fi
    grep -q "rename: OK" "$mode-$a.log" || { fail "$mode-$a rename"; tail -3 "$mode-$a.log"; continue; }
    echo "ok   rename $mode-$a ($(grep -o 'method=[a-z ]*' "$mode-$a.log" | head -1))"
    unzip -tqq "$mode-$a" >/dev/null 2>&1 || fail "$mode-$a: unzip -t"
    7z t "$mode-$a" >/dev/null 2>&1 || fail "$mode-$a: 7z t"
    python3 -I -c "import zipfile,sys; z=zipfile.ZipFile(sys.argv[1]); sys.exit(1 if z.testzip() else 0)" "$mode-$a" || fail "$mode-$a: python testzip"
    validate_extract "$mode-$a" unzip
    # A second scan must propose no further renames (idempotent).
    "$CLI" scan "$mode-$a" | grep -q -- '-> [^(]' && fail "$mode-$a: second scan still proposes renames"
  done
done

# In-place path: shorter names fit their local headers (padding extra field).
for a in deflate.zip stored.zip sevenzip.zip zip64.zip descriptor.zip; do
  cp "$a" "short-$a"
  # --no-rewrite: names that fit are renamed in place, others are reported.
  "$CLI" testrename "short-$a" --shorter --no-rewrite > "short-$a.log" 2>&1
  grep -q "testrename: OK method=in place" "short-$a.log" || { fail "short-$a: expected in-place rename"; tail -2 "short-$a.log"; continue; }
  unzip -tqq "short-$a" >/dev/null 2>&1 || fail "short-$a: unzip -t"
  7z t "short-$a" >/dev/null 2>&1 || fail "short-$a: 7z t"
  python3 -I -c "import zipfile,sys; z=zipfile.ZipFile(sys.argv[1]); sys.exit(1 if z.testzip() else 0)" "short-$a" || fail "short-$a: python testzip"
  [ "$(stat -c %s "short-$a")" -le "$(stat -c %s "$a")" ] || fail "short-$a grew"
  validate_extract "short-$a" unzip
  echo "ok   in-place short-$a ($(grep -o 'renamed=[0-9]* skipped=[0-9]*' "short-$a.log"))"
done

for a in rar5.rar rar5_solid.rar rar5_rr_qo.rar rar4.rar rar4_solid_rr.rar; do
  [ -f "$a" ] || { echo "skip $a (this rar version cannot create it)"; continue; }
  check_scan "$a"
  cp "$a" "renamed-$a"
  "$CLI" rename "renamed-$a" > "renamed-$a.log" 2>&1
  grep -q "rename: OK" "renamed-$a.log" || { fail "renamed-$a rename"; tail -3 "renamed-$a.log"; continue; }
  echo "ok   rename renamed-$a ($(grep -o 'method=[a-z ]*' "renamed-$a.log" | head -1)) $(grep -o 'Recovery record removed\|Quick-open data removed' "renamed-$a.log" | tr '\n' ' ')"
  unrar t -idq "renamed-$a" >/dev/null 2>&1 || fail "renamed-$a: unrar t"
  rar t -idq "renamed-$a" >/dev/null 2>&1 || fail "renamed-$a: rar t"
  # 7-Zip 23 cannot decode RAR 7 compression; only require it when it reads the original.
  if 7z t "$a" >/dev/null 2>&1; then 7z t "renamed-$a" >/dev/null 2>&1 || fail "renamed-$a: 7z t"; fi
  validate_extract "renamed-$a" unrar
  "$CLI" scan "renamed-$a" | grep -q -- '-> [^(]' && fail "renamed-$a: second scan still proposes renames"
done

# Encrypted archives: data-encrypted entries are review-only, header-encrypted archives are refused.
if [ $HAVE_RAR -eq 1 ]; then
"$CLI" scan rar5_encdata.rar > enc.scan 2>&1
grep -q "Encrypted entry" enc.scan && echo "ok   rar5_encdata.rar entries reported as encrypted" || fail "encrypted data not reported"
"$CLI" scan rar5_enchead.rar > enchead.scan 2>&1
grep -q "encrypted" enchead.scan && echo "ok   rar5_enchead.rar refused (headers encrypted)" || fail "encrypted headers not reported"
fi

# Extraction of projects out of every archive (archives stay unchanged).
grep '\.ols$' "$WORK/orig.sha" | awk '{print $1}' | sort > "$WORK/ols.sha"
for a in deflate.zip stored.zip sevenzip.zip zip64.zip descriptor.zip rar5.rar rar5_solid.rar rar5_rr_qo.rar; do
  [ -f "$a" ] || continue
  before=$(sha256sum "$a" | cut -d' ' -f1)
  out="$WORK/extract_$a"; rm -rf "$out"; mkdir -p "$out"
  "$CLI" extract "$a" "$out" > "$out.log" 2>&1 || { fail "extract $a"; tail -3 "$out.log"; continue; }
  (cd "$out" && sha256sum -- *.ols | awk '{print $1}' | sort) > "$out.sha"
  diff -q "$out.sha" "$WORK/ols.sha" >/dev/null || fail "extract $a: contents differ from originals"
  [ "$(sha256sum "$a" | cut -d' ' -f1)" = "$before" ] || fail "extract $a modified the archive"
  ls "$out" | grep -q 'olsrenamer-part' && fail "extract $a left partial files"
  # Extracting again into the same folder must not overwrite anything.
  first=$(cd "$out" && sha256sum -- *.ols | sort | sha256sum)
  "$CLI" extract "$a" "$out" > "$out.log2" 2>&1 || fail "second extract $a"
  [ "$(ls "$out" | wc -l)" -eq $((2 * $(wc -l < "$WORK/ols.sha"))) ] || fail "second extract $a: expected numbered copies"
  echo "ok   extract $a ($(wc -l < "$out.sha") projects byte-identical; archive unchanged; no overwrite on re-extract)"
done

# Damaged RAR5 (a broken header mid-archive, and a zero-filled tail like an
# unfinished download): the header walk refuses them, the UnRAR fallback still
# lists, analyses and extracts every reachable project byte-identically, and
# renaming inside them is refused.
if [ -f rar5.rar ]; then
python3 -I - rar5.rar <<'EOF'
import sys
d = bytearray(open(sys.argv[1], 'rb').read()); n = len(d)
def vint(b, p):
    v = s = 0
    while True:
        c = b[p]; p += 1; v |= (c & 0x7f) << s; s += 7
        if not c & 0x80: return v, p
pos, files = 8, []
while pos < n:
    hs, q = vint(d, pos + 4); t, r = vint(d, q); fl, r = vint(d, r); ds = 0
    if fl & 1: _, r = vint(d, r)
    if fl & 2: ds, r = vint(d, r)
    if t == 2: files.append(pos)
    pos = q + hs + ds
    if t == 5: break
mid = files[len(files) // 2]
z = bytearray(d); z[mid + 10] ^= 0x55; open('damaged_header.rar', 'wb').write(z)
z = bytearray(d); z[mid:] = bytes(n - mid); open('damaged_zerotail.rar', 'wb').write(z)
EOF
for a in damaged_header.rar damaged_zerotail.rar; do
  "$CLI" scan "$a" > "$a.scan" 2>&1
  grep -q "Read-only (listed by UnRAR" "$a.scan" || { fail "$a: fallback listing not used"; head -3 "$a.scan"; continue; }
  out="$WORK/extract_$a"; rm -rf "$out"; mkdir -p "$out"
  "$CLI" extract "$a" "$out" > "$out.log" 2>&1
  got=$(ls "$out" | grep -c '\.ols$')
  [ "$got" -gt 0 ] || fail "$a: nothing extracted"
  (cd "$out" && sha256sum -- *.ols | awk '{print $1}') | grep -qvxFf "$WORK/ols.sha" && fail "$a: an extracted file differs from every original"
  ls "$out" | grep -q 'olsrenamer-part' && fail "$a: partial files left"
  cp "$a" "rn-$a"
  "$CLI" rename "rn-$a" > "rn-$a.log" 2>&1
  grep -q "rename: OK" "rn-$a.log" && fail "$a: rename inside a damaged archive was not refused"
  cmp -s "$a" "rn-$a" || fail "$a: refused rename modified the archive"
  echo "ok   $a read via UnRAR fallback ($got projects extracted byte-identical; rename refused; $(grep -o 'zero-filled' "$a.scan" | head -1))"
done
fi

echo "work dir: $WORK"
[ $fails -eq 0 ] && echo "ARCHIVE INTEGRATION PASSED" || echo "ARCHIVE INTEGRATION FAILED ($fails)"
exit $fails
