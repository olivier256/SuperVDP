/* =========================================================================
   Test de visibility_build (dds32/visibility.c) contre un lancer de rayons.

   La référence ne partage rien avec le code testé : pour le centre de
   chaque triangle de l'image, elle suit le rayon de vue à travers les
   voxels (méthode des « dalles » : le rayon entre dans chaque boîte par
   l'une de ses trois faces visibles), et garde le voxel touché le plus
   près du spectateur, et la face par laquelle le rayon y entre.

   Repère : le point du monde (u, v, w) (unités, 16 par case) s'affiche en
     x = originX + 16 + (u - v)      y = originY + 16 + (u + v) / 2 - w
   Tous les points d'un même pixel forment une droite de direction
   (1, 1, 1) ; le spectateur est du côté des grands u, v, w.

   Jeux d'essai : la zone de la démo, puis 300 zones tirées au hasard
   (2 à 10 cases de côté, 1 à 8 niveaux, voxels pleins au hasard : ponts,
   arches et surplombs en quantité). Pour chaque triangle : vide ou non,
   le voxel, la face.
   ========================================================================= */

#include "demo.h"
#include "iso.h"
#include "visibility.h"
#include "zone.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SIDE 24
#define MAX_H 8
#define MAX_TRI 8192

static long checked = 0, failures = 0;

/* Les triangles de pixels de chaque emplacement, numérotés pour qu'on
   retrouve le type et l'emplacement : type x 8 + emplacement + 1 */
static uint16_t typeSlots[256][6];

/* ---- Référence ------------------------------------------------------- */

typedef struct {
    int hit, i, j, k, face;
} Hit;

static Hit trace(const uint8_t *vox, int W, int D, int H, double A, double B) {
    /* u = s + A, v = s, w = s + A / 2 - B */
    Hit best = {0, 0, 0, 0, 0};
    double bestS = -1e30;
    for (int k = 0; k < H; k++)
        for (int j = 0; j < D; j++)
            for (int i = 0; i < W; i++) {
                if (!vox[(k * D + j) * W + i])
                    continue;
                const double lo[3] = {16.0 * i - A, 16.0 * j,
                                      16.0 * k - A / 2 + B};
                const double hi[3] = {lo[0] + 16, lo[1] + 16, lo[2] + 16};
                double sLo = lo[0], sHi = hi[0];
                int axis = 0;
                for (int a = 1; a < 3; a++) {
                    if (lo[a] > sLo)
                        sLo = lo[a];
                    if (hi[a] < sHi) {
                        sHi = hi[a];
                        axis = a;
                    }
                }
                if (sLo >= sHi || sHi <= bestS)
                    continue;
                bestS = sHi;
                best.hit = 1;
                best.i = i;
                best.j = j;
                best.k = k;
                /* entrée par u = max : face droite ; v : gauche ; w : dessus */
                best.face = axis == 0   ? ISO_FACE_RIGHT
                            : axis == 1 ? ISO_FACE_LEFT
                                        : ISO_FACE_TOP;
            }
    return best;
}

/* ---- Une zone --------------------------------------------------------- */

static void checkZone(const uint8_t *vox, int W, int D, int H,
                      const char *what) {
    const int pad = ((D - 1) & 1) ? ISO_TRI_W : 0;
    const int originX = ISO_TRI_W * (D - 1) + pad;
    const int originY = ISO_UNITS * (H - 1);
    const int cols = (ISO_TRI_W * (W + D) + pad) / ISO_TRI_W;
    const int rows = (8 * (W + D) + ISO_UNITS * H) / ISO_ROW_PITCH;
    static uint16_t mosaic[MAX_TRI], depth[MAX_TRI];
    if (cols * rows > MAX_TRI) {
        fprintf(stderr, "%s : zone trop grande pour le test\n", what);
        exit(2);
    }
    const VisibilityInput in = {W,    D,       H,       vox,      cols,
                                rows, originX, originY, typeSlots};
    visibility_build(&in, mosaic, depth);

    for (int t = 0; t < rows; t++)
        for (int c = 0; c < cols; c++) {
            /* centre du triangle : à un tiers de sa largeur depuis le côté
               vertical, à mi-hauteur */
            const double x = ISO_TRI_W * c +
                             (iso_points_left(c, t) ? 16 - 16.0 / 3 : 16.0 / 3);
            const double y = ISO_ROW_PITCH * t + 7.5;
            const Hit h =
                trace(vox, W, D, H, x - originX - 16, y - originY - 16);
            const uint16_t m = mosaic[t * cols + c];
            checked++;
            int ok;
            if (!h.hit)
                ok = (m == 0);
            else if (m == 0)
                ok = 0;
            else {
                const int slot = (m - 1) % 8, type = (m - 1) / 8;
                /* voxel retrouvé par la position du triangle et sa
                   profondeur */
                const uint16_t d = depth[t * cols + c];
                const int k = d & 15, sum = d >> 4;
                const int c0 = c - iso_cube_slots[slot].dc;
                const int t0 = t - iso_cube_slots[slot].dt;
                const int iMinusJ = (ISO_TRI_W * c0 - originX) / ISO_TRI_W;
                const int iPlusJ = sum - k;
                const int i = (iMinusJ + iPlusJ) / 2,
                          j = (iPlusJ - iMinusJ) / 2;
                ok =
                    iso_cube_slots[slot].face == h.face && i == h.i &&
                    j == h.j && k == h.k && type == vox[(k * D + j) * W + i] &&
                    8 * (i + j) - ISO_UNITS * k + originY == ISO_ROW_PITCH * t0;
            }
            if (!ok && failures++ < 5)
                fprintf(stderr,
                        "%s : triangle (%d, %d) : mosaique %u, rayon %s "
                        "(%d, %d, %d) face %d\n",
                        what, c, t, m, h.hit ? "touche" : "vide", h.i, h.j, h.k,
                        h.face);
        }
}

int main(void) {
    for (int type = 1; type < 32; type++)
        for (int slot = 0; slot < 6; slot++)
            typeSlots[type][slot] = (uint16_t)(type * 8 + slot + 1);

    /* La zone de la démo (types 1 à 6) */
    const Zone *z = demo_zone();
    checkZone(z->voxels, z->width, z->depthCells, z->height, "zone de la demo");
    const long demoChecked = checked;

    /* Zones au hasard */
    static uint8_t vox[MAX_SIDE * MAX_SIDE * MAX_H];
    unsigned rng = 2026;
    for (int n = 0; n < 300; n++) {
        rng = rng * 1103515245u + 12345u;
        const int W = 2 + (int)((rng >> 16) % 9);
        rng = rng * 1103515245u + 12345u;
        const int D = 2 + (int)((rng >> 16) % 9);
        rng = rng * 1103515245u + 12345u;
        const int H = 1 + (int)((rng >> 16) % 8);
        rng = rng * 1103515245u + 12345u;
        const unsigned density = 20 + (rng >> 16) % 60; /* pour cent */
        for (int v = 0; v < W * D * H; v++) {
            rng = rng * 1103515245u + 12345u;
            vox[v] = ((rng >> 16) % 100 < density)
                         ? (uint8_t)(1 + (rng >> 8) % 31)
                         : 0;
        }
        char what[64];
        snprintf(what, sizeof what, "zone %d (%d x %d x %d)", n, W, D, H);
        checkZone(vox, W, D, H, what);
    }
    printf("visibilite : %ld triangles verifies (%ld dans la zone de la "
           "demo, le reste dans 300 zones au hasard), %ld echecs\n",
           checked, demoChecked, failures);
    return failures != 0;
}
