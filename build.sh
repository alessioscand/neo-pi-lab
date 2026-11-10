#!/bin/bash
# s16-pi build: ./build.sh [4|3|all]
# Circle in circle-rpi4mc / circle-rpi3mc (the same copies neo-pi uses).
# ./build.sh qemu: a Pi 3 kernel for QEMU -M raspi3b (circle-qemumc), with the
# USB keyboard working as a pad (PROJECT_FAKEPAD); not for real hardware
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
TARGETS="${1:-all}"
[ "$TARGETS" = "all" ] && TARGETS="4 3"

for P in $TARGETS; do
	B="$ROOT/build/rpi$P"
	# keep the core's objects between builds: only what changed is rebuilt
	mkdir -p "$B"
	find "$B" -maxdepth 1 -type f ! -name '*.core.o' ! -name '*.core.cpp' ! -name 'libs16.a' -delete
	# the core's objects do not track headers: a changed header rebuilds the whole core
	T="$ROOT/third_party"
	KEEP=""
	if [ -f "$B/libs16.a" ] && [ -n "$(find "$ROOT/core" "$T/musashi" "$T/z80" "$T/ymfm" \( -name '*.h' -o -name '*.inc' \) -newer "$B/libs16.a")" ]; then
		rm -f "$B"/*.core.o "$B/libs16.a"
	fi
	cp "$ROOT"/common/*.c "$ROOT"/common/*.cpp "$ROOT"/src/*.c "$ROOT"/src/*.cpp "$B"/
	cp "$ROOT"/third_party/miniz/miniz.c "$B"/
	cp "$ROOT"/third_party/snes_ntsc/snes_ntsc.c "$B"/ 2>/dev/null || true
	# the core, flattened: s16.cpp -> s16.core.cpp, m68kcpu.c -> m68kcpu.core.c ...
	for f in "$ROOT"/core/*.cpp "$ROOT"/core/*.c "$T/ymfm/ymfm_opm.cpp" "$T/musashi/m68kcpu.c" "$T/musashi/m68kops.c" "$T/z80/z80.c"; do
		n="$(basename "$f")"; n="${n%.*}.core.${n##*.}"
		cmp -s "$f" "$B/$n" || cp -p "$f" "$B/$n"
		KEEP="$KEEP $n"
	done
	for g in "$B"/*.core.c "$B"/*.core.cpp; do	# a source that is gone: its copy and object too
		[ -e "$g" ] || continue
		case " $KEEP " in *" $(basename "$g") "*) ;; *) rm -f "$g" "${g%.*}.o" "$B/libs16.a" ;; esac
	done
	CIRCLE="$ROOT/circle-rpi${P}mc"
	[ "$P" = "qemu" ] && CIRCLE="$ROOT/circle-qemumc"
	sed "s|@CIRCLEHOME@|$CIRCLE|; s|@SRC@|$ROOT/src|g; s|@COMMON@|$ROOT/common|g; s|@CORE@|$ROOT/core|g; s|@THIRD@|$ROOT/third_party|g; s|@MINIZ@|$ROOT/third_party/miniz|; s|@NTSC@|$ROOT/third_party/snes_ntsc|" \
		"$ROOT/src/Makefile.in" > "$B/Makefile"
	[ "$P" = "qemu" ] && echo "DEFINE += -DPROJECT_FAKEPAD" >> "$B/Makefile"
	make -C "$B" -j16 libs16.a
	make -C "$B" -j16
done

# sanity check: the whole kernel (code + data + bss) must fit in KERNEL_MAX_SIZE
for P in $TARGETS; do
	ELF=$(ls "$ROOT"/build/rpi$P/*.elf)
	[ "$P" = "qemu" ] && continue
	END=$("$ROOT"/toolchain/xpack-aarch64-none-elf-gcc-14.2.1-1.1/bin/aarch64-none-elf-nm "$ELF" | awk '$3=="__bss_end"||$3=="_end"{print $1}' | sort | tail -1)
	LIMIT=$((0x80000 + 0x1000000))
	if [ $((16#$END)) -ge $LIMIT ]; then echo "ERROR: kernel rpi$P too large (ends at 0x$END)"; exit 1; fi
	echo "rpi$P: kernel ends at 0x$END (limit $(printf 0x%x $LIMIT)) OK"
done
