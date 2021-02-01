#!/bin/bash
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
sudo rm -rf /mnt/pmem/*
sudo umount /mnt/pmem
sudo rmmod nova
sudo modprobe nova
sudo mount -t NOVA -o init /dev/pmem0 /mnt/pmem
sudo chown -R $USER:$USER /mnt/pmem

