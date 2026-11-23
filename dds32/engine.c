/* =========================================================================
   DDS 32 : boucles des deux SH2.

   À chaque image, l'écran entier est redessiné depuis la mosaïque de la
   zone (render.c) : chaque pixel est écrit une fois, par mots entiers,
   sans transparence, puis l'objet mobile et le décor qui le cache. Le
   Master dessine les lignes paires, le Slave les impaires ; chacun
   possède entièrement ses lignes.

   Déroulement d'une image :

     68000 (VBlank)  Master                       Slave
     --------------  ------                       -----
     COMM 3-7 =      relève COMM 3-7 (caméra,
     caméra, objet   objet), publie la scène,
                     COMM_FRAME = n          -->  lit la scène
                     dessine les lignes           dessine les lignes
                     paires                       impaires
                     attend                  <--  COMM_SLAVE_DONE = n
                     demande l'échange de
                     frame buffer, attend
                     qu'il soit fait (VBlank)

   Décalage d'écran : si la caméra est à une abscisse impaire, l'image est
   dessinée depuis l'abscisse paire d'avant, et affichée décalée d'un
   pixel (SFT). Le Master écrit SFT dans sa routine V_INT, d'après le frame
   buffer qui va s'afficher : c'est lui qui a la priorité sur le bus.
   ========================================================================= */

#include "cost.h"
#include "framebuffer.h"
#include "game.h"
#include "interface.h"
#include "platform.h"
#include "render.h"
#include "shared.h"
#include "vdp.h"

#include "demo.h"

/* Scène de l'image en cours, publiée par le Master (voir shared.h) */
static Scene sharedScene;

/* Décalage d'écran de chaque frame buffer (indice : valeur de FS quand il
   est affiché). Écrit par le Master avant l'échange, lu par sa V_INT. */
static volatile uint8_t bufferShift[2];

static void initVDP(int screenHeight);
static Scene readScene(void);
static void drawScene(const Scene *scene, int cpu, int screenHeight);
static int flipFrameBuffer(void);

/* ---- Interruptions -------------------------------------------------------
   Master : V_INT, pour le décalage d'écran. Slave : aucune. */

void master_vint(void) {
    interface_clear_vint();
    const int shown = (vdp_read_fbcr() & FBCR_FS) ? 1 : 0;
    *screenShiftRegister = bufferShift[shown];
}
void master_hint(void) { interface_clear_hint(); }
void slave_vint(void) { interface_clear_vint(); }
void slave_hint(void) { interface_clear_hint(); }

/* ---- Master ------------------------------------------------------------- */

void master_main(void) {
    const int screenHeight = vdp_screen_height();
    initVDP(screenHeight);

    uint16_t frame = 0;
    for (;;) {
        if (++frame == 0) /* 0 veut dire « pas encore d'image » */
            frame = 1;

        Scene scene = readScene();
        scene.drawBuffer = (vdp_read_fbcr() & FBCR_FS) ? 0 : 1;
        bufferShift[scene.drawBuffer] = (uint8_t)render_shift(scene.cameraX);

        /* Publier la scène, puis lancer le Slave */
        *SHARED(&sharedScene) = scene;
        SHARED_BARRIER();
        comm[COMM_FRAME] = frame;

        drawScene(&scene, PLATFORM_CPU_MASTER, screenHeight);

        /* Attendre que le Slave ait fini ses lignes */
        while (comm[COMM_SLAVE_DONE] != frame)
            if (!platform_running())
                return;

        if (!flipFrameBuffer())
            return;
    }
}

/* ---- Slave -------------------------------------------------------------- */

void slave_main(void) {
    const int screenHeight = vdp_screen_height();
    uint16_t lastFrame = 0;

    for (;;) {
        uint16_t frame;
        while ((frame = comm[COMM_FRAME]) == lastFrame)
            if (!platform_running())
                return;

        SHARED_BARRIER();
        const Scene scene = *SHARED(&sharedScene);

        drawScene(&scene, PLATFORM_CPU_SLAVE, screenHeight);

        SHARED_BARRIER();
        comm[COMM_SLAVE_DONE] = frame;
        lastFrame = frame;
    }
}

/* ---- Initialisation ----------------------------------------------------- */

static void initVDP(int screenHeight) {
    /* Mode Blank : l'échange de frame buffer est immédiat */
    *bitmapModeRegister = (*bitmapModeRegister & 0xFFFC);

    /* Une table de lignes dans chacun des deux frame buffers */
    fb_init_line_table(screenHeight);
    vdp_write_fbcr(vdp_read_fbcr() ^ FBCR_FS);
    fb_init_line_table(screenHeight);

    const uint16_t *palette = demo_palette();
    for (int i = 0; i < 256; i++)
        vdp_palette[i] = palette[i];

    /* Mode Packed Pixel */
    *bitmapModeRegister = (*bitmapModeRegister & 0xFFFC) | 0x0001;

    interface_clear_vint();
    interface_write_intmask(INTMASK_V);
}

/* ---- Registres COMM ------------------------------------------------------
   Le 68000 peut les écrire à tout moment. D'après le manuel, un registre
   COMM lu pendant que l'autre côté l'écrit donne une valeur indéfinie, et
   le 68000 peut n'avoir écrit qu'une partie des cinq. On relit donc les
   registres jusqu'à obtenir deux lectures identiques de suite. */

static Scene readOnce(void) {
    Scene s = {0};
    s.cameraX = (int16_t)comm[COMM_CAMERA_X];
    s.cameraY = (int16_t)comm[COMM_CAMERA_Y];
    s.u = (int16_t)comm[COMM_OBJECT_U];
    s.v = (int16_t)comm[COMM_OBJECT_V];
    s.w = (int16_t)comm[COMM_OBJECT_W];
    return s;
}

static int sameScene(const Scene *a, const Scene *b) {
    return a->cameraX == b->cameraX && a->cameraY == b->cameraY &&
           a->u == b->u && a->v == b->v && a->w == b->w;
}

static Scene readScene(void) {
    Scene previous = readOnce();
    for (;;) {
        const Scene current = readOnce();
        if (sameScene(&previous, &current))
            return current;
        previous = current;
    }
}

/* ---- Dessin ------------------------------------------------------------- */

static void drawScene(const Scene *scene, int cpu, int screenHeight) {
    DrawCost cost = {0};
    RenderView rv;
    render_view(&rv, demo_zone(), scene->cameraX, scene->cameraY,
                DEMO_BACKGROUND, demo_object_sprite(), scene->u, scene->v,
                scene->w);
    for (int y = cpu; y < screenHeight; y += PLATFORM_CPU_COUNT)
        render_line(&rv, y, &cost);

#if DDS32_COST_ENABLED && !defined(__sh__)
    /* Outil du simulateur : estimation déclarée pour cette image. Le
       Master paie en plus sa V_INT (négligeable). */
    vdp_count_cpu_cycles(cost_cycles(&cost));
#endif
}

/* Demande l'échange des frame buffers et attend qu'il soit fait.

   Sur 32X comme dans le simulateur, une demande faite pendant l'affichage
   prend effet au début du VBlank suivant, mais une demande faite PENDANT
   le VBlank prend effet aussitôt. Si une image est dessinée plus vite que
   le VBlank (c'est le cas sur l'hôte), plusieurs échanges auraient lieu
   dans le même VBlank : on tournerait à plus de 60 images/s. On attend
   donc la fin du VBlank avant de demander l'échange : il a lieu au VBlank
   suivant, une fois par image au plus. */
static int flipFrameBuffer(void) {
    while (vdp_read_fbcr() & FBCR_VBLK)
        if (!platform_running())
            return 0;

    const uint16_t wanted = (vdp_read_fbcr() ^ FBCR_FS) & FBCR_FS;
    vdp_write_fbcr(wanted);

    while ((vdp_read_fbcr() & FBCR_FS) != wanted)
        if (!platform_running())
            return 0;
    return 1;
}
