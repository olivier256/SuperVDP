/* =========================================================================
   bmp2c : convertit des images BMP 8 bits indexées en sprites pour DDS 32
   (voir dds32/assets_types.h).

   Usage : bmp2c MANIFESTE SORTIE
     Produit SORTIE.h et SORTIE.c.

   Le manifeste est un fichier texte, une ligne par image :
       # commentaire
       sprite  grass  grass.bmp
   Les chemins sont relatifs au manifeste. Chaque ligne produit une
   variable 'asset_<nom>' de type Sprite : l'image telle quelle, ligne par
   ligne ; l'index 0 est transparent.

   Palette (asset_palette) : celle de la première image, convertie au
   format 32X. Toutes les images doivent donner la même couleur aux index
   qu'elles utilisent.

   Formats acceptés : BMP Windows non compressé, 8 bits par pixel, de haut
   en bas ou de bas en haut. C'est ce qu'enregistrent GIMP (mode indexé),
   Aseprite, Photoshop ou Pillow en mode "P".
   ========================================================================= */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets_types.h" /* PALETTE_COLORS */
#include "bmpread.h"

#define MAX_ASSETS 64
#define MAX_PATH 1024

typedef struct {
    char name[64];
    char path[2 * MAX_PATH];
    BmpImage image;
} Asset;

const char *bmp_tool_name = "bmp2c";

static Asset assets[MAX_ASSETS];
static int assetCount = 0;

/* ---- Manifeste ----------------------------------------------------------- */

static void readManifest(const char *path) {
    FILE *f = fopen(path, "r");
    if (f == NULL)
        bmp_fail("impossible d'ouvrir le manifeste %s", path);

    /* Dossier du manifeste, pour les chemins relatifs */
    char dir[MAX_PATH];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash != NULL)
        slash[1] = '\0';
    else
        dir[0] = '\0';

    char line[MAX_PATH];
    int lineNo = 0;
    while (fgets(line, sizeof line, f) != NULL) {
        lineNo++;
        char kind[16], name[64], file[MAX_PATH];
        char *hash = strchr(line, '#');
        if (hash != NULL)
            *hash = '\0';
        int n = sscanf(line, "%15s %63s %1023s", kind, name, file);
        if (n <= 0)
            continue;
        if (n != 3) {
            fprintf(stderr,
                    "bmp2c : %s, ligne %d : 'sprite nom fichier' attendu\n",
                    path, lineNo);
            exit(1);
        }
        if (strcmp(kind, "sprite") != 0)
            bmp_fail("type inconnu '%s' (seul 'sprite' existe)", kind);
        if (assetCount == MAX_ASSETS)
            bmp_fail("trop d'images dans %s", path);
        Asset *a = &assets[assetCount++];
        snprintf(a->name, sizeof a->name, "%s", name);
        if (snprintf(a->path, sizeof a->path, "%s%s", dir, file) >=
            (int)sizeof a->path)
            bmp_fail("chemin trop long : %s", file);
        bmp_load(a->path, &a->image);
    }
    fclose(f);
    if (assetCount == 0)
        bmp_fail("aucune image dans %s", path);
}

/* Chaque image doit donner aux index qu'elle utilise la couleur de la
   palette de référence (celle de la première image). */
static void checkPalettes(void) {
    const BmpImage *ref = &assets[0].image;
    for (int i = 1; i < assetCount; i++) {
        const Asset *a = &assets[i];
        int used[256] = {0};
        for (int p = 0; p < a->image.width * a->image.height; p++)
            used[a->image.pixels[p]] = 1;
        for (int c = 1; c < 256; c++) {
            if (!used[c])
                continue;
            if (memcmp(a->image.rgb[c], ref->rgb[c], 3) != 0) {
                fprintf(stderr,
                        "bmp2c : %s : l'index %d vaut #%02X%02X%02X, mais "
                        "#%02X%02X%02X dans la palette de référence (%s)\n",
                        a->path, c, a->image.rgb[c][0], a->image.rgb[c][1],
                        a->image.rgb[c][2], ref->rgb[c][0], ref->rgb[c][1],
                        ref->rgb[c][2], assets[0].path);
                exit(1);
            }
        }
    }
}

/* ---- Sortie -------------------------------------------------------------- */

static void writeOutput(const char *base, const char *manifest) {
    char path[MAX_PATH + 3];
    const char *slash = strrchr(base, '/');
    const char *stem = slash ? slash + 1 : base;

    snprintf(path, sizeof path, "%s.h", base);
    FILE *h = fopen(path, "w");
    if (h == NULL)
        bmp_fail("impossible d'écrire %s", path);
    fprintf(h, "/* Généré par bmp2c depuis %s : ne pas modifier. */\n\n",
            manifest);
    fprintf(h, "#ifndef DEMO_ASSETS_H\n#define DEMO_ASSETS_H\n\n");
    fprintf(h, "#include \"assets_types.h\"\n\n");
    fprintf(h, "extern const uint16_t asset_palette[PALETTE_COLORS];\n");
    for (int i = 0; i < assetCount; i++)
        fprintf(h, "extern const Sprite asset_%s;\n", assets[i].name);
    fprintf(h, "\n#endif\n");
    fclose(h);

    snprintf(path, sizeof path, "%s.c", base);
    FILE *c = fopen(path, "w");
    if (c == NULL)
        bmp_fail("impossible d'écrire %s", path);
    fprintf(c, "/* Généré par bmp2c depuis %s : ne pas modifier. */\n\n",
            manifest);
    fprintf(c, "#include \"%s.h\"\n\n", stem);

    const BmpImage *ref = &assets[0].image;
    fprintf(c, "const uint16_t asset_palette[PALETTE_COLORS] = {");
    for (int i = 0; i < 256; i++) {
        unsigned r = ref->rgb[i][0] >> 3, g = ref->rgb[i][1] >> 3,
                 b = ref->rgb[i][2] >> 3;
        if (i % 8 == 0)
            fprintf(c, "\n   ");
        fprintf(c, " 0x%04X,", r | (g << 5) | (b << 10));
    }
    fprintf(c, "\n};\n\n");

    for (int i = 0; i < assetCount; i++) {
        const Asset *a = &assets[i];
        const int n = a->image.width * a->image.height;
        fprintf(c, "/* %s */\nstatic const uint8_t %s_pixels[%d] = {", a->path,
                a->name, n);
        for (int p = 0; p < n; p++) {
            if (p % a->image.width == 0)
                fprintf(c, "\n   ");
            fprintf(c, " %u,", a->image.pixels[p]);
        }
        fprintf(c, "\n};\n\nconst Sprite asset_%s = {%d, %d, %s_pixels};\n\n",
                a->name, a->image.width, a->image.height, a->name);
    }
    fclose(c);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "usage : bmp2c MANIFESTE SORTIE\n");
        return 2;
    }
    readManifest(argv[1]);
    checkPalettes();
    writeOutput(argv[2], argv[1]);
    fprintf(stderr, "bmp2c : %d sprites -> %s.c / %s.h\n", assetCount, argv[2],
            argv[2]);
    return 0;
}
