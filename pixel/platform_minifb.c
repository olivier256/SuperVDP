#include "game.h"
#include "interface.h"
#include "platform.h"
#include "vdp.h"

#include <MiniFB.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef __APPLE__
#include <pthread/qos.h>
#endif

/* ==== Paramètres du balayage ===============================================
   Une image dure 1/60 s et compte (lignes affichées + 38) lignes :
   224 + 38 = 262 lignes, soit 63,614 µs par ligne (timings NTSC). */

#define DEFAULT_SCREEN_HEIGHT 224
#define VBLANK_LINES 38
#define FRAME_RATE 60
#define STATS_PERIOD_S 5

/* La synchro dort entre deux événements et ne tourne en attente active
   qu'à l'approche des frontières précises (début et fin du VBlank).
   Ainsi, elle n'occupe pas en permanence un cœur performance, qui reste
   disponible pour l'affichage (MiniFB, Metal, WindowServer). */
#define SPIN_MARGIN_NS 300000LL /* tourner 300 µs avant une frontière */
#define MAX_SLEEP_NS                                                           \
    1000000LL /* rendre les lignes au moins toutes les 1 ms                    \
               */

/* ==== État ================================================================ */

static volatile int _running = 1;
static __thread int _cpuId = -1;

static struct mfb_window *_window = NULL;
static int _displayLines = 0; /* lignes affichées */
static int _totalLines = 0;   /* lignes par image */
static int64_t _linesPerSecond = 0;

/* Triple buffer RGB : la synchro vidéo produit, le thread principal affiche.
   Aucun des deux n'attend l'autre ; seuls les pointeurs sont échangés. */
static uint32_t *_rgb[3] = {NULL, NULL, NULL};
static uint32_t *_renderBuf;  /* en cours de rendu (synchro vidéo) */
static uint32_t *_readyBuf;   /* dernière image complète */
static uint32_t *_presentBuf; /* affichée (thread principal) */
static int _newFrame = 0;
static pthread_mutex_t _frameLock = PTHREAD_MUTEX_INITIALIZER;

static pthread_t _videoThread;
static pthread_t _masterThread;
static pthread_t _slaveThread;

/* Threads des CPU, pour leur envoyer les interruptions. Un CPU ne reçoit
   d'interruption qu'une fois son thread démarré. */
static pthread_t _cpuThread[PLATFORM_CPU_COUNT];
static volatile int _cpuReady[PLATFORM_CPU_COUNT];

/* Signal qui tient lieu de lignes IRL */
#define INTERRUPT_SIGNAL SIGUSR1

static volatile int64_t _vblankStartNs = 0;

/* ==== Diagnostics (activés par VDP_STATS=1) ===============================
   Compteurs écrits par un seul thread, lus par le thread principal :
   les valeurs affichées peuvent avoir une ligne de décalage, sans gravité. */

static int _statsEnabled = 0;
static int _qosEnabled = 1; /* VDP_QOS=0 pour le désactiver (diagnostic) */
static volatile unsigned long _statLateEdges =
    0;                                     /* début/fin de VBlank traités
                                               plus d'une ligne en retard */
static volatile int64_t _statMaxLagNs = 0; /* pire retard d'un début/fin
                                               de VBlank */
static volatile unsigned long _statSkippedFrames = 0; /* images sautées */
static volatile unsigned long _statPublished = 0;     /* images produites */
static volatile unsigned long _statOverwritten =
    0;                                   /* produites, jamais affichées */
static unsigned long _statPresented = 0; /* images affichées */
static int64_t _statMaxLoopNs = 0;       /* pire tour de boucle d'affichage */
static int64_t _statMaxUpdateNs = 0;     /* pire mfb_update() */
static int64_t _statMaxWaitNs = 0;       /* pire mfb_wait_sync() */
static unsigned long _statSlowLoops = 0; /* tours de plus de 2 images (33 ms) */

static int platform_init(int width, int height);
static void platform_shutdown(void);
static void *videoThread(void *arg);
static void *masterThread(void *arg);
static void *slaveThread(void *arg);
static void printStats(const char *when, double seconds);
static int installInterruptHandler(void);

/* ==== Temps =============================================================== */

static int64_t nowNs(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec;
}

/* Latence des interruptions. Sur la console, un SH2 prend une
   interruption en quelques cycles (s'il ne l'a pas masquée). Ici, le
   thread du CPU doit d'abord obtenir un processeur de l'hôte : sur une
   machine chargée (ou à 2 cœurs), cela peut prendre plus d'une ligne, et
   deux H_INT se confondraient. Le simulateur imite donc la garantie du
   matériel : après une H_INT, la synchro vidéo attend que sa routine
   soit terminée (pas seulement acquittée : elle acquitte avant d'écrire
   SFT), en cédant le processeur, au plus HINT_WAIT_NS. Au-delà, on
   continue : si la H_INT est encore en attente au HBlank suivant, elle
   est comptée en retard. La fenêtre garde ainsi le temps réel ; la
   plate-forme des tests (platform_headless.c), elle, attend toujours. */
#define HINT_WAIT_NS 50000 /* 50 µs, moins d'une ligne (63,5 µs) */

static void waitHintServed(unsigned long before, int count) {
    const int64_t limit = nowNs() + HINT_WAIT_NS;
    while (count > 0 && interface_routines_done() - before < (unsigned long)count &&
           nowNs() < limit)
        sched_yield();
}

/* Les threads qui tournent en attente active doivent rester sur les cœurs
   performance et ne pas être ralentis par macOS. */
static void setHighPriority(void) {
#ifdef __APPLE__
    if (_qosEnabled)
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
}

/* ==== Point d'entrée ====================================================== */

int main(int argc, char *argv[]) {
    int h = DEFAULT_SCREEN_HEIGHT;
    if (argc > 1) {
        h = atoi(argv[1]);
        if (h <= 0)
            return 1;
    }

    _displayLines = h;
    _totalLines = h + VBLANK_LINES;
    _linesPerSecond = (int64_t)_totalLines * FRAME_RATE;
    _statsEnabled = getenv("VDP_STATS") != NULL;
    const char *qos = getenv("VDP_QOS");
    _qosEnabled = !(qos != NULL && qos[0] == '0');

    /* Le thread principal (affichage) a la même priorité que les autres */
    setHighPriority();

    vdp_init(h);

    if (!platform_init(VDP_SCREEN_WIDTH, h))
        return 1;

    if (!installInterruptHandler()) {
        platform_shutdown();
        return 1;
    }

    /* Mise sous tension : la synchro vidéo démarre, puis la BootROM lance
       les deux CPU à leurs points d'entrée, en même temps. */
    int videoStarted =
        pthread_create(&_videoThread, NULL, videoThread, NULL) == 0;
    int masterStarted =
        pthread_create(&_masterThread, NULL, masterThread, NULL) == 0;
    int slaveStarted =
        pthread_create(&_slaveThread, NULL, slaveThread, NULL) == 0;

    if (!videoStarted || !masterStarted || !slaveStarted)
        _running = 0;

    /* Le thread principal ne fait plus qu'afficher (Cocoa l'exige) */
    int64_t start = nowNs();
    int64_t nextStats = start + STATS_PERIOD_S * 1000000000LL;

    int64_t loopStart = nowNs();

    while (_running) {
        pthread_mutex_lock(&_frameLock);
        if (_newFrame) {
            uint32_t *tmp = _presentBuf;
            _presentBuf = _readyBuf;
            _readyBuf = tmp;
            _newFrame = 0;
            _statPresented++;
        }
        pthread_mutex_unlock(&_frameLock);

        /* MiniFB détruit la fenêtre lui-même quand l'utilisateur la ferme */
        int64_t t0 = nowNs();
        if (mfb_update(_window, _presentBuf) != MFB_STATE_OK) {
            _window = NULL;
            break;
        }
        int64_t t1 = nowNs();
        if (!mfb_wait_sync(_window)) {
            _window = NULL;
            break;
        }
        int64_t t2 = nowNs();

        if (t1 - t0 > _statMaxUpdateNs)
            _statMaxUpdateNs = t1 - t0;
        if (t2 - t1 > _statMaxWaitNs)
            _statMaxWaitNs = t2 - t1;

        int64_t loopEnd = nowNs();
        int64_t loopNs = loopEnd - loopStart;
        loopStart = loopEnd;
        if (loopNs > _statMaxLoopNs)
            _statMaxLoopNs = loopNs;
        if (loopNs > 33333333LL)
            _statSlowLoops++;

        if (_statsEnabled && nowNs() >= nextStats) {
            printStats("", (nowNs() - start) / 1e9);
            nextStats += STATS_PERIOD_S * 1000000000LL;
        }
    }

    /* Arrêt : les CPU le voient dans leurs boucles d'attente */
    _running = 0;

    /* La synchro vidéo s'arrête d'abord : elle ne doit plus envoyer
       d'interruption à un thread CPU terminé. */
    if (videoStarted)
        pthread_join(_videoThread, NULL);
    if (masterStarted)
        pthread_join(_masterThread, NULL);
    if (slaveStarted)
        pthread_join(_slaveThread, NULL);

    if (_statsEnabled)
        printStats("fin ", (nowNs() - start) / 1e9);

    platform_shutdown();

    return 0;
}

/* Appelée dans chaque boucle d'attente du jeu : on y cède le processeur
   de l'hôte, pour qu'un CPU qui attend (le Slave entre deux images, par
   exemple) ne prive pas l'autre, ni la synchro vidéo, d'un cœur. Sur une
   machine à 2 cœurs, sans cela, le Master pouvait prendre ses H_INT avec
   plus d'une ligne de retard. */
int platform_running(void) {
    sched_yield();
    return _running;
}

/* ==== Manette =============================================================
   Clavier -> manette Mega Drive. MiniFB repère les touches par leur
   position physique (disposition US) : J, K, L tombent au même endroit
   en AZERTY et en QWERTY.
     Flèches = croix, J = A, K = B, L = C, Entrée = Start.
   Lu depuis la synchro vidéo ; le tampon de MiniFB est écrit par le
   thread principal : une touche peut arriver avec une image de retard. */

unsigned platform_read_pad(void) {
    struct mfb_window *window = _window;
    if (window == NULL)
        return 0;

    const uint8_t *keys = mfb_get_key_buffer(window);
    if (keys == NULL)
        return 0;

    unsigned pad = 0;
    if (keys[MFB_KB_KEY_UP])
        pad |= PAD_UP;
    if (keys[MFB_KB_KEY_DOWN])
        pad |= PAD_DOWN;
    if (keys[MFB_KB_KEY_LEFT])
        pad |= PAD_LEFT;
    if (keys[MFB_KB_KEY_RIGHT])
        pad |= PAD_RIGHT;
    if (keys[MFB_KB_KEY_J])
        pad |= PAD_A;
    if (keys[MFB_KB_KEY_K])
        pad |= PAD_B;
    if (keys[MFB_KB_KEY_L])
        pad |= PAD_C;
    if (keys[MFB_KB_KEY_ENTER])
        pad |= PAD_START;
    return pad;
}

int platform_cpu_id(void) { return _cpuId; }

/* Apple Silicon réordonne les écritures entre cœurs : barrière complète */
void platform_sync(void) { __sync_synchronize(); }

double platform_vblank_elapsed_ms(void) {
    return (nowNs() - _vblankStartNs) / 1000000.0;
}

/* ==== CPU =================================================================
   Chaque thread reçoit l'identité que le câblage de la broche MD5 donne au
   SH2 correspondant : masse pour le Master, Vcc pour le Slave. */

static void *masterThread(void *arg) {
    (void)arg;
    _cpuId = PLATFORM_CPU_MASTER;
    _cpuThread[PLATFORM_CPU_MASTER] = pthread_self();
    _cpuReady[PLATFORM_CPU_MASTER] = 1;
    setHighPriority();
    master_main();
    _cpuReady[PLATFORM_CPU_MASTER] = 0;
    return NULL;
}

static void *slaveThread(void *arg) {
    (void)arg;
    _cpuId = PLATFORM_CPU_SLAVE;
    _cpuThread[PLATFORM_CPU_SLAVE] = pthread_self();
    _cpuReady[PLATFORM_CPU_SLAVE] = 1;
    setHighPriority();
    slave_main();
    _cpuReady[PLATFORM_CPU_SLAVE] = 0;
    return NULL;
}

/* ==== Interruptions ========================================================
   Les lignes IRL d'un SH2 sont émulées par un signal envoyé à son thread :
   le thread est réellement interrompu, où qu'il en soit (boucle d'attente
   ou calcul), comme le SH2. Le gestionnaire joue le rôle de la table de
   vecteurs : il appelle la routine V_INT du CPU, et la rappelle tant que
   l'interruption reste active (non acquittée), comme le SH2 qui reprend
   l'interruption dès son retour (RTE).
   Pendant le gestionnaire, le signal est bloqué pour ce thread : une
   nouvelle interruption attend la fin de la routine, comme sur le SH2 où
   le niveau du SR est relevé pendant la routine. */

void platform_raise_interrupt(int cpu) {
    if (cpu < 0 || cpu >= PLATFORM_CPU_COUNT || !_cpuReady[cpu])
        return;
    pthread_kill(_cpuThread[cpu], INTERRUPT_SIGNAL);
}

static void interruptHandler(int sig) {
    (void)sig;
    int cpu = _cpuId;
    if (cpu < 0)
        return;

    /* V_INT d'abord (niveau 12), puis H_INT (niveau 10), comme le SH2 */
    while (_running) {
        if (interface_vint_active(cpu)) {
            if (cpu == PLATFORM_CPU_MASTER)
                master_vint();
            else
                slave_vint();
            interface_count_handled(cpu, !interface_vint_active(cpu));
        } else if (interface_hint_active(cpu)) {
            if (cpu == PLATFORM_CPU_MASTER)
                master_hint();
            else
                slave_hint();
            interface_count_hint_handled(cpu);
        } else {
            break;
        }
    }
}

static int installInterruptHandler(void) {
    struct sigaction sa;
    sa.sa_handler = interruptHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; /* les appels système interrompus reprennent */
    return sigaction(INTERRUPT_SIGNAL, &sa, NULL) == 0;
}

/* ==== Fenêtre ============================================================= */

static int platform_init(int width, int height) {
    _window = mfb_open("DDS 32", width, height);

    if (_window == NULL)
        return 0;

    for (int i = 0; i < 3; i++) {
        _rgb[i] = calloc((size_t)width * height, sizeof(uint32_t));
        if (_rgb[i] == NULL) {
            platform_shutdown();
            return 0;
        }
    }

    _renderBuf = _rgb[0];
    _readyBuf = _rgb[1];
    _presentBuf = _rgb[2];

    return 1;
}

static void platform_shutdown(void) {
    for (int i = 0; i < 3; i++) {
        free(_rgb[i]);
        _rgb[i] = NULL;
    }

    if (_window != NULL) {
        mfb_close(_window);
        _window = NULL;
    }
}

/* ==== Synchro vidéo =======================================================
   Le temps est figé : la ligne courante se déduit de l'horloge, à partir
   d'une origine fixe (pas de dérive). Le thread traite chaque frontière de
   ligne franchie, dans l'ordre, même en retard : il ne peut ni étirer ni
   sauter un VBlank, seulement le signaler en retard.

   Il dort entre deux événements et ne tourne en attente active que dans
   les SPIN_MARGIN_NS qui précèdent une frontière précise : début du VBlank
   (ligne _displayLines) et fin du VBlank (ligne 0). Les lignes affichées
   sont rendues au plus tard toutes les MAX_SLEEP_NS.
   Quand H_INT existera, toutes les lignes deviendront des frontières
   précises dès qu'un CPU l'aura démasquée. */

/* Numéro absolu de la ligne en cours (depuis l'origine) */
static int64_t lineIndexAt(int64_t elapsedNs) {
    return elapsedNs * _linesPerSecond / 1000000000LL;
}

/* Heure de début d'une ligne (depuis l'origine) */
static int64_t lineStartNs(int64_t lineIndex) {
    return lineIndex * 1000000000LL / _linesPerSecond;
}

/* Prochaine ligne (numéro absolu, >= line) qui exige la précision :
   ligne 0 (fin du VBlank) ou ligne _displayLines (début du VBlank). */
static int64_t nextPreciseLine(int64_t line) {
    int64_t inFrame = line % _totalLines;
    int64_t frameBase = line - inFrame;

    if (inFrame == 0 || inFrame == _displayLines)
        return line;
    if (inFrame < _displayLines)
        return frameBase + _displayLines;
    return frameBase + _totalLines;
}

static void sleepNs(int64_t ns) {
    struct timespec d = {(time_t)(ns / 1000000000LL),
                         (long)(ns % 1000000000LL)};
    nanosleep(&d, NULL);
}

static void publishFrame(void) {
    pthread_mutex_lock(&_frameLock);
    uint32_t *tmp = _readyBuf;
    _readyBuf = _renderBuf;
    _renderBuf = tmp;
    if (_newFrame)
        _statOverwritten++;
    _newFrame = 1;
    _statPublished++;
    pthread_mutex_unlock(&_frameLock);
}

/* Événements au début de la ligne 'line' (0 .. _totalLines - 1) */
static void beginLine(int line, int64_t startNs) {
    if (line == 0) {
        /* Fin du VBlank (ligne _totalLines = ligne 0 de l'image suivante) */
        vdp_end_vblank();
    }

    if (line < _displayLines) {
        /* Le VDP lit la ligne au moment où le faisceau la balaie : un
           changement de palette en cours d'image apparaîtra donc ici. */
        vdp_render_line(line, _renderBuf + (size_t)line * VDP_SCREEN_WIDTH);
        /* Puis le HBlank de cette ligne : H_INT. Sa routine a jusqu'au
           début de la ligne suivante (sur la console, seulement le HBlank,
           plus court) pour écrire ce qui doit valoir pour elle. */
        const unsigned long before = interface_routines_done();
        waitHintServed(before, interface_hblank(0));
    } else {
        interface_hblank(1);
    }
    if (line == _displayLines) {
        /* Début du VBlank : l'image affichée est complète */
        _vblankStartNs = startNs;
        /* VBlank Mega Drive : le 68000 d'abord, pour que ce qu'il écrit
           (manette) soit en place avant que les SH2 voient l'échange de
           frame buffer et commencent l'image suivante. */
        md_vint();
        vdp_begin_vblank();
        interface_raise_vint(); /* le VDP signale le VBlank à l'IC4 */
        publishFrame();
    }
}

static void *videoThread(void *arg) {
    (void)arg;
    setHighPriority();

    const int64_t origin = nowNs();
    int64_t next = 0; /* prochaine ligne à traiter (numéro absolu) */

    while (_running) {
        int64_t elapsed = nowNs() - origin;
        int64_t current = lineIndexAt(elapsed);

        if (current < next) {
            /* La ligne n'a pas encore commencé. Loin d'une frontière précise :
               on dort. Près d'elle : attente active, pour la viser exactement.
             */
            int64_t untilPrecise =
                lineStartNs(nextPreciseLine(next)) - elapsed - SPIN_MARGIN_NS;
            if (untilPrecise > 0)
                sleepNs(untilPrecise < MAX_SLEEP_NS ? untilPrecise
                                                    : MAX_SLEEP_NS);
            continue;
        }

        /* Gros retard de l'hôte (plus d'une image) : on reprend au début de
           l'image en cours plutôt que de rejouer les images perdues. */
        if (current - next > _totalLines) {
            _statSkippedFrames +=
                (unsigned long)((current - next) / _totalLines);
            next = current - current % _totalLines;
        }

        /* Traiter toutes les lignes franchies, dans l'ordre */
        while (next <= current) {
            int64_t start = origin + lineStartNs(next);

            /* Diagnostics : seules les frontières précises comptent ; les
               lignes affichées sont rendues par paquets, donc en retard par
               principe. */
            if (nextPreciseLine(next) == next) {
                int64_t lag = elapsed - lineStartNs(next);
                if (lag > _statMaxLagNs)
                    _statMaxLagNs = lag;
                if (next < current)
                    _statLateEdges++;
            }

            beginLine((int)(next % _totalLines), start);
            next++;
        }
    }

    return NULL;
}

/* ==== Diagnostics ========================================================= */

static void printStats(const char *when, double seconds) {
    vdp_stats vs;
    vdp_get_stats(&vs);
    interface_stats is;
    interface_get_stats(&is);

    fprintf(stderr,
            "[VDP %s%.1f s] V_INT levees %lu | master : traitees %lu, "
            "non acquittees %lu | slave : traitees %lu, non acquittees %lu"
            " | H_INT levees %lu, master traitees %lu (en retard %lu), "
            "slave traitees %lu (en retard %lu)\n",
            when, seconds, is.raised, is.handled[0], is.unacked[0],
            is.handled[1], is.unacked[1], is.hintRaised, is.hintHandled[0],
            is.hintLate[0], is.hintHandled[1], is.hintLate[1]);

    fprintf(stderr,
            "[VDP %s%.1f s] VBlank %lu, vus master %lu (rates %lu), "
            "vus slave %lu (rates %lu) | debut/fin VBlank en retard %lu, "
            "retard max (periode) %.1f us, "
            "images sautees %lu | images produites %lu, affichees %lu, "
            "ecrasees %lu | affichage : tour max (periode) %.1f ms "
            "(mfb_update %.1f ms, mfb_wait_sync %.1f ms), tours lents %lu%s\n",
            when, seconds, vs.vblanks, vs.seen[0], vs.vblanks - vs.seen[0],
            vs.seen[1], vs.vblanks - vs.seen[1], _statLateEdges,
            _statMaxLagNs / 1000.0, _statSkippedFrames, _statPublished,
            _statPresented, _statOverwritten, _statMaxLoopNs / 1e6,
            _statMaxUpdateNs / 1e6, _statMaxWaitNs / 1e6, _statSlowLoops,
            _qosEnabled ? "" : " [QoS desactivee]");

    char label[32];
    snprintf(label, sizeof label, "VDP %s%.1f s", when, seconds);
    vdp_print_write_stats(label);

    /* Les maxima sont remis à zéro à chaque période, pour voir les pics */
    _statMaxLagNs = 0;
    _statMaxLoopNs = 0;
    _statMaxUpdateNs = 0;
    _statMaxWaitNs = 0;
}
