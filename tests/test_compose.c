/* =========================================================================
   Tests de compose_line, hors simulateur (pas de threads, pas de VBlank).

   La référence est un rendu naïf, pixel par pixel, qui ne partage rien
   avec le moteur : pixel (x, y) de l'écran = pixel du plan A s'il est non
   nul, sinon pixel du plan B. Pour chaque ligne composée, on vérifie :
   - les 320 pixels AFFICHÉS, contre la référence : compose_line renvoie
     le décalage d'écran de la ligne (SFT) ; avec décalage, le pixel x de
     l'écran est le pixel x + SFT de la ligne ;
   - les écritures : 160 par ligne (161 avec décalage), toutes en 16 bits,
     à des adresses paires, chaque mot de la ligne écrit exactement une
     fois, rien hors de la ligne.
   Hypothèse bus32 : 256 pixels affichés, décalage de 0 à 3 ; 64 écritures
   par ligne (65 avec décalage), toutes en 32 bits, à des adresses
   multiples de 4.

   Jeux d'essai :
   1. la trajectoire de la démo : xA de 0 à 448, xB synchronisé, toutes
      les lignes ;
   2. toutes les colonnes de départ de A (0 à 767), contre 40 valeurs de
      xB (les 32 premières, et des valeurs négatives ou au-delà du plan),
      sur des lignes et des défilements verticaux variés ;
   3. 300 000 lignes tirées au hasard (défilements de -3000 à 3000).

   Autre usage : écrire l'image de référence d'un écran entier, pour la
   comparer à une capture du simulateur (tests/run_tests.sh) :
     test_compose --ppm XA XB YA YB fichier.ppm
   ========================================================================= */

#include "compose.h"
#include "framebuffer.h"
#include "vdp.h"

#include "demo_assets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_HEIGHT 224
#define MAX_LINE_WORDS (FB_PITCH / 2)
#define WRITE_BYTES VDP_BUS_BYTES /* taille attendue des écritures */

static const Tileset *const tiles = &asset_tiles;
static const Tilemap *const planeA = &asset_plane_a;
static const Tilemap *const planeB = &asset_plane_b;

/* ---- Frame buffer de test -------------------------------------------------
   compose_line écrit par vdp_dram_write16 : on la fournit ici, à la place
   du simulateur, pour tout vérifier. Une seule ligne est composée à la
   fois, à l'offset LINE_OFFSET. */

#define LINE_OFFSET 0x1000u

static uint8_t fb[VDP_DRAM_SIZE];
static int wordWrites[MAX_LINE_WORDS]; /* écritures de chaque mot */
static long badWrites = 0; /* hors ligne, mal alignées ou mauvaise taille */
static long writeCount = 0;
static long writes16 = 0, writes32 = 0;

void vdp_dram_write16(uint32_t offset, uint16_t pixels) {
    writeCount++;
    writes16++;
    if (WRITE_BYTES != 2 || (offset & 1) || offset < LINE_OFFSET ||
        offset >= LINE_OFFSET + FB_PITCH) {
        badWrites++;
        return;
    }
    fb[offset] = (uint8_t)(pixels >> 8);
    fb[offset + 1] = (uint8_t)pixels;
    wordWrites[(offset - LINE_OFFSET) / 2]++;
}

/* Hypothèse bus32 seulement : un mot de 32 bits, 4 pixels */
void vdp_dram_write32(uint32_t offset, uint32_t pixels) {
    writeCount++;
    writes32++;
    if (WRITE_BYTES != 4 || (offset & 3) || offset < LINE_OFFSET ||
        offset + 4 > LINE_OFFSET + FB_PITCH) {
        badWrites++;
        return;
    }
    fb[offset] = (uint8_t)(pixels >> 24);
    fb[offset + 1] = (uint8_t)(pixels >> 16);
    fb[offset + 2] = (uint8_t)(pixels >> 8);
    fb[offset + 3] = (uint8_t)pixels;
    wordWrites[(offset - LINE_OFFSET) / 2]++;
    wordWrites[(offset - LINE_OFFSET) / 2 + 1]++;
}

/* compose.c n'en a pas besoin, mais vdp.h les déclare */
void vdp_dram_write8(uint32_t offset, uint8_t pixel) {
    (void)offset;
    (void)pixel;
    badWrites++;
}

/* ---- Référence ------------------------------------------------------------
 */

static int wrap(int v, int n) {
    v %= n;
    return v < 0 ? v + n : v;
}

static uint8_t planePixel(const Tilemap *map, int x, int y) {
    x = wrap(x, map->widthTiles * TILE_SIZE);
    y = wrap(y, map->heightTiles * TILE_SIZE);
    const unsigned tile =
        map->cells[(y / TILE_SIZE) * map->widthTiles + x / TILE_SIZE];
    return tile_row(tiles, tile, (unsigned)(y % TILE_SIZE))[x % TILE_SIZE];
}

static uint8_t referencePixel(int sx, int xA, int yA, int xB, int yB) {
    const uint8_t a = planePixel(planeA, xA + sx, yA);
    return a != 0 ? a : planePixel(planeB, xB + sx, yB);
}

/* ---- Vérification d'une ligne ---------------------------------------------
 */

static long linesChecked = 0;
static long linesShifted = 0;
static long linesByShift[4]; /* lignes par valeur de SFT */
static long failures = 0;
static ComposeCost cost;

static void checkLine(int xA, int yA, int xB, int yB) {
    memset(wordWrites, 0, sizeof wordWrites);
    memset(fb + LINE_OFFSET, 0xEE, FB_PITCH);
    const long writesBefore = writeCount, badBefore = badWrites;

    const int shift =
        compose_line(LINE_OFFSET, tiles, planeA, xA, yA, planeB, xB, yB, &cost);
    linesChecked++;
    linesShifted += shift != 0;
    if (shift >= 0 && shift <= 3)
        linesByShift[shift]++;

    /* Mots de 16 bits de la ligne, et écritures attendues */
    const int words = (FB_WIDTH + (shift ? FB_LINE_EXTRA : 0)) / 2;
    const int writes = words * 2 / WRITE_BYTES;
    int ok = shift >= 0 && shift <= SCREEN_SHIFT_SFT &&
             writeCount - writesBefore == writes && badWrites == badBefore;
    for (int w = 0; w < MAX_LINE_WORDS; w++)
        ok &= wordWrites[w] == (w < words);

    int badX = -1;
    for (int sx = 0; sx < FB_WIDTH && badX < 0; sx++)
        if (fb[LINE_OFFSET + shift + sx] != referencePixel(sx, xA, yA, xB, yB))
            badX = sx;

    if (!ok || badX >= 0) {
        if (failures < 10)
            fprintf(stderr,
                    "ECHEC xA=%d yA=%d xB=%d yB=%d decalage %d : %ld "
                    "ecritures, %ld mauvaises, premier pixel faux x=%d\n",
                    xA, yA, xB, yB, shift, writeCount - writesBefore,
                    badWrites - badBefore, badX);
        failures++;
    }
}

/* ---- Jeux d'essai ---------------------------------------------------------
 */

/* Générateur pseudo-aléatoire reproductible (xorshift) */
static uint32_t rngState = 2026;
static int randomIn(int min, int max) {
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return min + (int)(rngState % (uint32_t)(max - min + 1));
}

static int demoScrollB(int xA) {
    const int maxA = planeA->widthTiles * TILE_SIZE - FB_WIDTH;
    const int maxB = planeB->widthTiles * TILE_SIZE - FB_WIDTH;
    return (xA * maxB + maxA / 2) / maxA;
}

static void testDemoPath(void) {
    const int maxA = planeA->widthTiles * TILE_SIZE - FB_WIDTH;
    const long before = linesChecked;
    memset(&cost, 0, sizeof cost);

    for (int xA = 0; xA <= maxA; xA++)
        for (int y = 0; y < SCREEN_HEIGHT; y++)
            checkLine(xA, y, demoScrollB(xA), y);

    const long frames = maxA + 1;
    printf("1. trajectoire de la demo : %ld lignes (%ld images)\n",
           linesChecked - before, frames);
    printf("   modele de couts, moyenne par image (deux CPU) : %u cycles hors "
           "ecritures FB\n",
           (unsigned)(cost_cycles(&cost) / frames));
    printf(
        "   par image : %u mots copies source paire, %u impaire, %u "
        "fusionnes ; %u morceaux de tile ; %u segments\n",
        (unsigned)(cost.wordsEven / frames), (unsigned)(cost.wordsOdd / frames),
        (unsigned)(cost.wordsMerged / frames),
        (unsigned)(cost.tileChunks / frames), (unsigned)(cost.runs / frames));
}

static void testAllColumns(void) {
    const int widthA = planeA->widthTiles * TILE_SIZE;
    const int widthB = planeB->widthTiles * TILE_SIZE;
    int xBs[40];
    for (int i = 0; i < 32; i++)
        xBs[i] = i;
    xBs[32] = -1;
    xBs[33] = -17;
    xBs[34] = widthB - 1;
    xBs[35] = widthB - 16;
    xBs[36] = widthB + 5;
    xBs[37] = 3 * widthB + 333;
    xBs[38] = -widthB - 7;
    xBs[39] = 351;

    const long before = linesChecked;
    for (int xA = 0; xA < widthA; xA++)
        for (int i = 0; i < 40; i++)
            for (int y = 0; y < SCREEN_HEIGHT; y += 9) {
                const int yA = y + randomIn(-300, 300);
                const int yB = y + randomIn(-300, 300);
                checkLine(xA, yA, xBs[i], yB);
            }
    printf("2. toutes les colonnes de A x 40 valeurs de xB : %ld lignes\n",
           linesChecked - before);
}

static void testRandom(void) {
    const long before = linesChecked;
    for (int i = 0; i < 300000; i++)
        checkLine(randomIn(-3000, 3000), randomIn(-3000, 3000),
                  randomIn(-3000, 3000), randomIn(-3000, 3000));
    printf("3. lignes au hasard : %ld lignes\n", linesChecked - before);
}

/* ---- Image de référence ---------------------------------------------------
 */

/* Même conversion que le simulateur (pixel/vdp.c) */
static void paletteRGB(uint8_t index, uint8_t rgb[3]) {
    const uint16_t c = asset_palette[index];
    const uint8_t r = c & 0x1F, g = (c >> 5) & 0x1F, b = (c >> 10) & 0x1F;
    rgb[0] = (uint8_t)((r << 3) | (r >> 2));
    rgb[1] = (uint8_t)((g << 3) | (g >> 2));
    rgb[2] = (uint8_t)((b << 3) | (b >> 2));
}

static int writeReferencePPM(int xA, int xB, int yA, int yB, const char *path) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        perror(path);
        return 1;
    }
    fprintf(f, "P6\n%d %d\n255\n", FB_WIDTH, SCREEN_HEIGHT);
    for (int y = 0; y < SCREEN_HEIGHT; y++)
        for (int x = 0; x < FB_WIDTH; x++) {
            uint8_t rgb[3];
            paletteRGB(referencePixel(x, xA, yA + y, xB, yB + y), rgb);
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc == 7 && strcmp(argv[1], "--ppm") == 0)
        return writeReferencePPM(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]),
                                 atoi(argv[5]), argv[6]);
    if (argc != 1) {
        fprintf(stderr,
                "usage : test_compose [--ppm XA XB YA YB fichier.ppm]\n");
        return 2;
    }

    testDemoPath();
    printf("   decalage choisi (SFT 0/1/2/3) : %ld / %ld / %ld / %ld lignes\n",
           linesByShift[0], linesByShift[1], linesByShift[2], linesByShift[3]);
    testAllColumns();
    testRandom();

    printf("%ld lignes verifiees (%ld decalees), %ld echecs, %ld ecritures "
           "(%ld en 16 bits, %ld en 32 bits)\n",
           linesChecked, linesShifted, failures, writeCount, writes16, writes32);
    return failures != 0 || badWrites != 0;
}
