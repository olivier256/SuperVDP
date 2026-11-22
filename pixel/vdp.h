#ifndef VDP_H
#define VDP_H

#include <stdint.h>

/* =========================================================================
   VDP 32X (émulé) : module sans thread.
   ========================================================================= */

/* ---- Hypothèse « bus32 » (make HYPO=bus32) ---------------------------------
   Une 32X imaginaire : écran de 256 x 224 (comme la Super Nintendo), deux
   frame buffers de 64 Ko faits chacun de deux DRAM 32K x 16 côte à côte,
   et un chemin 32 bits des SH2 jusqu'à la DRAM. Une écriture 32 bits
   (4 pixels) est alors UN accès au bus du VDP, au lieu de deux. Le
   registre de décalage d'écran passe à 2 bits (0 à 3 points), puisqu'un
   mot de 32 bits contient 4 pixels.
   Sans HYPO_BUS32 : la vraie 32X (320 x 224, 2 x 128 Ko, bus 16 bits). */
#ifndef HYPO_BUS32
#define HYPO_BUS32 0
#endif

#if HYPO_BUS32
#define VDP_SCREEN_WIDTH 256
#define VDP_DRAM_SIZE (64 * 1024)
#define VDP_BUS_BYTES 4 /* largeur du chemin vers la DRAM, en octets */
#else
#define VDP_SCREEN_WIDTH 320
#define VDP_DRAM_SIZE (128 * 1024)
#define VDP_BUS_BYTES 2
#endif
#define VDP_PALETTE_SIZE 256

/* Frame Buffer Control Register */
#define FBCR_VBLK 0x8000
#define FBCR_HBLK 0x4000
#define FBCR_FS 0x0001

/* ---- Vu des CPU (jeu) -------------------------------------------------- */

/* Frame buffer de dessin (celui qui n'est pas affiché) */
extern uint16_t *vdp_dram;
extern uint16_t vdp_palette[VDP_PALETTE_SIZE];

/* ---- Zone overwrite -------------------------------------------------------
   Sur 32X, le frame buffer de dessin est visible deux fois : en
   2400 0000h (accès normal) et en 2402 0000h (« overwrite image »). Une
   écriture dans la seconde zone ignore les octets nuls : le pixel déjà
   présent est conservé. C'est la transparence des sprites, faite par le
   matériel, quelle que soit la taille de l'accès.

   Comme pour le FBCR, l'effet ne peut pas être obtenu par un simple
   pointeur : ces fonctions tiennent lieu d'écritures à l'adresse
   2402 0000h + offset. offset est compté en octets depuis le début du
   frame buffer de dessin (table de lignes comprise).
   - 8 bits : un pixel.
   - 16 bits : deux pixels ; poids fort = pixel de gauche (adresse la plus
     basse), comme MOV.W sur SH2. offset doit être pair.
   - 32 bits : quatre pixels, même ordre. offset multiple de 4.
   Un accès mal aligné ou hors de la DRAM provoque, comme sur SH2, une
   erreur d'adresse : le simulateur s'arrête avec un message. */
#if defined(__sh__)
/* Sur SH2 (console, ou banc d'essai tools/sh2cycles) : de simples
   écritures à l'adresse de la zone, sans cache. */
#define VDP_OVERWRITE_ADDRESS 0x24020000u
static inline void vdp_overwrite_write8(uint32_t offset, uint8_t pixel) {
    *(volatile uint8_t *)(VDP_OVERWRITE_ADDRESS + offset) = pixel;
}
static inline void vdp_overwrite_write16(uint32_t offset, uint16_t pixels) {
    *(volatile uint16_t *)(VDP_OVERWRITE_ADDRESS + offset) = pixels;
}
static inline void vdp_overwrite_write32(uint32_t offset, uint32_t pixels) {
    *(volatile uint32_t *)(VDP_OVERWRITE_ADDRESS + offset) = pixels;
}
#else
void vdp_overwrite_write8(uint32_t offset, uint8_t pixel);
void vdp_overwrite_write16(uint32_t offset, uint16_t pixels);
void vdp_overwrite_write32(uint32_t offset, uint32_t pixels);
#endif

/* ---- Écritures normales ---------------------------------------------------
   Mêmes règles que ci-dessus (offset, ordre des octets, alignement), mais
   dans la zone normale (2400 0000h) : tous les octets sont écrits.
   Pour les pixels : les octets sont rangés dans l'ordre du SH2, comme
   vdp_dram[...] = BE16(...). Pas pour la table de lignes, que le
   simulateur relit comme des mots de l'hôte (voir bigendian.h).
   Sur console : *(volatile uint16_t *)(0x24000000 + offset) = pixels. */
#if defined(__sh__)
/* Sur SH2 : écritures directes dans le frame buffer de dessin, sans cache,
   comme sur la console. Pas de compteurs : ils sont l'affaire du
   simulateur (ou du banc tools/sh2cycles). */
#define VDP_DRAM_ADDRESS 0x24000000u
static inline void vdp_dram_write8(uint32_t offset, uint8_t pixel) {
    *(volatile uint8_t *)(VDP_DRAM_ADDRESS + offset) = pixel;
}
static inline void vdp_dram_write16(uint32_t offset, uint16_t pixels) {
    *(volatile uint16_t *)(VDP_DRAM_ADDRESS + offset) = pixels;
}
static inline void vdp_dram_write32(uint32_t offset, uint32_t pixels) {
    *(volatile uint32_t *)(VDP_DRAM_ADDRESS + offset) = pixels;
}
#else
void vdp_dram_write8(uint32_t offset, uint8_t pixel);
void vdp_dram_write16(uint32_t offset, uint16_t pixels);
void vdp_dram_write32(uint32_t offset, uint32_t pixels);
#endif

/* ---- Compteurs d'écritures dans le frame buffer ---------------------------
   Le simulateur ne mesure pas le temps SH2, mais il peut compter ce qui
   coûte : les accès au frame buffer, par le bus 16 bits du VDP.
   Une écriture 8 ou 16 bits = un accès ; une écriture 32 bits = deux.
   Seules les écritures par vdp_dram_write*() et vdp_overwrite_write*()
   sont comptées (pas celles faites directement par le pointeur vdp_dram).
   Une « image » va d'un échange de frame buffer au suivant : ce sont les
   écritures qui ont servi à dessiner l'image qui vient d'être affichée. */

enum { VDP_AREA_NORMAL, VDP_AREA_OVERWRITE, VDP_AREAS };
enum { VDP_WRITE_8, VDP_WRITE_16, VDP_WRITE_32, VDP_WRITE_SIZES };

typedef struct {
    uint32_t writes[2][VDP_AREAS][VDP_WRITE_SIZES]; /* [cpu][zone][taille] */
    uint32_t cpuCycles[2]; /* cycles CPU estimés, déclarés par le jeu */
} vdp_frame_writes;

/* Accès au bus pour un CPU : n8 + n16 + 2 x n32, toutes zones */
uint32_t vdp_bus_accesses(const vdp_frame_writes *frame, int cpu);

typedef struct {
    unsigned long frames;       /* images terminées depuis le début */
    vdp_frame_writes last;      /* la dernière image terminée */
    uint32_t maxBusAccesses[2]; /* pire image depuis la dernière remise à 0 */
    uint64_t totalBusAccesses[2]; /* depuis le début */
    uint32_t maxCpuCycles[2];     /* pire image (cycles estimés) */
} vdp_write_stats;

/* resetMax : remet maxBusAccesses à zéro après lecture (pics par période) */
void vdp_get_write_stats(vdp_write_stats *stats, int resetMax);

/* ---- Budget d'une image, d'après le 32X Hardware Manual ------------------
   - Horloge SH2 = horloge Mega Drive / 7 x 3 = 23,01 MHz (NTSC) ; une
     image (262 lignes de 3 420 Mck) dure donc environ 384 000 cycles SH2.
   - Écriture dans le frame buffer : 3 cycles, 5 si le FIFO (4 mots) est
     plein. Le VDP vide environ un mot toutes les 5 cycles, et les deux SH2
     partagent le bus et le FIFO : au mieux 384 000 / 5 = 76 800 écritures
     16 bits par image, pour les deux CPU ensemble.
   - Hypothèse bus32 : mêmes temps, mais chaque accès transporte un mot de
     32 bits (les deux DRAM d'un frame buffer écrites dans le même cycle) :
     76 800 accès de 4 pixels par image. */
#define VDP_SH2_CYCLES_PER_FRAME 384000u
#define VDP_FB_WRITE_CYCLES 3u      /* FIFO non plein */
#define VDP_FB_WRITE_CYCLES_FULL 5u /* FIFO plein */
#define VDP_FB_DRAIN_CYCLES 5u      /* un mot vidé par le VDP */
#define VDP_FB_WRITES_PER_FRAME (VDP_SH2_CYCLES_PER_FRAME / VDP_FB_DRAIN_CYCLES)

/* ---- Cycles CPU estimés ----------------------------------------------------
   Le simulateur exécute le jeu en code natif : il ne peut pas mesurer le
   temps SH2. Le jeu peut lui déclarer une ESTIMATION des cycles qu'il a
   dépensés (d'après son propre modèle de coûts), hors écritures dans le
   frame buffer, que le simulateur compte lui-même. Elle est comptée pour
   le CPU appelant et relevée à chaque échange de frame buffer, comme les
   écritures. Sur console, cette fonction n'existe pas. */
void vdp_count_cpu_cycles(uint32_t cycles);

/* Affiche les compteurs sur stderr, sur quatre lignes préfixées par
   'label', et remet les maxima à zéro. Utilisée par les deux plates-formes.
   - écritures par CPU, zone et taille ;
   - accès au bus par CPU, et total rapporté au plafond (76 800) ;
   - cycles du VDP pour vider ces écritures (partagés par les deux CPU) ;
   - cycles CPU estimés, plus 3 à 5 cycles par écriture du CPU. */
void vdp_print_write_stats(const char *label);

/* Registres */
extern volatile uint16_t *const bitmapModeRegister;

/* Screen Shift Control Register (2000 4102h), bit SFT : en mode Packed
   Pixel, décale l'affichage d'un point vers la gauche (le pixel x de
   l'écran est l'octet x + 1 de la ligne). Le VDP le lit à chaque ligne :
   une écriture prend effet à la ligne suivante, ce qui permet de le
   changer ligne par ligne (routine H_INT). Une ligne affichée décalée lit
   321 octets : prévoir au moins 161 mots par ligne.
   Hypothèse bus32 : 2 bits, décalage de 0 à 3 points (le pixel x de
   l'écran est l'octet x + SFT) ; une ligne décalée lit jusqu'à 259
   octets. */
#if HYPO_BUS32
#define SCREEN_SHIFT_SFT 0x0003
#else
#define SCREEN_SHIFT_SFT 0x0001
#endif
extern volatile uint16_t *const screenShiftRegister;
uint16_t vdp_read_fbcr(void);
void vdp_write_fbcr(uint16_t value);

/* Nombre de lignes affichées */
int vdp_screen_height(void);

/* ---- Vu de la plate-forme ---------------------------------------------- */

void vdp_init(int screenHeight);

/* Appelées par la synchro vidéo aux frontières de lignes */
void vdp_begin_vblank(void);
void vdp_end_vblank(void);

/* Décode une ligne affichée du frame buffer affiché vers RGB
   (VDP_SCREEN_WIDTH pixels, 0x00RRGGBB). */
void vdp_render_line(int line, uint32_t *rgb);

/* Diagnostics : nombre de VBlank, et nombre de VBlank vus par chaque CPU
   (au moins une lecture du FBCR avec VBLK = 1 pendant ce VBlank). */
typedef struct {
    unsigned long vblanks;
    unsigned long seen[2];
} vdp_stats;

void vdp_get_stats(vdp_stats *stats);

#endif
