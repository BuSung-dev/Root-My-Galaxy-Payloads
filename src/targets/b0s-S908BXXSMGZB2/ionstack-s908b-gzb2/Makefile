API ?= 35
PROJECT ?= S908WVLS8FYG7
PROJECT_SUFFIX := $(if $(USE_BUILDROOT),-qemu,)
OUTDIR ?= build/$(PROJECT)$(PROJECT_SUFFIX)/bin

TARGET_DIR := src/targets/$(PROJECT)
TARGET_HEADER := $(TARGET_DIR)/target.h

ifeq ($(wildcard $(TARGET_HEADER)),)
$(error unknown PROJECT=$(PROJECT), missing $(TARGET_HEADER))
endif

# pick_src: use target-specific override if it exists, otherwise use src/
define pick_src
$(if $(wildcard $(TARGET_DIR)/$(1)),$(TARGET_DIR)/$(1),src/$(1))
endef

# Auto-detect exp32 vs exp64 based on whether target has an exp64/ subdir
USE_EXP64 := $(if $(wildcard $(TARGET_DIR)/exp64),1,)

PRELOAD := $(OUTDIR)/cve-2026-43499
ROOT_HELPER := $(OUTDIR)/cve-2026-43499-root

ifdef USE_EXP64
EXP_OUT := $(OUTDIR)/cve-exp64
BLOB_S := $(call pick_src,exp64_blob.S)
CORE_SRCS := \
  $(call pick_src,main.c) \
  $(call pick_src,util.c) \
  $(call pick_src,slide.c) \
  $(call pick_src,fops.c) \
  $(call pick_src,pipe.c) \
  $(call pick_src,api.c) \
  $(BLOB_S) \
  $(call pick_src,root.c)
else
EXP_OUT := $(OUTDIR)/cve-exp32
BLOB_S := src/exp32_blob.S
CORE_SRCS := \
  $(call pick_src,main.c) \
  $(call pick_src,util.c) \
  $(call pick_src,slide.c) \
  $(call pick_src,fops.c) \
  $(call pick_src,pipe.c) \
  $(call pick_src,api.c) \
  $(BLOB_S) \
  $(call pick_src,root.c)
endif

PRELOAD_SRCS := $(CORE_SRCS) $(call pick_src,preload.c)

# Buildroot cross-compilation toolchain
BUILDROOT_DIR := buildroot-tc
BUILDROOT_TOOLCHAIN := $(BUILDROOT_DIR)/bin
BUILDROOT_CC := $(BUILDROOT_TOOLCHAIN)/aarch64-linux-gcc
BUILDROOT_SYSROOT := $(BUILDROOT_DIR)/aarch64-buildroot-linux-gnu/sysroot

DEFAULT_NDK_ROOT := $(or $(wildcard $(HOME)/Android/Sdk/ndk/27.2.12479018),$(HOME)/android-ndk-cache/android-ndk-r29)
NDK_ROOT ?= $(or $(ANDROID_NDK_HOME),$(ANDROID_NDK_ROOT),$(DEFAULT_NDK_ROOT))
NDK_TOOLCHAIN ?= $(if $(NDK_ROOT),$(NDK_ROOT)/toolchains/llvm/prebuilt/linux-x86_64)
NDK_CC := $(NDK_TOOLCHAIN)/bin/aarch64-linux-android$(API)-clang
HOST_CLANG ?= clang
SYSROOT ?= $(if $(NDK_TOOLCHAIN),$(NDK_TOOLCHAIN)/sysroot)
RESOURCE_DIR ?= $(if $(NDK_TOOLCHAIN),$(NDK_TOOLCHAIN)/lib/clang/21)

HOST_TARGET_FLAGS := \
  --target=aarch64-linux-android$(API) \
  --sysroot=$(SYSROOT) \
  -resource-dir $(RESOURCE_DIR) \
  --rtlib=compiler-rt \
  --unwindlib=none

HOST_COMMON_LDFLAGS := \
  -fuse-ld=lld \
  -Wl,-rpath-link,$(SYSROOT)/usr/lib/aarch64-linux-android/$(API) \
  -L$(SYSROOT)/usr/lib/aarch64-linux-android/$(API) \
  -L$(SYSROOT)/usr/lib/aarch64-linux-android

HOST_PIE_LDFLAGS := \
  $(HOST_COMMON_LDFLAGS) \
  -Wl,-dynamic-linker,/system/bin/linker64

ifneq ($(origin CC),default)
  TARGET_CC := $(CC)
  TARGET_FLAGS :=
  TARGET_COMMON_LDFLAGS :=
  TARGET_PIE_LDFLAGS :=
else ifneq ($(and $(USE_BUILDROOT),$(wildcard $(BUILDROOT_CC))),)
  BUILDROOT_CC_WORKS := $(shell $(BUILDROOT_CC) --version >/dev/null 2>&1 && echo yes)
  ifeq ($(BUILDROOT_CC_WORKS),yes)
    TARGET_CC := $(BUILDROOT_CC)
    TARGET_FLAGS := --sysroot=$(BUILDROOT_SYSROOT)
    TARGET_COMMON_LDFLAGS := -L$(BUILDROOT_SYSROOT)/usr/lib -L$(BUILDROOT_SYSROOT)/lib
    TARGET_PIE_LDFLAGS := $(TARGET_COMMON_LDFLAGS) -Wl,-dynamic-linker,/lib/ld-linux-aarch64.so.1
  else
    TARGET_CC := $(HOST_CLANG)
    TARGET_FLAGS := $(HOST_TARGET_FLAGS)
    TARGET_COMMON_LDFLAGS := $(HOST_COMMON_LDFLAGS)
    TARGET_PIE_LDFLAGS := $(HOST_PIE_LDFLAGS)
  endif
else ifneq ($(wildcard $(NDK_CC)),)
  NDK_CC_WORKS := $(shell $(NDK_CC) --version >/dev/null 2>&1 && echo yes)
  ifeq ($(NDK_CC_WORKS),yes)
    TARGET_CC := $(NDK_CC)
    TARGET_FLAGS :=
    TARGET_COMMON_LDFLAGS :=
    TARGET_PIE_LDFLAGS :=
  else
    TARGET_CC := $(HOST_CLANG)
    TARGET_FLAGS := $(HOST_TARGET_FLAGS)
    TARGET_COMMON_LDFLAGS := $(HOST_COMMON_LDFLAGS)
    TARGET_PIE_LDFLAGS := $(HOST_PIE_LDFLAGS)
  endif
else
  TARGET_CC := $(HOST_CLANG)
  TARGET_FLAGS := $(HOST_TARGET_FLAGS)
  TARGET_COMMON_LDFLAGS := $(HOST_COMMON_LDFLAGS)
  TARGET_PIE_LDFLAGS := $(HOST_PIE_LDFLAGS)
endif

EMBEDDIR ?= build/embed

ifdef USE_EXP64
# ── Embedded 64-bit (arm64-v8a) exploit stage ────────────────────────
# exp64 is used when __arm64_compat_sys_setsockopt is a ENOSYS stub
# (e.g. S901WVLS4DWL3) — the 32-bit compat stamp path is dead on those
# kernels.  Stack-stamp uses the native 64-bit setsockopt path instead.
EMBED_EXP := $(EMBEDDIR)/cve_exp64_arm64
EXP_SRCS := $(call pick_src,exp64/main.c) $(call pick_src,exp64/stack.c)

API64 ?= $(API)
NDK_CC64 := $(NDK_TOOLCHAIN)/bin/aarch64-linux-android$(API64)-clang
HOST_CC64 ?= aarch64-linux-android$(API64)-clang

ifneq ($(and $(USE_BUILDROOT),$(wildcard $(BUILDROOT_CC))),)
  EXP_CC := $(BUILDROOT_CC)
  EXP_CFLAGS := --sysroot=$(BUILDROOT_SYSROOT) -O2 -g0 -Wall -Isrc -I$(TARGET_DIR) -pthread \
    -Wno-unused-parameter -Wno-unused-function
  EXP_LDFLAGS := -static -pthread
else ifeq ($(shell command -v $(HOST_CC64) >/dev/null 2>&1 && echo yes),yes)
  EXP_CC := $(HOST_CC64)
  EXP_CFLAGS := -O2 -g0 -Wall -Isrc -I$(TARGET_DIR) -static-pie -pthread
  EXP_LDFLAGS := -static-pie -pthread
else ifneq ($(wildcard $(NDK_CC64)),)
  EXP_CC := $(NDK_CC64)
  EXP_CFLAGS := -O2 -g0 -Wall -Isrc -I$(TARGET_DIR) -fPIE -pthread \
    -Wno-unused-parameter -Wno-unused-function
  EXP_LDFLAGS := -static-pie -pthread
else
  $(error no 64-bit ARM toolchain: install aarch64-linux-android-gcc or an NDK)
endif

else
# ── Embedded 32-bit (armeabi-v7a) exploit stage ─────────────────────
# exp32 MUST stay 32-bit: the stack-stamp only lines up via the compat
# syscall path (see src/exp32/).  Built once, embedded via exp32_blob.S.
EMBED_EXP := $(EMBEDDIR)/cve_exp32_arm32
EXP_SRCS := \
  $(call pick_src,exp32/main.c) \
  $(if $(wildcard $(TARGET_DIR)/stack.c),$(TARGET_DIR)/stack.c,$(call pick_src,exp32/stack.c)) \
  $(wildcard $(TARGET_DIR)/exp32/tls_align.S)

API32 ?= 28
NDK_CC32 := $(NDK_TOOLCHAIN)/bin/armv7a-linux-androideabi$(API32)-clang
HOST_CC32 ?= arm-linux-gnueabi-gcc

ifeq ($(shell command -v $(HOST_CC32) >/dev/null 2>&1 && echo yes),yes)
  EXP_CC := $(HOST_CC32)
  EXP_CFLAGS := -O2 -g0 -Wall -Isrc $(TARGET_CFLAGS) -static -pthread
  EXP_LDFLAGS := -static -pthread
else ifneq ($(wildcard $(NDK_CC32)),)
  EXP_CC := $(NDK_CC32)
  EXP_CFLAGS := -O2 -g0 -Wall -Isrc $(TARGET_CFLAGS) -fPIE -pthread \
    -Wno-unused-parameter -Wno-unused-function
  EXP_LDFLAGS := -static -pie -pthread
else
  $(error no 32-bit ARM toolchain: install arm-linux-gnueabi-gcc or an NDK)
endif
endif

# ── Build flags ────────────────────────────────────────────────────
COMMON_CFLAGS := -O2 -g0 -Wall -Wextra -Isrc
PIE_CFLAGS := -fPIE -pie $(COMMON_CFLAGS)
SO_CFLAGS := -fPIC $(COMMON_CFLAGS)
WARN_CFLAGS := -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function
TARGET_CFLAGS := -DTARGET_CONFIG_H=\"targets/$(PROJECT)/target.h\" -I$(TARGET_DIR)

.DEFAULT_GOAL := preload

.PHONY: all preload root-helper info clean list-projects

all: preload root-helper

preload: $(PRELOAD) $(EXP_OUT)

root-helper: $(ROOT_HELPER)

$(OUTDIR):
	rm -rf $(OUTDIR)
	mkdir -p $@

$(EMBEDDIR):
	mkdir -p $@

$(EMBED_EXP): $(EXP_SRCS) src/kernelsnitch/utils.h | $(EMBEDDIR)
	$(EXP_CC) $(EXP_CFLAGS) $(EXP_SRCS) $(EXP_LDFLAGS) -o $@
	sha256sum $@

$(EXP_OUT): $(EMBED_EXP) | $(OUTDIR)
	cp $< $@
	sha256sum $@

$(ROOT_HELPER): $(call pick_src,su_daemon.c) $(TARGET_HEADER) | $(OUTDIR)
	$(TARGET_CC) $(TARGET_FLAGS) $(PIE_CFLAGS) $(TARGET_CFLAGS) \
	  $(call pick_src,su_daemon.c) $(TARGET_PIE_LDFLAGS) -o $@
	sha256sum $@

$(PRELOAD): $(PRELOAD_SRCS) $(EMBED_EXP) $(TARGET_HEADER) src/offset.h src/common.h src/kernelsnitch/*.h | $(OUTDIR)
	$(TARGET_CC) $(TARGET_FLAGS) $(SO_CFLAGS) $(WARN_CFLAGS) $(TARGET_CFLAGS) \
	  $(PRELOAD_SRCS) $(TARGET_COMMON_LDFLAGS) \
	  -shared -o $@ -pthread -ldl
	ln -sf $(notdir $@) $(OUTDIR)/cve.so
	sha256sum $@

info:
	@echo "PROJECT=$(PROJECT)"
	@echo "USE_EXP64=$(USE_EXP64)"
	@echo "TARGET_DIR=$(TARGET_DIR)"
	@echo "TARGET_CC=$(TARGET_CC)"
	@echo "TARGET_FLAGS=$(TARGET_FLAGS)"
	@echo "TARGET_COMMON_LDFLAGS=$(TARGET_COMMON_LDFLAGS)"
	@echo "TARGET_PIE_LDFLAGS=$(TARGET_PIE_LDFLAGS)"
	@echo "PRELOAD=$(PRELOAD)"
	@echo "ROOT_HELPER=$(ROOT_HELPER)"
	@echo "CORE_SRCS=$(CORE_SRCS)"

list-projects:
	@find src/targets -mindepth 2 -maxdepth 2 -name target.h -printf '%h\n' | sed 's#src/targets/##' | sort

clean:
	rm -rf build
