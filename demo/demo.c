/* =========================================================================
   Démo : une zone de 24 x 20 cases (demo/zone/demo.vox) et une caisse.
   Le jeu proprement dit (68000) : voir demo.h.
   ========================================================================= */

#include "demo.h"

#include "demo_assets.h" /* généré par bmp2c depuis demo/assets/assets.txt */
#include "zone_demo.h"   /* généré par dds32conv depuis demo/zone/demo.vox */

#define TYPE_WATER 4 /* voir demo/zone/cubes.txt */
#define SCREEN_WIDTH 320

const Zone *demo_zone(void) { return &zone_demo; }
const Sprite *demo_object_sprite(void) { return &asset_crate; }
const uint16_t *demo_palette(void) { return asset_palette; }

int demo_ground(int u, int v, int w) {
    const Zone *z = &zone_demo;
    if (u < 0 || v < 0 || u > (z->width - 1) * ISO_UNITS ||
        v > (z->depthCells - 1) * ISO_UNITS)
        return -1;
    const int i0 = u / ISO_UNITS, i1 = (u + ISO_UNITS - 1) / ISO_UNITS;
    const int j0 = v / ISO_UNITS, j1 = (v + ISO_UNITS - 1) / ISO_UNITS;

    /* Le sol : le dessus le plus haut, une marche au plus au-dessus des
       pieds */
    int ground = -1, water = 0;
    for (int i = i0; i <= i1; i++)
        for (int j = j0; j <= j1; j++)
            for (int k = (w / ISO_UNITS); k >= 0; k--) {
                const uint8_t t = zone_voxel(z, i, j, k);
                if (t == 0)
                    continue;
                const int top = (k + 1) * ISO_UNITS;
                if (top > ground) {
                    ground = top;
                    water = (t == TYPE_WATER);
                } else if (top == ground && t == TYPE_WATER)
                    water = 1;
                break;
            }
    if (ground < 0 || water || ground > w + ISO_UNITS)
        return -1;

    /* De la place pour l'objet (un niveau) */
    const int k = ground / ISO_UNITS;
    for (int i = i0; i <= i1; i++)
        for (int j = j0; j <= j1; j++)
            if (zone_voxel(z, i, j, k) != 0)
                return -1;
    return ground;
}

void demo_move(int *u, int *v, int *w, int du, int dv) {
    const int g = demo_ground(*u + du, *v + dv, *w);
    if (g < 0)
        return;
    *u += du;
    *v += dv;
    *w = g;
}

void demo_camera(int u, int v, int w, int screenHeight, int *cameraX,
                 int *cameraY) {
    const Zone *z = &zone_demo;
    int x = z->originX + (u - v) + ISO_CUBE_SIZE / 2 - SCREEN_WIDTH / 2;
    int y =
        z->originY + ((u + v) >> 1) - w + ISO_CUBE_SIZE / 2 - screenHeight / 2;
    const int maxX = z->pixelWidth - SCREEN_WIDTH;
    const int maxY = z->pixelHeight - screenHeight;
    *cameraX = x < 0 ? 0 : (x > maxX ? maxX : x);
    *cameraY = y < 0 ? 0 : (y > maxY ? maxY : y);
}
