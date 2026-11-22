#include "vdp.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>

/* Mémoire */
static uint16_t vdp_dram0[VDP_DRAM_SIZE / 2];
static uint16_t vdp_dram1[VDP_DRAM_SIZE / 2];
uint16_t vdp_palette[VDP_PALETTE_SIZE];

uint16_t *vdp_dram = vdp_dram1;
static uint16_t *displayFrameBuffer = vdp_dram0;

static int _screenHeight = 224;

/* Registres */
static volatile uint16_t _bitmapModeRegister;
volatile uint16_t *const bitmapModeRegister = &_bitmapModeRegister;
static volatile uint16_t _screenShiftRegister;
volatile uint16_t *const screenShiftRegister = &_screenShiftRegister;

/* représente la valeur effectivement appliquée */
static uint16_t _activeFS = 0;
/* conserve la dernière valeur écrite */
static uint16_t _requestedFS = 0;
static volatile int _inVBlank = 0;

/* Diagnostics */
static volatile unsigned long _vblanks = 0;
static volatile unsigned long _seen[PLATFORM_CPU_COUNT];
static unsigned long _lastSeenVBlank[PLATFORM_CPU_COUNT];

/* met à jour les deux pointeurs de mémoire */
static void applyFrameBuffer(void);

void vdp_init(int screenHeight) { _screenHeight = screenHeight; }

int vdp_screen_height(void) { return _screenHeight; }

/* renvoie la valeur active, pas nécessairement la valeur positionnée */
uint16_t vdp_read_fbcr(void) {
    uint16_t value = _activeFS ? FBCR_FS : 0;

    if (_inVBlank) {
        value |= FBCR_VBLK;

        /* Diagnostics : ce CPU a vu ce VBlank */
        int cpu = platform_cpu_id();
        unsigned long vblank = _vblanks;
        if (cpu >= 0 && _lastSeenVBlank[cpu] != vblank) {
            _lastSeenVBlank[cpu] = vblank;
            _seen[cpu]++;
        }
    }

    return value;
}

void vdp_write_fbcr(uint16_t value) {
    _requestedFS = value & FBCR_FS;

    if (_inVBlank || ((*bitmapModeRegister & 3) == 0))
        applyFrameBuffer();
}

/* Appelée par la synchro vidéo. L'ordre compte : les pointeurs
   doivent être échangés AVANT qu'un CPU puisse voir VBLK = 1. */
void vdp_begin_vblank(void) {
    applyFrameBuffer();
    _vblanks++;
    platform_sync();

    _inVBlank = 1;
}

void vdp_end_vblank(void) {
    platform_sync();
    _inVBlank = 0;
}

static void latchFrameWrites(void);

static void applyFrameBuffer(void) {
    if (_activeFS != _requestedFS)
        latchFrameWrites(); /* une image vient d'être terminée */
    _activeFS = _requestedFS;

    displayFrameBuffer = _activeFS ? vdp_dram1 : vdp_dram0;
    vdp_dram = _activeFS ? vdp_dram0 : vdp_dram1;
}

/* ---- Zone overwrite ------------------------------------------------------ */

/* Erreur d'adresse du SH2 : sur la console, le CPU part dans sa routine
   d'exception. Ici, on s'arrête net, avec de quoi retrouver l'accès. */
static void addressError(const char *access, uint32_t offset) {
    fprintf(stderr,
            "VDP : erreur d'adresse, %s en 2402 0000h + %05Xh (CPU %d)\n",
            access, (unsigned)offset, platform_cpu_id());
    abort();
}

/* ---- Compteurs d'écritures ---------------------------------------------
   Chaque CPU incrémente ses propres compteurs ; ils sont relevés (et
   remis à zéro) à chaque échange de frame buffer. Les CPU ne dessinent
   pas pendant l'échange, mais le relevé peut croiser un affichage des
   statistiques : un petit verrou protège les valeurs relevées. */

static uint32_t _current[PLATFORM_CPU_COUNT][VDP_AREAS][VDP_WRITE_SIZES];
static uint32_t _currentCycles[PLATFORM_CPU_COUNT];
static vdp_frame_writes _lastFrame;
static unsigned long _frames = 0;
static uint32_t _maxBus[PLATFORM_CPU_COUNT];
static uint32_t _maxCycles[PLATFORM_CPU_COUNT];
static uint64_t _totalBus[PLATFORM_CPU_COUNT];
static volatile int _writeStatsLock = 0;

static void lockWriteStats(void) {
    while (__atomic_test_and_set(&_writeStatsLock, __ATOMIC_ACQUIRE))
        ;
}
static void unlockWriteStats(void) {
    __atomic_clear(&_writeStatsLock, __ATOMIC_RELEASE);
}

static inline void countWrite(int area, int size) {
    int cpu = platform_cpu_id();
    if (cpu >= 0 && cpu < PLATFORM_CPU_COUNT)
        __atomic_fetch_add(&_current[cpu][area][size], 1, __ATOMIC_RELAXED);
}

void vdp_count_cpu_cycles(uint32_t cycles) {
    int cpu = platform_cpu_id();
    if (cpu >= 0 && cpu < PLATFORM_CPU_COUNT)
        __atomic_fetch_add(&_currentCycles[cpu], cycles, __ATOMIC_RELAXED);
}

/* Accès au bus du VDP : une écriture plus large que le bus en fait
   plusieurs. Vraie 32X (bus 16 bits) : une écriture 32 bits = 2 accès.
   Hypothèse bus32 : 1 accès, quelle que soit la taille. */
#define VDP_ACCESSES_PER_WRITE32 (4 / VDP_BUS_BYTES)

uint32_t vdp_bus_accesses(const vdp_frame_writes *frame, int cpu) {
    uint32_t n = 0;
    for (int area = 0; area < VDP_AREAS; area++)
        n += frame->writes[cpu][area][VDP_WRITE_8] +
             frame->writes[cpu][area][VDP_WRITE_16] +
             VDP_ACCESSES_PER_WRITE32 * frame->writes[cpu][area][VDP_WRITE_32];
    return n;
}

static void latchFrameWrites(void) {
    vdp_frame_writes frame;
    for (int cpu = 0; cpu < PLATFORM_CPU_COUNT; cpu++)
        for (int area = 0; area < VDP_AREAS; area++)
            for (int size = 0; size < VDP_WRITE_SIZES; size++)
                frame.writes[cpu][area][size] = __atomic_exchange_n(
                    &_current[cpu][area][size], 0, __ATOMIC_ACQ_REL);
    for (int cpu = 0; cpu < PLATFORM_CPU_COUNT; cpu++)
        frame.cpuCycles[cpu] =
            __atomic_exchange_n(&_currentCycles[cpu], 0, __ATOMIC_ACQ_REL);

    lockWriteStats();
    _lastFrame = frame;
    _frames++;
    for (int cpu = 0; cpu < PLATFORM_CPU_COUNT; cpu++) {
        uint32_t bus = vdp_bus_accesses(&frame, cpu);
        _totalBus[cpu] += bus;
        if (bus > _maxBus[cpu])
            _maxBus[cpu] = bus;
        if (frame.cpuCycles[cpu] > _maxCycles[cpu])
            _maxCycles[cpu] = frame.cpuCycles[cpu];
    }
    unlockWriteStats();
}

void vdp_get_write_stats(vdp_write_stats *stats, int resetMax) {
    lockWriteStats();
    stats->frames = _frames;
    stats->last = _lastFrame;
    for (int cpu = 0; cpu < PLATFORM_CPU_COUNT; cpu++) {
        stats->maxBusAccesses[cpu] = _maxBus[cpu];
        stats->totalBusAccesses[cpu] = _totalBus[cpu];
        stats->maxCpuCycles[cpu] = _maxCycles[cpu];
        if (resetMax) {
            _maxBus[cpu] = 0;
            _maxCycles[cpu] = 0;
        }
    }
    unlockWriteStats();
}

void vdp_print_write_stats(const char *label) {
    static const char *names[PLATFORM_CPU_COUNT] = {"master", "slave"};
    vdp_write_stats ws;
    vdp_get_write_stats(&ws, 1);

    fprintf(stderr,
            "[%s] ecritures FB, derniere image (8/16/32 bits) :", label);
    for (int cpu = 0; cpu < PLATFORM_CPU_COUNT; cpu++) {
        const uint32_t(*w)[VDP_WRITE_SIZES] = ws.last.writes[cpu];
        fprintf(stderr, "%s %s normale %u/%u/%u, overwrite %u/%u/%u",
                cpu ? " |" : "", names[cpu], w[VDP_AREA_NORMAL][0],
                w[VDP_AREA_NORMAL][1], w[VDP_AREA_NORMAL][2],
                w[VDP_AREA_OVERWRITE][0], w[VDP_AREA_OVERWRITE][1],
                w[VDP_AREA_OVERWRITE][2]);
    }
    fprintf(stderr, "\n");

    fprintf(stderr, "[%s] acces bus par image :", label);
    uint32_t lastTotal = 0;
    for (int cpu = 0; cpu < PLATFORM_CPU_COUNT; cpu++) {
        uint32_t last = vdp_bus_accesses(&ws.last, cpu);
        lastTotal += last;
        fprintf(stderr, "%s %s derniere %u, moyenne %.0f, max (periode) %u",
                cpu ? " |" : "", names[cpu], last,
                ws.frames ? (double)ws.totalBusAccesses[cpu] / ws.frames : 0.0,
                ws.maxBusAccesses[cpu]);
    }
    fprintf(stderr,
            " | total derniere %u = %.1f %% du plafond %u (ecran entier en "
            "%d bits : %d) | images %lu\n",
            lastTotal, 100.0 * lastTotal / VDP_FB_WRITES_PER_FRAME,
            VDP_FB_WRITES_PER_FRAME, 8 * VDP_BUS_BYTES,
            VDP_SCREEN_WIDTH * _screenHeight / VDP_BUS_BYTES, ws.frames);

    /* Le VDP vide le FIFO à son rythme, pour les deux CPU ensemble */
    const uint32_t drain = lastTotal * VDP_FB_DRAIN_CYCLES;
    fprintf(stderr,
            "[%s] cycles bus VDP (partages) : %u ecritures x %u = %u / %u "
            "(%.1f %%)\n",
            label, lastTotal, VDP_FB_DRAIN_CYCLES, drain,
            VDP_SH2_CYCLES_PER_FRAME, 100.0 * drain / VDP_SH2_CYCLES_PER_FRAME);

    /* Côté CPU : l'estimation déclarée par le jeu, plus le coût de ses
       propres écritures (3 cycles, 5 si le FIFO est plein) */
    fprintf(stderr,
            "[%s] cycles CPU estimes (modele du jeu + ecritures "
            "%u..%u c) :",
            label, VDP_FB_WRITE_CYCLES, VDP_FB_WRITE_CYCLES_FULL);
    for (int cpu = 0; cpu < PLATFORM_CPU_COUNT; cpu++) {
        const uint32_t model = ws.last.cpuCycles[cpu];
        const uint32_t bus = vdp_bus_accesses(&ws.last, cpu);
        const uint32_t lo = model + bus * VDP_FB_WRITE_CYCLES;
        const uint32_t hi = model + bus * VDP_FB_WRITE_CYCLES_FULL;
        if (model == 0)
            fprintf(stderr, "%s %s non declares", cpu ? " |" : "", names[cpu]);
        else
            fprintf(
                stderr,
                "%s %s %u + ecritures = %u..%u / %u (%.1f..%.1f %%), "
                "max modele (periode) %u",
                cpu ? " |" : "", names[cpu], model, lo, hi,
                VDP_SH2_CYCLES_PER_FRAME, 100.0 * lo / VDP_SH2_CYCLES_PER_FRAME,
                100.0 * hi / VDP_SH2_CYCLES_PER_FRAME, ws.maxCpuCycles[cpu]);
    }
    fprintf(stderr, "\n");

    /* Durée de l'image, en images de 1/60 s : le CPU le plus lent (au
       mieux, écritures à 3 cycles) ou le bus du VDP. Au-delà de 1, on ne
       tient pas 60 images/s. */
    double frames = (double)drain / VDP_SH2_CYCLES_PER_FRAME;
    int declared = 0;
    for (int cpu = 0; cpu < PLATFORM_CPU_COUNT; cpu++) {
        const uint32_t model = ws.last.cpuCycles[cpu];
        if (model == 0)
            continue;
        declared = 1;
        const double f = (double)(model + vdp_bus_accesses(&ws.last, cpu) *
                                              VDP_FB_WRITE_CYCLES) /
                         VDP_SH2_CYCLES_PER_FRAME;
        if (f > frames)
            frames = f;
    }
    if (declared)
        fprintf(
            stderr,
            "[%s] derniere image : %.2f image(s) de 1/60 s au mieux -> %s\n",
            label, frames,
            frames <= 1.0 ? "60 images/s" : "moins de 60 images/s");
}

/* ---- Écritures ---------------------------------------------------------- */

static inline uint8_t *dramBytes(void) { return (uint8_t *)vdp_dram; }

void vdp_dram_write8(uint32_t offset, uint8_t pixel) {
    if (offset >= VDP_DRAM_SIZE)
        addressError("ecriture 8 bits", offset);
    countWrite(VDP_AREA_NORMAL, VDP_WRITE_8);
    dramBytes()[offset] = pixel;
}

void vdp_dram_write16(uint32_t offset, uint16_t pixels) {
    if ((offset & 1) || offset + 2 > VDP_DRAM_SIZE)
        addressError("ecriture 16 bits", offset);
    countWrite(VDP_AREA_NORMAL, VDP_WRITE_16);
    dramBytes()[offset] = (uint8_t)(pixels >> 8);
    dramBytes()[offset + 1] = (uint8_t)pixels;
}

void vdp_dram_write32(uint32_t offset, uint32_t pixels) {
    if ((offset & 3) || offset + 4 > VDP_DRAM_SIZE)
        addressError("ecriture 32 bits", offset);
    countWrite(VDP_AREA_NORMAL, VDP_WRITE_32);
    dramBytes()[offset] = (uint8_t)(pixels >> 24);
    dramBytes()[offset + 1] = (uint8_t)(pixels >> 16);
    dramBytes()[offset + 2] = (uint8_t)(pixels >> 8);
    dramBytes()[offset + 3] = (uint8_t)pixels;
}

/* Écrit un octet dans le frame buffer de dessin, sauf s'il est nul */
static inline void overwriteByte(uint32_t offset, uint8_t pixel) {
    if (pixel != 0)
        dramBytes()[offset] = pixel;
}

void vdp_overwrite_write8(uint32_t offset, uint8_t pixel) {
    if (offset >= VDP_DRAM_SIZE)
        addressError("ecriture 8 bits", offset);
    countWrite(VDP_AREA_OVERWRITE, VDP_WRITE_8);
    overwriteByte(offset, pixel);
}

void vdp_overwrite_write16(uint32_t offset, uint16_t pixels) {
    if ((offset & 1) || offset + 2 > VDP_DRAM_SIZE)
        addressError("ecriture 16 bits", offset);
    countWrite(VDP_AREA_OVERWRITE, VDP_WRITE_16);
    overwriteByte(offset, (uint8_t)(pixels >> 8));
    overwriteByte(offset + 1, (uint8_t)pixels);
}

void vdp_overwrite_write32(uint32_t offset, uint32_t pixels) {
    if ((offset & 3) || offset + 4 > VDP_DRAM_SIZE)
        addressError("ecriture 32 bits", offset);
    countWrite(VDP_AREA_OVERWRITE, VDP_WRITE_32);
    overwriteByte(offset, (uint8_t)(pixels >> 24));
    overwriteByte(offset + 1, (uint8_t)(pixels >> 16));
    overwriteByte(offset + 2, (uint8_t)(pixels >> 8));
    overwriteByte(offset + 3, (uint8_t)pixels);
}

void vdp_get_stats(vdp_stats *stats) {
    /* Les CPU comptent un VBlank vu pendant qu'on lit : relever d'abord
       les VBlank vus, puis le nombre de VBlank. Dans l'autre ordre, un
       VBlank commencé et vu entre les deux lectures donnait plus de VBlank
       vus que de VBlank (et un nombre de « ratés » négatif). */
    for (int i = 0; i < PLATFORM_CPU_COUNT; i++)
        stats->seen[i] = _seen[i];
    __atomic_thread_fence(__ATOMIC_ACQUIRE); /* ordre gardé sur ARM aussi */
    stats->vblanks = _vblanks;
}

/* ---- Rendu ------------------------------------------------------------- */

static uint32_t get_palette_color(uint8_t index) {
    uint16_t color = vdp_palette[index];

    uint8_t r = color & 0x1F;
    uint8_t g = (color >> 5) & 0x1F;
    uint8_t b = (color >> 10) & 0x1F;

    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);

    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

void vdp_render_line(int line, uint32_t *rgb) {
    switch (*bitmapModeRegister & 3) {
    case 1: { // PACKED_PIXEL_MODE
        /* Un pixel = un octet, lus dans l'ordre des adresses : le pixel x est
           l'octet (adresse de ligne * 2 + x), comme sur la vraie 32X. Avec
           SFT, l'affichage est décalé : le pixel x est l'octet x + SFT
           (SFT vaut 0 ou 1 ; 0 à 3 avec l'hypothèse bus32). Le registre est lu ici, au moment où la ligne est
           balayée : sa valeur est celle écrite avant cette ligne. */
        const uint8_t *pixels = (const uint8_t *)displayFrameBuffer +
                                displayFrameBuffer[line] * 2 +
                                (*screenShiftRegister & SCREEN_SHIFT_SFT);
        for (int col = 0; col < VDP_SCREEN_WIDTH; col++)
            rgb[col] = get_palette_color(pixels[col]);
        break;
    }
    default: // BLANK_MODE ; DIRECT_COLOR_MODE, RUN_LENGTH_MODE : à faire
        for (int col = 0; col < VDP_SCREEN_WIDTH; col++)
            rgb[col] = 0;
        break;
    }
}
