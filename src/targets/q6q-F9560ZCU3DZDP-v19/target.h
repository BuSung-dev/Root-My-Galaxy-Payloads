#include "../q6q-F9560ZCU3DZDP-v18/target.h"

/* v19: tracefs slide route first - retire the blind boot_id candidate loop.
 *
 * The v18 device run proved the boot_id dance itself is reliable (17 clean
 * dance attempts, pselect/requeue/sched-trigger all green) but exposed two
 * structural problems:
 *
 * 1. The chain engages only stochastically (~1/18 attempts, matching the
 *    documented ~7% reclaim choreography), and the one engagement crashed
 *    with the kp10-class stale-waiter race (kp17: _raw_spin_trylock on NULL,
 *    x27=fake_lock but x24=0 - the zeroed-word anomaly).  The 32-candidate
 *    loop therefore has to survive ~15 dances per candidate discovery, each
 *    carrying the crash risk.
 * 2. A wrong-candidate marker write (p0 != slide) would land at an arbitrary
 *    Image .data offset (slot_alias + p0) and corrupt live kernel data; the
 *    v18 run did not prove whether the chain engaged on the misses, so the
 *    blind loop is unsafe by design even when it does not crash.
 *
 * v19 adds the ORIGINAL passive tracefs leak as the first route: shell holds
 * the readtracefs group (3012) and the live device shows
 * /sys/kernel/tracing/events/sched/sched_blocked_reason/enable world-
 * writable and per_cpu/cpu0/trace_pipe_raw readable.  The route reads the
 * sched_blocked_reason event's caller field for a blocked worker thread
 * (worker_thread's blocking schedule call site, caller return
 * SLIDE_TRACEFS_WORKER_CALLER_OFF = 0x000db1a0, event id 106) and computes
 * slide = caller - 0x000db1a0.  No chain, no writes, no crash risk.
 *
 * Route order in v19: tracefs -> boot_id (v18 loop, fallback only) ->
 * physical probe (fallback only).  The tracefs leak commits through the same
 * slide_commit_stext validation as the boot_id route and marks the in-process
 * non-physical slide for the fresh-P0-session gate in main.c.
 */
#define APP_SLIDE_TRACEFS_ROUTE 1
#define APP_SLIDE_NOPHYS_ROUTE 1

#undef BUILD_VARIANT_LABEL
#define BUILD_VARIANT_LABEL \
  "q6q-F9560ZCU3DZDP-app-production-v19-tracefs-slide"
