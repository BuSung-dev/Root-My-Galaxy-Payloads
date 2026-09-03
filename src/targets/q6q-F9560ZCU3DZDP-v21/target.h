#include "../q6q-F9560ZCU3DZDP-v20/target.h"

/* v21: p0 reference keeper for the FOPS root handoff.
 *
 * The v20 run reached the deepest point of any q6q run without a kernel
 * crash: tracefs slide -> FOPS production trigger (canonical target) ->
 * CFI stage -> pipe physrw install -> UMH root queue, and the SELinux
 * enforcing flag was successfully flipped to permissive (canonical write).
 * The only failure was app-side: install_android_root polls the root
 * daemon's hold-ready socket, which only appears after the daemon receives
 * the p0 reference fds from the ref-keeper process - and that keeper is
 * only ever spawned by the gate-verify flow, which never runs on the FOPS
 * path (step=8, errno=111).
 *
 * v21 spawns the keeper in try_cfi_stage right after install_pipe_physrw
 * succeeds (before install_android_root queues the UMH), and lets the
 * transfer fall back to a duplicate of the physrw pipe's read end when no
 * gate tee-holder exists.  The root daemon then receives the references,
 * binds the hold socket, and the app confirms root.
 *
 * Note for the run: the previous boot's session was consumed, and the v20
 * run left SELinux permissive - reboot before running v21.
 */
#undef BUILD_VARIANT_LABEL
#define BUILD_VARIANT_LABEL \
  "q6q-F9560ZCU3DZDP-app-production-v21-fops-ref-keeper"
