#!/bin/sh
# Cross-compiles "OLS Renamer.exe" on Linux/WSL. Needs:
#   apt install g++-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64 libz-mingw-w64-dev
set -e
x86_64-w64-mingw32-windres app.rc -O coff -o app.res
x86_64-w64-mingw32-g++-posix -std=c++17 -O2 -D_WIN32_WINNT=0x0A00 -D_WIN32_IE=0x0A00 \
  -municode -mwindows -static -static-libgcc -static-libstdc++ \
  olsrenamer.cpp app.res -o "OLS Renamer.exe" \
  -lz -lcomctl32 -lshell32 -lshlwapi -lole32 -luuid -ladvapi32
x86_64-w64-mingw32-strip "OLS Renamer.exe"
echo built "OLS Renamer.exe"
