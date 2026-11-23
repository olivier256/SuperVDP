#!/bin/sh
# =========================================================================
# Banc sh2cycles : mesure le temps du dessin de DDS 32 compilé pour SH2,
# et étalonne le modèle de coûts du simulateur (dds32/cost.h).
#
# 1. Compile dds32/render.c, dds32/sprite.c et la démo (zone, images,
#    jeu) pour SH2 (sh-elf-gcc -m2, -O2, sans les compteurs), avec le banc
#    bench_render.c, aux adresses de la 32X (bench.ld : tout en SDRAM).
# 2. L'exécute sous qemu-sh4eb avec le greffon sh2cycles, qui applique le
#    modèle de temps du SH2 (cycles par instruction, branchements,
#    dépendances, cache à 4 voies et SDRAM). Un programme par CPU (lignes
#    paires, impaires).
# 3. Calcule sur l'hôte les compteurs du modèle pour les mêmes images
#    (costs_render.c), vérifie que le code SH2 dessine les mêmes pixels
#    avec le même nombre d'écritures, et ajuste les coûts unitaires
#    (fit_render.py).
#
# Il faut (Linux ; qemu en mode utilisateur n'existe pas sous macOS, où
# l'on passe par tools/sh2cycles/docker.sh) :
# - la chaîne sh-elf et qemu-sh4eb avec les greffons, construits par
#   build-tools.sh dans $PREFIX (par défaut ~/opt) ;
# - glib (en-têtes), Python 3 et numpy.
#
# Usage : tools/sh2cycles/run.sh   (depuis la racine du dépôt)
# =========================================================================
set -e

if [ "$(uname -s)" != "Linux" ]; then
  echo "run.sh : Linux uniquement. Sur Mac : tools/sh2cycles/docker.sh"
  exit 1
fi

PREFIX=${PREFIX:-$HOME/opt}
SH_ELF=${SH_ELF:-$PREFIX/sh-elf/bin/sh-elf-}
QEMU=${QEMU:-$PREFIX/qemu/bin/qemu-sh4eb}
QEMU_INCLUDE=${QEMU_INCLUDE:-$PREFIX/qemu/include}
for tool in "${SH_ELF}gcc" "$QEMU"; do
  if [ ! -x "$tool" ]; then
    echo "run.sh : $tool introuvable. Construire les outils :"
    echo "  tools/sh2cycles/build-tools.sh   (ou PREFIX=... si ailleurs)"
    exit 1
  fi
done
OUT=build/sh2cycles
T=tools/sh2cycles
mkdir -p "$OUT"

# Données de la démo, converties par bmp2c et dds32conv compilés pour l'hôte
cc -std=c99 -O2 -Idds32 -Itools -o "$OUT/bmp2c" tools/bmp2c.c tools/bmpread.c
"$OUT/bmp2c" demo/assets/assets.txt "$OUT/demo_assets" 2>/dev/null
cc -std=c99 -O2 -Idds32 -Itools -o "$OUT/dds32conv" tools/dds32conv.c \
  tools/bmpread.c dds32/visibility.c
"$OUT/dds32conv" demo/zone/demo.vox demo/zone/cubes.txt demo/assets demo \
  "$OUT/zone_demo" 2>/dev/null

INC="-Ipixel -Idds32 -Idemo -I$T -I$OUT"
SOURCES="dds32/render.c dds32/sprite.c demo/demo.c $OUT/demo_assets.c $OUT/zone_demo.c"

# ---- Hôte : compteurs du modèle -------------------------------------------
cc -std=c99 -O2 -Wall $INC -o "$OUT/costs_render" $T/costs_render.c $SOURCES
"$OUT/costs_render" 0 >"$OUT/render_costs_master.csv"
"$OUT/costs_render" 1 >"$OUT/render_costs_slave.csv"

# ---- SH2 : le banc ----------------------------------------------------------
CFLAGS="-m2 -mb -O2 -std=c99 -ffreestanding -fno-builtin -Wall $INC"
"${SH_ELF}gcc" -m2 -mb -Wa,--isa=sh2 -c $T/start.S -o "$OUT/start.o"
OBJECTS=""
for src in $SOURCES; do
  obj="$OUT/$(basename "$src" .c).o"
  "${SH_ELF}gcc" $CFLAGS -c "$src" -o "$obj"
  OBJECTS="$OBJECTS $obj"
done

# ---- Greffon qemu -----------------------------------------------------------
cc -O2 -shared -fPIC -Wall -I"$QEMU_INCLUDE" $(pkg-config --cflags glib-2.0) \
  -o "$OUT/libsh2cycles.so" $T/sh2cycles_plugin.c

for cpu in master slave; do
  first=0
  [ $cpu = slave ] && first=1
  "${SH_ELF}gcc" $CFLAGS -DBENCH_FIRST_LINE=$first -c $T/bench_render.c \
    -o "$OUT/bench_render_$cpu.o"
  # Segments alignés sur 4 Ko : qemu (mode utilisateur Linux) les projette
  # en mémoire page par page.
  # shellcheck disable=SC2086
  "${SH_ELF}gcc" -m2 -mb -nostdlib -T $T/bench.ld -Wl,-z,max-page-size=0x1000 \
    -Wl,--no-warn-rwx-segments -o "$OUT/bench_render_$cpu.elf" \
    "$OUT/start.o" "$OUT/bench_render_$cpu.o" $OBJECTS -lgcc
  if ! "$QEMU" -plugin "$OUT/libsh2cycles.so" "$OUT/bench_render_$cpu.elf" \
    >"$OUT/render_prints_$cpu.txt" 2>"$OUT/render_cycles_$cpu.txt"; then
    echo "qemu : echec du banc ($cpu) :"
    grep -v '^sh2cycles;' "$OUT/render_cycles_$cpu.txt" | head -5
    exit 1
  fi
done

# Taille en SDRAM (256 Ko sur 32X) : code + données + images + pile
"${SH_ELF}size" -A "$OUT/bench_render_master.elf" | awk '
  $1 ~ /^\.(text|rodata|data|bss|stack)$/ { n += $2 }
  END { printf "SDRAM occupee par le banc : %d octets sur 262144 (%.0f %%)\n", n, 100*n/262144 }'

python3 $T/fit_render.py "$OUT"
