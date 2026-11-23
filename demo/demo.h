#ifndef DEMO_H
#define DEMO_H

#include <stdint.h>

#include "assets_types.h"
#include "zone.h"

/* =========================================================================
   Contenu de la démo, partagé par le moteur (SH2), le programme 68000 et
   les tests.
   ========================================================================= */

/* La zone (demo/zone/demo.vox, convertie par dds32conv) */
const Zone *demo_zone(void);

/* L'objet mobile : une caisse, conduite à la manette */
const Sprite *demo_object_sprite(void);
#define DEMO_START_U 48 /* case (3, 6), sur l'herbe */
#define DEMO_START_V 96

/* Palette et fond (index de palette) */
const uint16_t *demo_palette(void);
#define DEMO_BACKGROUND 1

/* --- Le jeu (68000) ------------------------------------------------------

   L'objet occupe le cube [u, u + 16[ x [v, v + 16[ x [w, w + 16[. Il
   repose sur le plus haut voxel plein sous lui, dans les cases qu'il
   couvre ; il monte d'un niveau au plus à la fois, tombe d'aussi haut
   qu'il faut, ne marche pas sur l'eau, et passe sous un voxel s'il y a la
   place (une arche, un pont). */

/* Hauteur (w) où l'objet repose s'il est en (u, v) avec les pieds en w ;
   -1 si l'endroit est interdit (eau, pas de place, marche trop haute) */
int demo_ground(int u, int v, int w);

/* Nouvelle position après un pas de (du, dv) unités, si c'est permis */
void demo_move(int *u, int *v, int *w, int du, int dv);

/* Caméra : l'objet au centre de l'écran, sans sortir de la zone */
void demo_camera(int u, int v, int w, int screenHeight, int *cameraX,
                 int *cameraY);

#endif
