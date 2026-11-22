#include "interface.h"
#include "platform.h"

/* Registres de communication */
volatile uint16_t comm[COMM_COUNT];

/* ---- Interruptions ------------------------------------------------------ */

/* Un masque et une V_INT en attente par CPU. La V_INT est mémorisée même
   si elle est masquée : la démasquer ensuite la déclenche aussitôt. Une
   routine qui veut l'éviter acquitte avant de démasquer. */
static volatile uint16_t _intMask[PLATFORM_CPU_COUNT];
static volatile int _vintPending[PLATFORM_CPU_COUNT];

/* H_INT : une en attente par CPU ; compteur H et HEN communs */
static volatile int _hintPending[PLATFORM_CPU_COUNT];
static volatile uint16_t _hcount = 0;
static volatile int _hen = 0;
static int _hcounter = 0; /* lignes avant la prochaine H_INT (synchro vidéo) */
static volatile unsigned long _hintRaised = 0;
static volatile unsigned long _hintHandled[PLATFORM_CPU_COUNT];
static volatile unsigned long _hintLate[PLATFORM_CPU_COUNT];

static volatile unsigned long _raised = 0;
static unsigned long _routinesDone = 0; /* accès atomiques seulement */
static volatile unsigned long _handled[PLATFORM_CPU_COUNT];
static volatile unsigned long _unacked[PLATFORM_CPU_COUNT];

uint16_t interface_read_intmask(void) {
    int cpu = platform_cpu_id();
    return (uint16_t)((cpu >= 0 ? _intMask[cpu] : 0) |
                      (_hen ? INTMASK_HEN : 0));
}

void interface_write_intmask(uint16_t value) {
    int cpu = platform_cpu_id();
    if (cpu < 0)
        return;

    _intMask[cpu] = value & 0x000F;
    _hen = (value & INTMASK_HEN) != 0;
    platform_sync();

    /* Effet immédiat : une interruption déjà en attente se déclenche */
    if (interface_vint_active(cpu) || interface_hint_active(cpu))
        platform_raise_interrupt(cpu);
}

void interface_clear_vint(void) {
    int cpu = platform_cpu_id();
    if (cpu < 0)
        return;

    _vintPending[cpu] = 0;
    platform_sync();
}

int interface_raise_vint(void) {
    _raised++;

    int sent = 0;
    for (int cpu = 0; cpu < PLATFORM_CPU_COUNT; cpu++) {
        _vintPending[cpu] = 1;
        platform_sync();

        if (_intMask[cpu] & INTMASK_V) {
            platform_raise_interrupt(cpu);
            sent++;
        }
    }
    return sent;
}

int interface_vint_active(int cpu) {
    return _vintPending[cpu] && (_intMask[cpu] & INTMASK_V);
}

/* ---- H_INT ----------------------------------------------------------------
 */

void interface_clear_hint(void) {
    int cpu = platform_cpu_id();
    if (cpu < 0)
        return;
    _hintPending[cpu] = 0;
    platform_sync();
}

uint16_t interface_read_hcount(void) { return _hcount; }

void interface_write_hcount(uint16_t value) {
    _hcount = value & 0x00FF;
    platform_sync();
}

int interface_hint_active(int cpu) {
    return _hintPending[cpu] && (_intMask[cpu] & INTMASK_H);
}

int interface_hblank(int inVBlank) {
    if (inVBlank && !_hen) {
        _hcounter = 0; /* le décompte repart avec la première ligne */
        return 0;
    }
    if (_hcounter > 0) {
        _hcounter--;
        return 0;
    }
    _hcounter = _hcount;
    _hintRaised++;

    int sent = 0;
    for (int cpu = 0; cpu < PLATFORM_CPU_COUNT; cpu++) {
        if (!(_intMask[cpu] & INTMASK_H))
            continue;
        if (_hintPending[cpu])
            _hintLate[cpu]++; /* la précédente n'a pas été servie à temps */
        _hintPending[cpu] = 1;
        platform_sync();
        platform_raise_interrupt(cpu);
        sent++;
    }
    return sent;
}

void interface_count_hint_handled(int cpu) {
    _hintHandled[cpu]++;
    __atomic_fetch_add(&_routinesDone, 1, __ATOMIC_RELEASE);
}

unsigned long interface_routines_done(void) {
    return __atomic_load_n(&_routinesDone, __ATOMIC_ACQUIRE);
}

int interface_hint_waiting(void) {
    for (int cpu = 0; cpu < PLATFORM_CPU_COUNT; cpu++)
        if (interface_hint_active(cpu))
            return 1;
    return 0;
}

void interface_count_handled(int cpu, int acknowledged) {
    _handled[cpu]++;
    if (!acknowledged)
        _unacked[cpu]++;
    __atomic_fetch_add(&_routinesDone, 1, __ATOMIC_RELEASE);
}

void interface_get_stats(interface_stats *stats) {
    stats->raised = _raised;
    for (int i = 0; i < PLATFORM_CPU_COUNT; i++) {
        stats->handled[i] = _handled[i];
        stats->unacked[i] = _unacked[i];
        stats->hintHandled[i] = _hintHandled[i];
        stats->hintLate[i] = _hintLate[i];
    }
    stats->hintRaised = _hintRaised;
}
