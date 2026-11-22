Première version : deux plans superposés, composés par segments

**Ce que fait cette version** :
- Nouveau dépôt, sur la base d'AMB96 :
  - `pixel/` : le simulateur (VDP, interface COMM/V_INT, plates-formes
    MiniFB et sans fenêtre), repris tel quel, sauf l'ajout des cycles
    estimés (ci-dessous) ;
  - `tools/bmp2c.c` : plans de dimensions quelconques (multiples de 16,
    plus de puissance de 2), et classe de chaque ligne de tile
    (transparente, opaque, mixte ; 2 bits par ligne, 32 bits par tile) ;
  - le reste d'AMB96 (sprites, assembleur, sh2test) n'est pas repris pour
    l'instant.
- Moteur `supervdp/` :
  - `compose_line` compose le plan A (index 0 transparent) sur le plan B
    **par segments, sans tampon** : copie de B sous les lignes de tile
    transparentes de A, copie de A sous les opaques (B n'est pas lu),
    fusion pixel par pixel sous les mixtes. Les tiles voisines de même
    classe forment un segment ; un mot à cheval sur deux segments (défilement de A impair)
    passe par la fusion ;
  - uniquement des écritures 16 bits à des adresses paires, chaque mot
    écrit une fois : **35 840 écritures par image**, quels que soient
    les défilements ; une décision de parité par ligne et par plan ;
  - Master et Slave composent une ligne sur deux ; le Master relève les
    registres COMM 4 à 7 (deux lectures identiques de suite) et publie la
    scène.
- Démo : décor de Chun-Li, plan A (768 × 224, calé en bas) devant plan B
  (672 × 224). Le programme 68000 (`demo/md68k.c`) lit la manette et
  écrit les défilements : xA de 0 à 448, xB = xA × 11 / 14 arrondi (de 0
  à 352).
- `VDP_STATS=1` : en plus des écritures, le pourcentage du plafond de
  76 800 écritures, les cycles du VDP pour les vider (5 par écriture,
  partagés), et les **cycles CPU estimés** par CPU : modèle de coûts du
  moteur (`supervdp/cost.h`, coûts provisoires) + 3 à 5 cycles par
  écriture. Nouvelle fonction du simulateur : `vdp_count_cpu_cycles()`.
- Vérifié :
  - `test_compose` : 1 168 576 lignes identiques au rendu de référence
    naïf (trajectoire de la démo, toutes les colonnes de A × 40 valeurs
    de B, 300 000 lignes au hasard, défilements négatifs ou au-delà du
    plan compris), 160 écritures 16 bits par ligne, chaque mot écrit une
    fois ;
  - simulateur complet sans fenêtre, 5 scénarios de manette : images
    identiques octet pour octet à la référence, 35 840 écritures par
    image (17 920 par CPU).

**Mesures (modèle provisoire)** :
- trajectoire de la démo, moyenne par image : 17 095 mots copiés source
  paire, 16 960 impaire, 1 783 fusionnés, 794 segments ;
- par CPU, hors écritures : environ 168 000 cycles (défilements pairs) à
  247 000 (A et B impairs), soit 58 à 88 % de l'image écritures
  comprises. Le chemin impair (deux `MOV.B`) est le premier poste.

**Ce qui reste pour plus tard** :
- étalonner les coûts unitaires sur le code SH2 compilé (banc qemu,
  émulateur 32X) ;
- copies en lectures `MOV.L` alignées et décalages en registres
  (4 variantes selon la source mod 4, l'idée des renderers de la
  première ébauche de SuperVDP), surtout pour le chemin impair ;
- frontières de segment : reconnaître les mots opaque + opaque et
  transparent + transparent sans passer par la fusion ;
- l'assembleur SH2 en option (même interface que le C, vérifié contre
  lui), avec le banc sh2test d'AMB96 ;
- sprites.

Message de commit proposé :

```
SuperVDP : deux plans superposés, composés par segments

Nouveau dépôt sur la base d'AMB96 : simulateur pixel, bmp2c (plans de
dimensions quelconques, classe des lignes de tile).

Le plan A (index 0 transparent) est composé sur le plan B ligne par
ligne, sans tampon : copie de B sous les lignes de tile transparentes,
copie de A sous les opaques, fusion sous les mixtes. Uniquement des
écritures 16 bits, chaque mot écrit une fois : 35 840 écritures par
image. Master et Slave composent une ligne sur deux.

Démo Chun-Li : le programme 68000 écrit les défilements dans COMM 4 à 7,
xB = xA x 11 / 14.

VDP_STATS=1 : plafond de 76 800 écritures, cycles du VDP, cycles CPU
estimés (modèle de coûts provisoire).

Tests : 1 168 576 lignes contre un rendu de référence, et 5 scénarios
du simulateur complet identiques octet pour octet.
```

---

Banc sh2cycles : temps SH2 mesuré, modèle de coûts étalonné

**Ce que fait cette version** :
- `tools/sh2cycles/` : banc d'essai qui mesure le temps du **vrai code**
  de `compose.c`, compilé pour SH2 (`sh-elf-gcc` 13.2, `-m2 -O2`, sans les
  compteurs) et lié aux adresses de la 32X (SDRAM en 0600 0000h, frame
  buffer en 2400 0000h) :
  - `sh2cycles_plugin.c` : greffon qemu qui applique un modèle du SH2 à
    chaque instruction exécutée (cycles par instruction, branchements pris
    ou non, dépendance après chargement, cache de 4 Ko 4 voies LRU commun
    aux instructions et aux données, défaut = 12 cycles, écriture en SDRAM
    = +1) ; les écritures dans le frame buffer sont comptées à part ;
    option `profile=on` : exécutions par adresse ;
  - `bench.c` compose les lignes d'un CPU pour 180 images (`frames.h` :
    trajectoire de la démo et défilements quelconques), un programme par
    CPU (chacun son cache) ; `costs.c` calcule sur l'hôte les compteurs du
    modèle pour les mêmes images ; `fit.py` vérifie les pixels (empreintes)
    et ajuste les coûts unitaires par moindres carrés ;
  - `build-tools.sh` construit binutils 2.42, gcc 13.2 pour `sh-elf`
    (SH2, gros-boutiste, libgcc) et qemu 8.2.2 avec les greffons. La même
    chaîne servira pour la console.
- `pixel/vdp.h` : sur SH2, `vdp_dram_write*()` et `vdp_overwrite_write*()`
  deviennent des écritures directes (2400 0000h, 2402 0000h + offset),
  comme sur la console.
- `supervdp/cost.h` : coûts unitaires étalonnés. Le coût d'un mot copié
  depuis une source paire est compris dans celui de la ligne (chaque ligne
  écrit 160 mots : on ne peut pas les séparer).
- `VDP_STATS=1` : nouvelle ligne, la durée de la dernière image en images
  de 1/60 s (CPU le plus lent ou bus du VDP), et le verdict 60 images/s.
- Vérifié : le code SH2 dessine les mêmes pixels que l'hôte (360 images,
  empreintes identiques) et écrit 17 920 mots par CPU et par image ; le
  modèle étalonné reproduit les temps du banc de -3 % à +6 % ;
  `test_compose` et les 5 scénarios du simulateur passent toujours.

**Mesures** (par CPU et par image, hors écritures FB) :
- 542 640 à 950 826 cycles, 717 077 en moyenne ; 508 859 instructions en
  moyenne : **le nombre d'instructions dépasse à lui seul le budget**
  (384 000 cycles) ;
- avec les écritures (3 cycles) : pire image 1 004 586 cycles, soit
  2,6 images de 1/60 s. La version actuelle tiendrait environ **30
  images/s** en moyenne, 20 dans le pire cas. L'ancien modèle provisoire
  sous-estimait d'un facteur 3,5 ;
- répartition des instructions : copie source impaire 33 %, formation
  des segments (`compose_line`) 30 %, copie source paire 18 %, fusion 9 %,
  divisions (`%`, ___sdivsi3) 7 %, décalages variables (___lshrsi3) 4 % ;
- le coût n'est pas dans les boucles de copie (4 instructions par mot en
  source paire, 11 en impaire), mais autour : environ 60 instructions par
  tile de A examinée et par morceau de tile copié. Le SH2 n'a ni décaleur
  à distance variable (appel de libgcc pour `>> (row * 2)`, 4 SHAR pour
  `x >> 4` signé), ni division (`%` à chaque ligne) ;
- SDRAM occupée par le banc (code, tiles, pile) : 169 556 octets sur
  262 144 (65 %).

**Ce qui reste pour plus tard** :
- réécrire le C pour le SH2, en mesurant chaque étape avec le banc :
  classes des lignes de tile rangées par ligne (un octet, sans décalage
  variable) ; parcours des tiles par pointeurs, décalages non signés,
  plus de `%` ni de boucle `advance()` par morceau ; chemin impair sans
  `EXTU.B` ;
- ensuite seulement, l'assembleur en option ;
- recouper le modèle de temps sur un émulateur 32X.

Message de commit proposé :

```
Banc sh2cycles : temps SH2 mesuré, modèle de coûts étalonné

tools/sh2cycles : compose.c compilé pour SH2 (sh-elf-gcc 13.2 -m2 -O2),
exécuté sous qemu avec un greffon qui applique un modèle de temps du SH2
(cycles par instruction, branchements, dépendances, cache de 4 Ko,
SDRAM). Les pixels sont vérifiés contre l'hôte, et les coûts unitaires
de supervdp/cost.h ajustés par moindres carrés (-3 % à +6 %).
build-tools.sh construit la chaîne sh-elf et qemu avec les greffons.

Sur SH2, vdp_dram_write*() écrit directement dans le frame buffer.
VDP_STATS=1 donne la durée de l'image en images de 1/60 s.

Mesure : 717 000 cycles par CPU et par image en moyenne, 1 000 000 au
pire écritures comprises : environ 30 images/s, pas 60. Le temps passe
autour des boucles de copie (segments, morceaux de tile, décalages
variables et divisions, absents du SH2).
```

---

Banc sh2cycles : sur Mac par Docker, outils dans ~/opt

**Ce que fait cette version** :
- `build-tools.sh` installe dans `$PREFIX` (par défaut `~/opt`) : plus
  besoin de `sudo`, ni de `/opt`. Il refuse de tourner hors de Linux avec
  un message clair (qemu en mode utilisateur n'existe pas sous macOS),
  ne retélécharge pas les sources déjà présentes, et configure qemu avec
  le Python du système (correction : avec un autre Python, qemu ne
  trouvait pas distlib).
- `run.sh` cherche les outils dans `$PREFIX` (même défaut) et dit quoi
  faire s'ils manquent ; il refuse aussi de tourner hors de Linux.
- `Dockerfile` et `docker.sh` : le banc sur Mac, dans un conteneur Ubuntu
  (Docker Desktop, OrbStack ou colima). L'image contient la chaîne
  `sh-elf` et qemu (environ 40 minutes à construire, une seule fois) ;
  le dépôt est monté dans le conteneur, les résultats arrivent dans
  `build/sh2cycles`.

**Ce qui reste pour plus tard** :
- inchangé : réécrire le C pour le SH2 en mesurant chaque étape.

Message de commit proposé :

```
Banc sh2cycles : sur Mac par Docker, outils dans ~/opt

build-tools.sh installe dans $PREFIX (~/opt par défaut, sans sudo),
refuse de tourner hors de Linux et configure qemu avec le Python du
système. run.sh cherche les outils au même endroit.

Dockerfile et docker.sh : le banc dans un conteneur Ubuntu, depuis un
Mac, où qemu en mode utilisateur n'existe pas.
```

---

compose.c réécrit pour le SH2, code de rendu en RAM interne

**Ce que fait cette version** :
- `supervdp/compose.c` réécrit pour le SH2, même principe (segments,
  une décision de parité par ligne et par plan, écritures 16 bits
  uniquement), chaque étape mesurée avec le banc :
  - plus de division ni de décalage à distance variable (le SH2 n'en a
    pas : appels à libgcc) : positions ramenées dans le plan par
    soustraction, classe des lignes de tile lue avec des masques préparés
    une fois par ligne, positions non signées ;
  - tiles parcourues par un pointeur sur leur numéro, qui revient au
    début de la rangée au bout du plan ;
  - tiles entières copiées sans boucle (8 mots) ; en source impaire, lues
    en quatre `MOV.L` ;
  - destination : un pointeur sur SH2 (écritures `MOV.W R0,@(disp,Rn)`),
    un offset sur l'hôte (chaque écriture passe toujours par
    `vdp_dram_write16`, qui la compte) ;
  - fusion par morceaux sans changement de tile, deux pixels à la fois ;
    mot de frontière entre segments lu directement (`mergeWord`).
- Cache en mode « 2 voies + RAM » (32X Hardware Manual, « Cache ») : 2 Ko
  de cache à 2 voies, et le code de `compose.c` (1 852 octets) dans les
  2 Ko de RAM interne du SH2, en C000 0000h. Le code de rendu ne subit
  plus de défaut de cache (le profil montrait les lignes de tile lues en
  continu chassant le code et la pile).
- Banc `tools/sh2cycles` :
  - greffon : options `ways=2` (cache 2 voies + RAM interne) et
    `ram=adresse` ; défauts de cache dus aux instructions comptés à part ;
    avec `profile=on`, défauts par ligne de 16 octets ;
  - `bench_ram.ld` place le code de `compose.c` dans la RAM interne ;
    `run.sh` mesure ce mode par défaut (`CACHE=4` : l'ancien) et donne la
    place occupée en RAM interne (2 Ko au plus, vérifié à l'édition de
    liens) ;
  - `fit.py` : coûts unitaires positifs ou nuls (lignes et tiles
    examinées varient presque ensemble).
- `supervdp/cost.h` réétalonné (-4,5 % / +5,3 %) ; `assets_types.h` :
  `tile_row()`, utilisée par la référence de `test_compose`.
- `tests/run_tests.sh` affiche aussi le verdict 60 images/s de chaque
  scénario.
- Vérifié : `test_compose` (1 168 576 lignes) et les 5 scénarios du
  simulateur ; sur SH2, pixels identiques à l'hôte et 17 920 écritures par
  CPU et par image.

**Mesures** (par CPU et par image, hors écritures FB ; pire image avec
écritures à 3 cycles) :

| Étape                                         | Moyenne | Pire    |
|-----------------------------------------------|---------|---------|
| Départ (version précédente, cache 4 voies)    | 717 077 | 950 826 |
| C sans division ni décalage variable          | 481 087 | 682 590 |
| Destination pointeur, fusion par morceaux     | 440 136 | 635 046 |
| Cache 2 voies + code en RAM interne           | 378 183 | 522 105 |
| Fusion deux pixels à la fois, mot de frontière| 358 369 | 487 021 |
| Tile entière impaire en `MOV.L`               | 336 469 | 441 039 |

- pire image écritures comprises : 494 799 cycles, soit 1,29 image de
  1/60 s (2,6 avant) ;
- sur la trajectoire de la démo, selon la parité de xA et xB :
  pair-pair 278 000 en moyenne, 290 000 au pire (**60 images/s**, 0,8
  image de 1/60 s avec les écritures) ; pair-impair 333 000 / 358 000 ;
  impair-pair 365 000 / 380 000 ; impair-impair 418 000 / 436 000 ;
- essayé et abandonné : ranger les tiles par ligne (la ligne 0 de toutes
  les tiles, puis la ligne 1...) pour répartir les lectures sur le cache.
  Plus lent dans les deux modes du cache (474 000 au lieu de 440 000 en
  4 voies, 384 000 au lieu de 378 000 en 2 voies + RAM).

**Ce qui reste pour plus tard** :
- registre de décalage d'écran (SFT, 2000 4102h : un point vers la
  gauche, prévu par la doc pour les défilements impairs en Packed
  Pixel) : xA toujours traité comme pair. Il faut 161 mots par ligne et
  la prise en charge de SFT dans le simulateur. Pire cas attendu :
  environ 358 000 cycles (B impair) ;
- il resterait environ 30 000 cycles à gagner pour tenir 60 images/s
  quand B est impair : segments et fusion (44 % des instructions),
  chemin impair (34 %), notamment les débuts et fins de segment ;
- écritures 32 bits dans le frame buffer : moitié moins d'instructions
  d'écriture, mais la doc ne cite que 8 et 16 bits ; à vérifier sur un
  émulateur 32X avant d'y toucher ;
- sur la console, le programme de démarrage SH2 (pas encore écrit) devra
  mettre le cache en mode 2 voies (CCR) et recopier le code de
  `compose.c` en C000 0000h.

Message de commit proposé :

```
compose.c réécrit pour le SH2, code de rendu en RAM interne

Même principe (segments, parité décidée par ligne et par plan,
écritures 16 bits), mais écrit pour le SH2 et mesuré pas à pas avec
tools/sh2cycles : plus de division ni de décalage variable, tiles
parcourues par pointeur et copiées sans boucle, tile impaire lue en
MOV.L, destination en pointeur sur SH2, fusion par morceaux.

Cache en mode 2 voies + RAM : le code de compose.c (1 852 octets) dans
la RAM interne du SH2. Banc : options ways=2 et ram=, bench_ram.ld,
défauts de cache par ligne, ajustement à coûts positifs. cost.h
réétalonné.

Par CPU et par image : 336 000 cycles en moyenne au lieu de 717 000,
441 000 au pire au lieu de 951 000. Défilements pairs : 60 images/s.
```

---

Décalage d'écran (SFT) choisi ligne par ligne, écrit par le Master en H_INT

**Ce que fait cette version** :
- Simulateur `pixel/` :
  - registre de décalage d'écran (`screenShiftRegister`, 2000 4102h, bit
    SFT) : lu par le VDP à chaque ligne affichée ; avec SFT, le pixel x
    de l'écran est l'octet x + 1 de la ligne ;
  - interruption H : masque (bit H), bit HEN (H_INT aussi pendant le
    VBlank), registre de compteur H (une H_INT toutes les n + 1 lignes),
    acquittement (`interface_clear_hint`), routines `master_hint` et
    `slave_hint` (V_INT passe avant H_INT, comme sur le SH2). La H_INT
    d'une ligne est levée juste après son affichage ;
  - contrôle : une H_INT encore en attente au HBlank suivant est comptée
    « en retard » (sa valeur arrive trop tard pour la ligne voulue) ;
    compteurs affichés avec les statistiques ;
  - latence des interruptions : sur la console, un SH2 prend une
    interruption en quelques cycles ; l'hôte, lui, peut tarder à donner un
    processeur au thread du CPU. Après chaque H_INT, la synchro vidéo
    attend donc que la routine ait acquitté (50 µs au plus), et
    `platform_running()`, appelée dans les boucles d'attente du jeu, cède
    le processeur. Sans cela, sur une machine à 2 cœurs, la moitié des
    H_INT étaient servies avec plus d'une ligne de retard.
- Moteur `supervdp/` :
  - `compose_line` forme les segments sur 322 pixels (de la colonne
    scrollAX - 1 à scrollAX + 320), compte les mots venant de A et de B,
    et choisit le décalage de la ligne qui rend pair le plan le plus
    coûteux en source impaire (un A impair coûte aussi ses mots de
    frontière). Elle renvoie ce décalage : ligne de 160 mots sans, 161
    avec ;
  - frame buffer : 161 mots par ligne (72 640 octets) ;
  - chaque CPU range le décalage de ses lignes dans une table par frame
    buffer ; la routine V_INT du Master prend la table du frame buffer
    affiché (d'après FS) et écrit SFT pour la ligne 0 ; sa routine H_INT
    l'écrit pour la ligne suivante. Le Master, parce qu'il a la priorité
    sur le bus ;
  - code froid : la copie depuis une source impaire, devenue rare, est
    rangée à part (section `.text.sdram`) et reste en SDRAM ; le code
    chaud (1 844 octets) tient dans la RAM interne ;
  - `cost.h` réétalonné (-7 % / +4 %) ; interruptions du Master comptées
    au modèle à 40 cycles chacune (estimation, non mesurée).
- Banc `tools/sh2cycles` : options `ccr_od=1` et `ccr_id=1` (bits OD et
  ID du CCR) ; 160 ou 161 écritures par ligne ; empreinte de ce que
  l'écran affiche.
- `tests/test_compose.c` vérifie ce que l'écran affiche, compte tenu du
  décalage renvoyé, et 160 ou 161 mots écrits une fois chacun.
- `supervdp.c` : le schéma de déroulement, aplati par clang-format,
  redessiné en moins de 80 colonnes.
- Vérifié : `test_compose`, 1 168 576 lignes dont 609 666 décalées, sans
  erreur ; les 5 scénarios du simulateur complet identiques à la
  référence ; sur SH2, pixels identiques à l'hôte.

**Mesures** (banc, par CPU et par image, hors écritures FB) :
- 309 819 cycles en moyenne (336 469 avant), 355 898 au pire (441 039) ;
  pire image avec ses écritures à 3 cycles : 409 850, soit 1,07 image de
  1/60 s (1,29 avant) ;
- trajectoire de la démo, mots copiés depuis une source impaire : 1 194
  par image au lieu de 16 960 ;
- selon la parité de xA et xB (moyenne / pire) :

| Parité xA, xB  | Version précédente  | Cette version       |
|----------------|---------------------|---------------------|
| pair, pair     | 278 000 / 290 000   | 292 000 / 305 000   |
| pair, impair   | 333 000 / 358 000   | 332 000 / 353 000   |
| impair, pair   | 365 000 / 380 000   | 343 000 / 356 000   |
| impair, impair | 418 000 / 436 000   | 299 000 / 311 000   |

  Le cas pair-pair coûte un peu plus (segments sur 322 pixels, choix par
  ligne, code froid) ; impair-impair rejoint pair-pair ; quand les
  parités diffèrent, un plan reste impair sur chaque ligne ;
- scénarios du simulateur (modèle étalonné) : 0,87 à 0,94 image de
  1/60 s, sauf parités différentes (« milieu ») : 1,05 ;
- options du cache, mesurées : OD = 1 deux fois plus lent (787 577 cycles
  en moyenne en 4 voies) ; ID = 1 en 2 voies + RAM : +22 000 cycles (le
  code resté en SDRAM n'est plus mis en cache) ; non retenues.

**Ce qui reste pour plus tard** :
- parités différentes : environ 26 000 cycles à gagner au pire. Pistes :
  segments impairs courts (environ 22 instructions par mot), mise en
  place de chaque morceau de fusion, part fixe de chaque ligne (environ
  140 instructions) ;
- le coût des interruptions du Master (estimé à 40 cycles) reste à
  mesurer ;
- sur la console, la routine H_INT dispose du seul HBlank pour écrire
  SFT (dans le simulateur, d'une ligne entière) : à vérifier sur un
  émulateur 32X ;
- sur une machine à 2 cœurs, le simulateur sert encore 1 à 3 % des H_INT
  en retard (hors des images capturées par les tests) ;
- le programme de démarrage SH2 devra mettre le cache en mode 2 voies,
  recopier le code chaud en C000 0000h, et démasquer V_INT et H_INT sur
  le Master.

Message de commit proposé :

```
Décalage d'écran (SFT) choisi ligne par ligne, écrit par le Master en H_INT

compose_line forme ses segments sur 322 pixels et choisit pour chaque
ligne le décalage d'écran qui rend pair le plan le plus coûteux ; une
ligne décalée fait 161 mots. Chaque CPU range ses décalages dans la table
du frame buffer qu'il dessine ; le Master, prioritaire sur le bus, écrit
SFT en V_INT (ligne 0) et en H_INT (ligne suivante).

Simulateur : registre SFT lu à chaque ligne, interruption H (masque, HEN,
compteur H, acquittement), H_INT en retard comptées, latence des
interruptions imitée (attente bornée, attentes du jeu qui cèdent le
processeur).

Copie depuis une source impaire en code froid (SDRAM), code chaud en RAM
interne (1 844 octets). Banc : options ccr_od et ccr_id. cost.h
réétalonné.

Par CPU et par image : 310 000 cycles en moyenne, 356 000 au pire (441 000
avant) ; impair-impair rejoint pair-pair. Mots en source impaire sur la
démo : 1 194 par image au lieu de 16 960.
```

---

Segments formés depuis les changements de classe, source impaire par mots alignés

**Ce que fait cette version** :
- Profil d'une image aux parités différentes (xA = 100, xB = 79), avant
  cette version : `compose_line` seule prenait 34 % des instructions,
  dont 27 000 par image pour la boucle qui forme les segments (12
  instructions par tile de A, 20 tiles par ligne), contre 7 % pour la
  copie depuis une source impaire. On a donc commencé par la formation
  des segments.
- `tools/bmp2c` : pour chaque plan, une table `classChanges` (champ
  ajouté à `Tilemap`, `assets_types.h`) : pour chaque ligne de pixels, un
  bit par colonne de tile, mis si la classe de la ligne y change. La
  rangée est suivie de 22 colonnes répétées (une ligne d'écran se lit
  d'un seul tenant) et d'un octet de garde : 10 octets par ligne pour A,
  9 pour B, 4,2 Ko en tout. Même préparation pour tous les plans.
- `compose_line` lit les bits des colonnes de la ligne en une fois
  (quatre octets, puis un décalage de 0 à 7 bits composé de décalages
  fixes), et ne s'arrête que sur les changements : 4 instructions par
  tile sans changement, et la boucle finit après le dernier. La classe
  du nouveau segment n'est lue qu'au changement.
- Parité d'un défilement prise directement sur scrollAX et scrollBX
  (les largeurs des plans sont paires) : deux réductions dans le plan en
  moins, et le code chaud retombe sous 2 Ko (2 028 octets).
- `copySpanOdd` (source impaire) : lecture par mots alignés ; chaque mot
  écrit réunit l'octet de poids faible d'un mot lu et l'octet de poids
  fort du suivant, le passage à la tile suivante n'est qu'un changement
  de pointeur ; tiles entières toujours par quatre MOV.L. 17 000
  instructions par image au lieu de 24 000 sur l'image profilée ; elle
  revient en RAM interne, seul le mot de frontière (`mergeWord`) reste en
  SDRAM.
- `cost.h` réétalonné (-6 % / +4 %).
- Vérifié : `test_compose`, 1 168 576 lignes dont 609 666 décalées, sans
  erreur ; les 5 scénarios du simulateur complet identiques à la
  référence ; sur SH2, pixels identiques à l'hôte.

**Mesures** (banc, par CPU et par image, hors écritures FB) :
- 276 852 cycles en moyenne (309 819 avant), 315 938 au pire (355 898) ;
  instructions : 184 439 en moyenne ;
- pire image avec ses écritures : 369 890 cycles à 3 cycles par
  écriture (96 % d'une image de 1/60 s), 405 858 à 5 cycles (106 %) ;
- selon la parité de xA et xB (moyenne / pire) :

| Parité xA, xB  | Version précédente  | Cette version       |
|----------------|---------------------|---------------------|
| pair, pair     | 292 000 / 305 000   | 265 000 / 282 000   |
| pair, impair   | 332 000 / 353 000   | 285 000 / 315 000   |
| impair, pair   | 343 000 / 356 000   | 293 000 / 316 000   |
| impair, impair | 299 000 / 311 000   | 269 000 / 287 000   |

- scénarios du simulateur (modèle étalonné, écritures à 3 cycles) : 0,80
  à 0,95 image de 1/60 s, « milieu » compris (1,05 avant).

**Ce qui reste pour plus tard** :
- écritures à 5 cycles (FIFO pleine) : le pire cas dépasse encore
  l'image de 6 %. Pistes : la fusion (24 % des instructions sur l'image
  profilée, environ 25 instructions par mot), la part fixe de chaque
  ligne (environ 230 instructions, hors formation des segments) ;
- segments impairs courts : la boucle fait 12 instructions par mot ; un
  passage en assembleur SH2 (MOV.L, deux mots par tour) n'en ferait
  gagner que 2 000 environ par image : non retenu pour l'instant ;
- le coût des interruptions du Master (estimé à 40 cycles) reste à
  mesurer ;
- sur la console, la routine H_INT dispose du seul HBlank pour écrire
  SFT : à vérifier sur un émulateur 32X ;
- le programme de démarrage SH2 devra mettre le cache en mode 2 voies,
  recopier le code chaud en C000 0000h, et démasquer V_INT et H_INT sur
  le Master.

Message de commit proposé :

```
Segments formés depuis les changements de classe, source impaire par mots alignés

bmp2c prépare pour chaque plan une table classChanges : pour chaque
ligne de pixels, un bit par colonne de tile où la classe de la ligne
change (4,2 Ko pour la démo). compose_line lit les colonnes de la ligne
en une fois et ne s'arrête que sur les changements, au lieu d'examiner
chaque tile de A. Parité des défilements prise sur les scrolls.

copySpanOdd lit le plan par mots alignés : chaque mot écrit réunit deux
octets de deux mots lus. Code chaud en RAM interne : 2 028 octets.
cost.h réétalonné.

Par CPU et par image, hors écritures FB : 277 000 cycles en moyenne,
316 000 au pire (310 000 et 356 000 avant).
```

---

Retrait de makePair, inutilisée depuis la lecture par mots alignés

**Ce que fait cette version** :
- `compose.c` : la fonction `makePair` (deux pixels lus par MOV.B
  assemblés en un mot) ne servait plus depuis que `copySpanOdd` lit le
  plan par mots alignés ; clang le signalait (`-Wunused-function`), gcc
  non. Retirée. Code produit inchangé.
- Vérifié : compilation sans avertissement avec clang et gcc
  (`-Wall -Wextra`) ; `test_compose` et les 5 scénarios sans erreur.

Message de commit proposé :

```
Retrait de makePair, inutilisée depuis la lecture par mots alignés

clang la signalait (-Wunused-function). Code produit inchangé.
```

---

Fusion : morceaux impairs corrigés, version en assembleur SH2 (option SUPERVDP_ASM)

**Ce que fait cette version** :
- `mergeSpan` (C) : après un morceau impair, tout le morceau suivant
  passait pixel par pixel (environ 22 instructions par pixel), et le
  profil en faisait plus de la moitié du temps de la fusion. Désormais,
  le premier pixel du morceau complète le mot en attente et le reste
  repart par mots entiers. Fusion : 37 700 instructions par image au lieu
  de 50 000 sur l'image profilée (xA = 100, xB = 79).
- `supervdp/merge_sh2.S` : la fusion en assembleur SH2, même interface
  et même résultat que `mergeSpan`, qui reste la référence et la version
  de l'hôte. Utilisée quand `compose.c` est compilé pour le SH2 avec
  `SUPERVDP_ASM=1` ; `compose.c` vérifie à la compilation les positions
  des champs de `PlaneRow` qu'elle lit. Ce qu'elle gagne sur gcc :
  - tout l'état dans les registres (gcc rangeait les curseurs dans la
    pile) : 48 instructions de mise en place par appel au lieu
    d'environ 70, 17 par morceau au lieu de 35 ;
  - mot fusionné en 15 instructions, plus une par pixel pris dans B : le
    pixel de droite est lu par MOV.B @(1,Rm),R0 et seul lui passe par
    EXTU.B ; les deux créneaux de retard des branchements sont remplis
    (proposition d'Olivier), sans lire un registre juste après son
    chargement.
  Fusion : 27 400 instructions par image au lieu de 37 700.
- `tools/sh2cycles` :
  - `test_merge.c` : vérifie sur SH2, sous qemu, l'assembleur contre le
    C : toutes les colonnes de A x 32 colonnes de B x longueurs de 2 à
    40 pixels, plus 100 000 cas au hasard ; 591 520 cas, gardes du
    tampon comprises, sans différence. Une faute volontaire dans
    l'assembleur donne 313 749 cas faux ;
  - `run.sh` : assembleur par défaut (`ASM=0` : tout en C), lance
    `test_merge` avant la mesure ;
  - `bench_ram.ld` : le code des fichiers `*_sh2.o` va aussi en RAM
    interne.
- `cost.h` réétalonné sur la version assembleur, celle de la console
  (-3 % / +4 %). Il ne sert qu'à l'estimation du simulateur.
- Code en RAM interne : 1 852 octets (2 028 tout en C).
- Vérifié : `test_compose`, 1 168 576 lignes sans erreur ; les 5
  scénarios du simulateur complet identiques à la référence ; sur SH2,
  pixels identiques à l'hôte, en C comme en assembleur.

**Mesures** (banc, par CPU et par image, hors écritures FB) :

| Version                   | Moyenne | Pire cas | Pire cas + écritures à 5 cycles |
|---------------------------|---------|----------|---------------------------------|
| Version précédente        | 276 852 | 315 938  | 405 858 (106 %)                 |
| C corrigé (`ASM=0`)       | 272 948 | 307 178  | 397 098 (103 %)                 |
| Assembleur (par défaut)   | 258 651 | 289 706  | 379 626 (99 %)                  |

- avec l'assembleur, le pire cas tient dans l'image même avec des
  écritures à 5 cycles (FIFO plein) ; à 3 cycles : 343 658, soit 89 % ;
- selon la parité de xA et xB (moyenne / pire, Master) :

| Parité xA, xB  | Version précédente  | Cette version       |
|----------------|---------------------|---------------------|
| pair, pair     | 265 000 / 282 000   | 251 000 / 265 000   |
| pair, impair   | 285 000 / 315 000   | 264 000 / 283 000   |
| impair, pair   | 293 000 / 316 000   | 272 000 / 290 000   |
| impair, impair | 269 000 / 287 000   | 255 000 / 269 000   |

- scénarios du simulateur (modèle étalonné, écritures à 3 cycles) : 0,77
  à 0,88 image de 1/60 s.

**Ce qui reste pour plus tard** :
- la part fixe de chaque ligne (environ 230 instructions, hors formation
  des segments) et `planeRowInit` (environ 40 instructions, deux fois par
  ligne) ;
- non modélisé par le banc : le conflit entre lecture d'instruction et
  accès aux données dans le même cycle ;
- le coût des interruptions du Master (estimé à 40 cycles) reste à
  mesurer ;
- sur la console, la routine H_INT dispose du seul HBlank pour écrire
  SFT : à vérifier sur un émulateur 32X ;
- le programme de démarrage SH2 devra mettre le cache en mode 2 voies,
  recopier le code chaud (compose.c et merge_sh2.S) en C000 0000h, et
  démasquer V_INT et H_INT sur le Master.

Message de commit proposé :

```
Fusion : morceaux impairs corrigés, version en assembleur SH2

mergeSpan ne repasse plus pixel par pixel tout un morceau après un
morceau impair : le premier pixel complète le mot en attente.

supervdp/merge_sh2.S : la fusion en assembleur SH2, même interface et
même résultat que mergeSpan (la référence), avec SUPERVDP_ASM=1. Tout
l'état en registres, mot fusionné en 15 instructions, créneaux de retard
remplis. tools/sh2cycles/test_merge.c la vérifie contre le C sur SH2
(591 520 cas) ; run.sh l'utilise par défaut (ASM=0 : tout en C).
cost.h réétalonné sur la version assembleur.

Par CPU et par image, hors écritures FB : 259 000 cycles en moyenne,
290 000 au pire (277 000 et 316 000 avant) ; le pire cas tient dans
l'image même avec des écritures à 5 cycles.
```

---

Part fixe de chaque ligne allégée ; statistiques VBlank corrigées

**Ce que fait cette version** :
- `compose_line` :
  - un segment ne garde que sa fin et ce qu'on y dessine (`kind` : copie
    de B, copie de A, fusion), décidé une fois quand il est formé : 4
    octets au lieu de 8, et plus de comparaison de classe au dessin. La
    table des segments est dans la pile, en SDRAM : moins d'écritures et
    moins de lignes de cache ;
  - la colonne de départ n'est calculée que pour le plan lu (une seule
    pour une copie, deux pour une fusion) ;
  - `PlaneRow` ne garde que ce que lisent la copie et la fusion (rangée,
    fin de rangée, ligne de tile, largeur) ; la ligne de tile et les
    changements de classe de A restent dans `compose_line` ;
  - `planeRowInit` recopiée dans `compose_line` avec la fusion en
    assembleur ; appelée tout en C, sinon le code chaud dépasserait
    2 Ko ;
  - le mot de frontière (code froid) calcule lui-même ses colonnes.
- Simulateur `pixel/` : le relevé des statistiques lisait le nombre de
  VBlank avant les VBlank vus par chaque CPU ; un VBlank commencé et vu
  entre les deux lectures donnait un nombre de « ratés » négatif
  (affiché 18446744073709551615). Relevé dans l'ordre inverse, avec une
  barrière (ARM).
- `cost.h` réétalonné (-3 % / +4 %).
- Vérifié : `test_compose`, 1 168 576 lignes sans erreur ; les 5
  scénarios du simulateur complet identiques à la référence ;
  `test_merge` sans différence ; sur SH2, pixels identiques à l'hôte, en
  C comme en assembleur.

**Mesures** (banc, par CPU et par image, hors écritures FB) :

| Version                   | Moyenne | Pire cas | Pire cas + écritures à 5 cycles |
|---------------------------|---------|----------|---------------------------------|
| Précédente, assembleur    | 258 651 | 289 706  | 379 626 (99 %)                  |
| Celle-ci, assembleur      | 247 820 | 276 774  | 366 694 (95 %)                  |
| Celle-ci, tout en C       | 261 543 | 292 469  | 382 389 (100 %)                 |

- code en RAM interne : 1 904 octets avec l'assembleur, 2 036 tout en C ;
- selon la parité de xA et xB (moyenne / pire, Master) : pair-pair
  239 000 / 252 000, pair-impair 253 000 / 271 000, impair-pair 260 000 /
  277 000, impair-impair 242 000 / 255 000 ;
- scénarios du simulateur (écritures à 3 cycles) : 0,75 à 0,85 image de
  1/60 s.

**Ce qui reste pour plus tard** :
- `compose_line` reste la deuxième dépense (32 % des instructions sur
  l'image profilée) : environ 200 instructions par ligne et 40 par
  segment. Une version en assembleur pourrait en gagner une quinzaine de
  milliers par image ; à discuter avant ;
- essai noté : la pile en RAM interne (accès sans attente) ne tient pas
  à côté du code (448 octets libres, débordement constaté) ;
- non modélisé par le banc : le conflit entre lecture d'instruction et
  accès aux données dans le même cycle ;
- le coût des interruptions du Master (estimé à 40 cycles) reste à
  mesurer ; la routine H_INT dans le seul HBlank, à vérifier sur un
  émulateur 32X ;
- le programme de démarrage SH2 (cache en 2 voies, code chaud en
  C000 0000h, V_INT et H_INT démasquées sur le Master).

Message de commit proposé :

```
Part fixe de chaque ligne allégée ; statistiques VBlank corrigées

compose_line : segments réduits à leur fin et à ce qu'on y dessine
(4 octets, décidé à la formation), colonne de départ calculée pour le
seul plan lu, PlaneRow réduite aux champs lus par la copie et la fusion,
planeRowInit recopiée avec SUPERVDP_ASM. cost.h réétalonné.

Simulateur : VBlank vus relevés avant le nombre de VBlank (le nombre de
ratés pouvait devenir négatif).

Par CPU et par image, hors écritures FB, avec l'assembleur : 248 000
cycles en moyenne, 277 000 au pire (259 000 et 290 000 avant).
```

---

Captures des tests indépendantes de la charge de la machine

Les scénarios du simulateur complet (`tests/run_tests.sh`) échouaient
parfois au hasard : une image capturée différait de la référence, puis
le même test repassait. Cause : le simulateur tourne en temps réel, et
la synchro vidéo n'attendait la routine H_INT du Master que 50 µs. Sur
une machine chargée, le thread du Master obtenait un cœur trop tard ;
la ligne suivante s'affichait alors avec l'ancien décalage d'écran
(SFT). Deux défauts aggravaient le problème : l'attente s'arrêtait à
l'acquittement, alors que la routine acquitte AVANT d'écrire SFT ; et
la V_INT, qui écrit le SFT de la ligne 0, n'était pas attendue du tout.

**Ce que fait cette version** :
- `pixel/interface` :
  - `interface_routines_done()` : compteur des routines d'interruption
    TERMINÉES (V_INT et H_INT), incrémenté au retour de chaque routine
    avec une barrière (release / acquire), pour que ce que la routine a
    écrit soit visible de qui voit le compteur avancer, ARM compris ;
  - `interface_hblank()` et `interface_raise_vint()` renvoient le nombre
    de CPU à qui l'interruption a été transmise (routines à attendre).
- `pixel/platform_headless.c` (plate-forme des tests) : après chaque
  H_INT et chaque V_INT, la synchro vidéo attend que les routines
  lancées soient terminées, sans limite de temps pratique (garde-fou :
  1 s, compté comme « abandonnée »). Le balayage prend du retard sur
  l'horloge et le rattrape ensuite ; l'image ne dépend plus que de
  l'ordre des événements. Compteurs affichés à chaque capture : routines
  attendues plus d'une ligne, attentes abandonnées.
- Option `--latency US` : chaque interruption est prise avec US
  microsecondes de retard, comme sur un hôte très chargé.
- `pixel/platform_minifb.c` (fenêtre) : garde le temps réel (attente de
  50 µs au plus), mais attend la fin de la routine et non plus son
  acquittement.
- `tests/run_tests.sh` : nouveau scénario « lent » (celui de « milieu »,
  où le décalage varie d'une ligne à l'autre, avec `--latency 100`) ; le
  compteur des routines attendues est recopié sous chaque scénario.
- Vérifié : tous les tests passent (environ 26 s au lieu de 23) ; aucune
  H_INT en retard dans les scénarios ; statistiques d'écritures et de
  cycles inchangées.

**Mesures** (scénario « milieu », image 180, comparée à la référence) :

| Latence imposée | Avant                        | Après                 |
|-----------------|------------------------------|-----------------------|
| aucune          | juste (échec rare au hasard) | juste                 |
| 30 µs           | fausse : 20 860 octets       | juste                 |
| 100 µs          | fausse : 40 937 octets       | juste (9 s au lieu de 3) |

Sans latence imposée, sur 2 cœurs, les scénarios comptent de 90 à 400
routines servies en plus d'une ligne : autant d'occasions où l'ancienne
attente pouvait fausser l'image.

**Ce qui reste pour plus tard** :
- la fenêtre peut toujours montrer une ligne mal décalée quand la
  machine est très chargée : c'est voulu (temps réel), et compté dans
  « en retard ».

Message de commit proposé :

```
Captures des tests indépendantes de la charge de la machine

platform_headless : après chaque H_INT et V_INT, la synchro vidéo
attend que les routines soient terminées (interface_routines_done,
avec barrière), et non plus 50 µs au plus jusqu'à l'acquittement. Une
H_INT servie trop tard ne fausse plus le décalage d'écran de la ligne
suivante, ni la V_INT celui de la ligne 0.

Option --latency US (hôte très chargé) et scénario « lent » dans
run_tests.sh : image juste avec 100 µs de retard par interruption
(fausse avant : 40 937 octets différents). La fenêtre garde le temps
réel, mais attend la fin de la routine plutôt que son acquittement.
```
