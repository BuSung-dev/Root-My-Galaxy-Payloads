API ?= 35
NDK_ROOT ?= $(ANDROID_NDK_HOME)
OUTDIR ?= build

ifeq ($(OS),Windows_NT)
PREBUILT := windows-x86_64
CC_SUFFIX := .cmd
else
ifeq ($(shell uname -s),Darwin)
PREBUILT := darwin-x86_64
else
PREBUILT := linux-x86_64
endif
endif

NDK_CC := $(NDK_ROOT)/toolchains/llvm/prebuilt/$(PREBUILT)/bin/aarch64-linux-android$(API)-clang$(CC_SUFFIX)

SRCS := src/core/main.c src/core/util.c src/core/kaslr.c \
        src/core/fops.c src/core/pipe.c src/core/root.c src/core/umh_helper.c
HEADERS := $(wildcard src/core/*.h src/core/kernelsnitch/*.h src/devices/*.h src/devices/*/*.h)
CFLAGS := -O2 -Wall -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function \
          -Isrc/core -Isrc/devices -DTARGET_CONFIG_H=\"target.h\"
LDFLAGS := -fPIE -pie -pthread

.DEFAULT_GOAL := all
.PHONY: all

all: $(OUTDIR)/payload $(OUTDIR)/pompomsu-helper

$(OUTDIR):
	mkdir -p "$@"

$(OUTDIR)/payload: $(SRCS) $(HEADERS) | $(OUTDIR)
	"$(NDK_CC)" $(CFLAGS) $(LDFLAGS) $(SRCS) -o "$@"

$(OUTDIR)/pompomsu-helper: src/core/su_daemon.c | $(OUTDIR)
	"$(NDK_CC)" $(CFLAGS) $(LDFLAGS) $< -ldl -o "$@"
