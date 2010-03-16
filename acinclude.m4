AC_DEFUN([AC_PROG_FIND], [
	AC_ARG_VAR(FIND, [unix find utility])
	AC_PATH_PROG(FIND, find,[NotFound])
	if test x$FIND = xNotFound
	then
		AC_MSG_ERROR([find utility not found]);
	fi
])

AC_DEFUN([AC_PROG_DIFF], [
	AC_ARG_VAR(DIFF, [unix diff utility])
	AC_PATH_PROG(DIFF, diff,[NotFound])
	if test x$DIFF = xNotFound
	then
		AC_MSG_ERROR([diff utility not found]);
	fi
])

AC_DEFUN([AC_PROG_PATCH], [
	AC_ARG_VAR(PATCH, [unix patch utility])
	AC_PATH_PROG(PATCH, patch,[NotFound])
	if test x$PATCH = xNotFound
	then
		AC_MSG_ERROR([patch utility not found]);
	fi
])

AC_DEFUN([AC_PROG_SORT], [
	AC_ARG_VAR(SORT, [unix sort utility])
	AC_PATH_PROG(SORT, sort,[NotFound])
	if test x$SORT = xNotFound
	then
		AC_MSG_ERROR([sort utility not found]);
	fi
])

AC_DEFUN([AC_PROG_IFNAMES], [
	AC_ARG_VAR(IFNAMES, [autoconf ifnames utility])
	AC_PATH_PROG(IFNAMES, ifnames,[NotFound])
	dnl missing ifnames is not fatal
])

AC_DEFUN([AC_PROG_KERNEL_BUILD_DIR], [
	AC_ARG_WITH([kerneldir],
	AS_HELP_STRING([--with-kerneldir=<path>], [path to the kernel build directory]), [
		if test -d $withval
		then
			KERNEL_BUILD_DIR=$withval
		else
			AC_MSG_ERROR([kernel build directory ($withval) does not exist])
		fi
	],
	[
		KERNEL_BUILD_DIR=
	])
])
