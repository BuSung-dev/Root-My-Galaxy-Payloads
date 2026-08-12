API ?= 35
TARGET ?= pa3q-S938NKSUACZF1
OUTDIR ?= build/$(TARGET)

ifneq ($(filter $(TARGET),dm3q-S918BXXSAFZF5 a54x-A546EXXSKFZF4),)
STACK_WRITER ?= mcast
ifeq ($(STACK_WRITER),mcast)
STACK_WRITER_CFLAG := -DSLIDE_STACK_WRITER=1
else ifeq ($(STACK_WRITER),sigreturn)
STACK_WRITER_CFLAG := -DSLIDE_STACK_WRITER=2
else
$(error STACK_WRITER must be mcast or sigreturn for $(TARGET))
endif
else
ifneq ($(strip $(STACK_WRITER)),)
$(error STACK_WRITER is not supported for $(TARGET))
endif
STACK_WRITER_CFLAG :=
endif

ifeq ($(TARGET),a54x-A546EXXSKFZF4)
SHELL_PORT_CFLAGS := -DSHELL_PERF_PAGE_ORACLE=1 -DSHELL_TRACEFS_SLIDE=1 -DSHELL_STACK_WRITER=1
SHELL_PORT_SRCS := src/slide_app.c
else
SHELL_PORT_CFLAGS :=
SHELL_PORT_SRCS :=
endif

TARGET_HEADER := src/targets/$(TARGET)/target.h
TARGET_INCLUDE := targets/$(TARGET)/target.h
TARGET_CC := $(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android$(API)-clang

ifeq ($(wildcard $(TARGET_CC)),)
$(error set ANDROID_NDK_HOME to an Android NDK containing $(TARGET_CC))
endif

PRELOAD := $(OUTDIR)/cve-2026-43499
APP_PRELOAD := $(OUTDIR)/cve-2026-43499-app.so
APP_RELEASE := $(OUTDIR)/cve-2026-43499-app.release.so
APP_RELEASE_SIZE := 104128
APP_STACK_WRITER_STAMP := $(OUTDIR)/.stack-writer-$(if $(STACK_WRITER),$(STACK_WRITER),default)
ROOT_HELPER := $(OUTDIR)/cve-2026-43499-root
NATIVE_MCAST_TEST := $(OUTDIR)/test-native-mcast-overlap
PERF_PAGE_TEST := $(OUTDIR)/test-perf-page-oracle

ALL_TARGETS := $(PRELOAD) $(APP_PRELOAD) $(ROOT_HELPER)

PRELOAD_SRCS := \
  src/main.c \
  src/util.c \
  src/slide.c \
  src/fops.c \
  src/pipe.c \
  src/root.c \
  src/preload.c \
  $(SHELL_PORT_SRCS)

APP_PRELOAD_SRCS := \
  src/main.c \
  src/util.c \
  src/slide_app.c \
  src/fops.c \
  src/pipe.c \
  src/root.c \
  src/preload.c

COMMON_CFLAGS := \
  -O2 -g0 -Wall -Wextra \
  -Wno-unused-parameter -Wno-sign-compare \
  -Isrc -DTARGET_HEADER='"$(TARGET_INCLUDE)"'

.DEFAULT_GOAL := all

.PHONY: all clean info release shell-bundle native-mcast-test perf-page-test

all: $(ALL_TARGETS)

release: $(APP_RELEASE)

shell-bundle: $(APP_RELEASE) $(ROOT_HELPER)

native-mcast-test: $(NATIVE_MCAST_TEST)

perf-page-test: $(PERF_PAGE_TEST)

$(OUTDIR):
	mkdir -p $@

$(APP_STACK_WRITER_STAMP): | $(OUTDIR)
	rm -f $(OUTDIR)/.stack-writer-*
	touch $@

$(PRELOAD): $(PRELOAD_SRCS) $(TARGET_HEADER) src/offset.h src/common.h src/kernelsnitch/*.h $(APP_STACK_WRITER_STAMP) | $(OUTDIR)
	$(TARGET_CC) $(STACK_WRITER_CFLAG) $(SHELL_PORT_CFLAGS) -fPIC $(COMMON_CFLAGS) $(PRELOAD_SRCS) \
	  -shared -pthread -Wl,--no-undefined -o $@

$(ROOT_HELPER): src/su_daemon.c | $(OUTDIR)
	$(TARGET_CC) -fPIE -pie -O2 -g0 -Wall -Wextra $< -ldl -o $@

$(NATIVE_MCAST_TEST): tools/test_native_mcast_overlap.c | $(OUTDIR)
	$(TARGET_CC) -fPIE -pie -O2 -g0 -Wall -Wextra -pthread $< -o $@

$(PERF_PAGE_TEST): tools/test_perf_page_oracle.c | $(OUTDIR)
	$(TARGET_CC) -fPIE -pie -O2 -g0 -Wall -Wextra $< -o $@

$(APP_PRELOAD): $(APP_PRELOAD_SRCS) $(TARGET_HEADER) src/offset.h src/common.h src/kernelsnitch/*.h $(APP_STACK_WRITER_STAMP) | $(OUTDIR)
	$(TARGET_CC) -DAPP_PAYLOAD=1 $(STACK_WRITER_CFLAG) -fPIC $(COMMON_CFLAGS) $(APP_PRELOAD_SRCS) \
	  -shared -pthread -Wl,--no-undefined -o $@

$(APP_RELEASE): $(APP_PRELOAD_SRCS) $(TARGET_HEADER) src/offset.h src/common.h src/kernelsnitch/*.h $(APP_STACK_WRITER_STAMP) | $(OUTDIR)
	$(TARGET_CC) -DAPP_PAYLOAD=1 $(STACK_WRITER_CFLAG) -fPIC -Oz -g0 \
	  -fno-unwind-tables -fno-asynchronous-unwind-tables \
	  -ffunction-sections -fdata-sections \
	  -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
	  -Isrc -DTARGET_HEADER='"$(TARGET_INCLUDE)"' \
	  $(APP_PRELOAD_SRCS) -shared -pthread \
	  -Wl,--gc-sections -Wl,--icf=all -s -o $@
	@test $$(stat -c %s $@) -le $(APP_RELEASE_SIZE)
	truncate -s $(APP_RELEASE_SIZE) $@

info:
	@echo "TARGET=$(TARGET)"
	@echo "STACK_WRITER=$(STACK_WRITER)"
	@echo "TARGET_CC=$(TARGET_CC)"
	@echo "PRELOAD=$(PRELOAD)"
	@echo "APP_PRELOAD=$(APP_PRELOAD)"
	@echo "APP_RELEASE=$(APP_RELEASE)"
	@echo "ROOT_HELPER=$(ROOT_HELPER)"
	@echo "NATIVE_MCAST_TEST=$(NATIVE_MCAST_TEST)"
	@echo "PERF_PAGE_TEST=$(PERF_PAGE_TEST)"

clean:
	rm -rf $(OUTDIR)
