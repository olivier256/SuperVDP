/* =========================================================================
   Programme 68000 (Mega Drive) de la démo.

   À chaque VBlank, il lit la manette, fait avancer la vue et écrit les
   défilements dans les registres COMM 4 à 7 : pour lui, la 32X est un
   VDP. Les SH2 ne font que dessiner.

   Le 68000 n'est pas émulé : ce programme est en C, appelé par la
   plate-forme au début de chaque VBlank (voir pixel/game.h). Sur la
   console, ce serait du code 68000 : lecture du port $A10003 (TH haut
   puis TH bas), calculs, puis écritures en $A15120 + 2 x n.

   Parallaxe : les deux plans arrivent ensemble au bout.
   - Plan A : xA de 0 à 768 - 320 = 448.
   - Plan B : xB de 0 à 672 - 320 = 352.
   xB = xA x 352 / 448 = xA x 11 / 14, arrondi : quand A avance de
   14 pixels, B en avance de 11. La division (DIVU sur 68000) n'est faite
   qu'une fois par image.
   Hypothèse bus32 (écran de 256 de large) : xA de 0 à 512, xB de 0 à 416,
   xB = xA x 416 / 512, arrondi.

   Commandes : Gauche / Droite déplacent la vue (le décor défile dans
   l'autre sens) ; C maintenu : 4 pixels par image au lieu de 1.
   ========================================================================= */

#include "game.h"
#include "interface.h"
#include "platform.h"
#include "shared.h"
#include "vdp.h"

#include "demo_assets.h"

#define SCREEN_WIDTH VDP_SCREEN_WIDTH
#define SPEED_SLOW 1 /* pixels par image */
#define SPEED_FAST 4

static int scrollA = 0; /* position de la vue dans le plan A */

static int clamp(int v, int min, int max) {
    return v < min ? min : (v > max ? max : v);
}

void md_vint(void) {
    const int maxA = asset_plane_a.widthTiles * TILE_SIZE - SCREEN_WIDTH;
    const int maxB = asset_plane_b.widthTiles * TILE_SIZE - SCREEN_WIDTH;

    const unsigned pad = platform_read_pad();
    const int speed = (pad & PAD_C) ? SPEED_FAST : SPEED_SLOW;
    const int dx = ((pad & PAD_RIGHT) ? 1 : 0) - ((pad & PAD_LEFT) ? 1 : 0);

    scrollA = clamp(scrollA + dx * speed, 0, maxA);
    const int scrollB = (scrollA * maxB + maxA / 2) / maxA;

    comm[COMM_HSCROLL_A] = (uint16_t)scrollA;
    comm[COMM_HSCROLL_B] = (uint16_t)scrollB;
    comm[COMM_VSCROLL_A] = 0; /* les plans font la hauteur de l'écran */
    comm[COMM_VSCROLL_B] = 0;
}
