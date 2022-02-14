#!/bin/sh
set -e

make -B arac.wasm
ls -la arac.wasm
cp arac.wasm ../../docs/hacker-chess/
