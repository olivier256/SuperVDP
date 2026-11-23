/* =========================================================================
   Test de render_line (dds32/render.c), hors simulateur.

   La référence ne partage rien avec le code testé :
   - l'image entière de la zone est posée triangle par triangle depuis la
     mosaïque, pixel par pixel ; on retient aussi, pour chaque pixel, le
     triangle qui le couvre ;
   - l'objet mobile est posé pixel par pixel par-dessus, sauf là où le
     triangle du pixel est devant lui (sa profondeur : voir zone.h) ;
   - l'écran est la fenêtre de 320 x 224 sous la caméra ; hors de la zone,
     le fond.
   (Que la mosaïque montre la bonne face de chaque voxel est vérifié par
   tests/test_visibility.)

   On dessine chaque ligne comme les deux CPU (paires, puis impaires), et
   l'on compare les 320 pixels affichés, décalage d'écran compris. Chaque
   écriture est vérifiée : 16 bits, adresse paire, dans la ligne en cours,
   jamais un mot nul par la zone overwrite, et chaque mot du décor écrit
   une seule fois avant l'objet.

   Jeux d'essai : la caméra sur toute la zone (abscisses paires et
   impaires, au-delà des bords), sans objet ; puis 3 000 images de la
   caisse promenée au hasard dans la zone (demo_move), caméra qui suit, et
   500 images de la caisse posée n'importe où, caméra n'importe où.

   Autre usage : écrire l'image de référence d'un écran, pour la comparer
   à une capture du simulateur (tests/run_tests.sh) :
     test_render --ppm CAMERA_X CAMERA_Y U V W fichier.ppm
   ========================================================================= */

#include "demo.h"
#include "framebuffer.h"
#include "iso.h"
#include "render.h"
#include "vdp.h"
#include "zone.h"

#include "demo_assets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_HEIGHT 224
#define MAX_ZONE_PIXELS (1024 * 1024)

/* ---- Frame buffer de test ------------------------------------------------ */

static uint8_t fb[VDP_DRAM_SIZE];
static int currentLine = 0;
static int decorWrites[FB_PITCH / 2]; /* écritures de chaque mot (décor) */
static long writeCount = 0, badWrites = 0;

static void checkWrite(uint32_t offset, int overwrite, uint16_t pixels) {
    writeCount++;
    const uint32_t line = fb_offset(0, currentLine);
    if ((offset & 1) || offset < line || offset + 2 > line + FB_PITCH ||
        (overwrite && pixels == 0)) {
        if (badWrites++ < 5)
            fprintf(stderr, "ecriture interdite : offset %u (ligne %d)\n",
                    offset, currentLine);
        return;
    }
    if (!overwrite)
        decorWrites[(offset - line) / 2]++;
}

void vdp_dram_write16(uint32_t offset, uint16_t pixels) {
    checkWrite(offset, 0, pixels);
    fb[offset] = (uint8_t)(pixels >> 8);
    fb[offset + 1] = (uint8_t)pixels;
}

void vdp_overwrite_write16(uint32_t offset, uint16_t pixels) {
    checkWrite(offset, 1, pixels);
    if (pixels >> 8)
        fb[offset] = (uint8_t)(pixels >> 8);
    if (pixels & 0xFF)
        fb[offset + 1] = (uint8_t)pixels;
}

/* Non utilisées par le moteur */
void vdp_dram_write8(uint32_t o, uint8_t p) { (void)o, (void)p, badWrites++; }
void vdp_dram_write32(uint32_t o, uint32_t p) { (void)o, (void)p, badWrites++; }
void vdp_overwrite_write8(uint32_t o, uint8_t p) {
    (void)o, (void)p, badWrites++;
}
void vdp_overwrite_write32(uint32_t o, uint32_t p) {
    (void)o, (void)p, badWrites++;
}

/* ---- Référence ----------------------------------------------------------- */

static uint8_t zoneImage[MAX_ZONE_PIXELS];
static int32_t zoneOwner[MAX_ZONE_PIXELS]; /* triangle du pixel, -1 : aucun */

static void buildZoneImage(const Zone *z) {
    const int n = z->pixelWidth * z->pixelHeight;
    memset(zoneImage, DEMO_BACKGROUND, (size_t)n);
    for (int p = 0; p < n; p++)
        zoneOwner[p] = -1;
    /* toutes les positions, fond compris (triangles 0 et 1), depuis la
       rangée t = -1 */
    for (int t = -1; t < z->rows; t++)
        for (int c = 0; c < z->cols; c++) {
            const uint16_t m = z->mosaic[t * z->cols + c];
            /* rangée r : 16 octets à la rangée r de la banque, pixels à
               leur place dans la colonne ; largeurs 2, 4, ... 16, 14, ... 2 */
            for (int r = 0; r < 15; r++) {
                const int width = r < 8 ? 2 * r + 2 : 30 - 2 * r;
                const int pointsLeft = ((c + t) % 2) == 0;
                const int start = pointsLeft ? 16 - width : 0;
                const uint8_t *px =
                    z->triangles + ((r * z->triangleCount + m) * 16);
                const int y = 8 * t + r;
                for (int x = start; x < start + width; x++) {
                    if (y < 0 || y >= z->pixelHeight ||
                        16 * c + x >= z->pixelWidth)
                        continue;
                    zoneImage[y * z->pixelWidth + 16 * c + x] = px[x];
                    zoneOwner[y * z->pixelWidth + 16 * c + x] = t * z->cols + c;
                }
            }
        }
}

static uint8_t ref[SCREEN_HEIGHT][FB_WIDTH];

static void referenceScreen(const Zone *z, int camX, int camY, const Sprite *s,
                            int u, int v, int w) {
    const int ox = z->originX + u - v, oy = z->originY + (u + v) / 2 - w;
    for (int y = 0; y < SCREEN_HEIGHT; y++)
        for (int x = 0; x < FB_WIDTH; x++) {
            const int zx = camX + x, zy = camY + y;
            uint8_t p = DEMO_BACKGROUND;
            int owner = -1;
            if (zx >= 0 && zy >= 0 && zx < z->pixelWidth &&
                zy < z->pixelHeight) {
                p = zoneImage[zy * z->pixelWidth + zx];
                owner = zoneOwner[zy * z->pixelWidth + zx];
            }
            if (s != NULL && zx >= ox && zx < ox + s->width && zy >= oy &&
                zy < oy + s->height) {
                const uint8_t q = s->pixels[(zy - oy) * s->width + zx - ox];
                /* le triangle cache-t-il l'objet ? ordre u + v + w, puis w */
                int front = 0;
                if (owner >= 0) {
                    const int d = z->depth[owner];
                    const int sum = (d >> 4) * 16, k16 = (d & 15) * 16;
                    front = sum != u + v + w ? sum > u + v + w : k16 > w;
                }
                if (q != 0 && !front)
                    p = q;
            }
            ref[y][x] = p;
        }
}

/* ---- Rendu et comparaison --------------------------------------------- */

static long screens = 0, failures = 0;

static void renderAndCompare(const Zone *z, int camX, int camY, const Sprite *s,
                             int u, int v, int w, const char *what) {
    RenderView rv;
    render_view(&rv, z, camX, camY, DEMO_BACKGROUND, s, u, v, w);
    referenceScreen(z, camX, camY, s, u, v, w);
    const int shift = render_shift(camX);
    for (int cpu = 0; cpu < 2; cpu++)
        for (int y = cpu; y < SCREEN_HEIGHT; y += 2) {
            currentLine = y;
            memset(decorWrites, 0, sizeof decorWrites);
            memset(fb + fb_offset(0, y), 0xEE, FB_PITCH);
            DrawCost cost = {0};
            render_line(&rv, y, &cost);
            for (int x = 0; x < FB_PITCH / 2; x++)
                if (decorWrites[x] < 1 && badWrites++ < 5)
                    fprintf(stderr, "%s : ligne %d, mot %d jamais ecrit\n",
                            what, y, x);
            for (int x = 0; x < FB_WIDTH; x++)
                if (fb[fb_offset(x + shift, y)] != ref[y][x]) {
                    if (failures++ < 5)
                        fprintf(stderr,
                                "%s : camera (%d, %d), objet (%d, %d, %d) : "
                                "pixel (%d, %d) = %d, attendu %d\n",
                                what, camX, camY, u, v, w, x, y,
                                fb[fb_offset(x + shift, y)], ref[y][x]);
                    break;
                }
        }
    screens++;
}

static unsigned rng = 12345;
static unsigned nextRandom(void) {
    rng = rng * 1103515245u + 12345u;
    return rng >> 16;
}

/* ---- Image de référence pour run_tests.sh ------------------------------- */

static int writeReferencePPM(int camX, int camY, int u, int v, int w,
                             const char *path) {
    const Zone *z = demo_zone();
    buildZoneImage(z);
    referenceScreen(z, camX, camY, demo_object_sprite(), u, v, w);
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        perror(path);
        return 1;
    }
    fprintf(f, "P6\n%d %d\n255\n", FB_WIDTH, SCREEN_HEIGHT);
    for (int y = 0; y < SCREEN_HEIGHT; y++)
        for (int x = 0; x < FB_WIDTH; x++) {
            const uint16_t c = asset_palette[ref[y][x]];
            const uint8_t r = c & 0x1F, g = (c >> 5) & 0x1F,
                          b = (c >> 10) & 0x1F;
            const uint8_t rgb[3] = {(uint8_t)((r << 3) | (r >> 2)),
                                    (uint8_t)((g << 3) | (g >> 2)),
                                    (uint8_t)((b << 3) | (b >> 2))};
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc == 8 && strcmp(argv[1], "--ppm") == 0)
        return writeReferencePPM(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]),
                                 atoi(argv[5]), atoi(argv[6]), argv[7]);
    if (argc != 1) {
        fprintf(stderr, "usage : test_render [--ppm CAMERA_X CAMERA_Y U V W "
                        "fichier.ppm]\n");
        return 2;
    }

    const Zone *z = demo_zone();
    if (z->pixelWidth * z->pixelHeight > MAX_ZONE_PIXELS) {
        fprintf(stderr, "zone trop grande pour le test\n");
        return 2;
    }
    buildZoneImage(z);
    const Sprite *crate = demo_object_sprite();

    /* 1. La caméra partout, sans objet */
    long n1 = 0;
    for (int camY = -40; camY <= z->pixelHeight - SCREEN_HEIGHT + 40;
         camY += 13)
        for (int camX = -41; camX <= z->pixelWidth - FB_WIDTH + 41; camX += 7) {
            renderAndCompare(z, camX, camY, NULL, 0, 0, 0, "decor");
            n1++;
        }

    /* 2. La caisse promenée, caméra qui suit */
    int u = DEMO_START_U, v = DEMO_START_V, w = demo_ground(u, v, 15 * 16);
    int du = 0, dv = 0, speed = 1, hold = 0;
    for (int f = 0; f < 3000; f++) {
        if (hold-- <= 0) {
            const unsigned r = nextRandom();
            du = (int)(r % 3) - 1;
            dv = (int)((r / 3) % 3) - 1;
            speed = (r / 9) % 4 == 0 ? 4 : 1;
            hold = 5 + (int)(nextRandom() % 60);
        }
        for (int s = 0; s < speed; s++) {
            if (du)
                demo_move(&u, &v, &w, du, 0);
            if (dv)
                demo_move(&u, &v, &w, 0, dv);
        }
        int cx, cy;
        demo_camera(u, v, w, SCREEN_HEIGHT, &cx, &cy);
        renderAndCompare(z, cx, cy, crate, u, v, w, "promenade");
    }

    /* 3. La caisse n'importe où, caméra n'importe où autour d'elle */
    for (int f = 0; f < 500; f++) {
        const int uu = (int)(nextRandom() % (unsigned)(16 * z->width));
        const int vv = (int)(nextRandom() % (unsigned)(16 * z->depthCells));
        const int ww = (int)(nextRandom() % (unsigned)(16 * z->height));
        int cx, cy;
        demo_camera(uu, vv, ww, SCREEN_HEIGHT, &cx, &cy);
        cx += (int)(nextRandom() % 101) - 50;
        cy += (int)(nextRandom() % 61) - 30;
        renderAndCompare(z, cx, cy, crate, uu, vv, ww, "au hasard");
    }

    printf("rendu : %ld ecrans verifies (%ld sans objet), %ld echecs, %ld "
           "ecritures dont %ld interdites\n",
           screens, n1, failures, writeCount, badWrites);
    return failures != 0 || badWrites != 0;
}
