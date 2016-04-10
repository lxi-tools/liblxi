lib_LTLIBRARIES = liblxi.la

liblxidir = $(includedir)
liblxi_HEADERS = include/lxi.h

liblxi_la_SOURCES = \
    lxi.c            \
	vxi11core_clnt.c   \
	vxi11core_xdr.c     \
	include/vxi11core.h \
    include/lxi.h

liblxi_la_CFLAGS = -I$(top_srcdir)/include

liblxi_la_LDFLAGS = -version-info $(LIBLXI_LT_VERSION) -init,init
