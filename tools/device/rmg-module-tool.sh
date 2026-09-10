#!/system/bin/sh
# rmg-module-tool.sh -- device-side half of tools/module-rescue.sh.
#
# This script runs ON THE PHONE, as root, inside the bootstrap root daemon that
# the CVE-2026-43499 exploit leaves behind:
#
#   /data/local/tmp/cve-2026-43499-root -c \
#       'sh /data/local/tmp/rmg-module-tool.sh list'
#
# It only ever touches the KernelSU/Magisk module trees (/data/adb/modules,
# /data/adb/modules_update, /data/adb/ksu/module_configs) and /data/local/tmp.
#
# Module names are arbitrary (non-ASCII, spaces, ...) and are always handled
# line-by-line with quoted expansions; the only hard rule is that a name must
# not contain "/" and must not be "." or "..", so nothing can escape the module
# trees.
#
# Actions:
#   status
#   list
#   diag
#   match    [pattern...]
#   backup   [id...]
#   disable  [id...]
#   disable-all
#   check-disabled [id...]
#   metamodule-off
#   purge    [id...]
#   verify   [id...]
#
# Actions that take ids/patterns read them either from argv or, when argv is
# empty, one per line from /data/local/tmp/rmg-module-args. The host helper uses
# the file so nothing has to survive nested adb/shell quoting.
#
# A pattern matches a module when it globs the module id ("*cert*") or when it
# appears literally in the id, the module.prop name or the description ("cert").
# Matching is case-insensitive; a bare pattern that is an exact module name is
# also resolved with stat, so operations still work when readdir is blocked.

MODS=/data/adb/modules
UPD=/data/adb/modules_update
CFG=/data/adb/ksu/module_configs
PF=/data/adb/post-fs-data.d
SVC=/data/adb/service.d
META=/data/adb/metamodule
ARGS_FILE=/data/local/tmp/rmg-module-args
BACKUP_ROOT=/data/local/tmp/rmg-module-backup

say() { printf '%s\n' "$*"; }
err() { printf '[-] %s\n' "$*" >&2; }

# safe_name: usable as a single path element under the module trees.
safe_name() {
  case "$1" in
    ''|.|..) return 1 ;;
  esac
  case "$1" in
    */*|*..*) return 1 ;;
  esac
  return 0
}

# plain_name: additionally restricted to the ASCII form the tooling prefers.
plain_name() {
  safe_name "$1" || return 1
  printf '%s' "$1" | grep -Eq '^[A-Za-z0-9._-]{1,64}$'
}

collect_args() {
  if [ "$#" -ge 1 ]; then
    printf '%s\n' "$@"
    return 0
  fi
  if [ -f "$ARGS_FILE" ]; then
    cat "$ARGS_FILE"
  fi
  return 0
}

prop() {
  # prop <module.prop> <key>
  [ -f "$1" ] || return 0
  sed -n "s/^$2=//p" "$1" | head -n 1
}

describe() {
  # describe <base> <id>
  d="$1/$2"
  if [ -f "$d/disable" ]; then state=DISABLED; else state=enabled; fi
  printf '  %s   [%s]\n' "$2" "$state"
  [ -f "$d/remove" ] && printf '      marker  : remove\n'
  [ -f "$d/skip_mount" ] && printf '      marker  : skip_mount\n'
  name=$(prop "$d/module.prop" name)
  ver=$(prop "$d/module.prop" version)
  desc=$(prop "$d/module.prop" description)
  [ -n "$name" ] && printf '      name    : %s\n' "$name"
  [ -n "$ver" ] && printf '      version : %s\n' "$ver"
  [ -n "$desc" ] && printf '      desc    : %s\n' "$desc"
  entries=''
  for p in system zygisk webroot post-fs-data.sh service.sh uninstall.sh action.sh; do
    [ -e "$d/$p" ] && entries="$entries $p"
  done
  [ -n "$entries" ] && printf '      entries :%s\n' "$entries"
  if [ -d "$d/system/etc/security/cacerts" ]; then
    cacerts=$(ls "$d/system/etc/security/cacerts" 2>/dev/null | wc -l | tr -d ' ')
    printf '      cacerts : %s file(s) under system/etc/security/cacerts\n' "$cacerts"
  fi
  if [ -d "$d/zygisk" ]; then
    printf '      zygisk  : %s\n' "$(ls "$d/zygisk" 2>/dev/null | tr '\n' ' ')"
  fi
  size=$(du -sk "$d" 2>/dev/null | sed 's/[^0-9].*//')
  [ -n "$size" ] && printf '      size    : %s KiB\n' "$size"
  plain_name "$2" || say '      note    : unusual name (non-ASCII / space); supported by purge/disable/backup'
  return 0
}

list_tree() {
  # list_tree <label> <dir>
  say "== $1 ($2) =="
  count=0
  for d in "$2"/*; do
    [ -d "$d" ] || continue
    id=${d##*/}
    count=$((count + 1))
    describe "$2" "$id"
  done
  say "   ($count module dir(s))"
}

action_list() {
  say "context: $(cat /proc/self/attr/current 2>/dev/null)  enforce=$(cat /sys/fs/selinux/enforce 2>/dev/null)"
  say
  say "raw $MODS:"
  ls -la "$MODS" 2>&1
  say "raw $UPD:"
  ls -la "$UPD" 2>&1
  say
  list_tree /data/adb/modules "$MODS"
  say
  list_tree /data/adb/modules_update "$UPD"
  say
  say "== $CFG =="
  if [ -d "$CFG" ]; then
    ls -la "$CFG" 2>&1
  else
    say '   (absent)'
  fi
  say
  say "== $PF =="
  if [ -d "$PF" ]; then
    ls -la "$PF" 2>&1
  else
    say '   (absent)'
  fi
  say
  say "== $SVC =="
  if [ -d "$SVC" ]; then
    ls -la "$SVC" 2>&1
  else
    say '   (absent)'
  fi
  say
  say "== $META =="
  if [ -L "$META" ] || [ -e "$META" ]; then
    ls -ld "$META" 2>&1
    say "    (dereferenced: $( [ -d "$META" ] && echo 'target exists' || echo 'target MISSING / dangling' ))"
  else
    say '   (absent)'
  fi
}

action_diag() {
  say '== self =='
  say "context   : $(cat /proc/self/attr/current 2>/dev/null)"
  say "enforce   : $(cat /sys/fs/selinux/enforce 2>/dev/null)"
  say "id        : $(id 2>/dev/null)"
  say
  say '== /data/adb =='
  ls -la /data/adb 2>&1
  for d in modules modules_update ksu post-fs-data.d service.d; do
    say
    say "== /data/adb/$d =="
    ls -la "/data/adb/$d" 2>&1
  done
  say
  say "== $META =="
  if [ -L "$META" ]; then
    say "symlink target : $(readlink "$META")"
  fi
  say "-L: $( [ -L "$META" ] && echo yes || echo no )   -e: $( [ -e "$META" ] && echo yes || echo no )   -d: $( [ -d "$META" ] && echo yes || echo no )"
  say 'ls -la with trailing slash (dereferences the target):'
  ls -la "$META/" 2>&1
  ls -la "$META/metamount.sh" 2>&1
  say
  say '== any module.prop under /data/adb (depth 5) =='
  find /data/adb -maxdepth 5 -name module.prop 2>/dev/null
  say
  say '== KSU logs (/data/adb/ksu/log) =='
  found_log=0
  for f in /data/adb/ksu/log/*; do
    [ -f "$f" ] || continue
    found_log=1
    say "--- $f (tail 40) ---"
    tail -n 40 "$f" 2>&1
  done
  [ "$found_log" = 1 ] || say '   (none)'
  say
  say '== KSU config files =='
  for f in /data/adb/ksu/.feature_config /data/adb/ksu/.ksurc /data/adb/ksurc; do
    [ -f "$f" ] || continue
    say "--- $f ---"
    cat "$f" 2>&1
  done
  say
  say '== mounts touching /data =='
  grep -E ' /data' /proc/mounts 2>/dev/null
  say
  say '== permission / visibility probes =='
  for p in /data/adb /data/adb/modules /data/adb/modules_update; do
    say "$p: r=$( [ -r "$p" ] && echo y || echo n ) w=$( [ -w "$p" ] && echo y || echo n ) x=$( [ -x "$p" ] && echo y || echo n )"
  done
  if ls /data/adb/modules >/dev/null 2>&1; then
    say '[+] readdir /data/adb/modules: OK'
  else
    say '[-] readdir /data/adb/modules: DENIED (listing above cannot be trusted)'
  fi
  if : >/data/adb/rmg-write-probe 2>/dev/null; then
    say '[+] /data/adb is writable from this context'
    rm -f /data/adb/rmg-write-probe
  else
    say '[-] /data/adb is NOT writable from this context'
  fi
  say
  say '== dmesg: avc / defex / kdp / watchdog / shutdown =='
  dmesg 2>/dev/null | grep -iE 'avc: *denied|watchdog|panic|sys.powerctl|Shutting down|restart|defex|kdp|rkp|safeplace|signature' | tail -n 30
  say
  say '== boot reason props + cmdline =='
  say "ro.boot.bootreason      : $(getprop ro.boot.bootreason 2>/dev/null)"
  say "ro.boot.bootloader      : $(getprop ro.boot.bootloader 2>/dev/null)"
  say "ro.boot.warranty_bit    : $(getprop ro.boot.warranty_bit 2>/dev/null)"
  tr ' ' '\n' </proc/cmdline 2>/dev/null | grep -iE 'reset|reason|restart|wdt|pon|lpm'
  say
  say '== /data/local/tmp =='
  ls -la /data/local/tmp 2>&1
  say
  say '== pstore (last crash, persists across reboot) =='
  ls -la /sys/fs/pstore 2>&1
  for f in /sys/fs/pstore/console-ramoops*; do
    [ -f "$f" ] || continue
    say "--- $f (tail 30) ---"
    tail -n 30 "$f" 2>&1
  done
}

pattern_hit() {
  # pattern_hit <lc_id> <lc_text> -- reads patterns (one per line) from $pats
  ph_hit=0
  while IFS= read -r p; do
    [ -n "$p" ] || continue
    lc_p=$(printf '%s' "$p" | tr 'A-Z' 'a-z')
    case "$1" in $lc_p) ph_hit=1 ;; esac
    case "$2" in *"$lc_p"*) ph_hit=1 ;; esac
  done <<EOF
$pats
EOF
}

action_match() {
  pats=$(collect_args "$@" | tr -d '\r')
  [ -n "$pats" ] || { err 'match needs at least one pattern'; exit 2; }
  seen=''
  for base in "$MODS" "$UPD"; do
    for d in "$base"/*; do
      [ -d "$d" ] || continue
      id=${d##*/}
      safe_name "$id" || continue
      text="$id $(prop "$d/module.prop" name) $(prop "$d/module.prop" description) $(prop "$d/module.prop" author)"
      lc_id=$(printf '%s' "$id" | tr 'A-Z' 'a-z')
      lc_text=$(printf '%s' "$text" | tr 'A-Z' 'a-z')
      pattern_hit "$lc_id" "$lc_text"
      [ "$ph_hit" = 1 ] || continue
      case "|$seen|" in *"|$id|"*) continue ;; esac
      seen="$seen|$id"
      printf '%s\n' "$id"
    done
  done
  # Fallback for a tree we cannot readdir: a bare pattern that is a safe module
  # name is also tested directly with stat.
  while IFS= read -r p; do
    [ -n "$p" ] || continue
    safe_name "$p" || continue
    case "|$seen|" in *"|$p|"*) continue ;; esac
    for base in "$MODS" "$UPD"; do
      [ -e "$base/$p" ] || continue
      seen="$seen|$p"
      printf '%s\n' "$p"
      break
    done
  done <<EOF
$pats
EOF
}

action_backup() {
  ids=$(collect_args "$@" | tr -d '\r')
  [ -n "$ids" ] || { err 'backup needs at least one module id'; exit 2; }
  ts=$(date +%Y%m%d-%H%M%S)
  out="$BACKUP_ROOT/$ts"
  mkdir -p "$out" || { err "cannot create $out"; exit 3; }
  while IFS= read -r id; do
    [ -n "$id" ] || continue
    safe_name "$id" || { err "refusing unsafe module name: $id"; exit 2; }
    found=0
    for base in "$MODS" "$UPD"; do
      [ -d "$base/$id" ] || continue
      cp -a "$base/$id" "$out/$id" || { err "cp failed: $base/$id"; exit 3; }
      say "[+] $base/$id -> $out/$id"
      found=1
    done
    for f in "$CFG/$id"*; do
      [ -e "$f" ] || continue
      cp -a "$f" "$out/" || err "cp failed: $f"
      say "[+] $f -> $out/"
      found=1
    done
    [ "$found" = 1 ] || say "[-] $id: nothing to back up"
  done <<EOF
$ids
EOF
  du -sk "$out" 2>/dev/null | sed 's/[^0-9].*//' | sed 's/^/[*] total: /;s/$/ KiB/'
  say "[*] backup complete: $out"
  say "RMG_BACKUP_DIR=$out"
}

action_disable() {
  ids=$(collect_args "$@" | tr -d '\r')
  [ -n "$ids" ] || { err 'disable needs at least one module id'; exit 2; }
  while IFS= read -r id; do
    [ -n "$id" ] || continue
    safe_name "$id" || { err "refusing unsafe module name: $id"; exit 2; }
    hit=0
    for base in "$MODS" "$UPD"; do
      [ -d "$base/$id" ] || continue
      : >"$base/$id/disable" || { err "cannot create $base/$id/disable"; exit 3; }
      say "[+] disabled $base/$id"
      hit=1
    done
    [ "$hit" = 1 ] || say "[-] $id: no module dir found, nothing to disable"
  done <<EOF
$ids
EOF
}

action_disable_all() {
  n=0
  for base in "$MODS" "$UPD"; do
    for d in "$base"/*; do
      [ -d "$d" ] || continue
      id=${d##*/}
      safe_name "$id" || continue
      : >"$d/disable" || { err "cannot create $d/disable"; exit 3; }
      say "[+] disabled $base/$id"
      n=$((n + 1))
    done
  done
  say "[*] $n module dir(s) marked disabled"
}

action_check_disabled() {
  ids=$(collect_args "$@" | tr -d '\r')
  [ -n "$ids" ] || { err 'check-disabled needs at least one module id'; exit 2; }
  rc=0
  while IFS= read -r id; do
    [ -n "$id" ] || continue
    safe_name "$id" || { err "refusing unsafe module name: $id"; exit 2; }
    hit=0
    for base in "$MODS" "$UPD"; do
      [ -d "$base/$id" ] || continue
      hit=1
      if [ -f "$base/$id/disable" ]; then
        say "[+] $base/$id: disabled"
      else
        err "not disabled: $base/$id"
        rc=1
      fi
    done
    if [ "$hit" = 0 ]; then
      err "no module dir found: $id"
      rc=1
    fi
  done <<EOF
$ids
EOF
  exit "$rc"
}

action_metamodule_off() {
  # The metamodule symlink lives at /data/adb/metamodule and points at
  # /data/adb/modules/<id>. A dangling one survives the removal of the module
  # and is the usual leftover of a half-finished metamodule uninstall.
  if [ -L "$META" ]; then
    target=$(readlink "$META")
    say "[*] $META -> $target"
    if [ -e "$META" ]; then
      say "[*] note: the target currently exists; KernelSU will stop using the metamodule"
    else
      say "[*] note: the target does not exist (dangling symlink)"
    fi
    ts=$(date +%Y%m%d-%H%M%S)
    out="$BACKUP_ROOT/$ts"
    mkdir -p "$out" || { err "cannot create $out"; exit 3; }
    printf '%s\n' "$target" >"$out/metamodule.symlink.txt"
    rm -f "$META" || { err "rm failed: $META"; exit 3; }
    say "[+] removed $META (target string kept in $out/metamodule.symlink.txt)"
    say "RMG_BACKUP_DIR=$out"
  elif [ -e "$META" ]; then
    say "[-] $META exists but is not a symlink:"
    ls -ld "$META" 2>&1
  else
    say "[-] $META does not exist, nothing to do"
  fi
}

action_purge() {
  ids=$(collect_args "$@" | tr -d '\r')
  [ -n "$ids" ] || { err 'purge needs at least one module id'; exit 2; }
  while IFS= read -r id; do
    [ -n "$id" ] || continue
    safe_name "$id" || { err "refusing unsafe module name: $id"; exit 2; }
    hit=0
    for base in "$MODS" "$UPD"; do
      [ -e "$base/$id" ] || continue
      # Belt and braces: the target must stay directly under a module tree.
      case "$base/$id" in
        "$MODS"/*|"$UPD"/*) : ;;
        *) err "unexpected path, skipping: $base/$id"; exit 2 ;;
      esac
      rm -rf "$base/$id" || { err "rm -rf failed: $base/$id"; exit 3; }
      say "[+] removed $base/$id"
      hit=1
    done
    for f in "$CFG/$id"*; do
      [ -e "$f" ] || continue
      rm -rf "$f" || err "rm -rf failed: $f"
      say "[+] removed $f"
      hit=1
    done
    [ "$hit" = 1 ] || say "[-] $id: nothing found to remove"
  done <<EOF
$ids
EOF
  sync
}

action_verify() {
  ids=$(collect_args "$@" | tr -d '\r')
  [ -n "$ids" ] || { err 'verify needs at least one module id'; exit 2; }
  rc=0
  while IFS= read -r id; do
    [ -n "$id" ] || continue
    safe_name "$id" || { err "refusing unsafe module name: $id"; exit 2; }
    bad=0
    for base in "$MODS" "$UPD"; do
      if [ -e "$base/$id" ]; then
        err "still present: $base/$id"
        bad=1
      fi
    done
    for f in "$CFG/$id"*; do
      if [ -e "$f" ]; then
        err "still present: $f"
        bad=1
      fi
    done
    if [ "$bad" = 0 ]; then
      say "[+] $id: gone"
    else
      rc=1
    fi
  done <<EOF
$ids
EOF
  exit "$rc"
}

action_status() {
  say "boot_id   : $(cat /proc/sys/kernel/random/boot_id 2>/dev/null)"
  say "uptime    : $(cut -d' ' -f1 /proc/uptime 2>/dev/null) s"
  say "enforcing : $(cat /sys/fs/selinux/enforce 2>/dev/null)"
  if grep -q '^kernelsu' /proc/modules 2>/dev/null; then
    say 'kernelsu  : loaded'
    grep -i '^kernelsu' /proc/modules 2>/dev/null | sed 's/^/            /'
  else
    say 'kernelsu  : not loaded'
  fi
  say "su -c id  : $(su -c id 2>&1 | tr '\n' ' ' | cut -c1-160)"
  say "ksud      : $(ls -l /data/adb/ksu/bin/ksud 2>&1 | cut -c1-120)"
  n=0
  nd=0
  for d in "$MODS"/*; do
    [ -d "$d" ] || continue
    n=$((n + 1))
    [ -f "$d/disable" ] && nd=$((nd + 1))
  done
  say "modules   : $n in $MODS (${nd} disabled)"
}

main() {
  [ "$#" -ge 1 ] || { err 'usage: rmg-module-tool.sh <action> [args...]'; exit 2; }
  action="$1"
  shift
  case "$action" in
    status) action_status ;;
    list) action_list ;;
    diag) action_diag ;;
    match) action_match "$@" ;;
    backup) action_backup "$@" ;;
    disable) action_disable "$@" ;;
    disable-all) action_disable_all ;;
    check-disabled) action_check_disabled "$@" ;;
    metamodule-off) action_metamodule_off ;;
    purge) action_purge "$@" ;;
    verify) action_verify "$@" ;;
    *) err "unknown action: $action"; exit 2 ;;
  esac
}

main "$@"
