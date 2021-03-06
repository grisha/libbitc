dnl Process this file with autoconf 2.52+ to produce a configure script.
dnl
dnl Copyright (C) 2001 Philipp Rumpf
dnl Copyright (C) 2004 Henrique de Moraes Holschuh <hmh@debian.org>
dnl
dnl This program is free software; you can redistribute it and/or modify
dnl it under the terms of the GNU General Public License as published by
dnl the Free Software Foundation; either version 2 of the License, or
dnl (at your option) any later version.
dnl
dnl This program is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl GNU General Public License for more details.
dnl
dnl You should have received a copy of the GNU General Public License
dnl along with this program; if not, write to the Free Software
dnl Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

AC_INIT(libbitc, 0.2.0,
  [libbitc <libbitc@users.noreply.github.com>],,
  [https://github.com/libbitc/libbitc])
AC_PREREQ(2.60)
LT_INIT
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([src/bitsy.c])
AM_INIT_AUTOMAKE(foreign)
AC_CONFIG_HEADERS([libbitc-config.h])

dnl Make sure anyone changing configure.ac/Makefile.am has a clue
AM_MAINTAINER_MODE

AC_CANONICAL_HOST

dnl Checks for programs
AC_PROG_CC
AC_PROG_GCC_TRADITIONAL

dnl Checks for header files.
AC_HEADER_STDC
dnl AC_CHECK_HEADERS(sys/ioctl.h unistd.h)

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_BIGENDIAN
AC_TYPE_SIZE_T

dnl TODO: AC_SYS_LARGEFILE does not produce O_LARGEFILE on 64-bit Fedora Linux?
AC_SYS_LARGEFILE

AH_VERBATIM([_LARGEFILE64_SOURCE],
[/* Enable LFS extensions on systems that have them.  */
#ifndef _LARGEFILE64_SOURCE
# define _LARGEFILE64_SOURCE 1
#endif])

tag_os=""
AS_CASE([$host_os],
	[linux*],
	[
		AS_CASE([$host_cpu],
			[x86_64],
				[
				tag_os="linux-x86_64"
				],
			[i?86],
				[
				tag_os="linux-x86_32"
				],
			[arm*],
				[
				tag_os="linux-arm32"
				],
			[aarch32],
				[
				tag_os="linux-arm32"
				],
			[aarch64],
				[
				tag_os="linux-arm64"
				])
	],
    [darwin*],
    [
		AH_VERBATIM([O_LARGEFILE],
		[/* OSX naturally supports 64-bit files */
		#ifndef O_LARGEFILE
		# define O_LARGEFILE 0
		#endif])
		CPPFLAGS="$CPPFLAGS -I/usr/local/include"
		LDFLAGS="$LDFLAGS -L/usr/local/lib"
		tag_os="osx"
    ])
AC_SUBST([tag_os])

dnl -----------------------------
dnl Checks for required libraries
dnl -----------------------------

AC_CHECK_LIB(m, log, MATH_LIBS=-lm,
  [AC_MSG_ERROR([Missing required libm])])
AC_CHECK_LIB(gmp, __gmpz_init, GMP_LIBS=-lgmp,
  [AC_MSG_ERROR([Missing required libgmp])])
AC_CHECK_LIB(argp, argp_parse, ARGP_LIBS=-lARGP)

dnl -------------------------------------
dnl Checks for optional library functions
dnl -------------------------------------
AC_CHECK_FUNCS(memmem strndup mkstemp)

dnl -------------------------------------
dnl Checks for Doxygen
dnl -------------------------------------
AC_CHECK_PROGS([DOXYGEN], [doxygen])
AS_IF([test -z "$DOXYGEN"], [
	AC_MSG_WARN([Doxygen not found - continuing without Doxygen support])
])
AM_CONDITIONAL([HAVE_DOXYGEN],[test -n "$DOXYGEN"])
AM_COND_IF([HAVE_DOXYGEN], [AC_CONFIG_FILES([external/libbitc.github.io/Doxygen/Doxyfile])])

dnl -----------------
dnl Configure options
dnl -----------------

dnl Coverage
AC_MSG_CHECKING([whether to code coverage])
AC_ARG_ENABLE([coverage],
	[AS_HELP_STRING([--enable-coverage],[generate code coverage instrumentation])],
	[],
	[enable_coverage=no])
AC_MSG_RESULT([$enable_coverage])

AS_IF([test "$enable_coverage" = "yes"], [
	AC_PATH_PROG(GCOV, gcov, no)
	AS_IF([test "$GCOV" = "no"], [
		AC_MSG_ERROR(gcov tool is not available)])
	AC_PATH_PROG(LCOV, lcov, no)
	AS_IF([test "$LCOV" = "no"], [
		AC_MSG_ERROR(lcov tool is not installed)])
	CFLAGS="-O0 -fprofile-arcs -ftest-coverage"
])

dnl Valgrind
AC_MSG_CHECKING([whether to use valgrind])
AC_ARG_ENABLE([valgrind],
	[AS_HELP_STRING([--enable-valgrind],[Use valgrind when running unit tests])],
	[],
	[enable_valgrind=no])
AC_MSG_RESULT([$enable_valgrind])

AS_IF([test "$enable_valgrind" = "yes"], [
	AC_PATH_PROG(VALGRIND, valgrind, no)
	AS_IF([test "$VALGRIND" = "no"], [
		AC_MSG_ERROR(valgrind tool is not installed)])
	AC_DEFINE(USE_VALGRIND, 1, [Define if valgrind should be used])
])
AM_CONDITIONAL(ENABLE_VALGRIND, [test "$enable_valgrind" = "yes"])

dnl secp256k1 tests
AC_MSG_CHECKING([whether to enable tests for secp256k1 lib])
AC_ARG_ENABLE([secp256k1-tests],
	[AS_HELP_STRING([--enable-secp256k1-tests],[Run secp256k1 tests as part fo make check])],
	[],
	[enable_secp256k1_tests=no])
AC_MSG_RESULT([$enable_secp256k1_tests])
AS_IF([test "$enable_secp256k1_tests" = "no"],
	[ac_configure_args="$ac_configure_args --enable-tests=no"])

dnl Electric Fence
AC_MSG_CHECKING([whether to enable electric fence (www.perens.com)])
AC_ARG_ENABLE([efence],
	[AS_HELP_STRING([--enable-efence],[Enable Electric Fence Malloc Debugger])],
	[],
	[enable_efence=no])
AC_MSG_RESULT([$enable_efence])

AS_IF([test "$enable_efence" = "yes"], [
	AC_CHECK_LIB(efence, memalign,,
	  [AC_MSG_ERROR([Missing required libefence])])
])

dnl --------------------------
dnl libev
dnl --------------------------
m4_include([external/libev/libev.m4])

dnl --------------------------
dnl autoconf output generation
dnl --------------------------

AC_SUBST(MATH_LIBS)
AC_SUBST(GMP_LIBS)
AC_SUBST(ARGP_LIBS)

AC_CONFIG_SUBDIRS([external/secp256k1])
AC_CONFIG_FILES([
	Makefile
	m4/Makefile
	doc/Makefile
	include/Makefile
	include/bitc/Makefile
	lib/Makefile
	src/Makefile
	test/Makefile
	test/data/Makefile
	external/Makefile
	external/libbitc.github.io/Makefile
	external/libbitc.github.io/Doxygen/Makefile
	libbitc.pc
	contrib/debian/control
	contrib/debian/changelog
	contrib/rpmbuild/SPECS/libbitc.spec
	])
AC_OUTPUT
