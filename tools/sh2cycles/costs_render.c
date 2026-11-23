/* =========================================================================
   Banc sh2cycles, côté hôte : pour chaque image de render_frames.h et les
   lignes d'un CPU, les compteurs du modèle (dds32/cost.h) et l'empreinte
   du frame buffer, calculée comme le programme SH2 (bench_render.c) :
   zone normale et zone overwrite séparées, mots rangés tels quels.

   Usage : costs_render PREMIERE_LIGNE      (0 = Master, 1 = Slave)
   Sortie (stdout), une ligne par image :
     image;lines;columns;partials;copies;fronts;segments;rows;words;edges;
     modele;empreinte;ecritures
   ========================================================================= */

#include "framebuffer.h"
#include "render.h"
#include "vdp.h"

#include "demo.h"
#include "render_frames.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t fb[2 * VDP_DRAM_SIZE];
static unsigned writes;

static void store(uint32_t a, uint16_t pixels) {
    fb[a] = (uint8_t)(pixels >> 8);
    fb[a + 1] = (uint8_t)pixels;
    writes++;
}
void vdp_dram_write16(uint32_t offset, uint16_t pixels) {
    store(offset, pixels);
}
void vdp_overwrite_write16(uint32_t offset, uint16_t pixels) {
    store(VDP_DRAM_SIZE + offset, pixels);
}

static uint32_t fingerprint(int first) {
    uint32_t h = 2166136261u;
    for (int y = first; y < RENDER_BENCH_SCREEN_HEIGHT; y += 2)
        for (int x = 0; x < FB_PITCH; x++)
            for (int zone = 0; zone < 2; zone++) {
                const uint32_t a = zone * VDP_DRAM_SIZE + fb_offset(x, y);
                h = (h ^ fb[a]) * 16777619u;
                fb[a] = 0;
            }
    return h;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage : costs_render PREMIERE_LIGNE\n");
        return 2;
    }
    const int first = atoi(argv[1]);
    RenderBench bench;
    render_bench_init(&bench);
    for (int i = 0; i < RENDER_BENCH_FRAMES; i++) {
        const RenderBenchFrame f = render_bench_frame(&bench, i);
        RenderView rv;
        DrawCost c;
        memset(&c, 0, sizeof c);
        render_view(&rv, demo_zone(), f.cameraX, f.cameraY, DEMO_BACKGROUND,
                    demo_object_sprite(), f.u, f.v, f.w);
        writes = 0;
        for (int y = first; y < RENDER_BENCH_SCREEN_HEIGHT; y += 2)
            render_line(&rv, y, &c);
        printf("%d;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%08x;%u\n", i, c.lines,
               c.columns, c.partials, c.copies, c.fronts, c.segments, c.rows,
               c.words, c.edges, cost_cycles(&c), fingerprint(first), writes);
    }
    return 0;
}
