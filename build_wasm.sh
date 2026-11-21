#!/usr/bin/env bash
# Emscripten/WebAssembly build of EDuke32 — classic 8-bit software renderer,
# SDL2, Asyncify, no OpenGL. Music via the built-in OPL3/AdLib synth.
#
#   LINK=1 ./build_wasm.sh      # build + link -> webhost/eduke32.js
#   REBUILD=1 ...               # force a full recompile
#   OPT=-O3 ...                 # override optimisation level
set -u
source /opt/emsdk/emsdk_env.sh >/dev/null 2>&1
OPT="${OPT:--O2}"
OBJ=obj-wasm; mkdir -p "$OBJ"

INC="-Isource/build/include -Isource/audiolib/include -Isource/mact/include -Isource/glad/include -Isource/imgui/include -Isource/mimalloc/include -Isource/voidwrap/include"
DEF="-DNOASM -DRENDERTYPESDL=1 -DSDL_TARGET=2 -DNDEBUG -D_REENTRANT -DB_LITTLE_ENDIAN=1 -DB_BIG_ENDIAN=0"
CF="$OPT -std=gnu++14 -fno-exceptions -fno-rtti -fno-strict-aliasing -fwrapv -fno-threadsafe-statics -funsigned-char -sUSE_SDL=2 $DEF $INC -w"
CFC="$OPT -fno-strict-aliasing -funsigned-char -sUSE_SDL=2 $DEF $INC -w"

BUILD_DIR=source/build/src;   BUILD="2d a-c baselayer cache1d clip colmatch common communityapi compat cpuid crc32 defs dxtfilter engine enet fix16 hash hightile klzw kplib loguru lz4 md4 mhk miniz miniz_tdef miniz_tinfl mmulti mutex osd palette pngwrite pragmas rev screenshot screentext scriptfile sdlayer sjson smalltextfont smmalloc smmalloc_generic smmalloc_tls softsurface texcache textfont tiles timer vfs xxhash"
DUKE_DIR=source/duke3d/src;   DUKE="actors anim cheats cmdline common config demo dnames game gamedef gameexec gamestructures gamevars global grpscan input menus network osdcmds osdfuncs player premap rts savegame sbar screens sector sounds text"
AUDIO_DIR=source/audiolib/src; AUDIO="driver_adlib driver_sdl driver_sf2 drivers flac formats fx_man gmtimbre midi mix mixst multivoc music opl3 pitch vorbis xa xmp"
MACT_DIR=source/mact/src;     MACT="animlib control joystick keyboard scriplib"
IMGUI_DIR=source/imgui/src;   IMGUI="imgui imgui_draw imgui_tables imgui_widgets imgui_impl_sdl2"
RSRC_DIR=source/duke3d/rsrc;  RSRC="eduke32_icon"

fail=0; objs=()
cg() { local grp="$1" dir="$2" names="$3"; mkdir -p "$OBJ/$grp"
  for n in $names; do local out="$OBJ/$grp/$n.o" src cc
    if [ -f "$dir/$n.cpp" ]; then src="$dir/$n.cpp"; cc="em++ $CF";
    elif [ -f "$dir/$n.c" ]; then src="$dir/$n.c"; cc="emcc $CFC";
    else echo "MISSING: $dir/$n.{cpp,c}"; fail=1; continue; fi
    if [ "$out" -nt "$src" ] && [ -z "${REBUILD:-}" ]; then objs+=("$out"); continue; fi
    if ! eval $cc -c "\"$src\"" -o "\"$out\"" 2>"$OBJ/$grp/$n.err"; then echo "FAIL: $src"; head -4 "$OBJ/$grp/$n.err"; fail=1; else objs+=("$out"); fi
  done; }

cg build "$BUILD_DIR" "$BUILD"
cg duke  "$DUKE_DIR"  "$DUKE"
cg audio "$AUDIO_DIR" "$AUDIO"
cg mact  "$MACT_DIR"  "$MACT"
cg imgui "$IMGUI_DIR" "$IMGUI"
cg rsrc  "$RSRC_DIR"  "$RSRC"

echo "compiled ${#objs[@]} objects, fail=$fail"
if [ "$fail" = 0 ] && [ "${LINK:-0}" = 1 ]; then
  echo "linking..."
  em++ $OPT -sUSE_SDL=2 -sASYNCIFY "-sASYNCIFY_REMOVE=C_ParseCommand(bool)" -sSTACK_SIZE=16MB \
       -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=256MB -sEXIT_RUNTIME=1 -sFORCE_FILESYSTEM=1 \
       -sEXPORTED_RUNTIME_METHODS=FS,callMain,ccall,cwrap \
       -sEXPORTED_FUNCTIONS=_main,_malloc,_free \
       "${objs[@]}" -o webhost/eduke32.js 2>"$OBJ/link.err"
  echo "link rc=$?"; grep -iE "error|undefined" "$OBJ/link.err" | head -40
fi
exit $fail
