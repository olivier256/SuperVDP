/* =========================================================================
   dds32conv : le « Mirage Type III » de DDS 32. Convertit une zone éditée
   dans MagicaVoxel en données pour la console, et en image d'aperçu.

   Usage : dds32conv ZONE.vox CUBES.txt DOSSIER_IMAGES NOM SORTIE [APERCU.bmp]
     Produit SORTIE.h et SORTIE.c (variable 'const Zone zone_NOM', voir
     dds32/zone.h) et, si demandé, l'image entière de la zone en BMP 8 bits,
     telle que la console l'affichera (sans objets mobiles).

   Entrées :
   - ZONE.vox : le fichier de MagicaVoxel (premier modèle). x, y, z de
     MagicaVoxel = i, j, k ; le numéro de couleur d'un voxel (1 à 255) est
     son type de cube ;
   - CUBES.txt : pour chaque type, ses trois faces, chacune prise dans une
     image de cube 32 x 32 : « 1 grass.top grass.left grass.right » (voir
     demo/zone/cubes.txt). Une face donnée pour le dessus doit être un
     dessus (.top), et de même pour les côtés ;
   - les images : DOSSIER_IMAGES/NOM.bmp, 8 bits indexés, 32 x 32, palette
     commune (celle de la première image lue sert à l'aperçu).

   Ce que fait l'outil :
   1. découpe chaque face utilisée en ses deux triangles de pixels (voir
      dds32/iso.h), sans garder deux fois le même triangle ;
   2. calcule, pour chaque triangle de l'image de la zone, la face visible
      et sa profondeur (dds32/visibility.c, le même code que pourront
      utiliser les SH2) ;
   3. écrit le tout en C.
   ========================================================================= */

#include "bmpread.h"
#include "iso.h"
#include "visibility.h"
#include "zone.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *bmp_tool_name = "dds32conv";

#define MAX_PATH 1024
#define MAX_IMAGES 64
#define MAX_TRIANGLES 4096

/* ---- Lecture du .vox -------------------------------------------------- */

static uint8_t *voxels; /* voir zone.h */
static int W, D, H;

static uint32_t rd32(const uint8_t *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void loadVox(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        bmp_fail("impossible d'ouvrir %s", path);
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)size);
    if (data == NULL || fread(data, 1, (size_t)size, f) != (size_t)size)
        bmp_fail("lecture impossible : %s", path);
    fclose(f);
    if (size < 20 || memcmp(data, "VOX ", 4) != 0 ||
        memcmp(data + 8, "MAIN", 4) != 0)
        bmp_fail("%s n'est pas un fichier MagicaVoxel", path);

    /* Les blocs fils de MAIN : on prend le premier SIZE et le premier XYZI */
    long p = 20 + (long)rd32(data + 12);
    int haveSize = 0, haveVoxels = 0;
    while (p + 12 <= size && !haveVoxels) {
        const uint8_t *id = data + p;
        const uint32_t n = rd32(data + p + 4), children = rd32(data + p + 8);
        const uint8_t *content = data + p + 12;
        if (p + 12 + (long)n > size)
            bmp_fail("%s : bloc tronqué", path);
        if (memcmp(id, "SIZE", 4) == 0 && !haveSize) {
            W = (int)rd32(content);
            D = (int)rd32(content + 4);
            H = (int)rd32(content + 8);
            if (W < 1 || D < 1 || H < 1 || H > 16 || W > 255 || D > 255)
                bmp_fail("%s : dimensions refusées (hauteur : 16 au plus)",
                         path);
            voxels = calloc((size_t)W * D * H, 1);
            haveSize = 1;
        } else if (memcmp(id, "XYZI", 4) == 0 && haveSize) {
            const uint32_t count = rd32(content);
            if (4 + 4 * (long)count > (long)n)
                bmp_fail("%s : bloc XYZI tronqué", path);
            for (uint32_t v = 0; v < count; v++) {
                const uint8_t *q = content + 4 + 4 * v;
                if (q[0] < W && q[1] < D && q[2] < H)
                    voxels[(q[2] * D + q[1]) * W + q[0]] = q[3];
            }
            haveVoxels = 1;
        }
        p += 12 + (long)n + (long)children;
    }
    if (!haveVoxels)
        bmp_fail("%s : pas de voxels", path);
    free(data);
}

/* ---- Images de cubes et triangles -----------------------------------------
 */

typedef struct {
    char name[64];
    BmpImage image;
} CubeImage;

static CubeImage images[MAX_IMAGES];
static int imageCount = 0;
static const char *imageDir;

static uint8_t triangles[MAX_TRIANGLES][ISO_TRI_PIXELS];
/* Sens de chaque triangle : 0 = côté vertical à droite (iso_points_left),
   1 = à gauche ; passé à iso_row_start comme une colonne */
static int triangleKind[MAX_TRIANGLES];
static int triangleCount = 0;

static const BmpImage *image(const char *name) {
    for (int n = 0; n < imageCount; n++)
        if (strcmp(images[n].name, name) == 0)
            return &images[n].image;
    if (imageCount == MAX_IMAGES)
        bmp_fail("trop d'images (%s)", name);
    CubeImage *ci = &images[imageCount++];
    snprintf(ci->name, sizeof ci->name, "%s", name);
    char path[2 * MAX_PATH];
    snprintf(path, sizeof path, "%s/%s.bmp", imageDir, name);
    bmp_load(path, &ci->image);
    if (ci->image.width != ISO_CUBE_SIZE || ci->image.height != ISO_CUBE_SIZE)
        bmp_fail("%s : il faut une image de 32 x 32", path);
    return &ci->image;
}

/* Le triangle de l'emplacement 'slot' de l'image : numéro + 1 */
static uint16_t triangle(const BmpImage *img, int slot) {
    uint8_t pixels[ISO_TRI_PIXELS];
    const int dc = iso_cube_slots[slot].dc, dt = iso_cube_slots[slot].dt;
    for (int r = 0; r < ISO_TRI_ROWS; r++) {
        const int x0 = ISO_TRI_W * dc + iso_row_start(dc, dt, r);
        const int y = ISO_ROW_PITCH * dt + r;
        for (int x = 0; x < iso_row_width(r); x++)
            pixels[iso_row_offset(r) + x] =
                img->pixels[y * ISO_CUBE_SIZE + x0 + x];
    }
    const int kind = iso_points_left(dc, dt) ? 0 : 1;
    for (int n = 0; n < triangleCount; n++)
        if (triangleKind[n] == kind &&
            memcmp(triangles[n], pixels, sizeof pixels) == 0)
            return (uint16_t)(n + 1);
    if (triangleCount == MAX_TRIANGLES)
        bmp_fail("trop de triangles différents%s", "");
    triangleKind[triangleCount] = kind;
    memcpy(triangles[triangleCount++], pixels, sizeof pixels);
    return (uint16_t)triangleCount;
}

/* ---- Types de cube ------------------------------------------------------ */

static uint16_t typeSlots[256][6];

static void loadCubes(const char *path) {
    static const char *const faceNames[ISO_FACES] = {"top", "left", "right"};
    FILE *f = fopen(path, "r");
    if (f == NULL)
        bmp_fail("impossible d'ouvrir %s", path);
    char line[MAX_PATH];
    int lineNo = 0;
    while (fgets(line, sizeof line, f) != NULL) {
        lineNo++;
        char *hash = strchr(line, '#');
        if (hash != NULL)
            *hash = '\0';
        int type;
        char refs[ISO_FACES][128];
        const int n = sscanf(line, "%d %127s %127s %127s", &type, refs[0],
                             refs[1], refs[2]);
        if (n <= 0)
            continue;
        if (n != 4 || type < 1 || type > 255) {
            fprintf(stderr,
                    "dds32conv : %s, ligne %d : 'type dessus gauche droite' "
                    "attendu\n",
                    path, lineNo);
            exit(1);
        }
        for (int face = 0; face < ISO_FACES; face++) {
            char *dot = strrchr(refs[face], '.');
            if (dot == NULL || strcmp(dot + 1, faceNames[face]) != 0) {
                fprintf(stderr,
                        "dds32conv : %s, ligne %d : '%s' : il faut NOM.%s\n",
                        path, lineNo, refs[face], faceNames[face]);
                exit(1);
            }
            *dot = '\0';
            const BmpImage *img = image(refs[face]);
            for (int slot = 0; slot < 6; slot++)
                if (iso_cube_slots[slot].face == face)
                    typeSlots[type][slot] = triangle(img, slot);
        }
    }
    fclose(f);
}

/* ---- Sorties ------------------------------------------------------------ */

static void writeArray16(FILE *c, const char *decl, const uint16_t *a, int n) {
    fprintf(c, "%s[%d] = {", decl, n);
    for (int i = 0; i < n; i++)
        fprintf(c, "%s%u,", i % 16 ? " " : "\n   ", a[i]);
    fprintf(c, "\n};\n\n");
}

static void writeArray8(FILE *c, const char *decl, const uint8_t *a, int n) {
    fprintf(c, "%s[%d] = {", decl, n);
    for (int i = 0; i < n; i++)
        fprintf(c, "%s%u,", i % 32 ? " " : "\n   ", a[i]);
    fprintf(c, "\n};\n\n");
}

static void writeBmp(const char *path, const uint8_t *pixels, int w, int h,
                     const uint8_t rgb[256][3]) {
    FILE *f = fopen(path, "wb");
    if (f == NULL)
        bmp_fail("impossible d'écrire %s", path);
    const int stride = (w + 3) & ~3;
    const uint32_t offset = 14 + 40 + 1024, size = offset + stride * h;
    uint8_t hdr[54] = {'B', 'M'};
    const uint32_t fields[] = {size, 0, offset, 40, (uint32_t)w, (uint32_t)h};
    for (int i = 0; i < 6; i++)
        for (int b = 0; b < 4; b++)
            hdr[2 + 4 * i + b] = (uint8_t)(fields[i] >> (8 * b));
    hdr[26] = 1; /* plans */
    hdr[28] = 8; /* bits par pixel */
    hdr[46] = 0; /* couleurs : 256 */
    fwrite(hdr, 1, sizeof hdr, f);
    for (int i = 0; i < 256; i++) {
        const uint8_t q[4] = {rgb[i][2], rgb[i][1], rgb[i][0], 0};
        fwrite(q, 1, 4, f);
    }
    uint8_t pad[4] = {0};
    for (int y = h - 1; y >= 0; y--) {
        fwrite(pixels + (size_t)y * w, 1, (size_t)w, f);
        fwrite(pad, 1, (size_t)(stride - w), f);
    }
    fclose(f);
}

int main(int argc, char *argv[]) {
    if (argc != 6 && argc != 7) {
        fprintf(stderr, "usage : dds32conv ZONE.vox CUBES.txt DOSSIER_IMAGES "
                        "NOM SORTIE [APERCU.bmp]\n");
        return 2;
    }
    imageDir = argv[3];
    const char *name = argv[4], *base = argv[5];
    loadVox(argv[1]);
    loadCubes(argv[2]);

    /* Voxels d'un type inconnu : refusés */
    for (int n = 0; n < W * D * H; n++)
        if (voxels[n] && typeSlots[voxels[n]][0] == 0) {
            char t[8];
            snprintf(t, sizeof t, "%u", voxels[n]);
            bmp_fail("type de cube %s absent de CUBES.txt", t);
        }

    /* Géométrie de l'image de la zone (voir zone.h) */
    const int pad = ((D - 1) & 1) ? ISO_TRI_W : 0; /* alignement du treillis */
    const int originX = ISO_TRI_W * (D - 1) + pad;
    const int originY = ISO_UNITS * (H - 1);
    const int pixelWidth = ISO_TRI_W * (W + D) + pad;
    const int pixelHeight = 8 * (W + D) + ISO_UNITS * H;
    const int cols = pixelWidth / ISO_TRI_W;
    const int rows = pixelHeight / ISO_ROW_PITCH;

    uint16_t *mosaic = calloc((size_t)cols * rows, sizeof *mosaic);
    uint16_t *depth = calloc((size_t)cols * rows, sizeof *depth);
    const VisibilityInput in = {W,    D,       H,       voxels,   cols,
                                rows, originX, originY, typeSlots};
    visibility_build(&in, mosaic, depth);

    int visible = 0;
    for (int n = 0; n < cols * rows; n++)
        visible += mosaic[n] != 0;

    /* Pour la console (voir zone.h) : une rangée t = -1 en tête ; le fond
       devient le triangle 0 ou 1 selon le sens de la position ; les autres
       triangles sont décalés de 2 (ZONE_FIRST_TRIANGLE ; visibility_build
       les numérote à partir de 1). */
    const int rowsOut = rows + 1;
    uint16_t *mosaicOut = calloc((size_t)cols * rowsOut, sizeof *mosaicOut);
    uint16_t *depthOut = calloc((size_t)cols * rowsOut, sizeof *depthOut);
    for (int t = -1; t < rows; t++)
        for (int col = 0; col < cols; col++) {
            const int o = (t + 1) * cols + col;
            const uint16_t m = t >= 0 ? mosaic[t * cols + col] : 0;
            mosaicOut[o] = m ? (uint16_t)(m - 1 + ZONE_FIRST_TRIANGLE)
                             : (uint16_t)(iso_points_left(col, t) ? 0 : 1);
            depthOut[o] = t >= 0 ? depth[t * cols + col] : 0;
        }

    /* En-tête */
    char path[MAX_PATH + 8];
    snprintf(path, sizeof path, "%s.h", base);
    FILE *h = fopen(path, "w");
    if (h == NULL)
        bmp_fail("impossible d'écrire %s", path);
    fprintf(h, "/* Généré par dds32conv depuis %s : ne pas modifier. */\n\n",
            argv[1]);
    fprintf(h,
            "#ifndef ZONE_%s_H\n#define ZONE_%s_H\n\n#include \"zone.h\"\n\n",
            name, name);
    fprintf(h, "extern const Zone zone_%s;\n\n#endif\n", name);
    fclose(h);

    snprintf(path, sizeof path, "%s.c", base);
    FILE *c = fopen(path, "w");
    if (c == NULL)
        bmp_fail("impossible d'écrire %s", path);
    const char *slash = strrchr(base, '/');
    fprintf(c,
            "/* Généré par dds32conv depuis %s : ne pas modifier.\n"
            "   %d x %d x %d voxels ; image %d x %d, %d x %d triangles dont "
            "%d visibles ;\n   %d triangles de pixels différents. */\n\n",
            argv[1], W, D, H, pixelWidth, pixelHeight, cols, rows, visible,
            triangleCount);
    fprintf(c, "#include \"%s.h\"\n\n", slash ? slash + 1 : base);
    writeArray8(c, "static const uint8_t voxels", voxels, W * D * H);
    writeArray16(c, "static const uint16_t mosaic", mosaicOut, cols * rowsOut);
    writeArray16(c, "static const uint16_t depth", depthOut, cols * rowsOut);
    /* Triangles rangée par rangée, 16 octets par rangée, chaque pixel à sa
       place dans la colonne (voir zone.h) ; alignés sur une ligne du
       cache du SH2 */
    fprintf(c,
            "static const uint8_t triangles[%d] "
            "__attribute__((aligned(16))) = {",
            ZONE_TRIANGLE_ROWS * (triangleCount + ZONE_FIRST_TRIANGLE) *
                ISO_TRI_W);
    for (int r = 0; r < ZONE_TRIANGLE_ROWS; r++)
        for (int n = 0; n < triangleCount + ZONE_FIRST_TRIANGLE; n++) {
            uint8_t row[ISO_TRI_W] = {0};
            const int start = iso_row_start(
                n < ZONE_FIRST_TRIANGLE ? n
                                        : triangleKind[n - ZONE_FIRST_TRIANGLE],
                0, r);
            if (n < ZONE_FIRST_TRIANGLE) /* le fond */
                memset(row + start, ZONE_BACKGROUND, (size_t)iso_row_width(r));
            else
                memcpy(row + start,
                       triangles[n - ZONE_FIRST_TRIANGLE] + iso_row_offset(r),
                       (size_t)iso_row_width(r));
            fprintf(c, "\n   ");
            for (int x = 0; x < ISO_TRI_W; x++)
                fprintf(c, " %u,", row[x]);
        }
    fprintf(c, "\n};\n\n");
    fprintf(c,
            "const Zone zone_%s = {\n    %d, %d, %d, voxels,\n    %d, %d, %d, "
            "%d, %d, %d,\n    mosaic + %d, depth + %d, %d, triangles,\n};\n",
            name, W, D, H, cols, rows, pixelWidth, pixelHeight, originX,
            originY, cols, cols, triangleCount + ZONE_FIRST_TRIANGLE);
    fclose(c);

    /* Aperçu : l'image de la zone, fond = index 1 */
    if (argc == 7) {
        uint8_t *img = malloc((size_t)pixelWidth * pixelHeight);
        memset(img, ZONE_BACKGROUND, (size_t)pixelWidth * pixelHeight);
        for (int t = 0; t < rows; t++)
            for (int col = 0; col < cols; col++) {
                const uint16_t m = mosaic[t * cols + col];
                if (m == 0)
                    continue;
                const uint8_t *tri = triangles[m - 1];
                for (int r = 0; r < ISO_TRI_ROWS; r++) {
                    const int y = ISO_ROW_PITCH * t + r;
                    const int x0 = ISO_TRI_W * col + iso_row_start(col, t, r);
                    if (y >= pixelHeight)
                        continue;
                    for (int x = 0; x < iso_row_width(r); x++)
                        img[(size_t)y * pixelWidth + x0 + x] =
                            tri[iso_row_offset(r) + x];
                }
            }
        writeBmp(argv[6], img, pixelWidth, pixelHeight, images[0].image.rgb);
        free(img);
    }

    fprintf(stderr,
            "dds32conv : %s : %d x %d x %d, image %d x %d, %d triangles "
            "visibles sur %d, %d triangles de pixels (%d octets)\n",
            argv[1], W, D, H, pixelWidth, pixelHeight, visible, cols * rows,
            triangleCount,
            (triangleCount + ZONE_FIRST_TRIANGLE) * ZONE_TRIANGLE_ROWS *
                ISO_TRI_W);
    return 0;
}
