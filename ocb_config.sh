#!/usr/bin/env bash

FREQ=5900
DEV=""
BW=10MHZ

while getopts i:f:b: option
do
case "${option}"
in
i) DEV=${OPTARG};;
f) FREQ=${OPTARG};;
b) BW=${OPTARG};;
esac
done

echo $(sudo ip link set $DEV down)
echo $(sudo iw dev $DEV set type ocb)
echo $(sudo ip link set $DEV up)
echo $(sudo iw dev $DEV ocb join $FREQ $BW)
