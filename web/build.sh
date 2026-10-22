#!/bin/sh
# Build kkrieger.wasm from the ported player sources (freestanding WebAssembly).
set -e
cd "$(dirname "$0")/.."
CXX=$HOME/llvm-install/bin/clang++
RES=$($CXX --print-resource-dir)
OUT=web/build
FLAGS="--target=wasm32 -std=c++17 -O2 -nostdlib -nostdinc -nostdinc++ -fno-exceptions -fno-rtti \
  -fms-extensions -fdeclspec -fno-threadsafe-statics -fno-strict-aliasing -fno-delete-null-pointer-checks -ffp-contract=off \
  -mbulk-memory -mnontrapping-fptoint -mmutable-globals \
  -isystem web/libc/include -isystem $RES/include -I. -Imaterials -Iweb \
  -DKKRIEGER -DRONAN -D__stdcall= -D_stdcall= -D__cdecl= -D__forceinline=inline \
  -Wno-ignored-attributes -Wno-invalid-source-encoding -Wno-unknown-pragmas -Wno-writable-strings \
  -Wno-c++11-narrowing -Wno-register -Wno-deprecated-declarations -Wno-microsoft-goto -Wno-ignored-pragma-intrinsic"
SRCS=${SRCS:-"web/libc/libc.cpp _types.cpp"}
OBJS=""
for s in $SRCS; do
  o=$OUT/$(echo $s | tr '/' '_' | sed 's/\.cpp$/.o/')
  if [ ! -f $o ] || [ $s -nt $o ] || [ -n "$FORCE" ]; then
    echo "  CXX $s"
    $CXX $FLAGS -c $s -o $o
  fi
  OBJS="$OBJS $o"
done
if [ -n "$LINK" ]; then
  /usr/local/bin/wasm-ld --no-entry --export-dynamic --initial-memory=67108864 \
    --max-memory=4294967296 --stack-first -z stack-size=8388608 --export=__heap_base --export=__wasm_call_ctors \
    --export=kk_alloc --export=kk_free --export=kk_load --export=kk_load_kx --export=kk_init --export=kk_frame --export=kk_key --export=kk_light_info --export=kk_debug_life --export=kk_debug_coll --export=kk_debug_teleport --export=kk_debug_cell --export=kk_debug_ray --export=kk_debug_part --export=kk_debug_sweep --export=kk_debug_check \
    --export=kk_mouse --export=kk_resize --export=kk_render_sound --export=kk_heap_used --export=kk_heap_peak \
    $OBJS -o web/kkrieger.wasm
  ls -la web/kkrieger.wasm
fi
