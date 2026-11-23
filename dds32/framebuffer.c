#include "framebuffer.h"

#include <stdint.h>

/* Seule écriture directe par vdp_dram, et non comptée : le simulateur
   relit la table de lignes comme des mots de l'hôte (voir bigendian.h),
   alors que vdp_dram_write16() range les octets dans l'ordre du SH2.
   Elle n'est faite qu'une fois, à l'initialisation. */
void fb_init_line_table(int screenHeight) {
    for (int y = 0; y < screenHeight; y++)
        vdp_dram[y] = (uint16_t)((FB_LINE_TABLE_BYTES + y * FB_PITCH) / 2);
}
