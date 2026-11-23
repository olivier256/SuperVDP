# DDS 32 : fiche d'architecture

Un jeu en 3D isométrique façon Landstalker sur la Sega 32X, dans un monde
quasi ouvert, sans tramage. Cette fiche résume les choix arrêtés ensemble
avant le premier patch, et ce que la preuve de concept en a vérifié.

## Le principe : faire comme le DDS520, avec ce qu'apporte la 32X

Landstalker ne dessine jamais un cube : ses outils (le DDS520 et
« Mirage Type III », sur PC-98) aplatissent le monde 3D en tiles que le
VDP de la Mega Drive affiche par le matériel. DDS 32 fait de même :

- **le travail 3D se fait sur Mac**, par le convertisseur `dds32conv`
  (notre « Mirage Type III ») : pour chaque zone, il calcule quelle face
  de quel voxel montre chaque triangle de l'image, et sa profondeur ;
- **les SH2 copient** ces triangles et dessinent les objets mobiles ;
- **le 68000 fait le jeu** : il lit les voxels en ROM (collisions), place
  les objets et la caméra, les écrit dans les registres COMM ; la Mega
  Drive garde le HUD sur son propre plan.

Ce que la 32X apporte par rapport au VDP de la Mega Drive : 256 couleurs
(aucun tramage), bien plus de place pour les graphismes (SDRAM et ROM au
lieu de 64 Ko de VRAM), des recouvrements exacts au triangle près (au lieu
d'un bit de priorité par tile 8 x 8 et de deux couches), et pas de limite
de sprites par ligne.

## Le monde : des voxels

- Une zone : W x D cases, H niveaux (H <= 16), chaque emplacement vide ou
  plein d'un **type de cube** (1 à 255). Ponts, arches, surplombs,
  tunnels : tout est permis.
- Repère du DDS520 (relevé dans liblandstalker, github.com/lordmir) :
  une case est un losange de 32 x 16 pixels, un niveau fait 16 pixels ;
  16 unités du monde par case.
- Édition dans **MagicaVoxel** (gratuit, Mac) : le numéro de couleur d'un
  voxel est son type de cube. Un fichier texte (`cubes.txt`) donne, pour
  chaque type, ses trois faces (dessus, gauche, droite), chacune prise
  dans une image de cube 32 x 32. Une face peut venir d'un autre cube :
  c'est ainsi qu'on varie.
- Zones d'environ 300 à 600 cases au sol pour commencer (une zone de
  Landstalker en compte 400 à 600 avec 1 000 à 1 500 voxels pleins), un
  monde plus continu ensuite.

## L'image : une mosaïque de triangles

L'écran se découpe en triangles de 16 x 16 pixels (`dds32/iso.h`) :
colonnes de 16 pixels, un côté vertical, une pointe ; 128 pixels chacun,
largeurs des rangées 2, 4, ... 16, 14, ... 2. Un cube en couvre six
(deux par face visible). Chaque triangle de l'écran montre **un seul**
morceau de face : celui du voxel le plus proche. Le convertisseur le
calcule (`dds32/visibility.c`, C portable, vérifié contre un lancer de
rayons) ; la console n'a plus qu'à copier.

Dans une colonne, une ligne de pixels est partagée entre deux triangles,
aux parts complémentaires. Les rangées des triangles sont rangées sur
16 octets, chaque pixel à sa place dans la colonne et des 0 ailleurs :
**la ligne de la colonne est le OU des deux rangées**. Une colonne coûte
deux lectures dans la mosaïque, huit lectures 32 bits, quatre OU, huit
écritures 16 bits, sans aucun test.

## Les objets mobiles

Le héros, les PNJ, les ennemis : des sprites avec transparence, dessinés
par la zone overwrite du VDP. Après chaque tranche d'un objet, les
morceaux des triangles du décor qui sont **devant lui** (profondeur
u + v + w, puis w) sont redessinés par-dessus, eux aussi par la zone
overwrite : leurs 0 n'effacent rien. Le décor cache l'objet au triangle
près.

## Les données

| Où | Quoi |
|----|------|
| ROM (cartouche) | voxels de chaque zone (collisions du 68000), mosaïque et profondeurs (2 + 2 octets par triangle), triangles de pixels |
| SDRAM (256 Ko) | les triangles de pixels de la zone courante (à faire : pour l'instant, lus depuis les données du programme), la scène partagée |
| Frame buffer | deux images, redessinées entièrement à chaque image |

Zone de démo : 24 x 20 x 8, 1 097 voxels, image de 720 x 480, 2 700
triangles (dont 1 293 visibles), 36 triangles de pixels différents.

## Le partage du travail entre les SH2

Le Master dessine les lignes paires, le Slave les impaires : chacun
possède entièrement ses lignes. La caméra peut être à une abscisse
impaire : on dessine depuis l'abscisse paire d'avant, et l'affichage est
décalé d'un pixel (SFT, écrit par le Master en V_INT).

## Le budget (mesuré)

Banc `tools/sh2cycles` (code SH2 compilé, modèle de temps du SH2), caméra
qui suit la caisse dans la zone :

| | cycles par CPU | part du budget (384 000) |
|-|---------------:|------------------------:|
| écran entier + caisse, hors écritures | 214 000 à 245 000 | 56 à 64 % |
| avec les écritures (18 000 à 18 500 par CPU, 3 à 5 cycles) | | **78 à 88 %** |
| bus du VDP (les deux CPU) | 36 500 écritures | 47,5 % |

L'écran entier est redessiné **à chaque image**, à 60 images/s : pas
besoin, pour l'instant, de frame buffer en anneau ni de bandes de
défilement. Il reste 12 à 22 % par CPU pour les autres objets mobiles.

## Les étapes

1. **Preuve de concept** (ce patch) : convertisseur, zone de démo,
   rendu de la mosaïque, une caisse, caméra qui suit, mesures.
2. Plusieurs objets mobiles, triés ; ordre exact d'un objet entre deux
   cases ; mesure du nombre d'objets à 60 images/s.
3. Les triangles de la zone copiés en SDRAM à l'entrée de la zone ; la
   palette (192 couleurs pour le décor de la zone, 64 pour les
   personnages, communes au jeu) ; les faces prises dans de grands dessins
   (`maison.bmp@1,0`).
4. Plusieurs zones, transitions (fondu depuis le noir, 2 à 3 images pour
   l'affichage d'une zone).
5. Le décor qui change en cours de jeu (porte, bloc poussé) : les SH2
   recalculent la visibilité d'une petite région (`visibility.c`).
6. Si le budget manque : défilement par la table de lignes, assembleur
   SH2 pour la boucle des colonnes, DMA.
