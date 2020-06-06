#!/bin/bash

source $(dirname ${0})/common.sh
check_root

DEV_ADDR=$(dev_addr ${SPEAKER_GROUP} ${CHANNEL} ${SUBCHANNEL})
MULTICAST_ADDR=$(multicast_addr ${SPEAKER_GROUP})

ip link set ${WIFI_DEV} down
ip link set ${WIFI_DEV} up
iw ${WIFI_DEV} set type ibss
iw ${WIFI_DEV} ibss join ${SSID} ${FREQ}
ip addr change ${DEV_ADDR}/96 dev ${WIFI_DEV}
ip -6 route add ${MULTICAST_ADDR}/128 dev ${WIFI_DEV} table local

# Set up hostname
hostnamectl set-hostname $(hostname ${SPEAKER_GROUP} ${CHANNEL} ${SUBCHANNEL})

# Set up /etc/hosts for a few possible combinations
grep -v '^fd37:' /etc/hosts > /etc/hosts.new
for C in $(seq 0 5); do
    for SC in $(seq 0 2); do
        echo -e "$(dev_addr ${SPEAKER_GROUP} ${C} ${SC})\t$(hostname ${SPEAKER_GROUP} ${C} ${SC})" >> /etc/hosts.new
    done
done

# Also add an entry for our own hostname
grep -v '^127.0.1.1' /etc/hosts.new > /etc/hosts
echo -e "127.0.1.1\t$(hostname ${SPEAKER_GROUP} ${CHANNEL} ${SUBCHANNEL})" >> /etc/hosts
rm -f /etc/hosts.new
