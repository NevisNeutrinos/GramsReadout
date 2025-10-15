#!/bin/bash

sudo /home/pgrams/WD1630LNX86_64/redist/wdreg windrvr1630 auto
sudo rmmod windrvr1630 && sudo modprobe windrvr1630
echo "Set up WinDriver.."
