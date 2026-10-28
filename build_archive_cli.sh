#!/bin/sh
# Builds the Linux/WSL archive CLI (archive_cli) and the archive tests.
# Needs: g++, zlib1g-dev, pthreads. Objects go to build/linux.
set -e
OUT=build/linux
mkdir -p "$OUT/unrar"
UNRAR_SRCS="rar strlist strfn pathfn smallfn global file filefn filcreat archive arcread unicode system crypt crc rawread encname resource match timefn rdwrfn consio options errhnd rarvm secpassword rijndael getbits sha1 sha256 blake2s hash extinfo extract volume list find unpack headers threadpool rs16 cmddata ui filestr scantree dll qopen"
for s in $UNRAR_SRCS; do
  if [ ! -f "$OUT/unrar/$s.o" ] || [ "third_party/unrar/$s.cpp" -nt "$OUT/unrar/$s.o" ]; then
    g++ -O2 -std=c++11 -w -DRARDLL -DSILENT -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -DRAR_SMP \
      -c "third_party/unrar/$s.cpp" -o "$OUT/unrar/$s.o"
  fi
done
ar rcs "$OUT/libunrar.a" "$OUT"/unrar/*.o
g++ -std=c++17 -O2 -Wall -Wextra -c ols_unrar.cpp -o "$OUT/ols_unrar.o"
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic archive_cli.cpp "$OUT/ols_unrar.o" "$OUT/libunrar.a" -lz -pthread -o archive_cli
g++ -std=c++17 -O1 -Wall -Wextra -Wpedantic archive_tests.cpp "$OUT/ols_unrar.o" "$OUT/libunrar.a" -lz -pthread -o archive_tests
echo built archive_cli archive_tests
