#ifndef RENDER_FRAMES_H
#define RENDER_FRAMES_H

/* =========================================================================
   Images mesurées par le banc (DDS 32) : la même suite pour le programme
   SH2 (bench_render.c, temps mesuré) et pour le programme hôte
   (costs_render.c, compteurs du modèle). Reproductible, sans hasard.

   Image i : la caisse promenée dans la zone de la démo (demo_move), la
   caméra qui la suit (demo_camera) ; une image sur trois, la caisse et la
   caméra tirées n'importe où (y compris hors de la zone, abscisse impaire),
   pour que les compteurs du modèle varient assez indépendamment.
   ========================================================================= */

#include "demo.h"

#define RENDER_BENCH_FRAMES 120
#define RENDER_BENCH_SCREEN_HEIGHT 224

typedef struct {
    int u, v, w, du, dv, speed, hold;
    unsigned rng;
} RenderBench;

typedef struct {
    int cameraX, cameraY, u, v, w;
} RenderBenchFrame;

static inline unsigned render_bench_next(RenderBench *b) {
    b->rng = b->rng * 1103515245u + 12345u;
    return b->rng >> 16;
}

static inline void render_bench_init(RenderBench *b) {
    b->u = DEMO_START_U;
    b->v = DEMO_START_V;
    b->w = demo_ground(b->u, b->v, 15 * 16);
    b->du = b->dv = 0;
    b->speed = 1;
    b->hold = 0;
    b->rng = 2026u;
}

static inline RenderBenchFrame render_bench_frame(RenderBench *b, int i) {
    RenderBenchFrame f;
    if (b->hold-- <= 0) {
        const unsigned r = render_bench_next(b);
        b->du = (int)(r % 3) - 1;
        b->dv = (int)((r / 3) % 3) - 1;
        b->speed = (r / 9) % 3 == 0 ? 4 : 1;
        b->hold = 3 + (int)((r / 27) % 10);
    }
    for (int s = 0; s < b->speed; s++) {
        if (b->du)
            demo_move(&b->u, &b->v, &b->w, b->du, 0);
        if (b->dv)
            demo_move(&b->u, &b->v, &b->w, 0, b->dv);
    }
    f.u = b->u;
    f.v = b->v;
    f.w = b->w;
    demo_camera(f.u, f.v, f.w, RENDER_BENCH_SCREEN_HEIGHT, &f.cameraX,
                &f.cameraY);
    if (i % 3 == 2) {
        const Zone *z = demo_zone();
        f.u = (int)(render_bench_next(b) % (unsigned)(16 * z->width));
        f.v = (int)(render_bench_next(b) % (unsigned)(16 * z->depthCells));
        f.w = (int)(render_bench_next(b) % (unsigned)(16 * z->height));
        f.cameraX =
            (int)(render_bench_next(b) % (unsigned)(z->pixelWidth)) - 160;
        f.cameraY =
            (int)(render_bench_next(b) % (unsigned)(z->pixelHeight)) - 112;
    }
    return f;
}

#endif
