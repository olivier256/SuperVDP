/* =========================================================================
   Programme 68000 (Mega Drive) de la démo : le jeu.

   À chaque VBlank, il lit la manette, déplace la caisse dans la zone
   (demo_move : sol, marches, eau, passages sous les arches et les ponts),
   place la caméra, et écrit les deux positions dans les registres COMM 3
   à 7 : pour lui, la 32X est une carte graphique. Les SH2 ne font que
   dessiner. Sur la console, la Mega Drive afficherait aussi le HUD sur son
   propre plan.

   Le 68000 n'est pas émulé : ce programme est en C, appelé par la
   plate-forme au début de chaque VBlank (voir pixel/game.h). Sur la
   console, ce serait du code 68000, qui lirait les voxels de la zone
   directement dans la cartouche.

   Commandes, dans les axes de la zone (comme dans Landstalker, une
   direction de la croix suit une diagonale de l'écran) :
     Droite : u + 1 (vers le bas à droite)   Gauche : u - 1
     Bas    : v + 1 (vers le bas à gauche)   Haut   : v - 1
     C maintenu : 4 unités par image au lieu de 1.
   ========================================================================= */

#include "demo.h"
#include "game.h"
#include "interface.h"
#include "platform.h"
#include "shared.h"

#define SPEED_SLOW 1 /* unités du monde par image */
#define SPEED_FAST 4
#define SCREEN_HEIGHT 224

static int started = 0;
static int u, v, w;

void md_vint(void) {
    if (!started) {
        u = DEMO_START_U;
        v = DEMO_START_V;
        w = demo_ground(u, v, 15 * 16);
        started = 1;
    }
    const unsigned pad = platform_read_pad();
    const int speed = (pad & PAD_C) ? SPEED_FAST : SPEED_SLOW;
    const int du = ((pad & PAD_RIGHT) ? 1 : 0) - ((pad & PAD_LEFT) ? 1 : 0);
    const int dv = ((pad & PAD_DOWN) ? 1 : 0) - ((pad & PAD_UP) ? 1 : 0);
    /* une unité à la fois : la caisse s'arrête au pied d'un obstacle,
       même à 4 unités par image */
    for (int s = 0; s < speed; s++) {
        if (du)
            demo_move(&u, &v, &w, du, 0);
        if (dv)
            demo_move(&u, &v, &w, 0, dv);
    }
    int cx, cy;
    demo_camera(u, v, w, SCREEN_HEIGHT, &cx, &cy);
    comm[COMM_CAMERA_X] = (uint16_t)cx;
    comm[COMM_CAMERA_Y] = (uint16_t)cy;
    comm[COMM_OBJECT_U] = (uint16_t)u;
    comm[COMM_OBJECT_V] = (uint16_t)v;
    comm[COMM_OBJECT_W] = (uint16_t)w;
}
