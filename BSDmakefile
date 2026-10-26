# Diablo 6 portable build; generated from original XMakefiles.
# Keep GNU Make and BSD Make build definitions in sync.
.OBJDIR: ${.CURDIR}
CC ?= clang
# Shared release version defaults; command-line values take precedence.
.include "build-version.mk"
CPPFLAGS += -I. -DVERS=\"${VERS}\" -DSUBREV=\"${SUBREV}\" -DDEBUG_ZALLOC -DDEBUG_MBUF
CFLAGS += -O2 -pipe -Wall -Wstrict-prototypes -fno-common -MMD -MP
LDFLAGS += -Lobj
LDLIBS += -lfilter -ldiablo -lm -lmd -lradius -lz -lcrypto
LIB_SRCS = lib/global.c lib/node.c lib/xopen.c lib/buffer.c lib/wildcmp.c lib/history.c lib/expire.c lib/newsfeed.c lib/parsedate.c lib/sigs.c lib/lock.c lib/alloc.c lib/subs.c lib/xmap.c lib/precommit.c lib/spamfilter.c lib/strerror.c lib/memcpy.c lib/zalloc.c lib/config.c lib/kpdb.c lib/active.c lib/msgid.c lib/hash.c lib/psstat.c lib/runprog.c lib/snprintf.c lib/fatal.c lib/log.c lib/logtime.c lib/iplist.c lib/dgp.c lib/pgp.c lib/hostauth.c lib/strsep.c lib/groupfind.c lib/spool.c lib/arttype.c lib/stats.c lib/dmd5.c lib/notify.c lib/wildmat.c lib/include.c lib/hashfeed.c lib/dgzip.c
FILTER_SRCS = filter/diab-filter.c filter/diab-read.c filter/socket.c
DR_SRCS = dreaderd/main.c dreaderd/thread.c dreaderd/reader.c dreaderd/dns.c dreaderd/mbuf.c dreaderd/subs.c dreaderd/list.c dreaderd/feed.c dreaderd/xover.c dreaderd/nntp.c dreaderd/misc.c dreaderd/post.c dreaderd/server.c dreaderd/group.c dreaderd/spool.c dreaderd/cache.c dreaderd/rtstatus.c dreaderd/control.c dreaderd/wildorcmp.c dreaderd/cancel.c dreaderd/post-addr-ck.c dreaderd/cleanfrom.c dreaderd/msg.c dreaderd/dfa.c dreaderd/filealloc.c dreaderd/crypto.c
UTIL_PROGS = dicmd drcmd didump dilookup dexpire didate diconvhist diload doutq dspaminfo dspoolout diloadfromspool dreadart dkp pgpverify dsyncgroups dexpireover dreadover dpath dprimehostcache dstart dclient dfeedinfo dlockhistory dfeedtest doverctl drequeue dhisbench dhisexpire dhisctl dexpirecache dcancel dexpirescoring diablo dnewslink dgrpctl
SUP_PROGS = plock showlocks
LIB_OBJS = ${LIB_SRCS:S,lib/,obj/lib_,g:S,.c,.o,g}
FILTER_OBJS = ${FILTER_SRCS:S,filter/,obj/filter_,g:S,.c,.o,g}
DR_OBJS = ${DR_SRCS:S,dreaderd/,obj/dr_,g:S,.c,.o,g}
DR_LIB_OBJS = ${DR_OBJS:Nobj/dr_main.o}
UTIL_OBJS = ${UTIL_PROGS:S,^,obj/util_,g:S,$,.o,g}
SUP_OBJS = ${SUP_PROGS:S,^,obj/sup_,g:S,$,.o,g}
PROTOS = obj/lib-protos.h obj/filter-protos.h obj/dreaderd-protos.h
BINS = dbin/dreaderd ${UTIL_PROGS:S,^,dbin/,g} ${SUP_PROGS:S,^,dbin/,g}
# Editing the shared version defaults rebuilds objects with updated version strings.
${LIB_OBJS} ${FILTER_OBJS} ${DR_OBJS} ${UTIL_OBJS} ${SUP_OBJS}: build-version.mk
.MAIN: all
.PHONY: all clean distclean install bininstall uninstall help
all: ${BINS} obj/libdreader.a
obj/lib-protos.h: ${LIB_SRCS}
	@mkdir -p obj
	@grep -h 'Prototype' ${LIB_SRCS} > ${.TARGET}.tmp || test $$? -eq 1
	@mv ${.TARGET}.tmp ${.TARGET}
obj/filter-protos.h: ${FILTER_SRCS}
	@mkdir -p obj
	@grep -h 'Prototype' ${FILTER_SRCS} > ${.TARGET}.tmp || test $$? -eq 1
	@mv ${.TARGET}.tmp ${.TARGET}
obj/dreaderd-protos.h: ${DR_SRCS}
	@mkdir -p obj
	@grep -h 'Prototype' ${DR_SRCS} > ${.TARGET}.tmp || test $$? -eq 1
	@mv ${.TARGET}.tmp ${.TARGET}
.for src in ${LIB_SRCS}
obj/lib_${src:T:R}.o: ${src} obj/lib-protos.h
	@mkdir -p obj
	${CC} ${CPPFLAGS} ${CFLAGS} -c ${src} -o ${.TARGET}
.endfor
.for src in ${FILTER_SRCS}
obj/filter_${src:T:R}.o: ${src} obj/filter-protos.h obj/lib-protos.h
	@mkdir -p obj
	${CC} ${CPPFLAGS} ${CFLAGS} -c ${src} -o ${.TARGET}
.endfor
.for src in ${DR_SRCS}
obj/dr_${src:T:R}.o: ${src} ${PROTOS}
	@mkdir -p obj
	${CC} ${CPPFLAGS} ${CFLAGS} -c ${src} -o ${.TARGET}
.endfor
.for src in ${UTIL_PROGS}
obj/util_${src}.o: util/${src}.c ${PROTOS}
	@mkdir -p obj
	${CC} ${CPPFLAGS} ${CFLAGS} -c util/${src}.c -o ${.TARGET}
.endfor
.for src in ${SUP_PROGS}
obj/sup_${src}.o: sup/${src}.c ${PROTOS}
	@mkdir -p obj
	${CC} ${CPPFLAGS} ${CFLAGS} -c sup/${src}.c -o ${.TARGET}
.endfor
obj/libdiablo.a: ${LIB_OBJS}
	@mkdir -p obj
	${AR} rcs ${.TARGET} ${LIB_OBJS}
	${RANLIB} ${.TARGET}
obj/libfilter.a: ${FILTER_OBJS}
	@mkdir -p obj
	${AR} rcs ${.TARGET} ${FILTER_OBJS}
	${RANLIB} ${.TARGET}
obj/libdreader.a: ${DR_LIB_OBJS}
	@mkdir -p obj
	${AR} rcs ${.TARGET} ${DR_LIB_OBJS}
	${RANLIB} ${.TARGET}
dbin/dreaderd: ${DR_OBJS} obj/libfilter.a obj/libdiablo.a
	@mkdir -p dbin
	${CC} ${LDFLAGS} ${DR_OBJS} -o ${.TARGET} ${LDLIBS}
.for prog in ${UTIL_PROGS}
dbin/${prog}: obj/util_${prog}.o obj/libfilter.a obj/libdiablo.a
	@mkdir -p dbin
	${CC} ${LDFLAGS} obj/util_${prog}.o -o ${.TARGET} ${LDLIBS}
.endfor
.for prog in ${SUP_PROGS}
dbin/${prog}: obj/sup_${prog}.o obj/libfilter.a obj/libdiablo.a
	@mkdir -p dbin
	${CC} ${LDFLAGS} obj/sup_${prog}.o -o ${.TARGET} ${LDLIBS}
.endfor
clean:
	rm -rf obj dbin
distclean: clean
PREFIX?= /news
DESTDIR?=
INSTALL_USER?= news
INSTALL_GROUP?= news

install bininstall: all
	sh ./install-portable.sh ${.TARGET} "${PREFIX}" "${DESTDIR}" "${INSTALL_USER}" "${INSTALL_GROUP}"

uninstall:
	sh ./install-portable.sh uninstall "${PREFIX}" "${DESTDIR}" "${INSTALL_USER}" "${INSTALL_GROUP}"
help:
	@echo 'Targets: all clean distclean help; installation: install bininstall uninstall; PREFIX=/news DESTDIR= optional.'
.for dep in ${LIB_OBJS} ${FILTER_OBJS} ${DR_OBJS} ${UTIL_OBJS} ${SUP_OBJS}
.if exists(${dep:R}.d)
.include "${dep:R}.d"
.endif
.endfor
