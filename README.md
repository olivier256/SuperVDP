# DDS 32

Un jeu en 3D isométrique façon Landstalker sur la Sega 32X, vue comme une
carte graphique de la Mega Drive. Le nom salue le DDS520 de Landstalker
(« Diamond Shaped Dimension System », Climax, 1992).

Le monde est fait de **voxels** (ponts, arches, surplombs compris), édités
dans MagicaVoxel. Un convertisseur, sur Mac, calcule d'avance quelle face
de quel voxel montre chaque triangle de l'image : la 32X n'a plus qu'à
copier une mosaïque de triangles, en 256 couleurs, et à dessiner les
objets mobiles, cachés par le décor au triangle près. Les choix et les
étapes sont dans [ARCHITECTURE.md](ARCHITECTURE.md).

DDS 32 est né d'un clone de SuperVDP (github.com/olivier256/SuperVDP,
commit 032a22f), dont il ne garde que l'architecture proche de la 32X :
le simulateur **pixel** (C, MiniFB, deux threads Master et Slave, VBlank
à 60 Hz, API qui imite les registres : `vdp_dram`, `vdp_palette`, FBCR,
`comm[]` ; il vient du projet AMB96), les boucles Master / Slave, les
tests et le banc de mesure.

## Compiler et lancer

```sh
git submodule update --init   # MiniFB, pour la fenêtre (voir ci-dessous)
make run                      # compile et lance la démo
make stats                    # la même, avec VDP_STATS=1
make test                     # tous les tests (environ 25 s)
```

La compilation convertit la zone de démo et produit son aperçu,
`build/apercu_demo.bmp` : l'image entière de la zone, telle que la
console l'affiche.

Après le clonage, MiniFB s'installe une fois par `git submodule update
--init` : sans lui, `make run` le rappelle et s'arrête. À la création du
dépôt, MiniFB s'ajoute une fois pour toutes par
`git submodule add https://github.com/emoon/minifb.git minifb`.
Sans MiniFB, seules les cibles sans fenêtre sont construites
(`dds32_headless`, les tests). Sous Linux, MiniFB demande les en-têtes
X11 ; `-DMINIFB_USE_OPENGL_API=OFF` évite OpenGL.

Dans VS Code : ouvrir `DDS32.code-workspace` ; tâches « Compiler »,
« Lancer la démo », « Lancer la démo avec VDP_STATS », « Tests ».

## La démo

Une zone de 24 × 20 cases sur 8 niveaux (`demo/zone/demo.vox`) : une
rivière, un pont de planches avec du vide dessous, une arche de pierre,
un plateau avec une marche, des murs, des caisses. Une caisse se conduit
à la manette ; la caméra la suit.

| Manette    | Clavier | Effet                                         |
|------------|---------|-----------------------------------------------|
| Droite / Gauche | flèches | u + 1 / u − 1 (diagonale bas droite / haut gauche) |
| Bas / Haut | flèches | v + 1 / v − 1 (diagonale bas gauche / haut droite) |
| C maintenu | L       | 4 unités par image au lieu de 1               |

Comme dans Landstalker, les directions de la croix suivent les axes de la
zone. La caisse monte d'un niveau à la fois (la marche, puis le plateau ;
le pont), ne marche pas sur l'eau, passe sous l'arche.

## Modifier la zone

1. Ouvrir `demo/zone/demo.vox` dans MagicaVoxel. Chaque couleur de la
   palette est un type de cube (1 herbe, 2 terre, 3 pierre, 4 eau,
   5 planches, 6 caisse) ; la hauteur est limitée à 16.
2. `demo/zone/cubes.txt` donne les trois faces de chaque type, prises
   dans les images 32 × 32 de `demo/assets/` (`NOM.top`, `NOM.left`,
   `NOM.right`). Une face peut venir d'un autre cube. Les images se
   dessinent dans n'importe quel éditeur en 256 couleurs, avec la palette
   commune ; seul l'hexagone du cube compte, et un pixel d'une face ne
   doit pas valoir 0.
3. `make` reconvertit la zone ; regarder `build/apercu_demo.bmp`, ou
   `make run`.

`tools/demozone.py` a écrit la zone de départ ; `tools/cubes2bmp.py` a
dessiné les cubes.

## Le moteur

- `tools/dds32conv.c` (sur Mac) : lit le `.vox`, `cubes.txt` et les
  images ; découpe les faces en triangles de 16 × 16 ; calcule la face
  visible de chaque triangle de l'image et sa profondeur
  (`dds32/visibility.c`) ; écrit le tout en C, et l'aperçu.
- `dds32/iso.h` : le treillis de triangles. `dds32/zone.h` : ce que lit
  la console.
- `dds32/render.c` : une ligne d'écran. Dans chaque colonne de 16 pixels,
  la ligne est le OU de deux rangées de triangles (parts
  complémentaires, rangées sur 16 octets) ; puis la tranche de l'objet
  mobile par la zone overwrite, et par-dessus les triangles du décor qui
  sont devant lui.
- `dds32/engine.c` : le Master dessine les lignes paires, le Slave les
  impaires ; l'écran entier est redessiné à chaque image. Caméra à une
  abscisse impaire : affichage décalé d'un pixel (SFT, en V_INT).
- `demo/md68k.c` : le jeu (68000) ; position de la caisse et de la caméra
  dans les registres COMM 3 à 7.

## Mesurer : `VDP_STATS=1`

Toutes les 5 secondes dans la fenêtre, et à chaque capture sans fenêtre :
écritures dans le frame buffer (comptées exactement), accès au bus
rapportés au plafond de 76 800 écritures par image, cycles CPU estimés
par le modèle de coûts (`dds32/cost.h`), étalonné par le banc
`tools/sh2cycles` (le vrai code compilé pour SH2, exécuté sous qemu avec
un modèle de temps du SH2 ; voir `tools/sh2cycles/README.md`). Budget :
environ 384 000 cycles SH2 par image et par CPU.

## Vérifier

`tests/run_tests.sh` (ou `make test`). Les deux premiers tests sont
compilés avec AddressSanitizer et UBSan : une lecture hors d'un tableau
échoue sur toutes les machines (`-DDDS32_SANITIZE=OFF` pour s'en passer).
1. `test_visibility` : la mosaïque contre un lancer de rayons, sur la zone
   de démo et 300 zones au hasard (ponts et surplombs en quantité).
2. `test_render` : le rendu des lignes contre un rendu de référence naïf
   (zone posée triangle par triangle, objet pixel par pixel), sur environ
   5 300 écrans ; chaque écriture vérifiée.
3. Le simulateur complet sans fenêtre : sept scénarios de manette (pont,
   arche, plateau, caméra à une abscisse impaire, interruptions en
   retard) ; l'image capturée doit être identique, octet pour octet, à
   l'image de référence.

## Organisation

| Dossier / fichier         | Rôle                                                    |
|---------------------------|---------------------------------------------------------|
| `ARCHITECTURE.md`         | la fiche : choix, données, budget, étapes               |
| `pixel/`                  | simulateur 32X : VDP, interface (COMM, V_INT), plates-formes fenêtre et sans fenêtre |
| `dds32/`                  | treillis, zone, visibilité, rendu, sprites, boucles des SH2, modèle de coûts |
| `demo/`                   | la zone, les images, le jeu (68000)                     |
| `tools/dds32conv.c`       | convertisseur MagicaVoxel → C, et aperçu                |
| `tools/bmp2c.c`           | sprites BMP → C                                         |
| `tools/*.py`              | dessin des cubes, zone de départ                        |
| `tests/`                  | tests                                                   |
| `tools/sh2cycles/`        | banc SH2 sous qemu : temps du code compilé, étalonnage de `cost.h` |

## Licence

DDS 32 est publié sous licence MIT. Copyright © 2026 Olivier BROSSERON.
Si vous utilisez DDS 32, merci de citer le projet et son auteur.
