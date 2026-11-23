# sh2cycles : temps SH2 du moteur, et étalonnage du modèle de coûts

Le simulateur pixel exécute DDS 32 en code natif : il compte exactement
les écritures dans le frame buffer, mais pas le temps des SH2. Il affiche
donc des cycles CPU **estimés**, d'après le modèle de coûts de
`dds32/cost.h`. Ce banc donne à ce modèle des valeurs mesurées.

## Ce que fait le banc

1. `dds32/render.c`, `dds32/sprite.c` et la démo (`demo/demo.c`, la
   zone convertie par dds32conv, les sprites convertis par bmp2c) sont
   compilés **pour SH2** (`sh-elf-gcc -m2 -O2`), sans les compteurs du
   modèle : c'est le code qui tournera sur la 32X. Ils sont liés aux
   adresses de la 32X (`bench.ld`) : code, données, images et pile en
   SDRAM (0600 0000h), frame buffer en 2400 0000h (zone overwrite en
   2402 0000h).
2. Le programme (`bench_render.c`) appelle `render_line` pour les lignes
   d'un CPU, sur 120 images (`render_frames.h`) : la caisse qui se
   promène dans la zone, caméra qui suit, et une image sur trois la
   caisse et la caméra n'importe où (pour que l'ajustement sépare les
   coûts). Il tourne sous
   `qemu-sh4eb`, avec le greffon `sh2cycles_plugin.c` qui mesure chaque
   image, d'après un modèle du SH2 :
   - temps de chaque instruction (SH7604 Hardware Manual) : 1 cycle, sauf
     branchements (BT/BF pris 3, BT/S/BF/S pris 2, BRA, JSR, RTS... 2),
     multiplications ;
   - +1 cycle quand une instruction lit le registre que la précédente
     vient de charger ;
   - cache à 4 voies (4 Ko, lignes de 16 octets, LRU, commun aux
     instructions et aux données, écriture simultanée) : +12 cycles par
     défaut (une ligne depuis la SDRAM), +1 par écriture en SDRAM ;
   - les écritures dans le frame buffer sont comptées à part (le
     simulateur leur compte 3 à 5 cycles).
   Un programme par CPU : chacun a son propre cache.
3. Sur l'hôte, `costs_render.c` calcule les compteurs du modèle pour les
   mêmes images. `fit_render.py` vérifie que le code SH2 dessine
   exactement les mêmes pixels (empreintes des zones normale et
   overwrite) avec le même nombre d'écritures, puis ajuste les coûts
   unitaires par moindres carrés.

Le greffon sait aussi simuler le mode « 2 voies + RAM » du cache (option
`ways=2`, avec `ram=adresse`) : 2 Ko de cache et 2 Ko de RAM interne sans
attente, pour y placer plus tard le code de dessin.

`fit_render.py` donne aussi le temps mesuré, séparément pour les images
où la caméra suit la caisse (le cas du jeu) et pour les autres, et le
profil (ci-dessous) montre les boucles chères : c'est ainsi qu'ont été
trouvées les deux réécritures de `render.c` décrites dans VERSION.md.

Non modélisé : le conflit entre lecture d'instruction et accès aux
données dans le même cycle, le partage de la SDRAM avec l'autre SH2,
l'attente quand le FIFO du frame buffer est plein. Les temps donnés sont
ceux d'un modèle, plus fin que `cost.h`, mais pas d'une 32X réelle.

## Lancer

**Sur Mac** : qemu en mode utilisateur n'existe que sous Linux. Le banc
tourne donc dans un conteneur Linux (Docker Desktop, OrbStack ou colima) :

```sh
tools/sh2cycles/docker.sh
```

La première fois, l'image est construite : chaîne `sh-elf` et qemu,
environ 40 minutes. Ensuite, le banc prend quelques secondes. Les
résultats sont dans `build/sh2cycles`.

**Sous Linux**, directement :

```sh
sudo apt install build-essential curl xz-utils libgmp-dev libmpfr-dev \
     libmpc-dev ninja-build libglib2.0-dev pkg-config python3-venv \
     python3-distlib python3-numpy flex bison texinfo
tools/sh2cycles/build-tools.sh     # une fois : ~/opt/sh-elf et ~/opt/qemu
tools/sh2cycles/run.sh
```

Ubuntu fournit `gcc-sh4-linux-gnu`, mais il ne sait produire que du SH4,
et son qemu n'a pas les greffons : d'où `build-tools.sh`. Variables :
`PREFIX` (dossier des outils, `~/opt` par défaut), ou, une par une,
`SH_ELF` (préfixe de la chaîne), `QEMU` et `QEMU_INCLUDE` (dossier de
`qemu-plugin.h`).

La même chaîne `sh-elf` servira à compiler DDS 32 pour la vraie 32X.

## Profil : où passe le temps

```sh
~/opt/qemu/bin/qemu-sh4eb \
    -plugin build/sh2cycles/libsh2cycles.so,profile=on \
    build/sh2cycles/bench_render_master.elf >/dev/null 2>profil.txt
```

- `profil;adresse;opcode;exécutions` : exécutions de chaque instruction
  pendant les mesures ; avec `sh-elf-objdump -d
  build/sh2cycles/bench_render_master.elf`, on retrouve les boucles chères ;
- `defaut;ligne;nombre` : défauts de cache par ligne de 16 octets ; avec
  `sh-elf-nm -n`, on voit quelles données (ou quel code) se chassent.

Chaque ligne `sh2cycles;...` donne aussi, en dernier champ, les défauts
de cache dus aux instructions.
