#!/bin/bash
mv $1 ${1}.org
sed -f sed.clean ${1}.org >$1
