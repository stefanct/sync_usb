#!/bin/sh

if [ ! ${USER} = "root" ]; then
	echo "You need to be root!"
	exit 1
fi

sudo -u ${SUDO_USER} make all
cset shield -c 2 -k on
cset proc -s user -e ./main.exe
#sleep 1
#cset proc -l -s user
cset shield -r
