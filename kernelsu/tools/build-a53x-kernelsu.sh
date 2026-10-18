#!/usr/bin/env bash
# Build the Samsung A53 5G (a53x / A536E) KernelSU late-load pair from source.
#
# The published pair in ../ is a KernelSU v3.3.0 build (KSU_VERSION 32601). This
# script reproduces the same pair from KernelSU v3.3.1 (KSU_VERSION 32680) so a
# KernelSU Manager 3.3.1 install stops reporting "Kernel update required".
#
# It has two phases because they need different toolchains:
#
#   module  Linux + docker or podman; runs the DDK image that carries the
#           android12-5.10 kernel tree and the Android clang used to build
#           kernelsu.ko.
#   ksud    Linux or Windows with WSL, rustup and an Android NDK; embeds the
#           module produced by the `module` phase.
#
# Usage:
#   kernelsu/tools/build-a53x-kernelsu.sh module
#   kernelsu/tools/build-a53x-kernelsu.sh ksud
#
# Environment overrides:
#   KERNELSU_TAG     KernelSU tag to build (default v3.3.1)
#   KERNELSU_REPO    KernelSU git remote  (default https://github.com/tiann/KernelSU.git)
#   PATCH            Samsung patch to apply (default ../patches/KernelSU-<tag>-samsung-kdp-rkp-defex.patch)
#   CONTAINER_ENGINE Container CLI          (default docker, then podman)
#   DDK_IMAGE        DDK container image  (default ghcr.io/ylarod/ddk-min:android12-5.10-20260313)
#   RELEASE          Exact target kernel release (default the A536EXXSNGZG3 release)
#   ANDROID_NDK_HOME Android NDK root, required by the `ksud` phase
#   OUT_DIR          Artifact output directory (default build/kernelsu-a53x-<tag>)
#   WORK_DIR         Checkout directory        (default build/KernelSU-<tag>)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KERNELSU_TAG="${KERNELSU_TAG:-v3.3.1}"
KERNELSU_REPO="${KERNELSU_REPO:-https://github.com/tiann/KernelSU.git}"
PATCH="${PATCH:-$REPO_ROOT/kernelsu/patches/KernelSU-$KERNELSU_TAG-samsung-kdp-rkp-defex.patch}"
DDK_IMAGE="${DDK_IMAGE:-ghcr.io/ylarod/ddk-min:android12-5.10-20260313}"
# Exact release the module vermagic has to carry. The late loader rewrites
# vermagic from the kernel's own rejection message and retries, so this module
# also loads on A536EXXSOGZH2 devices.
RELEASE="${RELEASE:-5.10.237-android12-9-31999025-abA536EXXSNGZG3}"
OUT_DIR="${OUT_DIR:-$REPO_ROOT/build/kernelsu-a53x-$KERNELSU_TAG}"
WORK_DIR="${WORK_DIR:-$REPO_ROOT/build/KernelSU-$KERNELSU_TAG}"

KMI="android12-5.10"
MODULE_ASSET="userspace/ksud/bin/aarch64/${KMI}_kernelsu.ko"
MODULE_OUT="${OUT_DIR}/${KMI}_kernelsu-A536EXXSNGZG3-kdp.ko"
KSUD_OUT="${OUT_DIR}/ksud-A536EXXSNGZG3-kdp"

log() { printf '\n== %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

# Fetch the tagged KernelSU tree and apply the Samsung patch. An already
# applied patch is accepted so the phases can run independently.
checkout() {
    [ -f "$PATCH" ] || die "missing patch: $PATCH"
    if [ ! -d "$WORK_DIR/.git" ]; then
        git clone --branch "$KERNELSU_TAG" --depth 1 "$KERNELSU_REPO" "$WORK_DIR"
    fi
    if git -C "$WORK_DIR" apply --reverse --check "$PATCH" 2>/dev/null; then
        log "patch already applied in $WORK_DIR"
    else
        git -C "$WORK_DIR" apply --check "$PATCH" ||
            die "patch does not apply to $KERNELSU_TAG: $PATCH"
        git -C "$WORK_DIR" apply "$PATCH"
    fi
}

# Phase 1: the module, built inside the DDK container.
build_module() {
    local engine="${CONTAINER_ENGINE:-}"
    if [ -z "$engine" ]; then
        engine="$(command -v docker || command -v podman || true)"
    fi
    [ -n "$engine" ] || die "docker or podman is required for the module phase"
    mkdir -p "$OUT_DIR"
    checkout

    log "building kernelsu.ko ($KERNELSU_TAG) against $RELEASE with $engine"
    "$engine" run --rm -v "$WORK_DIR:/workspace" -w /workspace/kernel "$DDK_IMAGE" bash -lc "
        set -euo pipefail

        # Full LTO inlines the CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT stub and then
        # drops the selinux_hide write hooks, so build thin like the v3.3.0 pair.
        # Trimmed DDK trees ship no scripts/config, so edit .config directly.
        if [ -x \"\$KDIR/scripts/config\" ]; then
            \"\$KDIR/scripts/config\" --file \"\$KDIR/.config\" \\
                --set-val LTO_CLANG_THIN y --disable LTO_CLANG_FULL
        else
            sed -i -e 's/^CONFIG_LTO_CLANG_FULL=y/# CONFIG_LTO_CLANG_FULL is not set/' \\
                   -e 's/^# CONFIG_LTO_CLANG_THIN is not set/CONFIG_LTO_CLANG_THIN=y/' \\
                   \"\$KDIR/.config\"
            grep -q '^CONFIG_LTO_CLANG_THIN=y' \"\$KDIR/.config\" ||
                printf 'CONFIG_LTO_CLANG_THIN=y\\n' >> \"\$KDIR/.config\"
        fi
        grep -E '^(CONFIG_LTO_CLANG_(FULL|THIN))|^# CONFIG_LTO_CLANG' \"\$KDIR/.config\"
        make -C \"\$KDIR\" olddefconfig modules_prepare

        # modules_prepare regenerates the release string; force the exact target.
        for f in \"\$KDIR/include/config/kernel.release\" \
                 \"\$KDIR/include/generated/utsrelease.h\"; do
            [ -f \"\$f\" ] || continue
            if grep -q '^#define UTS_RELEASE' \"\$f\"; then
                sed -i \"s@^#define UTS_RELEASE .*@#define UTS_RELEASE \\\"$RELEASE\\\"@\" \"\$f\"
            else
                printf '%s\\n' '$RELEASE' > \"\$f\"
            fi
        done

        # The late loader relocates every undefined symbol from /proc/kallsyms,
        # so the module must carry an empty __versions section.
        : > \"\$KDIR/Module.symvers\"

        make -C \"\$KDIR\" M=\"\$PWD\" src=\"\$PWD\" \
            ARCH=arm64 LLVM=1 LLVM_IAS=1 \
            CROSS_COMPILE=aarch64-linux-gnu- \
            CLANG_TRIPLE=aarch64-linux-gnu- \
            CONFIG_KSU=m \
            CONFIG_KSU_SAMSUNG_KDP=y \
            CONFIG_KSU_SAMSUNG_RKP=y \
            CONFIG_KSU_SAMSUNG_DEFEX=y \
            CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y \
            KBUILD_MODPOST_WARN=1 modules

        # KernelSU's own symbol checker has to be built before it can be used.
        clang tools/check_symbol.c -o check_symbol

        ./check_symbol kernelsu.ko \"\$KDIR/vmlinux\"
        modinfo kernelsu.ko | grep -E '^(vermagic|scmversion)'
        readelf -SW kernelsu.ko | grep __versions
        llvm-strip -d kernelsu.ko
        modinfo kernelsu.ko | grep '^vermagic'
    "

    cp "$WORK_DIR/kernel/kernelsu.ko" "$MODULE_OUT"
    mkdir -p "$WORK_DIR/$(dirname "$MODULE_ASSET")"
    cp "$WORK_DIR/kernel/kernelsu.ko" "$WORK_DIR/$MODULE_ASSET"
    verify_module "$MODULE_OUT"
    log "module written to $MODULE_OUT"
}

verify_module() {
    local ko="$1" got
    got="$(modinfo "$ko" 2>/dev/null | sed -n 's/^vermagic:[[:space:]]*//p' || true)"
    [ -n "$got" ] || got="$(strings -a "$ko" | grep -m1 "^$RELEASE " || true)"
    case "$got" in
        "$RELEASE "*) printf 'vermagic ok: %s\n' "$got" ;;
        *) die "module vermagic mismatch: '${got:-<none>}'" ;;
    esac
}

# Phase 2: the late-load binary, built against an NDK.
build_ksud() {
    [ -n "${ANDROID_NDK_HOME:-}" ] || die "set ANDROID_NDK_HOME to an Android NDK"
    [ -f "$MODULE_OUT" ] || die "run the module phase first: $MODULE_OUT"
    mkdir -p "$OUT_DIR"
    checkout
    cp "$MODULE_OUT" "$WORK_DIR/$MODULE_ASSET"

    log "building ksud for aarch64-linux-android"
    # rustup installs to ~/.cargo/bin; service managers may run without HOME.
    if [ -n "${HOME:-}" ] && [ -d "$HOME/.cargo/bin" ]; then
        export PATH="$HOME/.cargo/bin:$PATH"
    fi
    command -v rustup >/dev/null || die "rustup is required: https://rustup.rs"
    rustup target add aarch64-linux-android
    (cd "$WORK_DIR" &&
        python3 scripts/setup_cargo_config.py --ndk-root "$ANDROID_NDK_HOME" --force)
    # rust-embed caches the compressed assets; force a rebuild after the swap.
    touch "$WORK_DIR/userspace/ksud/build.rs"
    (cd "$WORK_DIR/userspace" &&
        cargo build --release --target aarch64-linux-android -p ksud)

    # cargo puts the binary in the workspace target dir, which is the KernelSU
    # root; older layouts kept it under userspace/.
    local built=""
    for candidate in \
        "$WORK_DIR/target/aarch64-linux-android/release/ksud" \
        "$WORK_DIR/userspace/target/aarch64-linux-android/release/ksud"; do
        [ -f "$candidate" ] && { built="$candidate"; break; }
    done
    [ -n "$built" ] || die "cargo finished but no ksud binary was found"

    cp "$built" "$KSUD_OUT"
    log "artifacts"
    stat -c '%n %s bytes' "$MODULE_OUT" "$KSUD_OUT"
    sha256sum "$MODULE_OUT" "$KSUD_OUT"
    cat <<EOF

Publish both files together:

  kernelsu/$(basename "$MODULE_OUT")
  kernelsu/$(basename "$KSUD_OUT")

Then re-run the manual-relocation audit against the recovered target vmlinux.
v3.3.1 rewrote kernel/feature/selinux_hide.c, so the undefined import set
differs from the v3.3.0 module and has to be re-audited:

  python3 kernelsu/tools/audit_module_against_target.py \\
    $MODULE_OUT \\
    /path/to/A536EXXSNGZG3/vmlinux \\
    /path/to/A536EXXSNGZG3/Module.symvers \\
    --manual-relocation
EOF
}

case "${1:-all}" in
    module) build_module ;;
    ksud) build_ksud ;;
    all) build_module; build_ksud ;;
    *) die "usage: $0 [module|ksud|all]" ;;
esac
