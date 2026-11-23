#ifndef ZONE_H
#define ZONE_H

#include <stdint.h>

#include "iso.h"

/* =========================================================================
   DDS 32 : une zone du monde, telle que la produit le convertisseur
   (tools/dds32conv) et telle que la lit la console.

   Pour le jeu (68000) : les voxels, W x D x H octets, rangés k par k,
   puis j, puis i : voxels[(k * D + j) * W + i] ; 0 = vide, sinon le type
   de cube (1 à 255).

   Pour l'affichage (SH2) : l'image de la zone, découpée en triangles
   (voir iso.h), cols x rows triangles, plus une rangée t = -1 au-dessus
   (pour la ligne 0 à 7 de l'image) :
   - mosaic[t * cols + c], t de -1 à rows - 1 : le numéro du triangle de
     pixels dans 'triangles'. Les triangles 0 et 1 sont le fond (index
     ZONE_BACKGROUND), côté vertical à droite (0) ou à gauche (1) : là où
     rien n'est visible, la mosaïque désigne celui des deux qui a le bon
     sens ;
   - depth[t * cols + c] : la profondeur du voxel visible, (i + j + k) x 16
     + k, pour savoir si un objet mobile passe devant ou derrière ; 0 pour
     le fond (jamais devant) ;
   - triangles : les triangles de pixels, sans doublon, rangés RANGÉE PAR
     RANGÉE : la rangée r (0 à 14) du triangle n (0 à triangleCount - 1)
     occupe les 16 octets zone_triangle_row(z, n, r), chaque pixel à sa
     place dans la colonne de 16 pixels, et des 0 ailleurs. Dans une
     colonne, une ligne de la zone est partagée entre deux triangles dont
     les parts sont complémentaires : la ligne de la colonne est le OU de
     leurs deux rangées. Une ligne d'écran ne lit que deux rangées par
     colonne (r et r + 8) : 16 octets par triangle, une ligne du cache du
     SH2. Un pixel d'une face ne doit donc jamais valoir 0.

   Position dans l'image de la zone du coin haut gauche du sprite du
   voxel (i, j, k) : x = 16 (i - j) + originX, y = 8 (i + j) - 16 k + originY.
   originX et originY placent toute la zone en coordonnées positives, et
   originX / 16 + originY / 8 est pair (le cube est aligné sur le
   treillis : voir iso.h).
   ========================================================================= */

typedef struct {
    uint16_t width, depthCells, height; /* W, D, H (cases, niveaux) */
    const uint8_t *voxels;
    uint16_t cols, rows; /* triangles */
    uint16_t pixelWidth, pixelHeight;
    int16_t originX, originY;
    const uint16_t *mosaic; /* rangée t = 0 ; la rangée -1 est juste avant */
    const uint16_t *depth;
    uint16_t triangleCount;
    const uint8_t *triangles; /* 15 x triangleCount x 16 octets */
} Zone;

#define ZONE_TRIANGLE_ROWS 15 /* la rangée 15 est vide */
#define ZONE_BACKGROUND 1     /* index de palette du fond (triangle 0) */

#define ZONE_FIRST_TRIANGLE 2 /* après les deux triangles du fond */

/* Les 16 octets de la rangée r du triangle n (n : valeur de la mosaïque) */
static inline const uint8_t *zone_triangle_row(const Zone *z, int n, int r) {
    return z->triangles + (((uint32_t)r * z->triangleCount + (uint32_t)n) << 4);
}

static inline uint8_t zone_voxel(const Zone *z, int i, int j, int k) {
    if (i < 0 || j < 0 || k < 0 || i >= z->width || j >= z->depthCells ||
        k >= z->height)
        return 0;
    return z->voxels[(k * z->depthCells + j) * z->width + i];
}

/* Profondeur d'un voxel, comparable à celle d'un objet (zone_object_depth) */
static inline uint16_t zone_voxel_depth(int i, int j, int k) {
    return (uint16_t)((i + j + k) * ISO_UNITS + k);
}

/* Un objet posé en (u, v, w) (unités du monde) est dessiné après les
   voxels de profondeur inférieure ou égale à celle-ci : ordre par u + v + w,
   puis par w, comme les cubes entre eux. */
static inline int zone_voxel_after_object(uint16_t voxelDepth, int u, int v,
                                          int w) {
    const int sum = (voxelDepth >> 4) * ISO_UNITS;
    const int k = voxelDepth & 15;
    return sum != u + v + w ? sum > u + v + w : k * ISO_UNITS > w;
}

#endif
