#!/system/bin/sh
# rmg-driver-chain.sh -- one-shot KernelSU driver bootstrap.
#
# Runs as root inside ONE bootstrap-daemon request (`cve-2026-43499-root -c
# 'sh /data/local/tmp/rmg-driver-chain.sh'`). Everything has to happen in this
# single request because loading kernelsu.ko flips SELinux back to enforcing,
# after which the daemon socket is no longer reachable from the shell domain.
#
# Steps: clean leaked bind mounts -> insmod kernelsu.ko only (no post-fs-data,
# no module mounts, no framework restart) -> inspect /data/adb as root ->
# enable the KernelSU userspace features -> try su -> leave a log behind.
#
# The /system/bin/logcat bind mount is kept alive on purpose: after the
# enforcement flip it is the only executable path to ksud, and `su` redirects
# to ksud. Remove it once the rescue is over (`umount /system/bin/logcat`).

LOG=/data/local/tmp/rmg-driver.log
KSUD=/data/local/tmp/ksud-s25u-kdp
KO=/data/local/tmp/kernelsu-s25u-kdp.ko
KO_MOUNT=/system/etc/hosts
KSU_BIN=/system/bin/logcat

exec >"$LOG" 2>&1
chmod 666 "$LOG" 2>/dev/null

say() { printf '%s\n' "$*"; }
say "==== chain start ===="
say "boot_id   : $(cat /proc/sys/kernel/random/boot_id 2>/dev/null)"
say "uptime    : $(cut -d' ' -f1 /proc/uptime 2>/dev/null) s"
say "enforce   : $(cat /sys/fs/selinux/enforce 2>/dev/null)"
say "context   : $(cat /proc/self/attr/current 2>/dev/null)"

say
say "== clean leaked mounts =="
for m in "$KSU_BIN" "$KO_MOUNT"; do
  if grep -q " $m " /proc/mounts 2>/dev/null; then
    say "[*] umount $m"
    umount "$m" || { umount -l "$m" || say "[-] umount $m failed: $?"; }
  fi
done

say
say "== load kernelsu.ko (insmod only) =="
if grep -q '^kernelsu ' /proc/modules 2>/dev/null; then
  say "[=] kernelsu already loaded"
else
  unshare -m /system/bin/sh -c \
    "mount -o bind $KSUD $KSU_BIN && mount -o bind $KO $KO_MOUNT && $KSU_BIN insmod $KO_MOUNT allow_shell=1"
  say "[*] insmod rc=$?"
fi
say "[*] /proc/modules: $(grep '^kernelsu ' /proc/modules 2>/dev/null)"
say "[*] enforce now: $(cat /sys/fs/selinux/enforce 2>/dev/null)"

say
say "== /data/adb visibility as root =="
ls -la /data/adb 2>&1
ls -la /data/adb/modules 2>&1
say "-- /data/adb/ksu/bin"
ls -la /data/adb/ksu/bin 2>&1
say "-- metamodule"
ls -la /data/adb/metamodule 2>&1
readlink /data/adb/metamodule 2>&1

say
say "== KernelSU feature state =="
"$KSU_BIN" feature list 2>&1
say "-- check su_compat"
"$KSU_BIN" feature check su_compat 2>&1
say "-- set su_compat 1"
"$KSU_BIN" feature set su_compat 1 2>&1
say "-- check su_compat after set"
"$KSU_BIN" feature check su_compat 2>&1

say
say "== su =="
say "-- ls -l /system/bin/su"
ls -l /system/bin/su 2>&1
say "-- su -c id"
su -c id 2>&1
say "-- su -c 'ls -la /data/adb/modules'"
su -c 'ls -la /data/adb/modules' 2>&1

say
say "== mounts still holding our bind =="
grep -E " $KSU_BIN | $KO_MOUNT " /proc/mounts 2>/dev/null

say
say "==== chain end ===="
