CXX=clang++
CFLAGS=--target=wasm32 -std=c++20 -fno-exceptions \
	-W -Wall -Wextra -Werror -Wno-unused
LDFLAGS=-nostdlib \
	-Wl,--no-entry \
	-Wl,--strip-all \
	-Wl,--import-memory \
	-Wl,-z,stack=size=0x20000 \
	-O3 -flto -Wl,--lto-O3

.PHONY=all
all: arac.wasm

arac.wasm: arac.cpp
	$(CXX) $(CFLAGS) $(LDFLAGS) -o $@ $+

arac.llvm: arac.cpp
	$(CXX) $(CFLAGS) -c -emit-llvm -S -o $@ $+
