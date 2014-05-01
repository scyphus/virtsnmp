#
# $Id$
#
# Copyright (c) 2012 Scyphus Solutions Co. Ltd.  All rights reserved.
# Authors:
#      Hirochika Asai  <asai@scyphus.co.jp>
#

AC_PREREQ(2.59)
AC_INIT([virtsnmpd], [0.3.0a], [bug@scyphus.co.jp])
AC_CONFIG_SRCDIR([virtsnmpd.c])
AC_CONFIG_HEADER([config.h])

# Get hostname and other information.
AC_CANONICAL_BUILD
AC_CANONICAL_HOST
AC_CANONICAL_TARGET

AM_INIT_AUTOMAKE
AC_SUBST(SUBDIRS)

# arguments
AC_ARG_ENABLE(debug,
  [  --enable-debug    Turn on debugging [default no]],
  [case "${enableval}" in
    yes) debug=yes; AC_MSG_RESULT(Checking for debug... yes); AC_DEFINE(DEBUG, 1, debugging option) ;;
    no)  debug=no;;
    *) AC_MSG_ERROR(bad value ${enableval} for --enable-debug) ;;
  esac],[debug=no])
AM_CONDITIONAL(DEBUG, test x$debug = xtrue)

## net-snmp-config
AC_PATH_PROGS(NET_SNMP_CONFIG, net-snmp-config, :)
if test $NET_SNMP_CONFIG = ":"; then
   AC_MSG_ERROR(Require net-snmp-config)
fi

## libxml2
AC_PATH_PROGS(XML2_CONFIG, xml2-config, :)
if test $XML2_CONFIG = ":"; then
   AC_MSG_ERROR(Require libxml2)
fi

# Checks for hosts
case $host_os in
     freebsd*)
        AC_DEFINE(TARGET_FREEBSD, 1, running on FreeBSD)
        arch=freebsd
        ;;
     netbsd*)
        AC_DEFINE(TARGET_NETBSD, 1, running on NetBSD)
        arch=netbsd
        ;;
     darwin*)
        AC_DEFINE(TARGET_DARWIN, 1, running on Mac OS X)
        arch=darwin
        ;;
     linux*)
        AC_DEFINE(TARGET_LINUX, 1, running on Linux)
        arch=linux
        ;;
     *) ;;
esac

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LIBTOOL

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([stdlib.h sys/mount.h sys/ioctl.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

# Checks for library functions.
#AC_CHECK_FUNCS([])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
