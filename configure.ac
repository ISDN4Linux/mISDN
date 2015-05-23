#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([mISDNkernel], [2.0.36], [i4ldeveloper@listserv.isdn4linux.de])
AC_CONFIG_SRCDIR([drivers/isdn/mISDN/core.c])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([-Werror foreign])

# Checks for programs.
AC_PROG_CC
AC_PROG_MAKE_SET
AC_PROG_SED
AC_PROG_GREP
AC_PROG_AWK
AC_PROG_FIND
AC_PROG_DIFF
AC_PROG_PATCH
AC_PROG_IFNAMES
AC_PROG_SORT
# Checks for libraries.

# Checks for header files.

# Checks for library functions.

# define kernel build directory

AC_PROG_KERNEL_BUILD_DIR

if test -z "$KERNEL_BUILD_DIR"; then
	if test -z "$KERNEL_VERSION"; then
		KERNEL_VERSION=`uname -r`
	fi
	KERNEL_MOD_DIR=/lib/modules/$KERNEL_VERSION
	KERNEL_BUILD_DIR=$KERNEL_MOD_DIR/build
	KERNEL_SOURCE_DIR=$KERNEL_MOD_DIR/source
else
	if test -d $KERNEL_BUILD_DIR/source; then
		KERNEL_SOURCE_DIR=$KERNEL_BUILD_DIR/source
	else
		KERNEL_SOURCE_DIR=$KERNEL_BUILD_DIR
	fi
		
fi

if test -d $KERNEL_BUILD_DIR -a -d $KERNEL_SOURCE_DIR; then
	if test -f $KERNEL_BUILD_DIR/include/generated/utsrelease.h; then
		KMAJ_=`$GREP "^VERSION =" $KERNEL_SOURCE_DIR/Makefile | cut -d '=' -f 2`
		KPAT_=`$GREP "^PATCHLEVEL =" $KERNEL_SOURCE_DIR/Makefile | cut -d '=' -f 2`
		KSUB_=`$GREP "^SUBLEVEL =" $KERNEL_SOURCE_DIR/Makefile | cut -d '=' -f 2`
		KMAJ=`echo -n $KMAJ_`
		KPAT=`echo -n $KPAT_`
		KSUB=`echo -n $KSUB_`
		if test x$KMAJ != x -a $KMAJ -ge 3 ; then
			KVERSION="${KMAJ}.${KPAT}"
		else
			KVERSION="${KMAJ}.${KPAT}.${KSUB}"
		fi
	elif test -f $KERNEL_BUILD_DIR/include/linux/version.h; then
		vcl=`$GREP LINUX_VERSION_CODE $KERNEL_BUILD_DIR/include/linux/version.h`
		KVERSION=`echo $vcl |$AWK '{printf ("%d.%d.%d",$3 / 65536, (($3/256)%256),$3 % 256)}'`
		KMAJOR=`echo $vcl |$AWK '{printf ("%d",$3 / 65536)}'`
		if test x$KMAJOR = x3; then
			KVERSION=`echo $vcl |$AWK '{printf ("%d.%d",$3 / 65536, (($3/256)%256))}'`
		fi
	else
		AC_MSG_ERROR([cannot find $KERNEL_BUILD_DIR/include/linux/version.h])
	fi
	if test x$KERNEL_BUILD_DIR = x$KERNEL_SOURCE_DIR; then
		AC_MSG_NOTICE([Configured for $KVERSION kernel source $KERNEL_SOURCE_DIR])
	else
		AC_MSG_NOTICE([Configured for $KVERSION  kernel build $KERNEL_BUILD_DIR source $KERNEL_SOURCE_DIR])
	fi
else
	AC_MSG_ERROR([kernel build tree does not exist build:$KERNEL_BUILD_DIR / source:$KERNEL_SOURCE_DIR])
fi

AC_SUBST(KERNEL_BUILD_DIR)
AC_SUBST(KERNEL_SOURCE_DIR)
AC_SUBST(KVERSION)

AC_CONFIG_FILES([
	Makefile
	standalone/Makefile
])
AC_OUTPUT
