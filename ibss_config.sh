#!/usr/bin/env bash

ESSID=WiPacket
FREQ=5900
DEV=""

while getopts i:e:f: option
do
case "${option}"
in
i) DEV=${OPTARG};;
e) ESSID=${OPTARG};;
f) FREQ=${OPTARG};;
esac
done

echo $(sudo ifconfig $DEV down)
echo $(sudo iw $DEV set type ibss)
echo $(sudo ifconfig $DEV up)
echo $(sudo iw $DEV ibss join $ESSID $FREQ)
