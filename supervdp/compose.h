#ifndef COMPOSE_H
#define COMPOSE_H

#include <stdint.h>

#include "assets_types.h"
#include "cost.h"

/* =========================================================================
   Composition d'une ligne d'écran : plan A (devant, index 0 transparent)
   sur plan B (derrière, opaque).

   Chaque mot de la ligne (deux pixels) est écrit une seule fois, en
   16 bits, à une adresse paire. Aucun tampon de ligne : chaque pixel est
   lu une fois, dans le plan qui le montre (voir compose.c).
   Hypothèse bus32 (voir pixel/vdp.h) : écritures de 32 bits (4 pixels) à
   des adresses multiples de 4 ; décalage de 0 à 3, une ligne décalée
   faisant 260 pixels (65 mots de 32 bits) au lieu de 256 (64).

   Décalage d'écran (SFT, voir pixel/vdp.h) : compose_line choisit pour
   chaque ligne de la dessiner avec ou sans décalage d'un point, selon ce
   qui coûte le moins (rendre pair le défilement du plan qui fournit le
   plus de mots). Avec décalage, la ligne fait 322 pixels (161 mots) au
   lieu de 320 (160 mots), et elle ne s'affiche juste que si SFT vaut 1
   pendant qu'elle est balayée : c'est à l'appelant de le faire (routine
   H_INT du Master, voir supervdp.c).
   ========================================================================= */

/* Compose la ligne qui commence à l'offset 'offset' du frame buffer de
   dessin (voir fb_offset) ; l'écran doit montrer :
   - plan A : sa ligne worldAY, à partir de sa colonne scrollAX ;
   - plan B : sa ligne worldBY, à partir de sa colonne scrollBX.
   Les quatre valeurs sont quelconques (négatives, ou plus grandes que le
   plan) : elles sont ramenées dans le plan, qui boucle.
   Les deux plans utilisent le même jeu de tiles. 'cost' reçoit les
   compteurs du modèle de coûts (voir cost.h).
   Renvoie le décalage à appliquer à cette ligne (valeur du bit SFT) : 0,
   la ligne fait 160 mots ; 1, elle en fait 161. */
int compose_line(uint32_t offset, const Tileset *tiles, const Tilemap *planeA,
                 int scrollAX, int worldAY, const Tilemap *planeB, int scrollBX,
                 int worldBY, ComposeCost *cost);

#endif
