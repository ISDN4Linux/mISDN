#!/bin/bash
KERNELDIR=/usr/src/linux
VERBOSE=false

usage() {
	cat<<EOM

	checkall.sh is used for checking files for being conform to the Linux
	kernel style
	checkall.sh [-h] [-k DIR]

	Options:

	-h	This Text.
	-k DIR	Kerneltree is in DIR instead of /usr/src/linux
	-v	list all issues

EOM
	exit
}

while getopts hk:v a ; do
	case $a in
		\?)	case $OPTARG in
				k)	echo "-k requires Kernel directory parameter"
					;;
				*)  echo "Unknown option: -$OPTARG"
					echo "Try std2kern -h"
					;;
			esac
			exit 1
			;;
		k)	
			KERNELDIR=$OPTARG
			;;
		h)	usage
			;;
		v)	VERBOSE=true
			;;
	esac
done
shift `expr $OPTIND - 1`

CHECKCMD="$KERNELDIR/scripts/checkpatch.pl --file --strict --summary-file"
FILES=`find . -name '*.[hc]'`
for f in $FILES; do
	if $VERBOSE; then
		$CHECKCMD $f
	else
		$CHECKCMD $f | grep "lines checked"
	fi
done
