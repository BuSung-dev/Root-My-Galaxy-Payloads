API ?= 35
TARGET ?= pa3q-S938NKSUACZF1
OUTDIR ?= build/$(TARGET)

# Validate TARGET with pure-make checks (no shell: avoids echo injection).
# Allowed: single word matching ^[A-Za-z0-9._-]+$.
SQ := '
HASH := \#
LP := (
RP := )
EMPTY :=
SPACE := $(EMPTY) $(EMPTY)
ifeq ($(words $(TARGET)),1)
else
$(error Invalid TARGET "$(TARGET)": must be a single word)
endif
ifneq ($(strip $(TARGET)),)
else
$(error Invalid TARGET: must not be empty)
endif
ifneq ($(firstword $(filter -%,$(TARGET))),)
$(error Invalid TARGET "$(TARGET)": must not start with -)
endif
TARGET_BAD := $(findstring /,$(TARGET))$(findstring \,$(TARGET))$(findstring $,$(TARGET))$(findstring `,$(TARGET))$(findstring ",$(TARGET))$(findstring $(SQ),$(TARGET))$(findstring ;,$(TARGET))$(findstring &,$(TARGET))$(findstring |,$(TARGET))$(findstring $(LP),$(TARGET))$(findstring $(RP),$(TARGET))$(findstring <,$(TARGET))$(findstring >,$(TARGET))$(findstring *,$(TARGET))$(findstring ?,$(TARGET))$(findstring [,$(TARGET))$(findstring ],$(TARGET))$(findstring {,$(TARGET))$(findstring },$(TARGET))$(findstring ~,$(TARGET))$(findstring !,$(TARGET))$(findstring $(HASH),$(TARGET))$(findstring %,$(TARGET))$(findstring :,$(TARGET))$(findstring =,$(TARGET))$(findstring +,$(TARGET))$(findstring @,$(TARGET))$(findstring ^,$(TARGET))$(findstring $(SPACE),$(TARGET))
# NOTE: comma is shell-harmless and would only produce a nonexistent
# src/targets/... path; space is already rejected by the words check above.
ifneq ($(TARGET_BAD),)
$(error Invalid TARGET "$(TARGET)": must match ^[A-Za-z0-9._-]+$$)
endif

# API must be a plain integer (used in compiler path and echoed to shell).
ifeq ($(strip $(API)),)
$(error Invalid API: must not be empty)
endif
# Reject anything that is not all digits.
ifneq ($(words $(API)),1)
$(error Invalid API "$(API)": must be a single word)
endif
API_DIGITS := $(subst 0,,$(subst 1,,$(subst 2,,$(subst 3,,$(subst 4,,$(subst 5,,$(subst 6,,$(subst 7,,$(subst 8,,$(subst 9,,$(API)))))))))))
ifneq ($(API_DIGITS),)
$(error Invalid API "$(API)": must contain only digits 0-9)
endif

# Guard against destructive OUTDIR with pure-make checks.
# Allowed chars: [A-Za-z0-9._-/]; absolute paths permitted.
ifeq ($(strip $(OUTDIR)),)
$(error Refusing to use empty OUTDIR)
endif
ifeq ($(OUTDIR),/)
$(error Refusing to use OUTDIR "/")
endif
ifneq ($(findstring ..,$(OUTDIR)),)
$(error Refusing to use OUTDIR "$(OUTDIR)" containing ..)
endif
ifneq ($(firstword $(filter -%,$(OUTDIR))),)
$(error Refusing to use OUTDIR "$(OUTDIR)" starting with -)
endif
OUTDIR_BAD := $(findstring \,$(OUTDIR))$(findstring $,$(OUTDIR))$(findstring `,$(OUTDIR))$(findstring ",$(OUTDIR))$(findstring $(SQ),$(OUTDIR))$(findstring ;,$(OUTDIR))$(findstring &,$(OUTDIR))$(findstring |,$(OUTDIR))$(findstring $(LP),$(OUTDIR))$(findstring $(RP),$(OUTDIR))$(findstring <,$(OUTDIR))$(findstring >,$(OUTDIR))$(findstring *,$(OUTDIR))$(findstring ?,$(OUTDIR))$(findstring [,$(OUTDIR))$(findstring ],$(OUTDIR))$(findstring {,$(OUTDIR))$(findstring },$(OUTDIR))$(findstring ~,$(OUTDIR))$(findstring !,$(OUTDIR))$(findstring $(HASH),$(OUTDIR))$(findstring %,$(OUTDIR))$(findstring :,$(OUTDIR))$(findstring =,$(OUTDIR))$(findstring +,$(OUTDIR))$(findstring @,$(OUTDIR))$(findstring ^,$(OUTDIR))$(findstring $(SPACE),$(OUTDIR))
ifneq ($(OUTDIR_BAD),)
$(error Refusing to use OUTDIR "$(OUTDIR)": must match ^[A-Za-z0-9._/-]+$$)
endif

SLIDE_STACK_WRITER_TARGETS := dm2q-S916BXXSAFZG1 dm3q-S918BXXSAFZF5 dm3q-S9180ZHS8FZG1 gts9u-X916BXXS6EZG3 dm1q-S911U1UES6DYI3 gts9-X710XXS6EZF1
ifneq ($(filter $(TARGET),$(SLIDE_STACK_WRITER_TARGETS)),)
APP_TARGET_CFLAGS := -DSLIDE_STACK_WRITER=1
else
APP_TARGET_CFLAGS :=
endif
ifeq ($(TARGET),a53x-A536EXXSNGZG3)
API := 31
endif

TARGET_HEADER := src/targets/$(TARGET)/target.h
TARGET_INCLUDE := targets/$(TARGET)/target.h
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
TARGET_CC := $(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android$(API)-clang
else
TARGET_CC := $(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android$(API)-clang
endif

# Bare `make` defaults to `all` (needs toolchain); only the listed
# inspection/cleanup goals skip the check.
ifeq ($(strip $(MAKECMDGOALS)),)
NEEDS_TC := all
else
NEEDS_TC := $(filter-out help info clean distclean,$(MAKECMDGOALS))
endif
ifeq ($(NEEDS_TC),)
# info/help/clean-only invocation: skip toolchain check.
else
ifeq ($(wildcard $(TARGET_CC)),)
$(error set ANDROID_NDK_HOME to an Android NDK containing aarch64-linux-android$(API)-clang for $(UNAME_S))
endif
endif

PRELOAD := $(OUTDIR)/cve-2026-43499
APP_PRELOAD := $(OUTDIR)/cve-2026-43499-app.so
APP_RELEASE := $(OUTDIR)/cve-2026-43499-app.release.so
APP_STABLE := $(OUTDIR)/cve-2026-43499-app.stable.so
APP_RELEASE_SIZE := 104128
ROOT_HELPER := $(OUTDIR)/cve-2026-43499-root
TARGET_CFLAGS :=
APP_RELEASE_OPT := -Oz
APP_RELEASE_LINK_FLAGS := -Wl,--gc-sections -Wl,--icf=all -s

PRELOAD_SRCS := \
  src/main.c \
  src/util.c \
  src/slide.c \
  src/fops.c \
  src/pipe.c \
  src/root.c \
  src/preload.c

APP_PRELOAD_SRCS := \
  src/main.c \
  src/util.c \
  src/slide_app.c \
  src/fops.c \
  src/pipe.c \
  src/root.c \
  src/preload.c

ifeq ($(TARGET),a53x-A536EXXSNGZG3)
APP_PRELOAD_SRCS := \
  src/targets/a53x-A536EXXSNGZG3/payload.c \
  src/targets/a53x-A536EXXSNGZG3/chain.c \
  src/targets/a53x-A536EXXSNGZG3/ghostlock.c \
  src/targets/a53x-A536EXXSNGZG3/page.c
PRELOAD_SRCS := $(APP_PRELOAD_SRCS)
APP_RELEASE_OPT := -O2
APP_RELEASE_LINK_FLAGS := -Wl,--gc-sections -Wl,--icf=all -s
endif

COMMON_CFLAGS := \
  -O2 -g0 -Wall -Wextra \
  -Wno-unused-parameter -Wno-sign-compare \
  -Isrc -DTARGET_HEADER='"$(TARGET_INCLUDE)"' \
  $(TARGET_CFLAGS)

.DEFAULT_GOAL := all

.PHONY: all clean distclean info release stable help

# Portable file size check (works on GNU + BSD/macOS): wc -c instead of stat -c %s.
# Truncate may be GNU-only; fall back to python3 to pad with zeros.

all: $(PRELOAD) $(APP_PRELOAD) $(ROOT_HELPER)

release: $(APP_RELEASE)

stable: $(APP_STABLE)

$(OUTDIR):
	mkdir -p -- "$@"

$(PRELOAD): $(PRELOAD_SRCS) $(TARGET_HEADER) src/offset.h src/common.h src/kernelsnitch/*.h | $(OUTDIR)
	"$(TARGET_CC)" -fPIC $(COMMON_CFLAGS) $(PRELOAD_SRCS) \
	  -shared -pthread -o "$@"

$(ROOT_HELPER): src/su_daemon.c | $(OUTDIR)
	"$(TARGET_CC)" -fPIE -pie -O2 -g0 -Wall -Wextra "$<" -ldl -o "$@"

$(APP_PRELOAD): $(APP_PRELOAD_SRCS) $(TARGET_HEADER) src/offset.h src/common.h src/kernelsnitch/*.h | $(OUTDIR)
	"$(TARGET_CC)" -DAPP_PAYLOAD=1 $(APP_TARGET_CFLAGS) -fPIC $(COMMON_CFLAGS) $(APP_PRELOAD_SRCS) \
	  -shared -pthread -o "$@"

$(APP_RELEASE): $(APP_PRELOAD_SRCS) $(TARGET_HEADER) src/offset.h src/common.h src/kernelsnitch/*.h | $(OUTDIR)
	"$(TARGET_CC)" -DAPP_PAYLOAD=1 $(APP_TARGET_CFLAGS) -fPIC $(APP_RELEASE_OPT) -g0 \
	  -fno-unwind-tables -fno-asynchronous-unwind-tables \
	  -ffunction-sections -fdata-sections \
	  -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
	  -Isrc -DTARGET_HEADER='"$(TARGET_INCLUDE)"' \
	  $(TARGET_CFLAGS) \
	  $(APP_PRELOAD_SRCS) -shared -pthread \
	  $(APP_RELEASE_LINK_FLAGS) -o "$@"
	@test $$(wc -c < "$@") -le $(APP_RELEASE_SIZE) || { echo "error: $@ exceeds $(APP_RELEASE_SIZE) bytes" >&2; exit 1; }
	@python3 -c "import sys; p=sys.argv[1]; n=int(sys.argv[2]); d=open(p,'rb').read(); open(p,'ab').write(b'\0'*(n-len(d)) if len(d)<n else b'')" "$@" $(APP_RELEASE_SIZE)

$(APP_STABLE): $(APP_PRELOAD_SRCS) $(TARGET_HEADER) src/offset.h src/common.h src/kernelsnitch/*.h | $(OUTDIR)
	"$(TARGET_CC)" -DAPP_PAYLOAD=1 -DAPP_S928_STABLE_RACE=1 $(APP_TARGET_CFLAGS) \
	  -fPIC -Oz -g0 -fvisibility=hidden -fno-semantic-interposition \
	  -fstack-protector-strong \
	  -fno-unwind-tables -fno-asynchronous-unwind-tables \
	  -ffunction-sections -fdata-sections \
	  -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
	  -Isrc -DTARGET_HEADER='"$(TARGET_INCLUDE)"' \
	  $(APP_PRELOAD_SRCS) -shared -pthread \
	  -Wl,--gc-sections -Wl,--icf=all -s -o "$@"
	@test $$(wc -c < "$@") -le $(APP_RELEASE_SIZE) || { echo "error: $@ exceeds $(APP_RELEASE_SIZE) bytes" >&2; exit 1; }
	@python3 -c "import sys; p=sys.argv[1]; n=int(sys.argv[2]); d=open(p,'rb').read(); open(p,'ab').write(b'\0'*(n-len(d)) if len(d)<n else b'')" "$@" $(APP_RELEASE_SIZE)

info:
	@echo "TARGET=$(TARGET)"
	@echo "APP_TARGET_CFLAGS=$(APP_TARGET_CFLAGS)"
	@echo "TARGET_CC=$(TARGET_CC)"
	@echo "PRELOAD=$(PRELOAD)"
	@echo "APP_PRELOAD=$(APP_PRELOAD)"
	@echo "APP_RELEASE=$(APP_RELEASE)"
	@echo "APP_STABLE=$(APP_STABLE)"
	@echo "ROOT_HELPER=$(ROOT_HELPER)"

clean:
	rm -rf -- "$(OUTDIR)"

distclean:
	rm -rf -- build

help:
	@echo "Targets: all release stable clean distclean info help"
	@echo "  TARGET=<profile> (default: $(TARGET))  API=<level> (default: $(API))"
	@echo "  OUTDIR=<dir> (default: build/\$$(TARGET))"
