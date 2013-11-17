#!/bin/sh

devdir="/sys/$DEVPATH/../"
busnum=$(cat "$devdir/busnum")
devnum=$(cat "$devdir/devnum")
exe=$1
shift
logger -s -i -t "udev.sh" -p daemon.notice "Executing $exe -b $busnum -d $devnum -D $*"
$exe -i 10 -v -b "$busnum" -d "$devnum" -D $*
