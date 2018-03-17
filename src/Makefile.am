lib_LTLIBRARIES = liblxi.la

EXTRA_DIST = vxi11core.rpcl

liblxidir = $(includedir)
liblxi_HEADERS = include/lxi.h

nodist_liblxi_la_SOURCES = vxi11core_clnt.c    \
                           vxi11core_xdr.c     \
                           include/vxi11core.h

liblxi_la_SOURCES = include/lxi.h \
                    include/session.h \
                    include/vxi11.h \
                    include/tcp.h \
                    include/error.h \
                    include/mdns.h \
                    lxi.c \
                    vxi11.c \
                    tcp.c \
                    mdns.c

if HAVE_AVAHI
liblxi_la_SOURCES += avahi.c \
                     include/avahi.h
endif

BUILT_SOURCES = vxi11core_clnt.c    \
                vxi11core_xdr.c     \
                include/vxi11core.h

CLEANFILES = vxi11core_clnt.c    \
             vxi11core_xdr.c     \
             vxi11core_svc.c     \
             include/vxi11core.h

liblxi_la_CFLAGS = -I$(top_srcdir)/src/include -fvisibility=hidden -D_GNU_SOURCE @libtirpc_CFLAGS@

liblxi_la_LDFLAGS = -version-info $(LIBLXI_LT_VERSION) -init,init @libtirpc_LIBS@
if HAVE_AVAHI
liblxi_la_LDFLAGS += -lavahi-client -lavahi-common
endif

.NOTPARALLEL:
vxi11core_clnt.c vxi11core_xdr.c include/vxi11core.h:
	rpcgen -M vxi11core.rpcl
	mv vxi11core.h include
