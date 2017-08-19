#!/bin/bash
PIDFILE=$1
mkdir -p /home/pi/logs
while [ 1=1 ]
do
#exec /home/pi/water-meter/water-meter >>/home/pi/logs/water-meter.log 2>&1 </dev/null
/home/pi/water-meter/water-meter >>/home/pi/logs/water-meter.log 2>&1 </dev/null
/home/pi/water-meter/usbreset /dev/bus/usb/001/004
#CHILD=$!
#echo $CHILD > $PIDFILE
done
