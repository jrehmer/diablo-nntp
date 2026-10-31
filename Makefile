
# Diablo 6 portable GNU Make build system
# Platforms: Linux, FreeBSD, illumos (SmartOS, OmniOS, etc.)
#
# Linux:   make -j4
# FreeBSD: gmake -j4
# illumos: gmake -j4
#
# Native FreeBSD make uses the separate BSDmakefile.

.DEFAULT_GOAL := all

UNAME_S := $(shell uname -s)

# Build tools
ifeq ($(UNAME_S),FreeBSD)
CC = clang
else
CC = gcc
endif

AR ?= ar
RANLIB ?= ranlib

# Version defaults shared with BSDmakefile; override with VERS=... SUBREV=...
include build-version.mk

# Compiler settings
CPPFLAGS += -I. -DVERS=\"$(VERS)\" -DSUBREV=\"$(SUBREV)\"
CPPFLAGS += -DDEBUG_ZALLOC -DDEBUG_MBUF

CFLAGS ?= -O2 -g -Wall -Wstrict-prototypes -fno-common
CFLAGS += -MMD -MP

LDFLAGS += -Lobj
LDLIBS += -lfilter -ldiablo -lm -lz -lcrypto

# Detect optional vendor features without embedding AWK regex parentheses
# inside a GNU make $(shell ...) expression. Older GNU make versions may
# misparse the original inline expressions, even for `make clean`.
RADIUS_ENABLED := $(shell awk -v option=RADIUS_ENABLED -f compat/detect-vendor-option.awk lib/vendor.h)
DB_ENABLED := $(shell awk -v option=DB_ENABLED -f compat/detect-vendor-option.awk lib/vendor.h)
CANLOCK_ENABLED := $(shell awk -v option=CANLOCK_ENABLED -f compat/detect-vendor-option.awk lib/vendor.h)
REJECT_DB_ENABLED := $(shell awk -v option=REJECT_DB -f compat/detect-vendor-option.awk lib/vendor.h)
# Either DB authentication or the rejection database uses dbopen().
DB185_REQUIRED := $(if $(filter 1,$(DB_ENABLED) $(REJECT_DB_ENABLED)),1,)

# libcanlock: in the system directories on Debian and Ubuntu, under a package
# prefix on FreeBSD (/usr/local) and SmartOS (/opt/local, /opt/ooce).
ifeq ($(CANLOCK_ENABLED),1)
LDLIBS += -lcanlock
CANLOCK_PREFIX := $(firstword $(foreach dir, /usr/local /opt/local /opt/ooce, \
    $(if $(wildcard $(dir)/include/libcanlock-3/canlock.h),$(dir))))
ifneq ($(CANLOCK_PREFIX),)
CPPFLAGS += -I$(CANLOCK_PREFIX)/include
LDFLAGS += -L$(CANLOCK_PREFIX)/lib -Wl,-R,$(CANLOCK_PREFIX)/lib
endif
endif

# Platform-specific settings

ifeq ($(UNAME_S),FreeBSD)
LDLIBS += -lmd

ifeq ($(RADIUS_ENABLED),1)
LDLIBS += -lradius
endif
endif

ifeq ($(UNAME_S),Linux)
CPPFLAGS += -D_FILE_OFFSET_BITS=64
# libdb-devel supplies libdb.so and the Berkeley DB 1.85 dbopen() shim.
# Neither feature is required when both DB_ENABLED and REJECT_DB are off.
ifeq ($(DB185_REQUIRED),1)
LDLIBS += -ldb
endif
endif

# illumos platform configuration
ifneq ($(filter SunOS illumos,$(UNAME_S)),)

CPPFLAGS += -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64
CPPFLAGS += -D_XOPEN_SOURCE=600 -D__EXTENSIONS__

# Berkeley DB 1.85 compatibility auto-detection.
# Generates obj/db185-config.mk on the first build.
# Either DB authentication or REJECT_DB requires the dbopen() library.
ifeq ($(DB185_REQUIRED),1)

DB185_CONFIG := obj/db185-config.mk

$(DB185_CONFIG): compat/detect-db185.sh | obj
	@echo "Detecting Berkeley DB 1.85 compatibility..."
	@CC="$(CC)" sh compat/detect-db185.sh > $@.tmp || { rm -f $@.tmp; exit 1; }
	@mv $@.tmp $@

include $(DB185_CONFIG)

CPPFLAGS += $(DB185_CPPFLAGS)
LDFLAGS += $(DB185_LDFLAGS)
LDLIBS += $(DB185_LIBS)

endif

# illumos networking and RADIUS dependencies.
LDLIBS += -lnsl -lsocket -lposix4 -lmd

# Enable RADIUS dependencies only when requested by vendor.h.
ifeq ($(RADIUS_ENABLED),1)

LDLIBS += -lradius

# Find the installed RADIUS library.
RADIUS_LIBDIR := $(firstword $(foreach dir, \
    /opt/local/lib \
    /opt/ooce/lib/amd64 \
    /opt/ooce/lib \
    /usr/local/lib \
    /usr/lib/amd64 \
    /usr/lib, \
    $(if $(wildcard $(dir)/libradius.so $(dir)/libradius.a),$(dir))))

ifneq ($(RADIUS_LIBDIR),)
LDFLAGS += -L$(RADIUS_LIBDIR)
endif
endif
# Compatibility implementation for libradius srandomdev().
ILLUMOS_COMPAT_OBJS = obj/compat_srandomdev.o

endif

# Source files

LIB_SRCS = \
    lib/global.c lib/node.c lib/xopen.c lib/buffer.c \
    lib/wildcmp.c lib/history.c lib/expire.c lib/newsfeed.c \
    lib/parsedate.c lib/sigs.c lib/lock.c lib/alloc.c \
    lib/subs.c lib/xmap.c lib/precommit.c lib/spamfilter.c \
    lib/strerror.c lib/memcpy.c lib/zalloc.c lib/config.c \
    lib/kpdb.c lib/active.c lib/msgid.c lib/hash.c \
    lib/psstat.c lib/runprog.c lib/snprintf.c lib/fatal.c \
    lib/log.c lib/logtime.c lib/iplist.c lib/dgp.c \
    lib/pgp.c lib/hostauth.c lib/strsep.c lib/groupfind.c \
    lib/spool.c lib/arttype.c lib/stats.c lib/dmd5.c \
    lib/notify.c lib/wildmat.c lib/include.c lib/hashfeed.c \
    lib/dgzip.c lib/canlock.c

FILTER_SRCS = \
    filter/diab-filter.c filter/diab-read.c filter/socket.c

DR_SRCS = \
    dreaderd/main.c dreaderd/thread.c dreaderd/reader.c \
    dreaderd/dns.c dreaderd/mbuf.c dreaderd/subs.c \
    dreaderd/list.c dreaderd/feed.c dreaderd/xover.c \
    dreaderd/nntp.c dreaderd/misc.c dreaderd/post.c \
    dreaderd/server.c dreaderd/group.c dreaderd/spool.c \
    dreaderd/cache.c dreaderd/rtstatus.c dreaderd/control.c \
    dreaderd/wildorcmp.c dreaderd/cancel.c \
    dreaderd/post-addr-ck.c dreaderd/cleanfrom.c \
    dreaderd/msg.c dreaderd/dfa.c dreaderd/filealloc.c \
    dreaderd/crypto.c

UTIL_PROGS = \
    dicmd drcmd didump dilookup dexpire didate \
    diconvhist diload doutq dspaminfo dspoolout \
    diloadfromspool dreadart dkp pgpverify dsyncgroups \
    dexpireover dreadover dpath dprimehostcache dstart \
    dclient dfeedinfo dlockhistory dfeedtest doverctl \
    drequeue dhisbench dhisexpire dhisctl dexpirecache \
    dcancel dexpirescoring diablo dnewslink dgrpctl djoblog \
    $(if $(filter 1,$(CANLOCK_ENABLED)),dcanlock)

SUP_PROGS = plock showlocks

# Object file lists

LIB_OBJS := $(patsubst lib/%.c,obj/lib_%.o,$(LIB_SRCS))
FILTER_OBJS := $(patsubst filter/%.c,obj/filter_%.o,$(FILTER_SRCS))
DR_OBJS := $(patsubst dreaderd/%.c,obj/dr_%.o,$(DR_SRCS))

DR_LIB_OBJS := $(filter-out obj/dr_main.o,$(DR_OBJS))

UTIL_OBJS := $(addprefix obj/util_,$(addsuffix .o,$(UTIL_PROGS)))
SUP_OBJS := $(addprefix obj/sup_,$(addsuffix .o,$(SUP_PROGS)))

PROTOS := obj/lib-protos.h obj/filter-protos.h obj/dreaderd-protos.h

BINS := dbin/dreaderd $(addprefix dbin/,$(UTIL_PROGS) $(SUP_PROGS))

# Editing the shared version defaults rebuilds objects with updated version strings.
$(LIB_OBJS) $(FILTER_OBJS) $(DR_OBJS) $(UTIL_OBJS) $(SUP_OBJS): build-version.mk

# Main targets

.PHONY: all clean distclean install bininstall uninstall help

all: $(BINS) obj/libdreader.a

# Output directories.
# Order-only prerequisites prevent unnecessary rebuilds.

obj dbin:
	mkdir -p $@

# Generated prototype headers

obj/lib-protos.h: $(LIB_SRCS) | obj
	@grep -h 'Prototype' $(LIB_SRCS) > $@.tmp || test $$? -eq 1
	@mv $@.tmp $@

obj/filter-protos.h: $(FILTER_SRCS) | obj
	@grep -h 'Prototype' $(FILTER_SRCS) > $@.tmp || test $$? -eq 1
	@mv $@.tmp $@

obj/dreaderd-protos.h: $(DR_SRCS) | obj
	@grep -h 'Prototype' $(DR_SRCS) > $@.tmp || test $$? -eq 1
	@mv $@.tmp $@

# Compilation rules

obj/lib_%.o: lib/%.c obj/lib-protos.h | obj
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

obj/filter_%.o: filter/%.c obj/filter-protos.h obj/lib-protos.h | obj
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

obj/dr_%.o: dreaderd/%.c $(PROTOS) | obj
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

obj/util_%.o: util/%.c $(PROTOS) | obj
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

obj/sup_%.o: sup/%.c $(PROTOS) | obj
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# illumos compatibility object

obj/compat_srandomdev.o: compat/srandomdev.c | obj
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Static libraries

obj/libdiablo.a: $(LIB_OBJS)
	$(AR) rcs $@ $^
	$(RANLIB) $@

obj/libfilter.a: $(FILTER_OBJS)
	$(AR) rcs $@ $^
	$(RANLIB) $@

obj/libdreader.a: $(DR_LIB_OBJS)
	$(AR) rcs $@ $^
	$(RANLIB) $@

# Reader daemon

dbin/dreaderd: $(DR_OBJS) $(ILLUMOS_COMPAT_OBJS) obj/libfilter.a obj/libdiablo.a | dbin
	$(CC) $(LDFLAGS) $(DR_OBJS) $(ILLUMOS_COMPAT_OBJS) -o $@ $(LDLIBS)

# Utility executables

define util_rule
dbin/$(1): obj/util_$(1).o obj/libfilter.a obj/libdiablo.a | dbin
	$$(CC) $$(LDFLAGS) obj/util_$(1).o -o $$@ $$(LDLIBS)
endef

$(foreach prog,$(UTIL_PROGS),$(eval $(call util_rule,$(prog))))

# Support executables

define sup_rule
dbin/$(1): obj/sup_$(1).o obj/libfilter.a obj/libdiablo.a | dbin
	$$(CC) $$(LDFLAGS) obj/sup_$(1).o -o $$@ $$(LDLIBS)
endef

$(foreach prog,$(SUP_PROGS),$(eval $(call sup_rule,$(prog))))

# Installation
# Preserve the original Diablo layout under /news.
# DESTDIR permits staged installations.

PREFIX ?= /news
DESTDIR ?=
INSTALL_OWNER ?= news
INSTALL_GROUP ?= news

# install-portable.sh takes these as arguments (as BSDmakefile passes them),
# not from the environment.
install bininstall uninstall: all
	@sh ./install-portable.sh "$@" "$(PREFIX)" "$(DESTDIR)" \
	  "$(INSTALL_OWNER)" "$(INSTALL_GROUP)"

# Maintenance

clean:
	rm -rf obj dbin

distclean: clean

help:
	@echo 'Targets: all clean distclean install bininstall uninstall help'
	@echo 'Default installation prefix: /news'
	@echo 'Use PREFIX=/path to override the installation prefix.'
	@echo 'Use DESTDIR=/path for staged installations.'

# Automatically generated compiler dependencies
-include $(wildcard obj/*.d)

