#!/bin/bash

function uninstall {
	for i in include/linux/*.h ; do 
		rm -f $INSTALL_PREFIX/usr/include/linux/$(basename $i) 
	done
}

function test_old_misdn {
	if test -f /usr/include/linux/mISDNif.h ; then
		V=$(grep "MISDN_MAJOR_VERSION" /usr/include/linux/mISDNif.h | cut -f 3)
		echo VERSION is $V
		if test $V -lt 4 ; then
			echo "!You should remove the following files:"
			echo
			echo "$LINUX/include/linux/mISDNif.h"
			echo "$LINUX/include/linux/isdn_compat.h"
			echo "/usr/include/linux/mISDNif.h"
			echo "/usr/include/linux/isdn_compat.h"
			echo
			echo "In order to upgrade to the mqueue branch"
			echo "I can do that for you, just type: make force"  
			echo
			exit 1;
		fi
	fi
}

case $1 in 
test_old_misdn)
	test_old_misdn
;;
uninstall)
	uninstall 
;;

esac
