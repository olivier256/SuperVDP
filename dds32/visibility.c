/* =========================================================================
   DDS 32 : triangles visibles (voir visibility.h).
   ========================================================================= */

#include "visibility.h"

#include "iso.h"
#include "zone.h"

static int solid(const VisibilityInput *in, int i, int j, int k) {
    if (i < 0 || j < 0 || k < 0 || i >= in->width || j >= in->depthCells ||
        k >= in->height)
        return 0;
    return in->voxels[(k * in->depthCells + j) * in->width + i] != 0;
}

void visibility_build(const VisibilityInput *in, uint16_t *mosaic,
                      uint16_t *depth) {
    for (int n = 0; n < in->cols * in->rows; n++)
        mosaic[n] = depth[n] = 0;

    const int maxSum = in->width + in->depthCells + in->height - 3;
    for (int s = 0; s <= maxSum; s++)
        for (int k = 0; k < in->height; k++)
            for (int i = 0; i < in->width; i++) {
                const int j = s - i - k;
                if (j < 0 || j >= in->depthCells)
                    continue;
                const uint8_t type =
                    in->voxels[(k * in->depthCells + j) * in->width + i];
                if (type == 0)
                    continue;
                const int hidden[ISO_FACES] = {
                    solid(in, i, j, k + 1), /* dessus */
                    solid(in, i, j + 1, k), /* face gauche */
                    solid(in, i + 1, j, k), /* face droite */
                };
                const int c0 = (ISO_TRI_W * (i - j) + in->originX) / ISO_TRI_W;
                const int t0 =
                    (8 * (i + j) - ISO_UNITS * k + in->originY) / ISO_ROW_PITCH;
                for (int slot = 0; slot < 6; slot++) {
                    if (hidden[iso_cube_slots[slot].face])
                        continue;
                    const int c = c0 + iso_cube_slots[slot].dc;
                    const int t = t0 + iso_cube_slots[slot].dt;
                    if (c < 0 || c >= in->cols || t < 0 || t >= in->rows)
                        continue;
                    mosaic[t * in->cols + c] = in->typeSlots[type][slot];
                    depth[t * in->cols + c] = zone_voxel_depth(i, j, k);
                }
            }
}
