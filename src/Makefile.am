lib_LTLIBRARIES = liblxi.la

liblxidir = $(includedir)
liblxi_HEADERS = include/lxi.h

if USE_INTERNAL_VXI
VXI_SOURCE = vxi11core_clnt.c    \
             vxi11core_xdr.c     \
             include/vxi11core.h
VXI_CFLAGS = -DUSE_INTERNAL_VXI
endif

liblxi_la_SOURCES = lxi.c         \
                    include/lxi.h \
                    $(VXI_SOURCE)

liblxi_la_CFLAGS = -I$(top_srcdir)/include $(VXI_CFLAGS)

liblxi_la_LDFLAGS = -version-info $(LIBLXI_LT_VERSION) -init,init
