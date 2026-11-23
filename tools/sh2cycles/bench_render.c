/* =========================================================================
   Banc sh2cycles, côté SH2 : render_line (dds32/render.c, dds32/sprite.c)
   pour les lignes d'un CPU, pour chaque image de render_frames.h. Le vrai
   code compilé pour SH2, sans les compteurs du modèle ; chaque image est
   marquée pour le greffon qemu.

   BENCH_FIRST_LINE : 0 pour le Master (lignes paires), 1 pour le Slave.

   Le frame buffer fait ici 256 Ko : la zone overwrite (2402 0000h) suit
   la zone normale (2400 0000h). qemu ne connaît pas la zone overwrite :
   les mots y sont rangés tels quels, octets nuls compris. Après chaque
   image, hors mesure, une empreinte des deux zones (lignes de ce CPU) est
   imprimée, puis elles sont remises à zéro ; le programme hôte
   (costs_render.c) la calcule de la même façon.
   ========================================================================= */

#include "framebuffer.h"
#include "render.h"
#include "vdp.h"

#include "demo.h"
#include "render_frames.h"

#ifndef BENCH_FIRST_LINE
#define BENCH_FIRST_LINE 0
#endif

/* Placés par bench.ld : 2000 0000h (marqueurs), 2400 0000h (frame buffer) */
volatile uint32_t bench_marker[4] __attribute__((section(".marker")));
volatile uint8_t bench_fb[2 * VDP_DRAM_SIZE] __attribute__((section(".fb")));

void sys_write(const char *text, int length);

static uint32_t fingerprint(void) {
    uint32_t h = 2166136261u;
    for (int y = BENCH_FIRST_LINE; y < RENDER_BENCH_SCREEN_HEIGHT; y += 2)
        for (int x = 0; x < FB_PITCH; x++)
            for (int zone = 0; zone < 2; zone++) {
                const uint32_t a = zone * VDP_DRAM_SIZE + fb_offset(x, y);
                h = (h ^ bench_fb[a]) * 16777619u;
                bench_fb[a] = 0;
            }
    return h;
}

static void printHex(uint32_t v) {
    static const char digits[] = "0123456789abcdef";
    char text[9];
    for (int i = 0; i < 8; i++)
        text[i] = digits[(v >> (28 - 4 * i)) & 15];
    text[8] = '\n';
    sys_write(text, 9);
}

static void draw(const RenderBenchFrame *f) {
    RenderView rv;
    DrawCost cost; /* non rempli : compteurs absents sur SH2 */
    render_view(&rv, demo_zone(), f->cameraX, f->cameraY, DEMO_BACKGROUND,
                demo_object_sprite(), f->u, f->v, f->w);
    for (int y = BENCH_FIRST_LINE; y < RENDER_BENCH_SCREEN_HEIGHT; y += 2)
        render_line(&rv, y, &cost);
}

int main(void) {
    RenderBench bench;
    render_bench_init(&bench);

    /* Une image hors mesure, pour remplir le cache comme en régime établi,
       puis la suite depuis le début (comme costs_render.c) */
    RenderBenchFrame f = render_bench_frame(&bench, 0);
    draw(&f);
    fingerprint();
    render_bench_init(&bench);

    for (int i = 0; i < RENDER_BENCH_FRAMES; i++) {
        f = render_bench_frame(&bench, i);
        bench_marker[0] = 1; /* début de la mesure */
        draw(&f);
        bench_marker[1] = 1; /* fin */
        printHex(fingerprint());
    }
    return 0;
}
