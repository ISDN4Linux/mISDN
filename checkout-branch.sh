#!/bin/bash

#checks out the given branch

if [ "$1" = "" ] ; then
	echo "please provide one of the branch names:"
	git branch -a
	exit
fi

git checkout -b $1 origin/$1
git config branch.$1.remote origin
git config branch.$1.merge refs/heads/$1
