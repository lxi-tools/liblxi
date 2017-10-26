lib_LTLIBRARIES = liblxi.la

EXTRA_DIST = vxi11core.rpcl

liblxidir = $(includedir)
liblxi_HEADERS = include/lxi.h

nodist_liblxi_la_SOURCES = vxi11core_clnt.c    \
                           vxi11core_xdr.c     \
                           include/vxi11core.h

liblxi_la_SOURCES = include/lxi.h \
                    lxi.c

BUILT_SOURCES = vxi11core_clnt.c    \
                vxi11core_xdr.c     \
                include/vxi11core.h

CLEANFILES = vxi11core_clnt.c    \
             vxi11core_xdr.c     \
             vxi11core_svc.c     \
             include/vxi11core.h

liblxi_la_CFLAGS = -I$(top_srcdir)/src/include

liblxi_la_LDFLAGS = -version-info $(LIBLXI_LT_VERSION) -init,init

vxi11core_clnt.c vxi11core_xdr.c include/vxi11core.h:
	rpcgen -M vxi11core.rpcl
	mv vxi11core.h include
