#!/bin/sh
# Cross-compiles "OLS Renamer.exe" on Linux/WSL. Needs:
#   apt install g++-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64 libz-mingw-w64-dev
set -e
CXX=x86_64-w64-mingw32-g++-posix
OUT=build/win
mkdir -p "$OUT/unrar" "$OUT/compat"
# UnRAR includes two Windows headers with mixed-case names; MinGW ships them lower-case.
for h in PowrProf.h Wbemidl.h; do
  lc=$(echo "$h" | tr 'A-Z' 'a-z')
  for dir in /usr/share/mingw-w64/include /usr/x86_64-w64-mingw32/include; do
    [ -f "$dir/$lc" ] && { ln -sf "$dir/$lc" "$OUT/compat/$h"; break; }
  done
done
UNRAR_SRCS="rar strlist strfn pathfn smallfn global file filefn filcreat archive arcread unicode system crypt crc rawread encname resource match timefn rdwrfn consio options errhnd rarvm secpassword rijndael getbits sha1 sha256 blake2s hash extinfo extract volume list find unpack headers threadpool rs16 cmddata ui filestr scantree dll qopen"
for s in $UNRAR_SRCS isnt; do
  if [ ! -f "$OUT/unrar/$s.o" ] || [ "third_party/unrar/$s.cpp" -nt "$OUT/unrar/$s.o" ]; then
    $CXX -O2 -std=c++11 -w -I"$OUT/compat" -DRARDLL -DSILENT -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -DRAR_SMP \
      -c "third_party/unrar/$s.cpp" -o "$OUT/unrar/$s.o"
  fi
done
x86_64-w64-mingw32-ar rcs "$OUT/libunrar.a" "$OUT"/unrar/*.o
$CXX -std=c++17 -O2 -I"$OUT/compat" -D_WIN32_WINNT=0x0A00 -c ols_unrar.cpp -o "$OUT/ols_unrar.o"
x86_64-w64-mingw32-windres app.rc -O coff -o app.res
$CXX -std=c++17 -O2 -D_WIN32_WINNT=0x0A00 -D_WIN32_IE=0x0A00 \
  -municode -mwindows -static -static-libgcc -static-libstdc++ \
  olsrenamer.cpp mingw_comsupp.cpp "$OUT/ols_unrar.o" "$OUT/libunrar.a" app.res -o "OLS Renamer.exe" \
  -lz -lcomctl32 -lshell32 -lshlwapi -lole32 -loleaut32 -luuid -ladvapi32 -lpowrprof -lwbemuuid
x86_64-w64-mingw32-strip "OLS Renamer.exe"
echo built "OLS Renamer.exe"
