AC_PREREQ([2.68])
AC_INIT([liblxi], [1.12], [], [liblxi], [https://lxi-tools.github.io])
AC_CONFIG_HEADERS([src/include/config.h])
AM_INIT_AUTOMAKE([1.11 foreign dist-xz no-dist-gzip -Wall -Werror])
AM_SILENT_RULES([yes])
AM_PROG_AR

LT_PREREQ([2.2])
LT_INIT

# Before making a release, the LIBLXI_LT_VERSION string should be
# modified.
# The string is of the form C:R:A (Current, Revision, Age).
# - If interfaces have been changed or added, but binary compatibility has
#   been preserved, change to C+1:0:A+1
# - If binary compatibility has been broken (e.g. removed or changed interfaces)
#   change to C+1:0:0
# - If the interface is the same as the previous version, change to C:R+1:A
LIBLXI_LT_VERSION=1:0:0
AC_SUBST(LIBLXI_LT_VERSION)

AC_ENABLE_SHARED
AC_ENABLE_STATIC
AC_PROG_CC
AC_LANG([C])
AC_PROG_INSTALL
PKG_PROG_PKG_CONFIG

# Check for rpc headers
rpc_headers_found=1
AC_CHECK_HEADERS(rpc/rpc.h, [], [rpc_headers_found=0])
if test $rpc_headers_found == 0
then
    # Check for libtirpc (Sun RPC, includes rpc.h etc.)
    PKG_CHECK_MODULES([libtirpc], [libtirpc])
fi

# Check for rpcgen tool
AC_PATH_PROG([RPCGEN], [rpcgen])
AC_ARG_VAR([RPCGEN], [RPC compiler command])
if test x$RPCGEN = x ; then
    AC_MSG_ERROR([could not locate rpcgen])
fi

# Check for Avahi (mDNS/DNS-SD implementation)
AC_ARG_ENABLE([avahi], AS_HELP_STRING([--disable-avahi], [Disable avahi mDNS feature]))
AS_IF([test "x$enable_avahi" != "xno"], [
  AC_CHECK_LIB([avahi-client], [avahi_client_new], [have_avahi_client=yes], [AC_ERROR(Missing avahi-client library)])
  AC_CHECK_LIB([avahi-common], [avahi_simple_poll_new], [have_avahi_common=yes], [AC_ERROR(Missing avahi-common library)])
])
if test x$have_avahi_client = xyes ; then
    if test x$have_avahi_common = xyes ; then
        have_avahi=yes
        AC_DEFINE([HAVE_AVAHI],[1],[Avahi available])
    fi
fi
AM_CONDITIONAL([HAVE_AVAHI],[test "x$have_avahi" == "xyes"])

# Check for libxml2
PKG_CHECK_MODULES(LIBXML2_PC, [libxml-2.0],
    [CPPFLAGS="${CPPFLAGS} ${LIBXML2_PC_CFLAGS}"
     LIBS="${LIBS} ${LIBXML2_PC_LIBS}"
     AC_CHECK_LIB(xml2, xmlInitParser, [], AC_ERROR(Missing xml2 library))
     ],
    [AC_ERROR(Missing xml2 library)])
AC_CHECK_HEADERS([libxml/xmlreader.h libxml/xmlwriter.h])

AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([src/Makefile])
AC_CONFIG_FILES([man/Makefile])
AC_OUTPUT
