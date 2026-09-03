#include "../q6q-F9560ZCU3DZDP-v16/target.h"

/* v17: write_window gating (kp15 mitigation).
 *
 * kp15 (v16 attempt 8) crashed in the sched trigger chain:
 * rt_mutex_adjust_prio_chain+0x1ec read [x27+0x38] with x27 =
 * lock->waiters.rb_leftmost = 0x30f, i.e. the chain walked a lock whose
 * fabricated bank did NOT land where the stale waiter's lock word pointed
 * (reclaim/frag placement variance).  The chain itself is unavoidable
 * once the sched trigger fires - the crash happens inside sched_setattr -
 * but the v16 run also showed that on the placement-miss attempts the
 * consumer-side pselect window never opened (attempts 2-7 all logged
 * write_window=0), while attempt 1 (chain ran, marker write executed)
 * logged write_window=1 with ret=2 ready fds.
 *
 * v17 therefore requires the consumer-side window to confirm open
 * (write_window=1) before the trigger counts as armed: window-less
 * attempts are reported as misses with a distinct exit code, and the
 * caller starts a fresh reclaim instead of accepting the unconfirmed
 * trigger state (which v16 accepted unconditionally via sched_ok under
 * APP_ACCEPT_SCHED_TRIGGER).
 *
 * slide_child_trigger_write exit codes change to:
 *   0 = armed (window opened), 1 = requeue/deadlock route failed,
 *   2 = window closed (placement miss), 3 = stale waiter not ok.
 * The parent's "p0 physical write" line now logs the raw exit code.
 */
#define APP_REQUIRE_WRITE_WINDOW 1

#undef BUILD_VARIANT_LABEL
#define BUILD_VARIANT_LABEL \
  "q6q-F9560ZCU3DZDP-app-production-v17-write-window-gate"
