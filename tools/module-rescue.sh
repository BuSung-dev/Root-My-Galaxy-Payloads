#!/usr/bin/env bash
# module-rescue.sh -- recover a Root-My-Galaxy device whose KernelSU modules
# wedge the late-load (freeze -> framework restart -> cold reboot -> the
# per-boot temporary root is lost before anything can be done about it).
#
# Why the app cannot fix this: the freeze happens inside the KernelSU
# late-load, which is also when /data/adb/modules is mounted. By the time the
# user sees the boot animation there is no usable window left. The way out is
# to *not* late-load KernelSU first:
#
#   1. tools/reroot.sh --no-ksu        # bootstrap root only; no KernelSU
#                                      # late-load, so no modules load and the
#                                      # device does not freeze
#   2. tools/module-rescue.sh list     # find the offending module
#   3. tools/module-rescue.sh purge cert   # back up + delete it
#   4. tools/module-rescue.sh load-ksu     # late-load KernelSU again
#
# Steps 2-4 use the root daemon that step 1 leaves behind
# (/data/local/tmp/temp_su.sock), so they need no further exploit run and the
# device stays usable while they run.
#
# Usage:
#   tools/module-rescue.sh [--serial SERIAL] [--attempts N] [--reroot]
#                          [--no-backup] [--no-su] [--dry-run] <action> [pattern...]
#
# Actions:
#   status                  boot_id / uptime / KernelSU / modules overview
#   list                    dump modules, modules_update, ksu configs, *.d scripts
#   diag                    raw SELinux / /data/adb / mounts / ksud logs / pstore
#   match <pattern>...      print the module ids that match
#   backup <pattern>...     copy matching modules to /data/local/tmp, then pull
#   disable <pattern>...    create the KernelSU `disable` marker (module kept)
#   disable-all             create `disable` in every module (safe-mode equivalent)
#   purge <pattern>...      backup, then delete the matching module dirs
#   metamodule-off          remove a dangling /data/adb/metamodule symlink
#   ksud <args...>          run ksud through the DEFEX-safe logcat bind mount
#   driver                  insmod the KernelSU kernel module only (no stage
#                           scripts, no module mounts, no framework restart)
#   chain                   one-root-request bootstrap: insmod + su_compat +
#                           /data/adb probe, log at /data/local/tmp/rmg-driver.log
#   driver-unload           ksud unload (undo `driver`)
#   load-ksu                stage ksud and run the helper's `--late-load`
#                           (add --shadow-modules to load KernelSU with no
#                           module mounted -- the rescue path)
#   snapshot                back up every module dir + ksu/.d config to the PC
#                           (run this *before* installing a new module)
#
# Patterns are shell globs against the module id ("*cert*") plus literal
# substrings against id/name/description ("cert"), matched case-insensitively.
# Pass several patterns to match any of them, e.g. `purge cert movecert 证书`.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEVICE_TOOL_SRC="$REPO_ROOT/tools/device/rmg-module-tool.sh"
DEVICE_TOOL_DST=/data/local/tmp/rmg-module-tool.sh
DEVICE_CHAIN_SRC="$REPO_ROOT/tools/device/rmg-driver-chain.sh"
DEVICE_CHAIN_DST=/data/local/tmp/rmg-driver-chain.sh
CHAIN_LOG=/data/local/tmp/rmg-driver.log
ARGS_FILE=/data/local/tmp/rmg-module-args
KSUD_SRC="$REPO_ROOT/kernelsu/ksud-s25u-kdp"
KSUD_DST=/data/local/tmp/ksud-s25u-kdp
KSUD_STAGE=/data/local/tmp/.ksud-stage
KO_SRC="$REPO_ROOT/kernelsu/android15-6.6_kernelsu-s25u-kdp.ko"
KO_DST=/data/local/tmp/kernelsu-s25u-kdp.ko
HELPER_CANDIDATES=(
  /data/local/tmp/cve-2026-43499-root
  /data/local/tmp/ksu-helper
)
ARTIFACT_DIR="$REPO_ROOT/artifacts/module-backups"

SERIAL="${ANDROID_SERIAL:-}"
REROOT=0
DRY_RUN=0
NO_BACKUP=0
NO_SU=0
SHADOW=0
SU_AVAILABLE=0
HELPER_OK=''
KO_MOUNT_DST=''
ATTEMPTS="${EXPLOIT_ATTEMPTS:-8}"
LATE_LOAD_TIMEOUT="${LATE_LOAD_TIMEOUT:-600}"
HELPER="${HELPER_CANDIDATES[0]}"

ADB=(adb)
[ -n "$SERIAL" ] && ADB=(adb -s "$SERIAL")

step() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
info() { printf '    %s\n' "$*"; }
warn() { printf '\033[1;33m    warn: %s\033[0m\n' "$*" >&2; }
die() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

usage() {
  cat <<'USAGE'
module-rescue.sh [--serial SERIAL] [--attempts N] [--reroot] [--no-backup] [--no-su] [--shadow-modules] [--dry-run] <action> [pattern...]

Transport: module actions run through KernelSU's `su` (u:r:ksu:s0) whenever it
answers, because the bootstrap root daemon (u:r:kernel:s0) hits Samsung
DEFEX/KDP EPERM on /data/adb. `--no-su` forces the bootstrap daemon.

Actions (all need the temporary root from `tools/reroot.sh --no-ksu`):
  status                  boot_id / uptime / KernelSU / modules overview
  list                    dump modules, modules_update, ksu configs, *.d scripts
  diag                    raw SELinux / /data/adb / mounts / ksud logs / pstore
  match   <pattern>...    print the module ids that match
  backup  <pattern>...    copy matching modules to /data/local/tmp, then pull here
  disable <pattern>...    create the KernelSU `disable` marker (module kept)
  disable-all             create `disable` in every module (safe-mode equivalent)
  purge   <pattern>...    backup, then delete the matching module dirs
  metamodule-off          remove a dangling /data/adb/metamodule symlink
  ksud <args...>          run ksud through the DEFEX-safe logcat bind mount
  driver                  insmod the KernelSU kernel module only (no stage
                          scripts, no module mounts, no framework restart)
  chain                   one-root-request bootstrap: insmod + su_compat +
                          /data/adb probe, log at rmg-driver.log
  driver-unload           ksud unload (undo `driver`)
  load-ksu                stage ksud and run the helper's `--late-load`
                          (add --shadow-modules to load KernelSU with no module
                          mounted -- the rescue path)
  snapshot                back up every module dir + ksu/.d config to the PC
                          (run this before installing a new module)

Patterns are shell globs against the module id ("*cert*") plus literal
substrings against id/name/description ("cert"), matched case-insensitively.

Recommended sequence:
  tools/reroot.sh --no-ksu
  tools/module-rescue.sh list
  tools/module-rescue.sh purge cert
  tools/module-rescue.sh load-ksu
USAGE
  exit "${1:-0}"
}

validate_pattern() {
  # Patterns only ever travel through /data/local/tmp/rmg-module-args and are
  # matched with `case` (no eval, no command line), so any text is safe; only
  # newlines would corrupt the one-per-line argument file.
  case "$1" in
    '') return 1 ;;
    *$'\n'*|*$'\r'*) return 1 ;;
  esac
  [ "${#1}" -le 200 ] || return 1
  return 0
}

dev_sh() {
  # dev_sh "<command string>"
  if [ "$DRY_RUN" -eq 1 ]; then
    printf '    [dry-run] %s shell %s\n' "${ADB[*]}" "$1" >&2
    return 0
  fi
  "${ADB[@]}" shell "$1"
}

helper_c() {
  # helper_c "<command string>" -- root command, no single quotes allowed inside
  if [ "$DRY_RUN" -eq 1 ]; then
    printf '    [dry-run] helper -c %s\n' "$1" >&2
    return 0
  fi
  [ -n "$HELPER_OK" ] || die '需要 bootstrap root daemon（先跑 tools/reroot.sh --no-ksu）；当前只有 su'
  case "$1" in
    *"'"*) die "helper_c command may not contain a single quote" ;;
  esac
  dev_sh "$HELPER -c '$1'"
}

dev_sh_timeout() {
  # dev_sh_timeout <seconds> "<command string>"
  local secs="$1"
  shift
  if [ "$DRY_RUN" -eq 1 ]; then
    printf '    [dry-run] timeout %s %s shell %s\n' "$secs" "${ADB[*]}" "$1" >&2
    return 0
  fi
  timeout "$secs" "${ADB[@]}" shell "$1"
}

helper_argv() {
  if [ "$DRY_RUN" -eq 1 ]; then
    printf '    [dry-run] helper %s\n' "$*" >&2
    return 0
  fi
  [ -n "$HELPER_OK" ] || die '需要 bootstrap root daemon（late-load 必须走它）'
  dev_sh_timeout "$LATE_LOAD_TIMEOUT" "$HELPER $*"
}

device_boot_id() {
  if [ "$DRY_RUN" -eq 1 ]; then
    printf '<boot-id>'
    return 0
  fi
  dev_sh 'cat /proc/sys/kernel/random/boot_id' 2>/dev/null | tr -d '\r'
}

push_device_tool() {
  step '推送设备端工具'
  if [ "$DRY_RUN" -eq 1 ]; then
    printf '    [dry-run] %s push %s %s\n' "${ADB[*]}" "$DEVICE_TOOL_SRC" "$DEVICE_TOOL_DST" >&2
    return 0
  fi
  "${ADB[@]}" push "$DEVICE_TOOL_SRC" "$DEVICE_TOOL_DST" >/dev/null
  "${ADB[@]}" shell "chmod 755 $DEVICE_TOOL_DST"
  info "pushed $DEVICE_TOOL_DST"
}

push_args() {
  # one pattern/id per line, pushed as a file so no quoting has to survive adb
  local tmp
  tmp="$(mktemp -t rmg-args.XXXXXX)"
  printf '%s\n' "$@" >"$tmp"
  if [ "$DRY_RUN" -eq 1 ]; then
    printf '    [dry-run] %s push <args:%s> %s\n' "${ADB[*]}" "$*" "$ARGS_FILE" >&2
  else
    "${ADB[@]}" push "$tmp" "$ARGS_FILE" >/dev/null
  fi
  rm -f "$tmp"
}

device_tool() {
  # device_tool <action>
  if [ "$SU_AVAILABLE" -eq 1 ]; then
    dev_sh "su -c 'sh $DEVICE_TOOL_DST $1'"
  else
    helper_c "sh $DEVICE_TOOL_DST $1"
  fi
}

resolve_helper() {
  local candidate out
  HELPER_OK=''
  for candidate in "${HELPER_CANDIDATES[@]}"; do
    out="$("${ADB[@]}" shell "$candidate -c id" 2>/dev/null | tr -d '\r' || true)"
    if printf '%s' "$out" | grep -q 'uid=0'; then
      HELPER="$candidate"
      HELPER_OK=1
      info "bootstrap root daemon: $out"
      info "helper              : $candidate"
      return 0
    fi
  done
  return 1
}

ensure_root() {
  if [ "$DRY_RUN" -eq 1 ]; then
    return 0
  fi
  resolve_helper || true
  if [ -n "$HELPER_OK" ]; then
    detect_su
    return 0
  fi
  # No bootstrap daemon. That happens once KernelSU is loaded: the module load
  # restores SELinux enforcing and the shell domain can no longer connect to
  # the daemon socket. A working `su` in KernelSU's own domain is then a fully
  # usable root channel, so use it instead of demanding a new exploit run.
  detect_su
  if [ "$SU_AVAILABLE" -eq 1 ]; then
    info 'bootstrap daemon socket 不可达，但 su 可用（u:r:ksu:s0），继续'
    return 0
  fi
  if [ "$REROOT" -eq 1 ]; then
    step '临时 root 不可用 -- 运行 reroot.sh --no-ksu（不 late-load KernelSU，不会卡死）'
    local args=(--no-ksu --attempts "$ATTEMPTS")
    [ -n "$SERIAL" ] && args+=(--serial "$SERIAL")
    "$REPO_ROOT/tools/reroot.sh" "${args[@]}"
    resolve_helper || die 'exploit 完成但 root daemon 仍不可用'
  else
    die '未检测到 root 通道（既没有 bootstrap daemon，su 也不可用）。先跑 tools/reroot.sh --no-ksu，或加 --reroot'
  fi
  detect_su
}

detect_su() {
  SU_AVAILABLE=0
  [ "$NO_SU" -eq 1 ] && return 0
  local out
  out="$(dev_sh_timeout 15 'su -c id' 2>/dev/null | tr -d '\r' | head -n 1 || true)"
  if printf '%s' "$out" | grep -q 'uid=0'; then
    SU_AVAILABLE=1
    info "su 可用 (KernelSU): $out"
    info '模块操作将通过 su (u:r:ksu:s0) 执行'
  else
    info 'su 不可用（KernelSU 尚未加载）；模块操作走 bootstrap root daemon'
    info "  提示：bootstrap root 在三星 DEFEX/KDP 下有 EPERM 限制，模块目录可能读不了"
  fi
}

resolve_matches() {
  # resolve_matches <pattern>... -> module ids, one per line
  local p
  for p in "$@"; do
    validate_pattern "$p" || die "非法匹配模式: '$p'（不能为空、不能含换行）"
  done
  push_args "$@"
  if [ "$DRY_RUN" -eq 1 ]; then
    printf '%s\n' "$@"
    return 0
  fi
  device_tool match | tr -d '\r' | sed '/^[[:space:]]*$/d' | LC_ALL=C sort -u
}

backup_flow() {
  # backup_flow <id>...
  push_args "$@"
  local out
  out="$(device_tool backup | tr -d '\r')"
  printf '%s\n' "$out"
  pull_backup_output "$out"
}

pull_backup_output() {
  # pull_backup_output <device tool output containing RMG_BACKUP_DIR=...>
  local out="$1" bdir localdir
  bdir="$(printf '%s\n' "$out" | sed -n 's/^RMG_BACKUP_DIR=//p' | tail -n 1)"
  if [ "$DRY_RUN" -eq 1 ]; then
    printf '    [dry-run] %s pull <device backup dir> %s/<timestamp>\n' "${ADB[*]}" "$ARTIFACT_DIR" >&2
    return 0
  fi
  [ -n "$bdir" ] || { warn '未解析到设备端备份目录，跳过 adb pull'; return 0; }
  localdir="$ARTIFACT_DIR/$(basename "$bdir")"
  mkdir -p "$localdir"
  step "拉取备份到本机 $localdir"
  "${ADB[@]}" pull "$bdir/." "$localdir" >/dev/null 2>&1 \
    || warn "adb pull 失败（设备端备份仍在 $bdir）"
  info "本机备份: $localdir"
}

wait_boot() {
  local deadline=$(( $(date +%s) + ${1:-240} ))
  if [ "$DRY_RUN" -eq 1 ]; then
    printf '    [dry-run] %s wait-for-device + poll sys.boot_completed\n' "${ADB[*]}"
    return 0
  fi
  "${ADB[@]}" wait-for-device >/dev/null 2>&1 || true
  while [ "$(date +%s)" -lt "$deadline" ]; do
    if [ "$("${ADB[@]}" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = '1' ]; then
      return 0
    fi
    sleep 3
  done
  return 1
}

action_status() {
  ensure_root
  push_device_tool
  step '临时 root / 模块状态'
  device_tool status || true
  step 'KernelSU / su'
  if [ "$DRY_RUN" -eq 0 ]; then
    if dev_sh 'cat /proc/modules' 2>/dev/null | grep -q '^kernelsu '; then
      info 'kernelsu: loaded'
    else
      info 'kernelsu: not loaded（冷启动后是正常的；late-load 之后才应有）'
    fi
    local su
    su="$(dev_sh_timeout 15 'su -c id' 2>/dev/null | tr -d '\r' | head -n 1 || true)"
    if printf '%s' "$su" | grep -q 'uid=0'; then
      info "su: $su"
    else
      info 'su: 未返回 uid=0（KernelSU 管理器可能还没给 shell 授权；临时 root daemon 仍然可用）'
    fi
  fi
}

action_list() {
  ensure_root
  push_device_tool
  step '设备上的模块与脚本'
  device_tool list
}

action_diag() {
  ensure_root
  push_device_tool
  step '原始状态（SELinux / /data/adb / mounts / ksud 日志 / pstore）'
  device_tool diag
}

action_metamodule_off() {
  ensure_root
  push_device_tool
  step '移除 /data/adb/metamodule 软链（目标不存在时）'
  local out
  out="$(device_tool metamodule-off | tr -d '\r')"
  printf '%s\n' "$out"
  pull_backup_output "$out"
}

action_match() {
  [ "$#" -ge 1 ] || die 'match 需要至少一个模式'
  ensure_root
  push_device_tool
  resolve_matches "$@"
}

action_backup() {
  [ "$#" -ge 1 ] || die 'backup 需要至少一个模式'
  ensure_root
  push_device_tool
  step '解析匹配的模块'
  local ids
  ids="$(resolve_matches "$@")"
  [ -n "$ids" ] || die '没有模块匹配该模式'
  info "匹配: $(printf '%s' "$ids" | tr '\n' ' ')"
  step '备份到设备并拉回本机'
  backup_flow "$ids"
}

action_disable() {
  [ "$#" -ge 1 ] || die 'disable 需要至少一个模式'
  ensure_root
  push_device_tool
  step '解析匹配的模块'
  local ids
  ids="$(resolve_matches "$@")"
  [ -n "$ids" ] || die '没有模块匹配该模式'
  info "匹配: $(printf '%s' "$ids" | tr '\n' ' ')"
  step '写入 disable 标记（模块保留，late-load 时跳过）'
  push_args "$ids"
  device_tool disable
  if ! device_tool check-disabled; then
    die 'disable 标记没有写入成功'
  fi
  info '已禁用；需要恢复时删除模块目录里的 disable 文件即可'
}

action_disable_all() {
  ensure_root
  push_device_tool
  step '给所有模块写入 disable 标记（等同于 KernelSU safe mode 的效果）'
  device_tool disable-all
  device_tool status
}

action_purge() {
  [ "$#" -ge 1 ] || die 'purge 需要至少一个模式'
  ensure_root
  push_device_tool
  step '解析匹配的模块'
  local ids
  ids="$(resolve_matches "$@")"
  [ -n "$ids" ] || die '没有模块匹配该模式（先跑 list / match 看看真实模块 id）'
  info "将备份并删除: $(printf '%s' "$ids" | tr '\n' ' ')"
  if [ "$NO_BACKUP" -eq 1 ]; then
    warn '--no-backup: 跳过备份（仅在模块目录无法读取拷贝时使用）'
  else
    step '备份到设备并拉回本机'
    backup_flow "$ids"
  fi
  step '删除模块目录'
  push_args "$ids"
  device_tool purge
  step '校验删除结果'
  if ! device_tool verify; then
    die '模块仍然存在，没有删除干净'
  fi
  info '模块已清除，现在可以 late-load KernelSU: tools/module-rescue.sh load-ksu'
}

cleanup_bind_mounts() {
  # `unshare -m` from toybox cannot make the tree rprivate, so the bind mounts
  # leak into the global namespace and must be removed again. DEFEX logs an
  # "Immutable open violation" for the umount itself but lets it through.
  local m
  if [ "$DRY_RUN" -eq 1 ]; then
    printf '    [dry-run] check+umount leaked /system/bin/logcat (+ ko bind) mounts\n' >&2
    return 0
  fi
  for m in /system/bin/logcat "$KO_MOUNT_DST"; do
    [ -n "$m" ] || continue
    if dev_sh "grep -q \" $m \" /proc/mounts" 2>/dev/null; then
      warn "检测到泄漏的 $m bind mount，正在清理"
      helper_c "umount $m" >/dev/null 2>&1 || true
      helper_c "umount -l $m" >/dev/null 2>&1 || true
    fi
  done
  info "logcat: $(dev_sh 'ls -l /system/bin/logcat' 2>/dev/null | tr -s ' ' || true)"
}

ksud_run() {
  # ksud_run <ksud args...> through the DEFEX-safe logcat bind mount
  helper_c "unshare -m /system/bin/sh -c \"mount -o bind $KSUD_DST /system/bin/logcat && /system/bin/logcat $*\""
}

ksud_run_with_ko() {
  # ksud_run_with_ko <ko path> <ksud args...>; also binds the ko into the
  # immutable root so DEFEX lets ksud read it.
  local ko="$1"
  shift
  helper_c "unshare -m /system/bin/sh -c \"mount -o bind $KSUD_DST /system/bin/logcat && mount -o bind $ko $KO_MOUNT_DST && /system/bin/logcat $*\""
}

pick_ko_mount_target() {
  # a regular, non-symlink file under the immutable root we can shadow briefly
  local c out
  for c in /system/etc/hosts /system/build.prop /vendor/build.prop /system/bin/uncrypt; do
    out="$(dev_sh "[ -f $c ] && [ ! -L $c ] && echo $c" 2>/dev/null | tr -d '\r' | head -n 1 || true)"
    if [ -n "$out" ]; then
      KO_MOUNT_DST="$out"
      return 0
    fi
  done
  return 1
}

action_ksud() {
  [ "$#" -ge 1 ] || die 'ksud 需要参数，例如: tools/module-rescue.sh ksud insmod --help'
  local a
  for a in "$@"; do
    case "$a" in
      ''|*"'"*) die "非法 ksud 参数: '$a'" ;;
      *[!A-Za-z0-9._=:,/+*-]*) die "ksud 参数含不支持的字符: '$a'" ;;
    esac
  done
  ensure_root
  step "ksud $*"
  ksud_run "$*" || true
  cleanup_bind_mounts
  if [ "$DRY_RUN" -eq 0 ]; then
    dev_sh 'dmesg | grep -iE "kernelsu|defex|kdp" | tail -n 10' || true
    dev_sh 'cat /proc/modules' 2>/dev/null | grep -q '^kernelsu ' \
      && info 'kernelsu: loaded' || info 'kernelsu: not loaded'
  fi
}

driver_attempt() {
  # driver_attempt <label> <ko-arg> [use-ko-bind]
  local label="$1" ko_arg="$2" use_bind="${3:-0}"
  info "尝试: $label"
  if [ "$use_bind" = 1 ]; then
    ksud_run_with_ko "$ko_arg" "insmod $KO_MOUNT_DST allow_shell=1" || true
  else
    ksud_run "insmod $ko_arg allow_shell=1" || true
  fi
  cleanup_bind_mounts
  if [ "$DRY_RUN" -eq 1 ]; then
    return 1
  fi
  if dev_sh 'cat /proc/modules' 2>/dev/null | grep -q '^kernelsu '; then
    info "[+] $label -> kernelsu loaded"
    return 0
  fi
  warn "[-] $label -> 未加载"
  dev_sh 'dmesg | grep -iE "kernelsu|defex|kdp|resolve module" | tail -n 6' || true
  return 1
}

action_chain() {
  ensure_root
  [ -f "$KSUD_SRC" ] || die "缺少 $KSUD_SRC"
  [ -f "$KO_SRC" ] || die "缺少 $KO_SRC"
  step '推送 ksud / ko / chain 脚本'
  if [ "$DRY_RUN" -eq 1 ]; then
    printf '    [dry-run] push ksud+ko+chain, then one helper request\n' >&2
    return 0
  fi
  "${ADB[@]}" push "$KSUD_SRC" "$KSUD_DST" >/dev/null
  "${ADB[@]}" push "$KO_SRC" "$KO_DST" >/dev/null
  "${ADB[@]}" push "$DEVICE_CHAIN_SRC" "$DEVICE_CHAIN_DST" >/dev/null
  "${ADB[@]}" shell "chmod 755 $KSUD_DST $DEVICE_CHAIN_DST; chmod 644 $KO_DST"
  helper_c "cp $KSUD_DST $KSUD_STAGE && chmod 755 $KSUD_STAGE" >/dev/null 2>&1 || true
  helper_c 'rm -f /data/local/tmp/rmg-driver.log'

  step '单次 root 请求执行整条链（insmod + su_compat + /data/adb 探测）'
  info '加载 .ko 后 SELinux 会翻回 enforcing，daemon socket 随之失效，所以必须一次跑完'
  helper_c "sh $DEVICE_CHAIN_DST" || warn 'chain 返回非零（日志仍已落盘）'

  step '取回日志'
  local out
  out="$ARTIFACT_DIR/chain-$(date +%Y%m%d-%H%M%S).log"
  mkdir -p "$ARTIFACT_DIR"
  if "${ADB[@]}" pull "$CHAIN_LOG" "$out" >/dev/null 2>&1; then
    info "log: $out"
    cat "$out"
  else
    warn "拉取 $CHAIN_LOG 失败"
  fi
}

action_snapshot() {
  ensure_root
  push_device_tool
  step '枚举全部模块'
  local ids
  ids="$(resolve_matches '*')"
  [ -n "$ids" ] || die '没有列出任何模块（readdir 被挡？试试 su 通道）'
  info "模块: $(printf '%s' "$ids" | tr '\n' ' ')"
  backup_flow "$ids"
  step '配置与脚本快照'
  if [ "$DRY_RUN" -eq 0 ]; then
    mkdir -p "$ARTIFACT_DIR"
    dev_sh 'ls -la /data/adb/ksu/ 2>&1; echo ---; cat /data/adb/ksu/.feature_config 2>&1; echo ---; ls -la /data/adb/post-fs-data.d /data/adb/service.d 2>&1' \
      | tee "$ARTIFACT_DIR/snapshot-$(date +%Y%m%d-%H%M%S).txt" || true
  fi
  info '快照完成：模块目录在 artifacts/module-backups/，配置输出在 artifacts/snapshot-*.txt'
}

action_driver() {
  ensure_root
  [ -f "$KSUD_SRC" ] || die "缺少 $KSUD_SRC"
  [ -f "$KO_SRC" ] || die "缺少 $KO_SRC"
  local before after out su
  before="$(device_boot_id)"
  step "加载内核驱动前的 boot_id: $before"

  step '推送 ksud 与独立内核模块'
  if [ "$DRY_RUN" -eq 1 ]; then
    printf '    [dry-run] %s push %s %s\n' "${ADB[*]}" "$KSUD_SRC" "$KSUD_DST" >&2
    printf '    [dry-run] %s push %s %s\n' "${ADB[*]}" "$KO_SRC" "$KO_DST" >&2
  else
    "${ADB[@]}" push "$KSUD_SRC" "$KSUD_DST" >/dev/null
    "${ADB[@]}" push "$KO_SRC" "$KO_DST" >/dev/null
    "${ADB[@]}" shell "chmod 755 $KSUD_DST; chmod 644 $KO_DST"
  fi
  helper_c "cp $KSUD_DST $KSUD_STAGE && chmod 755 $KSUD_STAGE"
  helper_c 'echo 1 > /proc/sys/kernel/kptr_restrict'

  step '只 insmod 内核驱动（不跑 post-fs-data，不挂模块，不重启框架）'
  info 'allow_shell=1 -> shell 无需管理器授权即可 su'
  local loaded=0
  # DEFEX 的 immutable-root 规则会拦 /data/local/tmp 的 .ko，因此：
  #   1) 换个不带 .ko 后缀的名字（规则可能匹配 *.ko）
  #   2) 把 ko bind 到 immutable root 里的一个普通文件上，再用那个路径
  helper_c "cp $KO_DST /data/local/tmp/ksuimg.bin && chmod 644 /data/local/tmp/ksuimg.bin"
  if driver_attempt 'alt name /data/local/tmp/ksuimg.bin' /data/local/tmp/ksuimg.bin 0; then
    loaded=1
  elif pick_ko_mount_target; then
    info "ko bind 目标: $KO_MOUNT_DST"
    if driver_attempt "bind ko -> $KO_MOUNT_DST" "$KO_DST" 1; then
      loaded=1
    fi
  fi
  [ "$loaded" -eq 1 ] || warn 'ksud insmod 两次都没成功，看下面的 DEFEX/dmesg 输出'
  cleanup_bind_mounts

  if [ "$DRY_RUN" -eq 1 ]; then
    return 0
  fi

  step '内核侧结果'
  dev_sh 'dmesg | grep -iE "kernelsu|kdp|defex" | tail -n 15' || true
  if dev_sh 'cat /proc/modules' 2>/dev/null | grep -q '^kernelsu '; then
    info 'kernelsu: loaded'
  else
    warn 'kernelsu: 未出现在 /proc/modules —— insmod 没有成功'
  fi

  step '验证 su (u:r:ksu:s0)'
  su="$(dev_sh_timeout 15 'su -c id' 2>/dev/null | tr -d '\r' | head -n 1 || true)"
  if printf '%s' "$su" | grep -q 'uid=0'; then
    info "su: $su"
    detect_su
    info '接下来: tools/module-rescue.sh list  ->  tools/module-rescue.sh purge <模块id>'
  else
    warn "su 还不可用: ${su:-<empty>}"
    warn '可以试 B 方案（safe mode late-load）或 ksud libadbroot / sepolicy patch'
  fi

  after="$(device_boot_id)"
  if [ "$before" = "$after" ]; then
    info "boot_id 未变化 ($after) -> 没有重启，可以继续操作"
  else
    warn 'boot_id 变了 -> 设备重启了（本次不是 late-load 引起的？请把 dmesg/pstore 发我）'
  fi
}

action_driver_unload() {
  ensure_root
  step '卸载 KernelSU 内核驱动（ksud unload）'
  ksud_run 'unload' || warn 'ksud unload 返回非零'
  cleanup_bind_mounts
  if [ "$DRY_RUN" -eq 0 ]; then
    dev_sh 'cat /proc/modules' 2>/dev/null | grep -q '^kernelsu ' \
      && warn 'kernelsu 仍在 /proc/modules' || info 'kernelsu 已卸载'
  fi
}

action_load_ksu() {
  ensure_root
  [ -f "$KSUD_SRC" ] || die "缺少 $KSUD_SRC"
  local before after su
  before="$(device_boot_id)"
  step "late-load 前的 boot_id: $before"

  if [ "$SHADOW" -eq 1 ]; then
    step '遮蔽模块目录：把空目录 bind 到 /data/adb/modules(_update)（rescue 模式）'
    info 'ksud 的 post-fs-data 将看不到任何模块 -> 不挂载 -> 框架重启不会被拽死'
    helper_c 'mkdir -p /data/local/tmp/emptydir'
    helper_c 'mount -o bind /data/local/tmp/emptydir /data/adb/modules'
    helper_c '[ -d /data/adb/modules_update ] && mount -o bind /data/local/tmp/emptydir /data/adb/modules_update; true'
    dev_sh 'grep -E " /data/adb/modules" /proc/mounts' || true
  fi

  step '推送 ksud 并暂存 .ksud-stage'
  if [ "$DRY_RUN" -eq 1 ]; then
    printf '    [dry-run] %s push %s %s\n' "${ADB[*]}" "$KSUD_SRC" "$KSUD_DST"
  else
    "${ADB[@]}" push "$KSUD_SRC" "$KSUD_DST" >/dev/null
    "${ADB[@]}" shell "chmod 755 $KSUD_DST"
  fi
  helper_c "cp $KSUD_DST $KSUD_STAGE && chmod 755 $KSUD_STAGE"

  step 'late-load KernelSU（zygote/system_server 会重启一次，屏幕显示开机动画属预期；这不是冷重启）'
  if [ "$SHADOW" -eq 1 ]; then
    info '已遮蔽模块目录；若仍想双保险，可同时连点「音量下键」3~5 下触发 safe mode'
  else
    info '若怀疑某个模块会拖死重启：改用 tools/module-rescue.sh --shadow-modules load-ksu'
    info '（或现在连点「音量下键」3~5 下触发 KSU safe mode，ksud 会给所有模块写 disable）'
  fi
  helper_argv --late-load || warn "--late-load 返回非零，继续观察设备状态"

  step '等待系统恢复'
  if wait_boot 240; then
    info '框架已恢复'
  else
    warn '等待超时；继续检查状态'
  fi

  if [ "$DRY_RUN" -eq 1 ]; then
    return 0
  fi

  after="$(device_boot_id)"
  step "late-load 后的 boot_id: $after"
  if [ -n "$before" ] && [ "$before" = "$after" ]; then
    info 'boot_id 未变化 -> 内核没有冷重启，本 boot 的临时 root 保留'
  else
    warn 'boot_id 变化 -> 设备发生了冷重启，临时 root 已丢失'
    warn '说明还有别的东西在 late-load 时把系统拖死：检查其它模块 / post-fs-data.d / service.d / KernelSU Zygisk'
  fi

  if [ "$SHADOW" -eq 1 ]; then
    step '解除模块目录遮蔽'
    detect_su
    local um="for i in 1 2 3 4; do umount /data/adb/modules 2>/dev/null; umount /data/adb/modules_update 2>/dev/null; done; true"
    if [ "$SU_AVAILABLE" -eq 1 ]; then
      dev_sh "su -c '$um'" || warn 'su 解除遮蔽失败'
    else
      helper_c "$um" || warn 'daemon 已失效，遮蔽挂载会在下次重启时消失'
    fi
    dev_sh 'grep -E " /data/adb/modules" /proc/mounts' || info '遮蔽已解除（无残留挂载）'
  fi

  step '验证 KernelSU'
  if dev_sh 'cat /proc/modules' 2>/dev/null | grep -q '^kernelsu '; then
    info 'kernelsu: loaded'
  else
    warn 'kernelsu: 未出现在 /proc/modules'
  fi
  su="$(dev_sh_timeout 15 'su -c id' 2>/dev/null | tr -d '\r' | head -n 1 || true)"
  if printf '%s' "$su" | grep -q 'uid=0'; then
    info "su: $su"
  else
    warn 'su -c id 未返回 uid=0；可先在 KernelSU 管理器里授权 shell，或继续用临时 root daemon'
  fi
}

main() {
  local action
  while [ "$#" -gt 0 ]; do
    case "$1" in
      --serial)  SERIAL="$2"; ADB=(adb -s "$SERIAL"); shift 2 ;;
      --attempts) ATTEMPTS="$2"; shift 2 ;;
      --reroot)  REROOT=1; shift ;;
      --no-backup) NO_BACKUP=1; shift ;;
      --no-su)   NO_SU=1; shift ;;
      --shadow-modules) SHADOW=1; shift ;;
      --dry-run) DRY_RUN=1; shift ;;
      -h|--help) usage 0 ;;
      --) shift; break ;;
      -*) die "unknown option: $1" ;;
      *) break ;;
    esac
  done
  [ "$#" -ge 1 ] || usage 2
  action="$1"; shift

  if [ "$DRY_RUN" -eq 0 ]; then
    "${ADB[@]}" get-state >/dev/null 2>&1 || die "没有 adb 设备；插上 USB 并确认 'adb devices' 已授权（或 --serial <ip:port> 走无线调试）"
  fi

  case "$action" in
    status)      action_status ;;
    list)        action_list ;;
    diag)        action_diag ;;
    match)       action_match "$@" ;;
    backup)      action_backup "$@" ;;
    disable)     action_disable "$@" ;;
    disable-all) action_disable_all ;;
    purge)       action_purge "$@" ;;
    metamodule-off) action_metamodule_off ;;
    ksud)        action_ksud "$@" ;;
    driver)      action_driver ;;
    chain)       action_chain ;;
    snapshot)    action_snapshot ;;
    driver-unload) action_driver_unload ;;
    load-ksu)    action_load_ksu ;;
    *)           die "unknown action: $action" ;;
  esac
}

main "$@"
