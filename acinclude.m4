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

